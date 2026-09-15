// ============================================================================
//  main.cpp  —  AI 서버 (llama.cpp + Gemma4)  [E4B/E2B + 배칭 벤치]
//
//  이전(E4B 대응)판에서 추가된 것 — 이 3가지만, 나머지는 동일:
//   (A) ctx_params.n_seq_max = kMaxBatchSize;  (main)
//        → 안 그러면 배칭 2번째 시퀀스에서 invalid seq_id 크래시.
//   (B) 콘솔 '/batch' 명령  (main 콘솔 루프)
//        → 같은 4프롬프트로 배칭 1회(4-in-4-out) vs 순차 4회 시간 비교.
//   (C) GenerateBatchResponses 내부 타이밍 출력
//        → prefill/생성 시간 + 총 생성 토큰 + aggregate tok/s.
//
//  ※ 네트워크는 바이너리 프레이밍(Common/Protocol.h 의 SA_AI/AS_AI)으로 전환됨.
//    게임서버와 소켓 하나를 공유하고 userIndex 로 손님을 구분한다. 배칭은 프로토콜과
//    독립이라 그대로 동작한다.
// ============================================================================

#include "stdafx.h"

#include "../../../Common/Type.h"
#include "../../../Common/Log.h"
#include "../../../Common/Protocol.h"
#include "NpcAgent.h"

#include "llamaCpp/llama-cpp.h"
#include "llamaCpp/ggml-backend.h"

#pragma comment(lib, "ws2_32.lib")

// lib/dll 은 AI/lib 에 모아 둔다(.gitattributes 가 그 경로를 LFS 로 지정).
// 헤더만 llamaCpp 에 남는다. 경로는 프로젝트 폴더 기준.
#pragma comment(lib, "../../lib/llama.lib")
#pragma comment(lib, "../../lib/ggml.lib")
#pragma comment(lib, "../../lib/ggml-base.lib")
#pragma comment(lib, "../../lib/ggml-cpu.lib")

// GPU 백엔드(CUDA/Vulkan)는 ggml_backend_load_all()로 런타임 DLL 로드.
// 링크 에러 날 때만 아래를 켠다.
// #pragma comment(lib, "../../lib/ggml-cuda.lib")
// #pragma comment(lib, "../../lib/ggml-vulkan.lib")

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <print>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// ----------------------------------------------------------------------------
//  대화 한 턴
// ----------------------------------------------------------------------------
struct ChatTurn
{
	std::string role; // "user" 또는 "assistant"
	std::string text;
};

// ----------------------------------------------------------------------------
//  ConversationStore — 손님(who)별로 대화 기억(history)을 분리 보관한다.
//
//  [왜 필요한가]
//  예전에는 소켓 연결 1개 = 손님 1명이라, 연결마다 로컬 history를 두면 자동으로
//  분리됐다. 하지만 진짜 게임 서버는 연결을 1개만 맺고 who(유저 ID)로 여러 손님을
//  구분해 보낸다. 그래서 연결이 아니라 who를 키로 history를 나눠야 한다.
//
//  [동작]
//  who별로 history를 따로 들고 있으므로, foo 입장에선 매 요청이 "이 손님과의
//  대화"로만 보인다. who="1"을 추론할 땐 1번 history만, who="2"를 추론할 땐 2번
//  history만 프롬프트에 들어간다. 처음 보는 who면 빈 history가 생성된다
//  (= foo가 그 손님을 태어나서 처음 만나는 셈).
//
//  키는 std::string. 게임 서버의 숫자 userIndex가 와도 std::to_string으로 문자열화해
//  같은 저장소를 쓴다. 워커 스레드 단일 처리지만, 안전을 위해 mutex로 보호한다.
// ----------------------------------------------------------------------------
class ConversationStore
{
public:
	// who의 history 참조를 얻는다. 없으면 새로 만든다(처음 만난 손님).
	std::vector<ChatTurn>& GetOrCreate(const std::string& who)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return historyByWho_[who];
	}

	// 한 손님의 기억만 지운다.
	void Reset(const std::string& who)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		historyByWho_.erase(who);
	}

	// 모든 손님의 기억을 지운다(방 해체 등).
	void ResetAll()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		historyByWho_.clear();
	}

	// 현재 기억 중인 손님 수(디버그용).
	size_t UserCount()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return historyByWho_.size();
	}

private:
	std::map<std::string, std::vector<ChatTurn>> historyByWho_;
	std::mutex mutex_;
};

// 31B 는 답 앞에 thought 채널을 여는데, 빈 thought 블록을 미리 넣어 두면 추론 없이 바로 답한다.
// https://ai.google.dev/gemma/docs/capabilities/thinking
constexpr const char* kGenerationPrefix = "<|turn>model\n<|channel>thought\n<channel|>";
constexpr int kPrefillChunkTokens = 512; // 큰 모델의 프리필 작업 버퍼를 줄인다

constexpr int kContextTokensPerSeq = 8192;
constexpr int kMaxBatchSize = 4;
constexpr int kTotalContextTokens = kContextTokensPerSeq * kMaxBatchSize;
constexpr int kMaxGenTokens = 256;
constexpr int kReservedTokens = 512;
constexpr int kPromptBudgetPerSeq = kContextTokensPerSeq - kMaxGenTokens - kReservedTokens;
constexpr int kMaxClientCount = 4;
constexpr int kRecvBufferSize = 2048;

// 게임서버의 MAX_SOCKBUF(Define.h) 와 같아야 한다. 이보다 큰 패킷은 게임서버가 통째로 버린다.
constexpr int kMaxWirePacketBytes = 1024;
constexpr int kBatchGatherWaitMs = 15;
constexpr bool kPrintPromptDebug = false;

// 콘솔 테스트 루프. true 면 main 이 콘솔 입력에서 블로킹돼 네트워크 리슨까지 못 간다
// (게임서버가 34444 연결에 실패하고 NpcChat 이 비활성된다). 평소에는 false.
constexpr bool kEnableConsoleTestLoop = false;

// 파일 I/O 테스트 모드 — input.txt(줄마다 JSON) → 추론 → output.txt(JSON) append.
// who별로 history를 분리하므로, 손님이 섞여 들어와도 각각 따로 기억된다.
// (소켓 대신 파일을 버퍼로 쓰는 Step 4. true면 콘솔/네트워크 대신 이 모드만 실행.)
constexpr bool kEnableFileIoMode = false;
constexpr const char* kInputFilePath = "io/input.txt";
constexpr const char* kOutputFilePath = "io/output.txt";

// ==================== declarations ====================

std::string FormatPromptForModel(
	llama_model* model,
	const std::string& systemPrompt,
	const std::vector<ChatTurn>& history,
	const std::string& userInput);

std::string GenerateResponse(
	llama_model* model,
	llama_context* ctx,
	const std::string& formattedPrompt);

std::vector<llama_token> TokenizeText(
	llama_model* model,
	const std::string& text,
	bool addSpecial,
	bool parseSpecial);

std::vector<std::string> GenerateBatchResponses(
	llama_model* model,
	llama_context* ctx,
	const std::vector<std::string>& formattedPrompts,
	const std::vector<const char*>& grammars = {});

std::string LoadTextFileOrDefault(
	const std::filesystem::path& path,
	const std::string& fallbackText);

void RunFileIoMode(
	llama_model* model,
	llama_context* ctx,
	const std::string& systemPrompt);

bool SendAll(SOCKET socket, const std::string& text);

bool SendAllBytes(SOCKET socket, const void* data, int length);

bool SendAiResponse(SOCKET socket, uint32_t userIndex, const std::string& text);

size_t Utf8SafeLength(const std::string& text, size_t maxBytes);

void HandleClient(
	SOCKET clientSocket,
	int clientId,
	class AiBatchWorkerQueue& aiQueue,
	std::atomic<int>& activeClientCount);

// ==================== small utilities ====================

std::string TrimString(const std::string& s)
{
	size_t start = 0;
	while (start < s.size() &&
		std::isspace(static_cast<unsigned char>(s[start])))
	{
		++start;
	}

	size_t end = s.size();
	while (end > start &&
		std::isspace(static_cast<unsigned char>(s[end - 1])))
	{
		--end;
	}

	return s.substr(start, end - start);
}

void RemoveAllOccurrences(std::string& text, const std::string& target)
{
	if (target.empty())
		return;

	size_t pos = 0;
	while ((pos = text.find(target, pos)) != std::string::npos)
	{
		text.erase(pos, target.size());
	}
}

// ── 추론 채널 구간을 "본문째" 제거 ──
// E2B·E4B는 thinking이 기본 켜져 있고, 큰 모델의 "빈 thought 채널 억제 스텁"이
// 학습돼 있지 않다. 그래서 모델이 진짜 <|channel>thought ... <channel|> 를 뱉을 수 있다.
// CleanGemmaControlText는 "마커"만 지우므로, 여기서 여는~닫는 사이를 통째로 제거한다.
// ※ CleanGemmaControlText '이전에' 호출해야 한다(마커가 살아 있어야 구간을 찾음).
std::string StripLeakedReasoning(std::string text)
{
	const std::string chOpen = "<|channel>";
	const std::string chClose = "<channel|>";

	while (true)
	{
		size_t open = text.find(chOpen);
		if (open == std::string::npos)
			break;

		size_t close = text.find(chClose, open);
		if (close == std::string::npos)
		{
			text.erase(open);
			break;
		}

		text.erase(open, (close + chClose.size()) - open);
	}

	return TrimString(text);
}

// Gemma4 control/channel 마커 정리(개별 마커 제거).
std::string CleanGemmaControlText(std::string text)
{
	RemoveAllOccurrences(text, "<|think|>");
	RemoveAllOccurrences(text, "<|channel>thought");
	RemoveAllOccurrences(text, "<|channel|>thought");
	RemoveAllOccurrences(text, "<channel>thought");
	RemoveAllOccurrences(text, "<channel|>thought");

	RemoveAllOccurrences(text, "<|channel>final");
	RemoveAllOccurrences(text, "<|channel|>final");
	RemoveAllOccurrences(text, "<channel>final");
	RemoveAllOccurrences(text, "<channel|>final");

	RemoveAllOccurrences(text, "<channel|>");
	RemoveAllOccurrences(text, "<|channel|>");
	RemoveAllOccurrences(text, "<|channel>");

	RemoveAllOccurrences(text, "<|turn>");
	RemoveAllOccurrences(text, "<turn|>");
	RemoveAllOccurrences(text, "<bos>");
	RemoveAllOccurrences(text, "<eos>");

	return TrimString(text);
}

void StripLeadingRoleLabel(std::string& text)
{
	static const std::vector<std::string> labels =
	{
		"model:",
		"Model:",
		"assistant:",
		"Assistant:",
		"NPC:",
		"AI:"
	};

	bool changed = true;

	while (changed)
	{
		changed = false;
		text = TrimString(text);

		for (const auto& label : labels)
		{
			if (text.size() >= label.size() &&
				text.compare(0, label.size(), label) == 0)
			{
				text.erase(0, label.size());
				changed = true;
				break;
			}
		}
	}

	text = TrimString(text);
}

std::string NormalizeReceivedText(std::string text)
{
	while (!text.empty() &&
		(text.back() == '\n' || text.back() == '\r' || text.back() == '\0'))
	{
		text.pop_back();
	}

	return TrimString(text);
}

bool SendAll(SOCKET socket, const std::string& text)
{
	int totalSent = 0;
	const int sendLen = static_cast<int>(text.size());

	while (totalSent < sendLen)
	{
		int sent = send(
			socket,
			text.c_str() + totalSent,
			sendLen - totalSent,
			0
		);

		if (sent <= 0)
			return false;

		totalSent += sent;
	}

	return true;
}

// UTF-8 문자 중간에서 자르면 깨진 글자가 남는다. 선행 바이트까지 물러난 길이를 준다.
size_t Utf8SafeLength(const std::string& text, size_t maxBytes)
{
	if (text.size() <= maxBytes)
		return text.size();

	size_t cut = maxBytes;
	while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
		--cut;

	return cut;
}

bool SendAllBytes(SOCKET socket, const void* data, int length)
{
	const char* cursor = static_cast<const char*>(data);
	int totalSent = 0;

	while (totalSent < length)
	{
		int sent = send(socket, cursor + totalSent, length - totalSent, 0);

		if (sent <= 0)
			return false;

		totalSent += sent;
	}

	return true;
}

// AS_AI(헤더 + userIndex + 가변 UTF-8) 한 장으로 답을 실어 보낸다.
// 게임서버는 이 userIndex 로 어느 플레이어에게 중계할지 찾는다.
bool SendAiResponse(SOCKET socket, uint32_t userIndex, const std::string& text)
{
	// 게임서버 버퍼를 넘으면 통째로 버려지므로 여기서 잘라 보낸다.
	const size_t maxTextBytes = kMaxWirePacketBytes - sizeof(AS_AI);
	const size_t textLen = Utf8SafeLength(text, maxTextBytes);

	if (textLen < text.size())
	{
		Log::Warning("Response truncated {} -> {} bytes (userIndex={})",
			text.size(), textLen, userIndex);
	}

	const size_t totalSize = sizeof(AS_AI) + textLen;

	std::vector<char> buffer(totalSize, 0);
	auto* header = reinterpret_cast<AS_AI*>(buffer.data());
	header->size = static_cast<uint16_t>(totalSize);
	header->id = static_cast<uint16_t>(PACKET_ID::AS_AI);
	header->userIndex = userIndex;

	if (textLen > 0)
		std::memcpy(buffer.data() + sizeof(AS_AI), text.data(), textLen);

	return SendAllBytes(socket, buffer.data(), static_cast<int>(totalSize));
}

bool TokenToPiece(
	const llama_vocab* vocab,
	llama_token token,
	std::string& outPiece)
{
	if (vocab == nullptr)
		return false;

	std::vector<char> pieceBuffer(256);

	int32_t pieceLen = llama_token_to_piece(
		vocab,
		token,
		pieceBuffer.data(),
		static_cast<int32_t>(pieceBuffer.size()),
		0,
		true
	);

	if (pieceLen < 0)
	{
		pieceBuffer.resize(static_cast<size_t>(-pieceLen) + 8);

		pieceLen = llama_token_to_piece(
			vocab,
			token,
			pieceBuffer.data(),
			static_cast<int32_t>(pieceBuffer.size()),
			0,
			true
		);
	}

	if (pieceLen < 0)
		return false;

	if (pieceLen == 0)
	{
		outPiece.clear();
		return true;
	}

	outPiece.assign(pieceBuffer.data(), static_cast<size_t>(pieceLen));
	return true;
}

// ----------------------------------------------------------------------------
//  TokensToString — 여러 토큰을 "한 번에" 문자열로 만든다.
//
//  [왜 필요한가]
//  한글 한 글자는 UTF-8에서 3바이트다(예: "저" = EC A0 80). 그런데 모델의
//  토큰(BPE)은 글자 단위가 아니라서, 한글 한 글자가 토큰 2개에 걸쳐 나올 수
//  있다(토큰A=[EC A0], 토큰B=[80]). 이때 토큰을 '하나씩' llama_token_to_piece로
//  바꿔 이어붙이면, 토큰A만 변환하는 순간 'EC A0'는 완성되지 않은 UTF-8이라
//  U+FFFD(�)로 깨질 수 있다. 글자가 완성되기 전에 성급히 디코딩해서 생기는 문제다.
//
//  [해결]
//  토큰을 즉시 변환하지 않고 ID만 모아 두었다가, 생성이 끝난 뒤 전체 토큰 배열을
//  llama_detokenize로 '통째로' 디코딩한다. 글자 경계가 전부 배열 안에 들어있으므로
//  토큰 경계에서 글자가 쪼개져도 올바른 UTF-8로 복원된다.
// ----------------------------------------------------------------------------
std::string TokensToString(
	const llama_vocab* vocab,
	const std::vector<llama_token>& tokens)
{
	if (vocab == nullptr || tokens.empty())
		return std::string();

	// 토큰당 평균 몇 바이트로 잡고 넉넉히 확보(부족하면 아래서 재시도).
	std::vector<char> buffer(tokens.size() * 4 + 64);

	int32_t written = llama_detokenize(
		vocab,
		tokens.data(),
		static_cast<int32_t>(tokens.size()),
		buffer.data(),
		static_cast<int32_t>(buffer.size()),
		false,   // remove_special : 특수 토큰을 지우지 않음(정제 함수가 따로 처리)
		true     // unparse_special: 특수 토큰을 텍스트로 렌더(기존 TokenToPiece와 동일)
	);

	// 버퍼가 부족하면 음수(-필요크기)를 반환 → 정확한 크기로 재시도.
	if (written < 0)
	{
		buffer.resize(static_cast<size_t>(-written));

		written = llama_detokenize(
			vocab,
			tokens.data(),
			static_cast<int32_t>(tokens.size()),
			buffer.data(),
			static_cast<int32_t>(buffer.size()),
			false,
			true
		);
	}

	if (written < 0)
		return std::string();   // 그래도 실패하면 빈 문자열

	return std::string(buffer.data(), static_cast<size_t>(written));
}

// ==================== AI batch worker queue ====================

struct AiRequest
{
	int clientId = -1;
	std::string userText;
	std::vector<ChatTurn>* history = nullptr;
	std::shared_ptr<std::promise<std::string>> result;
	bool agentStep = false;
	bool sayOnly = false; // agent step that must end the turn with dialogue
	const char* systemPromptOverride = nullptr;
};

class AiBatchWorkerQueue
{
public:
	AiBatchWorkerQueue() = default;

	AiBatchWorkerQueue(const AiBatchWorkerQueue&) = delete;
	AiBatchWorkerQueue& operator=(const AiBatchWorkerQueue&) = delete;

	~AiBatchWorkerQueue()
	{
		Stop();
	}

	void Start(
		llama_model* model,
		llama_context* ctx,
		const std::string& systemPrompt)
	{
		model_ = model;
		ctx_ = ctx;
		systemPrompt_ = &systemPrompt;

		{
			std::lock_guard<std::mutex> lock(mutex_);
			running_ = true;
		}

		workerThread_ = std::thread(&AiBatchWorkerQueue::WorkerLoop, this);
	}

	void Stop()
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);

			if (!running_ && !workerThread_.joinable())
				return;

			running_ = false;
		}

		condition_.notify_all();

		if (workerThread_.joinable())
			workerThread_.join();
	}

	// Whether an agent step fits the per-sequence prompt budget. The chat template adds only a few tokens.
	bool FitsAgentPrompt(const std::string& userText) const
	{
		constexpr int kTemplateTokens = 32;
		const auto tokens = TokenizeText(model_, std::string(NpcAgent::SystemPrompt) + userText, false, false);
		return static_cast<int>(tokens.size()) + kTemplateTokens <= kPromptBudgetPerSeq;
	}

	std::future<std::string> Submit(
		int clientId,
		const std::string& userText,
		std::vector<ChatTurn>& history,
		bool agentStep = false,
		const char* systemPromptOverride = nullptr,
		bool sayOnly = false)
	{
		auto request = std::make_shared<AiRequest>();
		request->clientId = clientId;
		request->userText = userText;
		request->history = &history;
		request->agentStep = agentStep;
		request->systemPromptOverride = systemPromptOverride;
		request->sayOnly = sayOnly;
		request->result = std::make_shared<std::promise<std::string>>();

		std::future<std::string> future = request->result->get_future();

		{
			std::lock_guard<std::mutex> lock(mutex_);

			if (!running_)
			{
				request->result->set_value("(AI worker is not running)");
				return future;
			}

			requestQueue_.push(request);
			Log::Info("AI request queued. clientId={}, queueSize={}",
				clientId,
				requestQueue_.size());
		}

		condition_.notify_one();

		return future;
	}

private:
	void WorkerLoop()
	{
		Log::Info("AI batch worker thread started");

		while (true)
		{
			std::vector<std::shared_ptr<AiRequest>> batchRequests;

			{
				std::unique_lock<std::mutex> lock(mutex_);

				condition_.wait(
					lock,
					[this]()
					{
						return !requestQueue_.empty() || !running_;
					}
				);

				if (!running_ && requestQueue_.empty())
					break;

				batchRequests.push_back(requestQueue_.front());
				requestQueue_.pop();

				if (requestQueue_.empty() && running_)
				{
					condition_.wait_for(
						lock,
						std::chrono::milliseconds(kBatchGatherWaitMs),
						[this]()
						{
							return !requestQueue_.empty() || !running_;
						}
					);
				}

				while (!requestQueue_.empty() &&
					static_cast<int>(batchRequests.size()) < kMaxBatchSize)
				{
					batchRequests.push_back(requestQueue_.front());
					requestQueue_.pop();
				}

				Log::Info("AI batch dequeued. batchSize={}, remainingQueueSize={}",
					batchRequests.size(),
					requestQueue_.size());
			}

			ProcessBatch(batchRequests);
		}

		Log::Info("AI batch worker thread stopped");
	}

	void ProcessBatch(const std::vector<std::shared_ptr<AiRequest>>& batchRequests)
	{
		if (batchRequests.empty())
			return;

		if (model_ == nullptr || ctx_ == nullptr || systemPrompt_ == nullptr)
		{
			for (const auto& request : batchRequests)
			{
				if (request && request->result)
					request->result->set_value("(AI worker internal error)");
			}

			return;
		}

		std::vector<std::shared_ptr<AiRequest>> validRequests;
		std::vector<std::string> formattedPrompts;

		validRequests.reserve(batchRequests.size());
		formattedPrompts.reserve(batchRequests.size());

		for (const auto& request : batchRequests)
		{
			if (!request || !request->result || request->history == nullptr)
				continue;

			try
			{
				std::string formattedPrompt =
					FormatPromptForModel(
						model_,
						request->systemPromptOverride ? std::string(request->systemPromptOverride) :
							request->agentStep ? std::string(NpcAgent::SystemPrompt) : *systemPrompt_,
						*request->history,
						request->userText
					);

				if (formattedPrompt.empty())
				{
					request->result->set_value("(prompt formatting failed)");
					continue;
				}

				if constexpr (kPrintPromptDebug)
				{
					std::cout << "\n[DEBUG PROMPT BEGIN clientId="
						<< request->clientId << "]\n";
					std::cout << formattedPrompt << "\n";
					std::cout << "[DEBUG PROMPT END]\n\n";
				}

				validRequests.push_back(request);
				formattedPrompts.push_back(std::move(formattedPrompt));
			}
			catch (const std::exception& e)
			{
				Log::Error("Prompt formatting exception. clientId={}, error={}",
					request->clientId,
					e.what());

				request->result->set_value("(prompt formatting exception)");
			}
			catch (...)
			{
				Log::Error("Prompt formatting unknown exception. clientId={}",
					request->clientId);

				request->result->set_value("(prompt formatting unknown exception)");
			}
		}

		if (validRequests.empty())
			return;

		std::vector<std::string> responses;

		try
		{
			std::vector<const char*> grammars;
			for (const auto& request:validRequests)
				grammars.push_back(!request->agentStep ? nullptr : request->sayOnly ? NpcAgent::SayGrammar : NpcAgent::Grammar);
			responses = GenerateBatchResponses(model_, ctx_, formattedPrompts,grammars);
		}
		catch (const std::exception& e)
		{
			Log::Error("GenerateBatchResponses exception: {}", e.what());

			for (const auto& request : validRequests)
			{
				request->result->set_value("(batch generation exception)");
			}

			return;
		}
		catch (...)
		{
			Log::Error("GenerateBatchResponses unknown exception");

			for (const auto& request : validRequests)
			{
				request->result->set_value("(batch generation unknown exception)");
			}

			return;
		}

		if (responses.size() != validRequests.size())
		{
			Log::Error("Batch response size mismatch. requests={}, responses={}",
				validRequests.size(),
				responses.size());

			for (const auto& request : validRequests)
			{
				request->result->set_value("(batch generation failed)");
			}

			return;
		}

		for (size_t i = 0; i < validRequests.size(); ++i)
		{
			auto& request = validRequests[i];
			std::string aiText = responses[i];

			const bool validResponse = !aiText.empty();

			if (!validResponse)
				aiText = "(No response)";

			if (!request->agentStep) request->history->push_back({ "user", request->userText });

			if (validResponse && !request->agentStep)
			{
				request->history->push_back({ "assistant", aiText });
			}

			request->result->set_value(aiText);
		}
	}

private:
	std::mutex mutex_;
	std::condition_variable condition_;
	std::queue<std::shared_ptr<AiRequest>> requestQueue_;
	std::thread workerThread_;
	bool running_ = false;

	llama_model* model_ = nullptr;
	llama_context* ctx_ = nullptr;
	const std::string* systemPrompt_ = nullptr;
};

// ==================== shared-link request dispatch ====================

bool SendAgentResponse(SOCKET socket, uint32_t playerId, const std::string& context, const std::string& say, bool completed)
{
	auto body=NpcMcp::Json{{"context",context},{"say",say},{"completed",completed}}.dump();
	if (body.size()+sizeof(AS_AI)>kMaxWirePacketBytes)
		body=NpcMcp::Json{{"context",context},{"say","답변이 너무 길어 다시 정리해야겠네."},{"completed",false}}.dump();
	std::vector<char> data(sizeof(AS_AI)+body.size());
	auto* header=reinterpret_cast<AS_AI*>(data.data());
	header->size=static_cast<uint16_t>(data.size()); header->id=static_cast<uint16_t>(PACKET_ID::AS_AI_AGENT); header->userIndex=playerId;
	std::memcpy(data.data()+sizeof(AS_AI),body.data(),body.size());
	return SendAllBytes(socket,data.data(),static_cast<int>(data.size()));
}

// 수신은 스레드 하나가 맡고, 서로 다른 대화는 일꾼 스레드들이 나눠 처리한다.
// 같은 손님의 일(초기화·요약 포함)은 도착한 순서대로 끝난다.
class ConversationWorkQueue
{
public:
	ConversationWorkQueue()
	{
		try
		{
			for (int i = 0; i < kMaxBatchSize; ++i)
				threads_.emplace_back([this] { Run(); });
		}
		catch (...) { Stop(); throw; }
	}
	~ConversationWorkQueue() { Stop(); }
	bool Submit(const std::string& key, std::function<void()> job)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (stopping_ || outstanding_ >= 128) return false;
		auto [lane, inserted] = lanes_.try_emplace(key);
		lane->second.push_back(std::move(job));
		if (inserted) ready_.push(key);
		++outstanding_;
		condition_.notify_one();
		return true;
	}
	void Stop()
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);
			stopping_ = true;
		}
		condition_.notify_all();
		for (auto& thread : threads_) if (thread.joinable()) thread.join();
		// 실행 중이던 일이 모두 끝났다: 붙잡고 있던 대화 기억을 이제 지워도 안전하다.
		lanes_.clear();
	}
private:
	void Run()
	{
		while (true)
		{
			std::string key;
			std::function<void()> job;
			{
				std::unique_lock<std::mutex> lock(mutex_);
				condition_.wait(lock, [this] { return stopping_ || !ready_.empty(); });
				if (stopping_) return;
				key = ready_.front(); ready_.pop();
				auto& lane = lanes_.at(key);
				job = std::move(lane.front()); lane.pop_front();
			}
			try { job(); }
			catch (const std::exception& e) { Log::Error("Conversation job failed: {}", e.what()); }
			catch (...) { Log::Error("Conversation job failed"); }
			{
				std::lock_guard<std::mutex> lock(mutex_);
				--outstanding_;
				auto lane = lanes_.find(key);
				if (lane->second.empty()) lanes_.erase(lane);
				else if (!stopping_) { ready_.push(key); condition_.notify_one(); }
			}
		}
	}
	std::mutex mutex_;
	std::condition_variable condition_;
	std::map<std::string, std::deque<std::function<void()>>> lanes_;
	std::queue<std::string> ready_;
	std::vector<std::thread> threads_;
	size_t outstanding_ = 0;
	bool stopping_ = false;
};

// recv 는 한 곳에서만 한다. 보내는 프레임은 MCP 를 포함해 통째로 직렬화한다.
// 게임서버가 RPC id 를 재사용할 수 있으므로 도구 교환만 직렬화하고, 추론은 배칭되게 둔다.
class SharedAiLink
{
public:
	explicit SharedAiLink(SOCKET socket) : socket_(socket) {}
	bool Closed() const { return closed_.load(); }
	void Close()
	{
		// 끊김 신호를 놓치지 않도록 대기 조건과 같은 잠금으로 묶는다.
		bool first;
		{
			std::lock_guard<std::mutex> lock(replyMutex_);
			first = !closed_.exchange(true);
		}
		if (first) shutdown(socket_, SD_BOTH);
		replyCondition_.notify_all();
	}
	template<class Writer> bool Send(Writer writer)
	{
		std::lock_guard<std::mutex> lock(sendMutex_);
		if (Closed()) return false;
		if (writer(socket_)) return true;
		Close();
		return false;
	}
	void Deliver(const NpcMcp::Json& response)
	{
		std::lock_guard<std::mutex> lock(replyMutex_);
		if (waiting_ && !ready_ && response.is_object() && response.contains("id") && response["id"] == expectedId_)
		{
			reply_ = response; ready_ = true;
			replyCondition_.notify_one();
		}
	}
	NpcMcp::Json Exchange(const NpcMcp::Json& request)
	{
		std::lock_guard<std::mutex> rpcLock(rpcMutex_);
		if (Closed()) throw std::runtime_error("MCP disconnected");
		const auto body = request.dump();
		if (sizeof(PacketHeader) + body.size() > kMaxWirePacketBytes)
			throw std::runtime_error("MCP request exceeds wire packet limit");
		std::vector<char> data(sizeof(PacketHeader) + body.size());
		PacketHeader header{};
		header.size = static_cast<uint16_t>(data.size());
		header.id = static_cast<uint16_t>(PACKET_ID::AS_MCP);
		std::memcpy(data.data(), &header, sizeof(header));
		std::memcpy(data.data() + sizeof(header), body.data(), body.size());
		const bool expectsReply = request.contains("id");
		{
			std::lock_guard<std::mutex> lock(replyMutex_);
			waiting_ = expectsReply; ready_ = false;
			if (expectsReply) expectedId_ = request["id"];
		}
		if (!Send([&](SOCKET socket) { return SendAllBytes(socket, data.data(), static_cast<int>(data.size())); }))
			throw std::runtime_error("MCP send failed");
		if (!expectsReply) return nullptr;
		std::unique_lock<std::mutex> lock(replyMutex_);
		const bool signaled = replyCondition_.wait_for(lock, std::chrono::seconds(5),
			[this] { return ready_ || Closed(); });
		waiting_ = false;
		if (!signaled)
		{
			lock.unlock();
			// 늦게 온 답이 같은 id 로 다음 호출에 붙는 일을 막는다.
			Close();
			throw std::runtime_error("MCP response timeout");
		}
		if (Closed()) throw std::runtime_error("MCP disconnected");
		return std::move(reply_);
	}
private:
	SOCKET socket_;
	std::atomic<bool> closed_{false};
	std::mutex sendMutex_, rpcMutex_, replyMutex_;
	std::condition_variable replyCondition_;
	bool waiting_ = false, ready_ = false;
	NpcMcp::Json expectedId_, reply_;
};

struct ClientConversationState
{
	ConversationStore conversations;
	std::mutex agentMutex;
	std::map<std::string, std::shared_ptr<NpcAgent::Memory>> agentHistory;
	std::shared_ptr<NpcAgent::Memory> AgentMemory(const std::string& key, bool create)
	{
		std::lock_guard<std::mutex> lock(agentMutex);
		auto found = agentHistory.find(key);
		if (found != agentHistory.end()) return found->second;
		if (!create) return {};
		if (agentHistory.size() >= 128)
		{
			// 지금 대화 중인 기억은 지우지 않는다.
			auto idle = std::find_if(agentHistory.begin(), agentHistory.end(),
				[](const auto& entry) { return entry.second.use_count() == 1; });
			if (idle == agentHistory.end()) throw std::runtime_error("NPC memory capacity reached");
			agentHistory.erase(idle);
		}
		auto memory = std::make_shared<NpcAgent::Memory>();
		agentHistory.emplace(key, memory);
		return memory;
	}
};

void ProcessClientRequest(SharedAiLink& link, ClientConversationState& state,
	AiBatchWorkerQueue& aiQueue, int clientId, SA_AI request, const std::string& userText)
{
	if (link.Closed()) return;
	if (request.id == static_cast<uint16_t>(PACKET_ID::SA_AI_AGENT))
	{
		std::string context, say;
		bool completed = false;
		try
		{
			const auto envelope = NpcMcp::Json::parse(userText);
			context = NpcMcp::Text(envelope, "context", 32);
			const auto conversation = NpcMcp::Text(envelope, "conversation", 32);
			const auto speech = NpcMcp::Text(envelope, "say", 600);
			if (context.size() != 32 || conversation.size() != 32 || speech.empty())
				throw std::runtime_error("Invalid NPC envelope");
			auto memory = state.AgentMemory(conversation, true);
			if (speech == "/reset") *memory = {};
			Log::Info("[Agent] customer: {}", speech);
			NpcAgent::Client mcp(context, [&](const auto& rpc)
			{
				auto response = link.Exchange(rpc);
				if (rpc.value("method", std::string{}) == "tools/call")
					Log::Info("[Agent] tool {} {} -> {}", NpcMcp::Text(rpc["params"], "name", 64),
						rpc["params"].value("arguments", NpcMcp::Json::object()).dump(), response.value("result", response).dump());
				return response;
			});
			say = NpcAgent::Run(mcp, speech, *memory, [&](const std::string& prompt, bool sayOnly)
			{
				if (link.Closed()) throw std::runtime_error("Client disconnected");
				std::vector<ChatTurn> scratch;
				auto output = aiQueue.Submit(clientId, prompt, scratch, true, nullptr, sayOnly).get();
				Log::Info("[Agent] model: {}", output);
				return output;
			}, [&](const std::string& prompt) { return aiQueue.FitsAgentPrompt(prompt); });
			memory->turns.push_back({{"customer", speech}, {"npc", say}});
			// 넉넉히 쌓아두고, 프롬프트에는 토큰 한도가 허용하는 만큼만 최근 턴을 넣는다.
			if (memory->turns.size() > 200) memory->turns.erase(memory->turns.begin());
			completed = true;
		}
		catch (const std::exception& e)
		{
			Log::Warning("[Agent] turn failed: {}", e.what());
			say = "지금은 상점 정보를 확인할 수 없네. 잠시 후 다시 이야기해 주겠나?";
		}
		link.Send([&](SOCKET socket) { return SendAgentResponse(socket, request.userIndex, context, say, completed); });
		return;
	}
	if (request.id == static_cast<uint16_t>(PACKET_ID::SA_AI_SUMMARIZE))
	{
		// 상점 창을 닫은 뒤라 이 연결에서 답을 기다리는 사람은 없다.
		const auto envelope = NpcMcp::Json::parse(userText, nullptr, false);
		auto memory = state.AgentMemory(NpcMcp::Text(envelope, "conversation", 32), false);
		if (!memory) return;
		const auto started = std::chrono::steady_clock::now();
		const bool compressed = NpcAgent::Compress(*memory, [&](const std::string& input)
		{
			if (link.Closed()) throw std::runtime_error("Client disconnected");
			std::vector<ChatTurn> scratch;
			auto notes = TrimString(aiQueue.Submit(clientId, input, scratch, false, NpcAgent::SummaryPrompt).get());
			return notes.starts_with("-") ? notes : std::string{}; // 실패하면 "(...)" 로 돌아온다
		});
		if (compressed)
			Log::Info("[Agent] memory compressed in {} ms, {} recent turns kept:\n{}",
				std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count(),
				memory->turns.size(), memory->notes);
		return;
	}

	const auto text = NormalizeReceivedText(userText);
	if (text.empty()) return;
	const auto who = std::to_string(request.userIndex);
	if (text == "/reset")
	{
		state.conversations.Reset(who);
		link.Send([&](SOCKET socket) { return SendAiResponse(socket, request.userIndex, "대화 기억을 초기화했습니다."); });
		return;
	}
	// 종료 명령은 두지 않는다. 게임서버와의 연결은 공유 링크라
	// 플레이어가 친 한 마디로 끊기면 모두의 NPC 대화가 죽는다.
	Log::Info("Received. clientId={} userIndex={} npcIndex={} text={}", clientId, request.userIndex, request.npcIndex, text);
	auto& history = state.conversations.GetOrCreate(who);
	std::string output;
	try { output = aiQueue.Submit(clientId, text, history).get(); }
	catch (const std::exception& e)
	{
		Log::Error("AI response failed: {}", e.what());
		output = "(Failed to get AI response)";
	}
	link.Send([&](SOCKET socket) { return SendAiResponse(socket, request.userIndex, output); });
}

void HandleClient(SOCKET clientSocket, int clientId, AiBatchWorkerQueue& aiQueue, std::atomic<int>& activeClientCount)
{
	// 소켓은 모든 작업이 끝날 때까지 살아 있고, shutdown 이 먼저 I/O 를 끊는다.
	struct SocketGuard
	{
		SOCKET socket; std::atomic<int>& count;
		~SocketGuard() { closesocket(socket); --count; }
	} guard{clientSocket, activeClientCount};
	const DWORD sendTimeout = 5000;
	setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&sendTimeout), sizeof(sendTimeout));
	SharedAiLink link(clientSocket);
	try
	{
		ClientConversationState state;
		ConversationWorkQueue jobs;
		// 예외로 빠져나갈 때도 jobs/state 보다 먼저 돌아 대기 중인 작업을 깨운다.
		struct LinkGuard { SharedAiLink& link; ~LinkGuard() { link.Close(); } } linkGuard{link};
		std::string stream;
		char buffer[kRecvBufferSize];
		Log::Info("Client connected. clientId={}", clientId);
		while (!link.Closed())
		{
			const auto received = recv(clientSocket, buffer, sizeof(buffer), 0);
			if (received <= 0) break;
			stream.append(buffer, received);
			while (!link.Closed() && stream.size() >= sizeof(PacketHeader))
			{
				PacketHeader header{};
				std::memcpy(&header, stream.data(), sizeof(header));
				if (header.size < sizeof(PacketHeader) || header.size > kMaxWirePacketBytes)
					throw std::runtime_error("Invalid AI frame size");
				if (stream.size() < header.size) break;
				const auto packet = stream.substr(0, header.size);
				stream.erase(0, header.size);
				if (header.id == static_cast<uint16_t>(PACKET_ID::SA_MCP))
				{
					link.Deliver(NpcMcp::Json::parse(packet.substr(sizeof(PacketHeader)), nullptr, false));
					continue;
				}
				if (header.id != static_cast<uint16_t>(PACKET_ID::SA_AI) &&
					header.id != static_cast<uint16_t>(PACKET_ID::SA_AI_AGENT) &&
					header.id != static_cast<uint16_t>(PACKET_ID::SA_AI_SUMMARIZE))
				{
					Log::Warning("Unexpected packet id={} - skipped. clientId={}", header.id, clientId);
					continue;
				}
				if (header.size < sizeof(SA_AI)) throw std::runtime_error("Short AI frame");
				SA_AI request{};
				std::memcpy(&request, packet.data(), sizeof(request));
				const auto text = packet.substr(sizeof(request));
				// 손님별(일반 대화)·대화별(상인) 차선으로 나눠 순서를 지킨다.
				std::string key = "chat:" + std::to_string(request.userIndex);
				if (header.id != static_cast<uint16_t>(PACKET_ID::SA_AI))
				{
					const auto envelope = NpcMcp::Json::parse(text, nullptr, false);
					const auto conversation = NpcMcp::Text(envelope, "conversation", 32);
					if (conversation.size() != 32) throw std::runtime_error("Invalid conversation id");
					key = "agent:" + conversation;
				}
				if (!jobs.Submit(key, [&, request, text]
				{
					try { ProcessClientRequest(link, state, aiQueue, clientId, request, text); }
					catch (...) { link.Close(); throw; }
				}))
					throw std::runtime_error("AI connection queue full (128 outstanding requests)");
			}
		}
	}
	catch (const std::exception& e) { Log::Warning("Client {} stopped: {}", clientId, e.what()); }
	catch (...) { Log::Warning("Client {} stopped after an unexpected error", clientId); }
	link.Close();
	Log::Info("Client thread ended. clientId={}", clientId);
}


// ==================== main ====================

std::string ResolveModelPath()
{
	namespace fs = std::filesystem;
	constexpr auto filename = "gemma-4-31B-it-Q8_0.gguf";
	std::vector<fs::path> candidates;
	auto add = [&](const fs::path& path)
	{
		const auto absolute = fs::absolute(path).lexically_normal();
		if (std::find(candidates.begin(), candidates.end(), absolute) == candidates.end())
			candidates.push_back(absolute);
	};
	size_t envSize = 0;
	if (_wgetenv_s(&envSize, nullptr, 0, L"TI_AI_MODEL") != 0)
		throw std::runtime_error("Cannot read TI_AI_MODEL");
	if (envSize > 1)
	{
		std::vector<wchar_t> value(envSize);
		if (_wgetenv_s(&envSize, value.data(), value.size(), L"TI_AI_MODEL") != 0)
			throw std::runtime_error("Cannot read TI_AI_MODEL");
		// An explicit override must not silently load a different model.
		add(fs::path(value.data()));
	}
	else
	{
		std::vector<wchar_t> module(32768);
		const auto length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
		if (!length || length >= module.size()) throw std::runtime_error("Cannot resolve executable path");
		const auto exeDir = fs::path(std::wstring(module.data(), length)).parent_path();
		add(exeDir / "models" / filename);
		add(fs::current_path() / "models" / filename);
		// Source builds may place executables in a different output directory than the model.
		for (const auto& root : {exeDir, fs::current_path()})
		{
			for (auto dir = root; !dir.empty();)
			{
				for (const auto& project : {dir, dir / "Server", dir / "AI/Server/Server"})
				{
					std::error_code error;
					if (!fs::is_regular_file(project / "Server.vcxproj", error)) continue;
					if (!fs::is_regular_file(project / "main.cpp", error)) continue;
					add(project / "models" / filename);
					add(project / "x64/Release/models" / filename);
					add(project / "x64/Debug/models" / filename);
					add(project.parent_path() / "models" / filename);
					add(project.parent_path() / "x64/Release/models" / filename);
					add(project.parent_path() / "x64/Debug/models" / filename);
				}
				const auto parent = dir.parent_path();
				if (parent == dir) break;
				dir = parent;
			}
		}
	}
	for (const auto& candidate : candidates)
	{
		std::error_code error;
		if (!fs::is_regular_file(candidate, error)) continue;
		const auto utf8 = candidate.u8string();
		return std::string(utf8.begin(), utf8.end());
	}
	Log::Error("GGUF not found. Set TI_AI_MODEL to the full path of an existing .gguf file.");
	for (const auto& candidate : candidates)
	{
		const auto utf8 = candidate.u8string();
		Log::Error("Checked model path: {}", std::string(utf8.begin(), utf8.end()));
	}
	return {};
}

// llama.cpp/ggml 로그 필터. DEBUG("CUDA Graph id N reused" 등)는 버리고 INFO 이상만 출력한다.
// CONT 는 직전 로그의 이어쓰기라 직전 레벨을 따른다.
static void LlamaLogCallback(ggml_log_level level, const char* text, void* /*user_data*/)
{
	static ggml_log_level lastLevel = GGML_LOG_LEVEL_INFO;
	if (level != GGML_LOG_LEVEL_CONT) lastLevel = level;
	if (lastLevel == GGML_LOG_LEVEL_DEBUG) return;
	fputs(text, stderr);
}

int main(int argc, char** argv)
{
	// 콘솔 입출력을 UTF-8로 고정한다.
	// - SetConsoleOutputCP : 콘솔에 찍는 한글(UTF-8)이 깨지지 않게.
	// - SetConsoleCP       : 콘솔로 입력받는 한글(UTF-8)이 깨지지 않게.
	// (모델 출력/입력이 전부 UTF-8이므로 둘 다 맞춰 준다.)
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);

	std::string model_path;
	try { model_path = ResolveModelPath(); }
	catch (const std::exception& error) { Log::Error("Model path error: {}", error.what()); return -1; }
	if (model_path.empty()) return -1;
	Log::Info("GGUF model: {}", model_path);
	if (argc == 2 && std::string(argv[1]) == "--check-model-path") return 0;

	llama_log_set(LlamaLogCallback, nullptr);
	llama_backend_init();
	ggml_backend_load_all();

	struct BackendGuard
	{
		~BackendGuard()
		{
			llama_backend_free();
		}
	} backendGuard;

	llama_model_params model_params = llama_model_default_params();
	model_params.n_gpu_layers = 99;
	// GPU 가 여러 장이면 레이어를 고르게 나눈다(31B 는 한 장에 안 들어간다). 한 장이면 기본값 그대로.
	std::vector<ggml_backend_dev_t> gpuDevices;
	for (size_t i = 0; i < ggml_backend_dev_count(); ++i)
	{
		ggml_backend_dev_t device = ggml_backend_dev_get(i);
		if (device && ggml_backend_dev_type(device) == GGML_BACKEND_DEVICE_TYPE_GPU) gpuDevices.push_back(device);
	}
	std::vector<float> gpuSplit(llama_max_devices(), 0.f);
	if (gpuDevices.size() > 1 && gpuSplit.size() >= gpuDevices.size())
	{
		gpuDevices.push_back(nullptr); // devices is a null-terminated list
		for (size_t i = 0; i + 1 < gpuDevices.size(); ++i) gpuSplit[i] = 1.f;
		model_params.devices = gpuDevices.data();
		model_params.split_mode = LLAMA_SPLIT_MODE_LAYER;
		model_params.tensor_split = gpuSplit.data();
		Log::Info("GPU layout: {} devices, layer split evenly (weights and layer KV caches)", gpuDevices.size() - 1);
	}

	Log::Info("GGUF를 로딩중...");
	Log::Info("Loading .gguf file...");

	llama_model_ptr model(llama_model_load_from_file(model_path.c_str(), model_params));

	if (!model)
	{
		Log::Error("GGUF file exists, but loading failed: {}. Check the llama.cpp errors above (format, access or memory).", model_path);
		return -1;
	}

	// (참고) 모델 내장 chat template 덤프 — 수동 마커가 맞는지 눈으로 확인용.
	{
		const char* tmpl = llama_model_chat_template(model.get(), nullptr);
		std::cout << "\n[Debug] ===== embedded chat_template begin =====\n"
			<< (tmpl ? tmpl : "(none)")
			<< "\n[Debug] ===== embedded chat_template end =====\n\n";
	}

	llama_context_params ctx_params = llama_context_default_params();
	ctx_params.n_ctx = kTotalContextTokens;
	ctx_params.n_batch = kPrefillChunkTokens;
	ctx_params.n_ubatch = kPrefillChunkTokens;
	// 시퀀스들이 KV 를 한 풀에서 나눠 쓴다(GPU 분할과는 무관). 나머지는 큰 모델 메모리를 아끼는 설정.
	ctx_params.kv_unified = true;
	ctx_params.swa_full = false;
	ctx_params.type_k = GGML_TYPE_F16;
	ctx_params.type_v = GGML_TYPE_F16;
	ctx_params.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_ENABLED;

	// (A) 배칭 필수: 시퀀스를 최대 kMaxBatchSize개까지 동시 보유.
	//     이게 1(기본값)이면 배칭 2번째 시퀀스에서 invalid seq_id 크래시.
	//     n_ctx(32768)가 seq당 8192로 나뉜다(= kContextTokensPerSeq). 프롬프트 3161 충분.
	ctx_params.n_seq_max = kMaxBatchSize;

	llama_context_ptr ctx(llama_init_from_model(model.get(), ctx_params));
	if (!ctx)
	{
		Log::Error("AI Context 할당 실패!");
		Log::Error("AI Context allocate failure!");
		return -1;
	}

	const std::string fallbackSystemPrompt =
		"You are an in-game NPC for roleplay conversation. "
		"Always answer naturally in Korean. "
		"Stay in character and reply like spoken dialogue. "
		"Be friendly, warm, respectful, and helpful. "
		"Do not mock, insult, scold, or belittle the player. "
		"Do not be sarcastic or passive-aggressive. "
		"Keep answers concise and direct. "
		"Do not output analysis. "
		"Do not output hidden thoughts. "
		"Do not output channel tags. "
		"Output only the NPC's spoken reply.";

	const std::string systemPrompt = LoadTextFileOrDefault(
		"systemPrompt/npc_prompt.txt",
		fallbackSystemPrompt
	);

	Log::Info("AI 준비 완료");
	Log::Info("AI now ready");
	Log::Info("Batch mode: maxBatchSize={}, perSeqCtx={}, totalCtx={}",
		kMaxBatchSize,
		kContextTokensPerSeq,
		kTotalContextTokens);

	// ==================== file I/O test mode (Step 4) ====================
	// 켜져 있으면 파일 모드만 실행하고 종료(콘솔/네트워크로 안 넘어감).
	if constexpr (kEnableFileIoMode)
	{
		RunFileIoMode(model.get(), ctx.get(), systemPrompt);
		return 0;
	}

	// ==================== optional console test loop ====================

	if constexpr (kEnableConsoleTestLoop)
	{
		std::vector<ChatTurn> consoleHistory;

		printf("\n=== 콘솔 테스트 모드 ===\n");
		printf("exit  : 종료\n");
		printf("/reset: 대화 기억 초기화\n");
		printf("/batch: 배칭 벤치마크 (배칭 1회 vs 순차 4회 속도 비교)\n\n");

		while (true)
		{
			std::string input;
			std::print("You > ");
			std::getline(std::cin, input);

			if (input.empty())
				continue;

			if (input == "exit")
				break;

			if (input == "/reset")
			{
				consoleHistory.clear();
				llama_memory_clear(llama_get_memory(ctx.get()), true);
				std::print("AI  > 대화 기억을 초기화했습니다.\n\n");
				continue;
			}

			// ── (B) 배칭 벤치마크 ──
			if (input == "/batch")
			{
				// 같은 4개 손님 입력 → 프롬프트 4개(실제 시스템 프롬프트 사용).
				const std::vector<std::string> benchInputs =
				{
					"포션 하나 주시오.",
					"여기 좋은 검도 파시오?",
					"10달러는 비싸오. 좀 깎아주시오.",
					"현금은 없고 이 보석으로 대신하면 안 되겠소?"
				};

				std::vector<std::string> benchPrompts;
				benchPrompts.reserve(benchInputs.size());
				{
					std::vector<ChatTurn> emptyHist;
					for (const auto& in : benchInputs)
					{
						std::string p =
							FormatPromptForModel(model.get(), systemPrompt, emptyHist, in);
						if (!p.empty())
							benchPrompts.push_back(std::move(p));
					}
				}

				if (benchPrompts.empty())
				{
					std::print("benchmark: 유효한 프롬프트가 없습니다.\n\n");
					continue;
				}

				printf("\n===== BATCH BENCHMARK (%zu prompts) =====\n",
					benchPrompts.size());

				// [1] 배칭 1회: 4-in-4-out
				printf("\n--- [1] BATCH (single call, %zu seqs) ---\n",
					benchPrompts.size());
				const auto tb0 = std::chrono::high_resolution_clock::now();
				std::vector<std::string> batchOut =
					GenerateBatchResponses(model.get(), ctx.get(), benchPrompts);
				const auto tb1 = std::chrono::high_resolution_clock::now();
				const double batchWall =
					std::chrono::duration<double>(tb1 - tb0).count();

				// [2] 순차 4회
				printf("\n--- [2] SEQUENTIAL (%zu separate calls) ---\n",
					benchPrompts.size());
				const auto ts0 = std::chrono::high_resolution_clock::now();
				std::vector<std::string> seqOut;
				seqOut.reserve(benchPrompts.size());
				for (const auto& p : benchPrompts)
					seqOut.push_back(GenerateResponse(model.get(), ctx.get(), p));
				const auto ts1 = std::chrono::high_resolution_clock::now();
				const double seqWall =
					std::chrono::duration<double>(ts1 - ts0).count();

				// 비교
				printf("\n===== RESULT =====\n");
				printf("Batch      (1 call,  %zu seqs) : %.2f s\n",
					benchPrompts.size(), batchWall);
				printf("Sequential (%zu calls)          : %.2f s\n",
					benchPrompts.size(), seqWall);
				if (batchWall > 0.0)
					printf("Speedup (sequential / batch)   : %.2fx\n",
						seqWall / batchWall);
				printf("==================\n\n");

				// 배칭 응답 내용
				for (size_t i = 0; i < batchOut.size(); ++i)
				{
					const char* txt =
						batchOut[i].empty() ? "(No response)" : batchOut[i].c_str();
					printf("[batch %zu] %s\n", i, txt);
				}
				printf("\n");

				// 벤치 후 정리(콘솔 대화 KV에 영향 없도록)
				llama_memory_clear(llama_get_memory(ctx.get()), true);

				continue;
			}

			// ── 일반 콘솔 대화(단일 경로) ──
			std::string formattedPrompt =
				FormatPromptForModel(model.get(), systemPrompt, consoleHistory, input);

			if (formattedPrompt.empty())
			{
				std::print("AI  > (prompt formatting failed)\n\n");
				continue;
			}

			if constexpr (kPrintPromptDebug)
			{
				std::cout << "\n[DEBUG PROMPT BEGIN]\n";
				std::cout << formattedPrompt << "\n";
				std::cout << "[DEBUG PROMPT END]\n\n";
			}

			std::string output =
				GenerateResponse(model.get(), ctx.get(), formattedPrompt);

			const bool validResponse = !output.empty();

			if (!validResponse)
				output = "(No response)";

			std::print("AI  > {}\n\n", output);

			consoleHistory.push_back({ "user", input });

			if (validResponse)
			{
				consoleHistory.push_back({ "assistant", output });
			}
		}
	}

	// Warm up GPU kernels, weight paging and the agent grammar so the first customer does not wait for them.
	{
		const auto started = std::chrono::steady_clock::now();
		const NpcMcp::Json warmup = {{"available_tools", NpcMcp::Tools()}, {"previous_dialogue", NpcMcp::Json::array()},
			{"customer_speech", "Hello"}, {"tool_observations", NpcMcp::Json::array()}, {"remaining_tool_calls", NpcMcp::MaxToolCalls - 1}};
		const std::string prompt = FormatPromptForModel(model.get(), NpcAgent::SystemPrompt, {}, warmup.dump());
		if (!prompt.empty()) GenerateBatchResponses(model.get(), ctx.get(), {prompt}, {NpcAgent::Grammar});
		Log::Info("AI warmup done in {} ms",
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
	}

	// ==================== network server loop ====================

	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		Log::Error("WSAStartup 실패");
		Log::Error("WSAStartup fail");
		return -1;
	}

	SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listenSocket == INVALID_SOCKET)
	{
		Log::Error("소켓 생성 실패");
		Log::Error("socket creation failure");
		WSACleanup();
		return -1;
	}

	sockaddr_in serverAddr = {};
	serverAddr.sin_family = AF_INET;
	// The trusted game/AI link carries scoped tool capabilities, never expose it publicly.
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddr.sin_port = htons(NetConfig::AI_SERVER_PORT);
	char portText[16]{}; size_t portLength=0;
	getenv_s(&portLength,portText,sizeof(portText),"TI_AI_PORT");
	if (portLength>1)
	{
		char* end=nullptr; const long port=std::strtol(portText,&end,10);
		if (!end || *end!='\0' || port<1024 || port>65535) { Log::Error("Invalid TI_AI_PORT"); closesocket(listenSocket); WSACleanup(); return -1; }
		serverAddr.sin_port=htons(static_cast<u_short>(port));
	}

	if (bind(listenSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		Log::Error("bind 실패");
		Log::Error("bind fail");
		closesocket(listenSocket);
		WSACleanup();
		return -1;
	}

	if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		Log::Error("listen 실패");
		Log::Error("listen fail");
		closesocket(listenSocket);
		WSACleanup();
		return -1;
	}

	AiBatchWorkerQueue aiQueue;
	aiQueue.Start(model.get(), ctx.get(), systemPrompt);

	std::atomic<int> activeClientCount = 0;
	std::atomic<int> nextClientId = 1;

	Log::Info("AI TCP server listening on loopback... (Port: {})", ntohs(serverAddr.sin_port));
	Log::Info("최대 {}개의 클라이언트를 동시에 받습니다.", kMaxClientCount);

	while (true)
	{
		sockaddr_in clientAddr = {};
		int clientAddrSize = sizeof(clientAddr);

		SOCKET clientSocket = accept(
			listenSocket,
			reinterpret_cast<SOCKADDR*>(&clientAddr),
			&clientAddrSize
		);

		if (clientSocket == INVALID_SOCKET)
		{
			Log::Error("accept 실패");
			Log::Error("accept fail");
			continue;
		}

		int currentActive = activeClientCount.load();

		if (currentActive >= kMaxClientCount)
		{
			Log::Warning("Client rejected: server is full. activeClientCount={}",
				currentActive);

			// 안내 문구를 평문으로 보내면 상대(게임서버)가 프레임 헤더로 읽어 스트림이 어긋난다.
			// userIndex 를 알 수 없어 AS_AI 로 감쌀 수도 없으므로 그냥 끊는다.
			closesocket(clientSocket);
			continue;
		}

		++activeClientCount;
		const int clientId = nextClientId++;

		std::thread(
			HandleClient,
			clientSocket,
			clientId,
			std::ref(aiQueue),
			std::ref(activeClientCount)
		).detach();
	}

	aiQueue.Stop();

	closesocket(listenSocket);
	WSACleanup();

	return 0;
}

// ==================== prompt formatting: Gemma4 manual template ====================

std::string FormatPromptForModel(
	llama_model* model,
	const std::string& systemPrompt,
	const std::vector<ChatTurn>& history,
	const std::string& userInput)
{
	if (model == nullptr)
		return "";

	auto CountTokens =
		[&](const std::string& text) -> int32_t
		{
			const llama_vocab* vocab = llama_model_get_vocab(model);
			if (vocab == nullptr)
				return -1;

			int32_t count = llama_tokenize(
				vocab,
				text.c_str(),
				static_cast<int32_t>(text.size()),
				nullptr,
				0,
				false,
				true
			);

			if (count < 0)
				count = -count;

			return count;
		};

	auto AppendTurn =
		[](std::string& prompt, const std::string& role, const std::string& content)
		{
			// Gemma4: turn 시작 <|turn>, 종료 <turn|>
			prompt += "<|turn>";
			prompt += role;
			prompt += "\n";
			prompt += content;
			prompt += "<turn|>\n";
		};

	auto BuildGemma4Prompt =
		[&](size_t startIndex, const std::string& cleanInput) -> std::string
		{
			std::string prompt;

			std::string cleanSystemPrompt = CleanGemmaControlText(systemPrompt);

			if (cleanSystemPrompt.empty())
			{
				cleanSystemPrompt =
					"You are an in-game NPC. "
					"Always answer naturally in Korean. "
					"Be friendly, respectful, and helpful. "
					"Output only the NPC's spoken reply.";
			}

			// bos를 문자열로 직접(파서가 special로 인식). add_special=false 와 짝.
			prompt += "<bos>";

			// 시스템 턴. ※ <|think|> 를 넣지 않으므로 thinking은 "비활성" 의도.
			AppendTurn(prompt, "system", cleanSystemPrompt);

			for (size_t i = startIndex; i < history.size(); ++i)
			{
				std::string content = CleanGemmaControlText(history[i].text);

				if (content.empty())
					continue;

				if (history[i].role == "user")
				{
					AppendTurn(prompt, "user", content);
				}
				else if (history[i].role == "assistant")
				{
					AppendTurn(prompt, "model", content);
				}
			}

			AppendTurn(prompt, "user", cleanInput);

			// E4B/E2B 생성 프롬프트: <|turn>model\n 으로만 끝낸다.
			// (모델 내장 템플릿도 정확히 여기서 끝남 — 덤프로 확인됨)
			prompt += kGenerationPrefix;

			return prompt;
		};

	const std::string cleanInput = CleanGemmaControlText(userInput);

	if (cleanInput.empty())
		return "";

	size_t startIndex = 0;

	while (true)
	{
		std::string prompt = BuildGemma4Prompt(startIndex, cleanInput);

		int32_t tokenCount = CountTokens(prompt);
		if (tokenCount < 0)
		{
			Log::Error("Failed to count prompt tokens.");
			return "";
		}

		if (tokenCount <= kPromptBudgetPerSeq)
		{
			std::cout << "[Debug] prompt tokens : " << tokenCount
				<< " / per-seq budget " << kPromptBudgetPerSeq << std::endl;

			return prompt;
		}

		if (startIndex < history.size())
		{
			++startIndex;
			continue;
		}

		Log::Error("Prompt is too long even without history.");
		return "";
	}
}

// ==================== tokenization ====================

std::vector<llama_token> TokenizeText(
	llama_model* model,
	const std::string& text,
	bool addSpecial,
	bool parseSpecial)
{
	std::vector<llama_token> result;

	if (model == nullptr || text.empty())
		return result;

	const llama_vocab* vocab = llama_model_get_vocab(model);
	if (vocab == nullptr)
		return result;

	int32_t tokenCount = llama_tokenize(
		vocab,
		text.c_str(),
		static_cast<int32_t>(text.size()),
		nullptr,
		0,
		addSpecial,
		parseSpecial
	);

	if (tokenCount < 0)
		tokenCount = -tokenCount;

	if (tokenCount <= 0)
		return result;

	result.resize(static_cast<size_t>(tokenCount));

	int32_t written = llama_tokenize(
		vocab,
		text.c_str(),
		static_cast<int32_t>(text.size()),
		result.data(),
		static_cast<int32_t>(result.size()),
		addSpecial,
		parseSpecial
	);

	if (written <= 0)
	{
		result.clear();
		return result;
	}

	result.resize(static_cast<size_t>(written));
	return result;
}

// ==================== batched generation ====================

struct BatchGenerationState
{
	llama_seq_id seqId = 0;
	std::vector<llama_token> promptTokens;
	std::vector<llama_token> genTokens;   // 생성된 토큰 ID를 모았다가 끝나고 한 번에 디코딩
	std::string response;
	int32_t pos = 0;
	llama_token pendingToken = -1;
	bool active = true;
	bool failed = false;
	int32_t lastLogitIndex = -1;
	llama_sampler* sampler = nullptr;
};

std::vector<std::string> GenerateBatchResponses(
	llama_model* model,
	llama_context* ctx,
	const std::vector<std::string>& formattedPrompts,
	const std::vector<const char*>& grammars)
{
	std::vector<std::string> outputs(formattedPrompts.size());

	if (model == nullptr || ctx == nullptr || formattedPrompts.empty())
		return outputs;

	const llama_vocab* vocab = llama_model_get_vocab(model);
	if (vocab == nullptr)
	{
		Log::Error("Failed to get model vocab.");
		return outputs;
	}

	const int batchSize = static_cast<int>(formattedPrompts.size());

	if (batchSize > kMaxBatchSize)
	{
		Log::Error("GenerateBatchResponses received too many prompts. batchSize={}",
			batchSize);
		return outputs;
	}

	llama_memory_clear(llama_get_memory(ctx), true);

	std::vector<BatchGenerationState> states;
	states.resize(batchSize);

	int32_t totalPromptTokens = 0;

	for (int i = 0; i < batchSize; ++i)
	{
		states[i].seqId = static_cast<llama_seq_id>(i);
		states[i].promptTokens = TokenizeText(model, formattedPrompts[i], false, true);

		if (states[i].promptTokens.empty())
		{
			states[i].failed = true;
			states[i].active = false;
			outputs[i] = "";
			continue;
		}

		states[i].pos = static_cast<int32_t>(states[i].promptTokens.size());
		totalPromptTokens += static_cast<int32_t>(states[i].promptTokens.size());

		llama_sampler_chain_params samplerParams = llama_sampler_chain_default_params();
		states[i].sampler = llama_sampler_chain_init(samplerParams);

		if (states[i].sampler == nullptr)
		{
			states[i].failed = true;
			states[i].active = false;
			outputs[i] = "";
			continue;
		}

		// The grammar has to sit in the chain: it must mask the logits before top_k throws candidates away.
		if (i<static_cast<int>(grammars.size()) && grammars[i])
		{
			auto* grammar=llama_sampler_init_grammar(vocab,grammars[i],"root");
			if (!grammar) { states[i].failed=true; states[i].active=false; continue; }
			llama_sampler_chain_add(states[i].sampler,grammar);
		}
		llama_sampler_chain_add(states[i].sampler, llama_sampler_init_top_k(64));
		llama_sampler_chain_add(states[i].sampler, llama_sampler_init_top_p(0.95f, 1));
		llama_sampler_chain_add(states[i].sampler, llama_sampler_init_temp(1.00f));
		llama_sampler_chain_add(states[i].sampler, llama_sampler_init_dist(1234 + i));
	}

	if (totalPromptTokens <= 0)
	{
		for (auto& state : states)
		{
			if (state.sampler != nullptr)
			{
				llama_sampler_free(state.sampler);
				state.sampler = nullptr;
			}
		}

		return outputs;
	}

	if (totalPromptTokens > kTotalContextTokens)
	{
		Log::Error("Total prompt tokens exceed total context. total={}, n_ctx={}",
			totalPromptTokens,
			kTotalContextTokens);

		for (auto& state : states)
		{
			if (state.sampler != nullptr)
			{
				llama_sampler_free(state.sampler);
				state.sampler = nullptr;
			}
		}

		return outputs;
	}

	Log::Info("Batch prefill start. batchSize={}, totalPromptTokens={}",
		batchSize,
		totalPromptTokens);

	// (C) 타이밍: 총 생성 토큰 수 + prefill/생성 시간 측정
	int totalGenTokens = 0;
	const auto tStart = std::chrono::high_resolution_clock::now();

	llama_batch promptBatch = llama_batch_init(totalPromptTokens, 0, 1);

	int32_t cursor = 0;

	for (int i = 0; i < batchSize; ++i)
	{
		auto& state = states[i];

		if (state.failed || state.promptTokens.empty())
			continue;

		const int32_t promptCount =
			static_cast<int32_t>(state.promptTokens.size());

		for (int32_t j = 0; j < promptCount; ++j)
		{
			promptBatch.token[cursor] = state.promptTokens[j];
			promptBatch.pos[cursor] = j;
			promptBatch.n_seq_id[cursor] = 1;
			promptBatch.seq_id[cursor][0] = state.seqId;

			const bool isLastPromptToken = (j == promptCount - 1);
			promptBatch.logits[cursor] = isLastPromptToken ? 1 : 0;

			if (isLastPromptToken)
				state.lastLogitIndex = cursor;

			++cursor;
		}
	}

	promptBatch.n_tokens = cursor;

	// 프롬프트를 kPrefillChunkTokens 씩 나눠 넣는다(작업 버퍼 절약). 모든 토큰은 그대로 평가된다.
	int activeCount = 0;
	for (int32_t offset = 0; offset < cursor; offset += kPrefillChunkTokens)
	{
		llama_batch chunk = promptBatch;
		chunk.n_tokens = std::min(kPrefillChunkTokens, cursor - offset);
		chunk.token = promptBatch.token + offset;
		chunk.pos = promptBatch.pos + offset;
		chunk.n_seq_id = promptBatch.n_seq_id + offset;
		chunk.seq_id = promptBatch.seq_id + offset;
		chunk.logits = promptBatch.logits + offset;
		if (llama_decode(ctx, chunk) != 0)
		{
			Log::Error("llama_decode failed on batch prompt chunk (offset={}).", offset);
			llama_batch_free(promptBatch);
			for (auto& state : states)
			{
				if (state.sampler != nullptr) { llama_sampler_free(state.sampler); state.sampler = nullptr; }
			}
			return outputs;
		}

		// 로짓은 방금 넣은 청크의 것만 살아 있다. 그 시퀀스의 마지막 프롬프트 토큰이 들어간 청크에서 첫 토큰을 뽑는다.
		for (auto& state : states)
		{
			if (state.failed || state.sampler == nullptr ||
				state.lastLogitIndex < offset || state.lastLogitIndex >= offset + chunk.n_tokens) continue;
			const llama_token newToken = llama_sampler_sample(state.sampler, ctx, state.lastLogitIndex - offset);
			state.active = !llama_vocab_is_eog(vocab, newToken);
			if (!state.active) continue;
			// 토큰을 즉시 문자열로 바꾸지 않고 ID만 모은다(디코딩은 생성이 끝난 뒤 한 번에).
			state.genTokens.push_back(newToken);
			++totalGenTokens;
			state.pendingToken = newToken;
			++activeCount;
		}
	}

	llama_batch_free(promptBatch);

	// prefill 완료 시점
	const auto tPrefillEnd = std::chrono::high_resolution_clock::now();

	llama_batch genBatch = llama_batch_init(kMaxBatchSize, 0, 1);

	for (int genStep = 1; genStep < kMaxGenTokens && activeCount > 0; ++genStep)
	{
		genBatch.n_tokens = 0;

		for (int i = 0; i < batchSize; ++i)
		{
			auto& state = states[i];

			if (!state.active || state.failed)
				continue;

			const int32_t batchIndex = genBatch.n_tokens;

			genBatch.token[batchIndex] = state.pendingToken;
			genBatch.pos[batchIndex] = state.pos;
			genBatch.n_seq_id[batchIndex] = 1;
			genBatch.seq_id[batchIndex][0] = state.seqId;
			genBatch.logits[batchIndex] = 1;

			state.lastLogitIndex = batchIndex;

			++state.pos;
			++genBatch.n_tokens;
		}

		if (genBatch.n_tokens <= 0)
			break;

		if (llama_decode(ctx, genBatch) != 0)
		{
			Log::Error("llama_decode failed during batched generation.");

			for (auto& state : states)
			{
				if (state.active)
				{
					state.failed = true;
					state.active = false;
				}
			}

			break;
		}

		activeCount = 0;

		for (int i = 0; i < batchSize; ++i)
		{
			auto& state = states[i];

			if (!state.active || state.failed || state.sampler == nullptr)
				continue;

			llama_token newToken =
				llama_sampler_sample(state.sampler, ctx, state.lastLogitIndex);

			if (llama_vocab_is_eog(vocab, newToken))
			{
				state.active = false;
				continue;
			}

			// 토큰 ID만 모은다(디코딩은 생성 종료 후 한 번에).
			state.genTokens.push_back(newToken);
			++totalGenTokens;
			state.pendingToken = newToken;
			++activeCount;
		}
	}

	llama_batch_free(genBatch);

	// (C) 타이밍 끝 + 출력
	const auto tEnd = std::chrono::high_resolution_clock::now();
	const double prefillSec =
		std::chrono::duration<double>(tPrefillEnd - tStart).count();
	const double genSec =
		std::chrono::duration<double>(tEnd - tPrefillEnd).count();
	const double aggTokPerSec = (genSec > 0.0) ? (totalGenTokens / genSec) : 0.0;

	printf(
		"[batch timing] %d seqs | prefill %.2f s (%d prompt tokens) | "
		"generation %.2f s (%d gen tokens) | aggregate %.2f tok/s\n",
		batchSize, prefillSec, totalPromptTokens,
		genSec, totalGenTokens, aggTokPerSec);

	for (int i = 0; i < batchSize; ++i)
	{
		auto& state = states[i];

		if (state.sampler != nullptr)
		{
			llama_sampler_free(state.sampler);
			state.sampler = nullptr;
		}

		if (!state.failed)
		{
			// 모아둔 토큰 ID를 '한 번에' 디코딩(여기서 비로소 문자열로).
			// 토큰별 디코딩이 아니라서 한글이 토큰 경계에서 깨지지 않는다.
			state.response = TokensToString(vocab, state.genTokens);

			// 추론 채널 구간 제거 → 개별 마커 정리 → 역할 라벨 제거 순서.
			std::string cleaned = StripLeakedReasoning(state.response);
			cleaned = CleanGemmaControlText(cleaned);
			StripLeadingRoleLabel(cleaned);
			outputs[i] = cleaned;
		}
	}

	Log::Info("Batch generation done. batchSize={}", batchSize);

	return outputs;
}

// ==================== single generation: console mode ====================

std::string GenerateResponse(
	llama_model* model,
	llama_context* ctx,
	const std::string& formattedPrompt)
{
	if (model == nullptr || ctx == nullptr || formattedPrompt.empty())
		return "";

	const llama_vocab* vocab = llama_model_get_vocab(model);
	if (vocab == nullptr)
	{
		Log::Error("Failed to get model vocab.");
		return "";
	}

	llama_memory_clear(llama_get_memory(ctx), true);

	std::vector<llama_token> tokens =
		TokenizeText(model, formattedPrompt, false, true);

	if (tokens.empty())
	{
		Log::Error("Failed to tokenize prompt.");
		return "";
	}

	const int promptTokenCount = static_cast<int>(tokens.size());

	// ── 타이밍: 측정 시작 ──
	const auto tStart = std::chrono::high_resolution_clock::now();

	llama_batch promptBatch =
		llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()));

	if (llama_decode(ctx, promptBatch) != 0)
	{
		Log::Error("llama_decode failed on prompt.");
		return "";
	}

	// prefill 완료 시점
	const auto tPrefillEnd = std::chrono::high_resolution_clock::now();

	llama_sampler_chain_params samplerParams = llama_sampler_chain_default_params();
	llama_sampler* sampler = llama_sampler_chain_init(samplerParams);

	if (sampler == nullptr)
	{
		Log::Error("Failed to create sampler.");
		return "";
	}

	const uint32_t kSeed = 1234;
	llama_sampler_chain_add(sampler, llama_sampler_init_top_k(64));
	llama_sampler_chain_add(sampler, llama_sampler_init_top_p(0.95f, 1));
	llama_sampler_chain_add(sampler, llama_sampler_init_temp(1.00f));
	llama_sampler_chain_add(sampler, llama_sampler_init_dist(kSeed));

	std::string response;
	response.reserve(1024);

	std::vector<llama_token> genTokens;   // 생성 토큰 ID 수집(끝나고 한 번에 디코딩)
	genTokens.reserve(256);

	bool generationError = false;
	int genTokenCount = 0;

	for (int i = 0; i < kMaxGenTokens; ++i)
	{
		llama_token newToken = llama_sampler_sample(sampler, ctx, -1);

		if (llama_vocab_is_eog(vocab, newToken))
			break;

		// 토큰을 즉시 변환하지 않고 ID만 모은다.
		// (디코딩은 생성 종료 후 TokensToString로 한 번에 → 글자 깨짐 방지)
		genTokens.push_back(newToken);
		++genTokenCount;

		llama_batch genBatch = llama_batch_get_one(&newToken, 1);

		if (llama_decode(ctx, genBatch) != 0)
		{
			Log::Error("llama_decode failed during generation.");
			generationError = true;
			break;
		}
	}

	// ── 타이밍: 측정 끝 ──
	const auto tEnd = std::chrono::high_resolution_clock::now();

	llama_sampler_free(sampler);

	const double prefillSec =
		std::chrono::duration<double>(tPrefillEnd - tStart).count();
	const double genSec =
		std::chrono::duration<double>(tEnd - tPrefillEnd).count();
	const double totalSec =
		std::chrono::duration<double>(tEnd - tStart).count();

	const double genTokPerSec = (genSec > 0.0) ? (genTokenCount / genSec) : 0.0;

	// oobabooga 스타일 한 줄
	printf(
		"Output generated in %.2f seconds (%.2f tokens/s, %d tokens, context %d, seed %u)\n",
		totalSec,
		genTokPerSec,
		genTokenCount,
		promptTokenCount + genTokenCount,
		kSeed);

	// prefill/생성 분리 (thinking 진단: 생성 토큰 수가 핵심 — 켜지면 수백, 꺼지면 수십)
	printf(
		"  [timing] prompt eval %.2f s (%d tokens) | generation %.2f s (%d tokens, %.2f tok/s)\n",
		prefillSec, promptTokenCount,
		genSec, genTokenCount, genTokPerSec);

	if (generationError)
		return "";

	// 모아둔 토큰 ID를 '한 번에' 디코딩(여기서 비로소 문자열로).
	// 토큰별 디코딩이 아니라서 한글이 토큰 경계에서 깨지지 않는다.
	response = TokensToString(vocab, genTokens);

	// 추론 채널 구간 제거 → 개별 마커 정리 → 역할 라벨 제거 순서.
	response = StripLeakedReasoning(response);
	response = CleanGemmaControlText(response);
	StripLeadingRoleLabel(response);

	return response;
}

// ==================== file loading ====================

std::string LoadTextFileOrDefault(
	const std::filesystem::path& path,
	const std::string& fallbackText)
{
	std::ifstream file(path, std::ios::binary);

	if (!file)
	{
		Log::Warning("System prompt file not found: {}", path.string());
		Log::Warning("Using fallback system prompt.");
		return fallbackText;
	}

	std::ostringstream ss;
	ss << file.rdbuf();

	std::string text = ss.str();

	if (text.size() >= 3 &&
		static_cast<unsigned char>(text[0]) == 0xEF &&
		static_cast<unsigned char>(text[1]) == 0xBB &&
		static_cast<unsigned char>(text[2]) == 0xBF)
	{
		text.erase(0, 3);
	}

	if (text.empty())
	{
		Log::Warning("System prompt file is empty: {}", path.string());
		Log::Warning("Using fallback system prompt.");
		return fallbackText;
	}

	return text;
}

// ============================================================================
//  파일 I/O 모드 (Step 4)
//   input.txt(줄마다 JSON) → who별 history로 추론 → output.txt(JSON) append
//   소켓 대신 파일을 버퍼로 쓴다. who로 손님을 분리해 각각 따로 기억한다.
// ============================================================================

// 입력 JSON에서 뽑아낸 손님 한 명의 한 턴.
struct ParsedInput
{
	std::string who;          // 손님 식별자(키)
	int hp = 0;
	int maxHp = 0;
	int lv = 0;
	int maxLv = 0;
	std::string say;          // 손님의 말
	bool ok = false;          // 파싱 성공 여부
};

// 아주 단순한 JSON 값 추출기들.
// 입력은 게임 서버/우리가 만든 '신뢰 가능한' JSON이므로, 정식 파서 없이
// 키를 직접 찾아 값을 뜯는다(가벼움). 깨진 입력은 ok=false로 흘려보낸다.

// "key": "문자열값"  →  문자열값 (이스케이프는 최소 처리)
static bool ExtractJsonString(
	const std::string& json,
	const std::string& key,
	std::string& out)
{
	const std::string needle = "\"" + key + "\"";
	size_t k = json.find(needle);
	if (k == std::string::npos)
		return false;

	size_t colon = json.find(':', k + needle.size());
	if (colon == std::string::npos)
		return false;

	// 콜론 뒤 첫 따옴표
	size_t q1 = json.find('"', colon + 1);
	if (q1 == std::string::npos)
		return false;

	// 닫는 따옴표(이스케이프된 \" 는 건너뜀)
	std::string value;
	for (size_t i = q1 + 1; i < json.size(); ++i)
	{
		char c = json[i];
		if (c == '\\' && i + 1 < json.size())
		{
			char n = json[i + 1];
			// 흔한 이스케이프만 풀어 준다.
			if (n == '"')      value += '"';
			else if (n == '\\') value += '\\';
			else if (n == 'n')  value += '\n';
			else if (n == 't')  value += '\t';
			else                value += n;
			++i;
			continue;
		}
		if (c == '"')
		{
			out = value;
			return true;
		}
		value += c;
	}
	return false;
}

// "key": 숫자  →  정수
static bool ExtractJsonInt(
	const std::string& json,
	const std::string& key,
	int& out)
{
	const std::string needle = "\"" + key + "\"";
	size_t k = json.find(needle);
	if (k == std::string::npos)
		return false;

	size_t colon = json.find(':', k + needle.size());
	if (colon == std::string::npos)
		return false;

	// 콜론 뒤 첫 숫자/부호
	size_t i = colon + 1;
	while (i < json.size() && (json[i] == ' ' || json[i] == '\t'))
		++i;

	bool neg = false;
	if (i < json.size() && (json[i] == '-' || json[i] == '+'))
	{
		neg = (json[i] == '-');
		++i;
	}

	if (i >= json.size() || !std::isdigit(static_cast<unsigned char>(json[i])))
		return false;

	long long value = 0;
	while (i < json.size() && std::isdigit(static_cast<unsigned char>(json[i])))
	{
		value = value * 10 + (json[i] - '0');
		++i;
	}

	out = static_cast<int>(neg ? -value : value);
	return true;
}

// 입력 JSON 한 줄을 파싱한다.
// who는 문자열 또는 숫자 둘 다 허용(숫자면 문자열로 변환해 키로 쓴다).
static ParsedInput ParseInputLine(const std::string& line)
{
	ParsedInput r;

	// who: 문자열 우선, 없으면 숫자로 시도
	if (!ExtractJsonString(line, "who", r.who))
	{
		int whoNum = 0;
		if (ExtractJsonInt(line, "who", whoNum))
			r.who = std::to_string(whoNum);
		else
			return r; // who 없음 → 실패
	}

	// HP/lv 등 — 없으면 0으로 두되, say는 필수
	ExtractJsonInt(line, "HP", r.hp);
	ExtractJsonInt(line, "maxHP", r.maxHp);
	ExtractJsonInt(line, "lv", r.lv);
	ExtractJsonInt(line, "maxLv", r.maxLv);

	if (!ExtractJsonString(line, "say", r.say))
		return r; // say 없음 → 실패

	r.ok = true;
	return r;
}

// 모델에게 줄 입력 문자열을 만든다.
// 시스템 프롬프트가 학습한 입력 형식(JSON)과 동일하게 재조립한다.
// (who는 식별용 — say에는 영향 없음. 모델이 같은 손님임을 인지하도록 그대로 넣는다.)
static std::string BuildModelInputJson(const ParsedInput& in)
{
	std::ostringstream ss;
	ss << "{\"who\":\"" << in.who << "\","
		<< "\"HP\":" << in.hp << ","
		<< "\"maxHP\":" << in.maxHp << ","
		<< "\"lv\":" << in.lv << ","
		<< "\"maxLv\":" << in.maxLv << ","
		<< "\"say\":\"";
	// say 안의 따옴표/역슬래시/개행은 이스케이프
	for (char c : in.say)
	{
		if (c == '"')       ss << "\\\"";
		else if (c == '\\') ss << "\\\\";
		else if (c == '\n') ss << "\\n";
		else if (c == '\t') ss << "\\t";
		else                ss << c;
	}
	ss << "\"}";
	return ss.str();
}

// 모델 출력에서 표준 JSON 객체 하나만 추출/정제한다.
// 모델은 거의 {"say":...,"price":N}만 내지만, 혹시 앞뒤에 사족이 붙어도
// 첫 '{' ~ 짝이 맞는 '}' 까지만 잘라낸다(문자열 안의 중괄호는 무시).
// 추출 실패 시 빈 문자열.
static std::string ExtractJsonObject(const std::string& modelOutput)
{
	size_t start = modelOutput.find('{');
	if (start == std::string::npos)
		return std::string();

	bool inString = false;
	bool escaped = false;
	int depth = 0;

	for (size_t i = start; i < modelOutput.size(); ++i)
	{
		char c = modelOutput[i];

		if (inString)
		{
			if (escaped)            escaped = false;
			else if (c == '\\')     escaped = true;
			else if (c == '"')      inString = false;
			continue;
		}

		if (c == '"')       inString = true;
		else if (c == '{')  ++depth;
		else if (c == '}')
		{
			--depth;
			if (depth == 0)
				return modelOutput.substr(start, i - start + 1);
		}
	}
	return std::string(); // 짝 안 맞음
}

// 파일 I/O 모드 본체.
//  - input.txt를 줄마다 읽어 파싱
//  - who별 history(ConversationStore)로 추론
//  - 출력 JSON을 output.txt에 append
void RunFileIoMode(
	llama_model* model,
	llama_context* ctx,
	const std::string& systemPrompt)
{
	namespace fs = std::filesystem;

	const fs::path inPath = kInputFilePath;
	const fs::path outPath = kOutputFilePath;

	// 입력 파일 열기(UTF-8 바이트 그대로)
	std::ifstream inFile(inPath, std::ios::binary);
	if (!inFile)
	{
		Log::Error("input file not found: {}", inPath.string());
		Log::Error("io 폴더에 input.txt를 두세요. (예: io/input.txt)");
		return;
	}

	// 출력 파일은 append 모드(기존 내용 뒤에 이어 씀)
	// 출력 디렉터리 보장
	std::error_code ec;
	fs::create_directories(outPath.parent_path(), ec);

	std::ofstream outFile(outPath, std::ios::binary | std::ios::app);
	if (!outFile)
	{
		Log::Error("cannot open output file: {}", outPath.string());
		return;
	}

	ConversationStore store;

	Log::Info("File I/O mode start. input={}, output={}",
		inPath.string(), outPath.string());

	std::string rawLine;
	int lineNo = 0;
	int okCount = 0;
	int failCount = 0;

	while (std::getline(inFile, rawLine))
	{
		++lineNo;

		// CR 제거(윈도우 CRLF 대비) + 양끝 공백 제거
		std::string line = NormalizeReceivedText(rawLine);
		if (line.empty())
			continue;

		// 1) 입력 JSON 파싱
		ParsedInput in = ParseInputLine(line);
		if (!in.ok)
		{
			Log::Warning("line {}: parse failed, skipped. ({})", lineNo, line);
			++failCount;
			continue;
		}

		// 2) who별 history 가져오기(없으면 새 손님)
		std::vector<ChatTurn>& history = store.GetOrCreate(in.who);

		// 3) 모델 입력 문자열 재조립 → 프롬프트 → 추론
		const std::string modelInput = BuildModelInputJson(in);

		const std::string prompt =
			FormatPromptForModel(model, systemPrompt, history, modelInput);

		if (prompt.empty())
		{
			Log::Warning("line {}: prompt formatting failed.", lineNo);
			++failCount;
			continue;
		}

		std::string rawOut = GenerateResponse(model, ctx, prompt);

		// 4) 출력에서 JSON 객체만 추출
		std::string jsonOut = ExtractJsonObject(rawOut);
		if (jsonOut.empty())
		{
			// 추출 실패해도 원문을 남겨 디버깅 가능하게(주석 형태)
			Log::Warning("line {}: no JSON object in output. raw kept.", lineNo);
			jsonOut = rawOut;
		}

		// 5) output.txt에 append (한 줄에 who도 같이 남겨 추적 쉽게)
		//    형식: {"who":"1","reply":{"say":...,"price":N}}
		//    → who별로 어떻게 답했는지 사람이 바로 확인 가능
		std::ostringstream rec;
		rec << "{\"who\":\"" << in.who << "\",\"reply\":" << jsonOut << "}";
		outFile << rec.str() << "\n";
		outFile.flush();

		// 6) history에 이번 턴 누적(다음 턴이 이 손님 기억을 잇도록)
		history.push_back({ "user", modelInput });
		history.push_back({ "assistant", jsonOut });

		++okCount;
		Log::Info("line {}: who={} done. (history turns={})",
			lineNo, in.who, history.size());
	}

	Log::Info("File I/O mode done. ok={}, fail={}, users={}",
		okCount, failCount, store.UserCount());
	Log::Info("결과는 {} 에 append 되었습니다.", outPath.string());
}

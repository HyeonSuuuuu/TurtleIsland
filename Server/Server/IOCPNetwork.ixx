module;
#define WIN32_LEAN_AND_MEAN
#include <winSock2.h>
#include <ws2tcpip.h>

#include <vector>
#include <queue>
#include <mutex>
#include <memory>
#include <thread>
#include <chrono>
#include <optional>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")

export module IOCPNetwork;
import Common;
import Define;
import Session;
import User;

export class IOCPNetwork
{
public:
	IOCPNetwork() = default;
	virtual ~IOCPNetwork()
	{
		WSACleanup();
	}

	bool InitEnvironment()
	{
		return true;
	}
	bool InitSocket()
	{
		WSADATA wsaData;

		int32 ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (ret != 0)
		{
			Log::ErrorDisplay("WSAStartup() 실패", WSAGetLastError());
			return false;
		}

		if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
		{
			Log::ErrorDisplay("Winsock.dll의 사용 가능한 버전을 찾을 수 없습니다", WSAGetLastError());
			return 1;
		}


		m_listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
		if (m_listenSocket == INVALID_SOCKET)
		{
			Log::ErrorDisplay("socket() 실패", WSAGetLastError());
		}

		Log::Success("소켓 초기화 성공");
		return true;
	}

	bool ConnectToAIServer(const std::string& ip, uint16 port)
	{
		m_aiSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
		if (m_aiSocket == INVALID_SOCKET)
		{
			Log::Error("Ai 소켓 생성 실패", WSAGetLastError());
			return false;
		}

		sockaddr_in aiAddr{};
		aiAddr.sin_family = AF_INET;
		inet_pton(AF_INET, ip.c_str(), &aiAddr.sin_addr);
		aiAddr.sin_port = htons(port);

		Log::Info("Ai 서버에 연결 시도 (IP: {}, Port: {})", ip, port);
		if (connect(m_aiSocket, (sockaddr*)&aiAddr, sizeof(aiAddr)) == SOCKET_ERROR)
		{
			Log::Error("Ai 서버 연결 실패", WSAGetLastError());
			closesocket(m_aiSocket);
			return false;
		}

		m_aiSession = std::make_unique<Session>(AI_ID);
		if (m_aiSession == nullptr)
		{
			Log::Error("AI 세션 생성 실패");
			return false;
		}


		HANDLE hIOCP = CreateIoCompletionPort((HANDLE)m_aiSocket, m_iocpHandle, (ULONG_PTR)&m_aiSession, 0);
		if (hIOCP == nullptr || hIOCP != m_iocpHandle)
		{
			Log::ErrorDisplay("aiSocket IOCP 등록 실패", GetLastError());
			return false;
		}

		return true;
	}

	bool BindandListen(const int32 port) const
	{
		sockaddr_in serverAddr{};
		serverAddr.sin_family = AF_INET;
		serverAddr.sin_port = htons(port);
		serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

		int32 ret = bind(m_listenSocket, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR_IN));
		if (ret != 0)
		{
			Log::ErrorDisplay("bind() 실패", WSAGetLastError());
			return false;
		}

		ret = listen(m_listenSocket, SOMAXCONN);
		if (ret != 0)
		{
			Log::ErrorDisplay("listen() 실패", WSAGetLastError());
			return false;
		}

		Log::Success("서버 등록 성공");
		return true;
	}

	bool StartServer(const uint32 maxClientCount)
	{
		CreateClient(maxClientCount);
		m_maxWorkerThread = std::thread::hardware_concurrency() * 2;
		m_iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, m_maxWorkerThread);
		if (m_iocpHandle == nullptr)
		{
			Log::ErrorDisplay("CreateIoCompletionPort() 실패", GetLastError());
			return false;
		}

		HANDLE hIOCP = CreateIoCompletionPort((HANDLE)m_listenSocket, m_iocpHandle, (ULONG_PTR)0, 0);
		if (hIOCP == nullptr || hIOCP != m_iocpHandle)
		{
			Log::ErrorDisplay("ListenSocket IOCP 등록 실패", GetLastError());
			return false;
		}

		bool ret = CreateWorkerThread();
		if (ret == false)
		{
			return false;
		}

		for (int i = 0; i < 10; ++i)
		{
			Session* session = GetEmptySession();
			if (session == nullptr)
			{
				Log::Error("초기 세션 부족 ");
				break;
			}
			if (session->PostAccept(m_listenSocket) == false)
			{
				ReturnSession(session->GetIndex());
			}
		}

		Log::Success("서버 시작");
		return true;
	}

	void DestroyThread()
	{
		m_isWorkerRun = false;
		CloseHandle(m_iocpHandle);

		for (auto& th : m_IOWorkerThreads)
		{
			if (th.joinable())
			{
				th.join();
			}
		}

		closesocket(m_listenSocket);
	}

	bool SendMsg(const uint32 clientIndex, const std::span<const byte> msg)
	{
		return m_users[clientIndex]->SendMsg(msg);
	}

	virtual void OnConnect(const uint32 clientIndex) = 0;
	virtual void OnClose(const uint32 clientIndex) = 0;
	User* GetUser(uint32 index)
	{
		if (index >= m_users.size()) return nullptr;
		return m_users[index].get();
	}

protected:
private:

	int64 GetTick()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	Session* GetEmptySession()
	{
		std::lock_guard<std::mutex> lock(m_reuseLock);
		if (m_reuseQueue.empty()) return nullptr;
		auto [closedTime, index] = std::move(m_reuseQueue.front());
		if (GetTick() - closedTime < REUSE_WAIT_MS) return nullptr;
		m_reuseQueue.pop();
		return m_users[index].get();
	}

	void ReturnSession(uint32 index)
	{
		std::lock_guard<std::mutex> lock(m_reuseLock);
		m_reuseQueue.emplace(GetTick(), index);
	}


	void CreateClient(const uint32 maxClientCount)
	{
		m_users.reserve(maxClientCount);
		for (uint32 i = 0; i < maxClientCount; ++i)
		{
			m_users.emplace_back(std::make_unique<User>(i));
			m_reuseQueue.emplace(0, i);
		}
	}
	bool CreateWorkerThread()
	{
		m_isWorkerRun = true;
		for (int i = 0; i < m_maxWorkerThread; ++i)
		{
			m_IOWorkerThreads.emplace_back([this]() { WorkerThread(); });
		}

		Log::Success("{}개의 Worker Thread 생성 완료", m_maxWorkerThread);
		return true;
	}

	void WorkerThread()
	{
		Session* pSession = nullptr;
		bool isSuccess = true;
		DWORD dwIoSize = 0;
		LPOVERLAPPED lpOverlapped = nullptr;

		while (m_isWorkerRun)
		{
			isSuccess = GetQueuedCompletionStatus(m_iocpHandle,
				&dwIoSize,
				(PULONG_PTR)&pSession,
				&lpOverlapped,
				INFINITE);

			if (isSuccess && dwIoSize == 0 && lpOverlapped == nullptr)
			{
				m_isWorkerRun = false;
				continue;
			}

			if (lpOverlapped == nullptr)
			{
				Log::ErrorDisplay("GetQueuedCompletionStatus() 실패", GetLastError());
				continue;
			}

			OverlappedEx* pOverlappedEx = (OverlappedEx*)lpOverlapped;

			if (IOOperation::ACCEPT == pOverlappedEx->m_operation)
			{
				uint32 targetIndex = pOverlappedEx->m_sessionIndex;
				pSession = m_users[targetIndex].get();
				if (pSession)
				{
					pSession->AcceptCompleted(m_iocpHandle);
					OnConnect(pSession->GetIndex());
					m_clientCnt.fetch_add(1);
					Session* session = GetEmptySession();
					if (session == nullptr)
					{
						Log::Error("초기 세션 부족 ");
					}
					else if (session->PostAccept(m_listenSocket) == false)
					{
						ReturnSession(session->GetIndex());
					}
				}
				continue;
			}
			else if (!isSuccess || (dwIoSize == 0 && isSuccess))
			{
				if (pSession)
				{
					CloseSocket(pSession->GetIndex());
				}
				continue;
			}

			else if (IOOperation::RECV == pOverlappedEx->m_operation)
			{
				const auto dataSpan{ pSession->GetRecvData(dwIoSize) };

				User* pUser = m_users[pSession->GetIndex()].get();
				if (pUser)
				{
					// 1. 유저 버퍼에 저장
					pUser->SetPacket(dataSpan);

					// 패킷 조립
					while (auto packetOpt = pUser->GetPacket())
					{
						pUser->ProcessPacket(packetOpt.value());
					}
				}

				pSession->BindRecv();
			}
			else if (IOOperation::SEND == pOverlappedEx->m_operation)
			{
				pSession->SendCompleted(dwIoSize, pOverlappedEx);
			}
			else
			{
				Log::Warning("socket({}에서 예외상황 ", pSession->GetSocketId());
			}
		}
	}

	void CloseSocket(uint32 index, bool isForce = false)
	{
		auto pUser = m_users[index].get();
		pUser->Close(isForce);

		OnClose(index);

		ReturnSession(index);
		m_clientCnt.fetch_sub(1);
	}

	SOCKET m_listenSocket = INVALID_SOCKET;
	SOCKET m_aiSocket = INVALID_SOCKET;
	std::atomic_int m_clientCnt = 0;

	std::vector<std::thread> m_IOWorkerThreads;
	std::atomic_bool m_isWorkerRun = false;
	std::unique_ptr<Session> m_aiSession;

	HANDLE m_iocpHandle = INVALID_HANDLE_VALUE;

	uint32 m_maxWorkerThread;

	std::vector<std::unique_ptr<User>> m_users;
	std::queue<std::pair<int64, uint32>> m_reuseQueue;
	std::mutex m_reuseLock;
};
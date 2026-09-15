#include "pch.h"
#include "GameServer.h"
#include "Globals.h"
#include "NpcTick.h"
#include "MonsterScript.h"
#include "ObstacleMap.h"
#include "Database.h"
#include <cstdlib>

void GameServer::OnConnect(const uint32_t slotIndex)
{
	Log::Info("[OnConnect] slot={}", slotIndex);
}

void GameServer::OnClose(const uint32_t slotIndex)
{
	User* u = GetSession(slotIndex);
	if (u)
	{
		// 온라인 목록에서 제거 (중복 접속 차단용)
		std::string uid = u->GetUserId();
		if (!uid.empty())
		{
			std::lock_guard lock(g_onlineMutex);
			auto it = g_onlineUsers.find(uid);
			if (it != g_onlineUsers.end() && it->second == slotIndex)
				g_onlineUsers.erase(it);
		}

		if (u->GetUserState() == User::UserState::Room)
			u->LeaveCurrentRoom();
	}
	Log::Info("[OnClose] slot={}", slotIndex);
}

void GameServer::OnRecv(uint32_t slotIndex, const Packet& packet)
{
	if (slotIndex == AI_ID)   // AI 서버에서 온 응답
	{
		OnAiRecv(packet);
		return;
	}
	User* u = GetSession(slotIndex);
	if (u) u->Dispatch(packet);
}

// AS_AI(고정 헤더 userIndex + 가변 텍스트) → 해당 유저에게 SC_NpcChat 전달
void GameServer::OnAiRecv(const Packet& packet)
{
	if (packet.packetId == static_cast<uint16_t>(PACKET_ID::AS_MCP))
	{
		using namespace NpcMcp;
		Json request, response;
		try
		{
			if (packet.dataSpan.size() < sizeof(PacketHeader) || packet.dataSpan.size() > MAX_SOCKBUF) return;
			request=Json::parse(packet.dataSpan.begin()+sizeof(PacketHeader),packet.dataSpan.end());
			bool handled=false;
			for (auto& room:g_rooms) if (room.HandleNpcMcp(request,response)) { handled=true; break; }
			if (!handled) response=Error(request.is_object()?request.value("id",Json{}):Json{},-32001,"Invalid NPC context");
		}
		catch (const std::exception&) { response=Error(nullptr,-32600,"Invalid MCP message"); }
		if (response.is_null()) return;
		auto text=response.dump();
		if (text.size()>MaxMessageBytes) text=Error(response.value("id",Json{}),-32603,"Response too large").dump();
		std::vector<uint8_t> data(sizeof(PacketHeader)+text.size());
		auto* header=reinterpret_cast<PacketHeader*>(data.data());
		header->size=static_cast<uint16_t>(data.size()); header->id=static_cast<uint16_t>(PACKET_ID::SA_MCP);
		std::memcpy(data.data()+sizeof(PacketHeader),text.data(),text.size());
		if (g_aiSession) g_aiSession->Send(data);
		return;
	}
	if (packet.packetId == static_cast<uint16_t>(PACKET_ID::AS_AI_AGENT))
	{
		try
		{
			if (packet.dataSpan.size()<sizeof(AS_AI) || packet.dataSpan.size()>MAX_SOCKBUF) return;
			const auto* header=reinterpret_cast<const AS_AI*>(packet.dataSpan.data());
			const auto response=NpcMcp::Json::parse(packet.dataSpan.begin()+sizeof(AS_AI),packet.dataSpan.end());
			for (auto& room:g_rooms) if (room.CompleteNpcChat(header->userIndex,response)) break;
		}
		catch (const std::exception&) { Log::Warning("[NPC] Invalid agent response ignored"); }
		return;
	}
	if (packet.packetId != static_cast<uint16_t>(PACKET_ID::AS_AI))
		return;
	if (packet.dataSpan.size() < AS_AI_PACKET_SIZE)
		return;

	auto* pAsAi          = reinterpret_cast<const AS_AI*>(packet.dataSpan.data());
	const uint8_t* text  = packet.dataSpan.data() + sizeof(AS_AI);
	size_t         textLen = packet.dataSpan.size() - sizeof(AS_AI);

	auto it = g_idToIndexMap.find(pAsAi->userIndex);
	if (it == g_idToIndexMap.end()) return;

	User* pUser = g_userPool.Get(it->second);
	if (!pUser) return;

	const size_t totalSize = sizeof(SC_NpcChat) + textLen;
	std::vector<uint8_t> buf(totalSize, 0);
	auto* pResp = reinterpret_cast<SC_NpcChat*>(buf.data());
	pResp->size = static_cast<uint16_t>(totalSize);
	pResp->id   = static_cast<uint16_t>(PACKET_ID::SC_NPC_CHAT);
	if (textLen > 0)
		CopyMemory(buf.data() + sizeof(SC_NpcChat), text, textLen);

	pUser->Send(std::span<const uint8_t>(buf.data(), buf.size()));
}

void GameServer::Run()
{
	User::RegisterHandlers();
	g_obstacleMap.LoadConfigured("CollisionSettings.json");
	LoadMonsterScript("Monster.lua"); // 몬스터 정의 로드 (없으면 폴백 스폰)
	LoadSkillScript("Skill.lua");     // 플레이어 스킬 판정값 (없으면 코드 기본값)
	if (!g_npcShopConfig.Load("Shop.lua")) Log::Warning("[NPC] Missing/invalid Shop.lua; shop disabled");

	// DB 연결: 환경변수 TI_DB_CONN 우선, 없으면 기본값. 실패해도 서버는 계속(로그인 폴백).
	char connBuf[512] = {}; size_t connLen = 0;
	getenv_s(&connLen, connBuf, sizeof(connBuf), "TI_DB_CONN");
	g_db.Connect(connLen > 0 ? connBuf : DbConfig::DEFAULT_CONN);
	StartServer();                    // IOCP 생성 + 워커 스레드
	g_iocpHandle = GetIocpHandle();

	std::string ai_ip;
	uint16_t ai_port;
	std::cout << "AI 서버 주소: ";
	std::cin >> ai_ip;
	std::cout << "AI 서버 포트: ";
	std::cin >> ai_port;
	ConnectToAIServer(ai_ip.c_str(), ai_port);   // AI 서버(NetConfig::AI_SERVER_PORT) — 없으면 경고 후 진행
	StartNpcTick();                   // NPC 틱 스레드
}

void GameServer::Stop()
{
	StopNpcTick();
	g_db.Disconnect();
	IOCPNetwork::Stop();
}

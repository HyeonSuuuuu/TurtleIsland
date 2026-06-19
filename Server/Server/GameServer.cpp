#include "pch.h"
#include "GameServer.h"
#include "Globals.h"
#include "NpcTick.h"
#include "MonsterScript.h"

void GameServer::OnConnect(const uint32_t slotIndex)
{
	Log::Info("[OnConnect] slot={}", slotIndex);
}

void GameServer::OnClose(const uint32_t slotIndex)
{
	User* u = GetSession(slotIndex);
	if (u && u->GetUserState() == User::UserState::Room)
		u->LeaveCurrentRoom();
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
	LoadMonsterScript("Monster.lua"); // 몬스터 정의 로드 (없으면 폴백 스폰)
	StartServer();                    // IOCP 생성 + 워커 스레드
	g_iocpHandle = GetIocpHandle();
	ConnectToAIServer("127.0.0.1", 4444);   // AI 서버(NetConfig::AI_SERVER_PORT) — 없으면 경고 후 진행
	StartNpcTick();                   // NPC 틱 스레드
}

void GameServer::Stop()
{
	StopNpcTick();
	IOCPNetwork::Stop();
}

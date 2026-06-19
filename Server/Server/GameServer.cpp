#include "pch.h"
#include "GameServer.h"
#include "Globals.h"
#include "NpcTick.h"

void GameServer::OnConnect(const uint32_t slotIndex)
{
	Log::Info("[OnConnect] SlotIndex({})", slotIndex);
}

void GameServer::OnClose(const uint32_t slotIndex)
{
	Log::Info("[OnClose] SlotIndex({})", slotIndex);

	// If user was in a room, perform clean leave
	User* pUser = GetUser(slotIndex);
	if (pUser && pUser->GetUserState() == User::UserState::Room)
		pUser->LeaveCurrentRoom();
}

void GameServer::OnRecv(uint32_t slotIndex, const Packet& packet)
{
	if (slotIndex == AI_ID)
	{
		OnAiRecv(packet);
		return;
	}

	User* pUser = GetUser(slotIndex);
	if (pUser) pUser->Dispatch(packet);
}

void GameServer::OnAiRecv(const Packet& packet)
{
	// AS_AI: fixed header (userIndex) + variable text -> SC_NpcChat to that user
	if (packet.packetId != static_cast<uint16_t>(PACKET_ID::AS_AI))
	{
		Log::Warning("[AI] Unknown AI packet id={}", packet.packetId);
		return;
	}

	if (packet.dataSpan.size() < AS_AI_PACKET_SIZE)
		return;

	auto* pAsAi   = reinterpret_cast<const AS_AI*>(packet.dataSpan.data());
	const uint8_t* textPtr = packet.dataSpan.data() + sizeof(AS_AI);
	size_t textLen         = packet.dataSpan.size()  - sizeof(AS_AI);

	// playerId -> slotIndex
	auto it = g_idToIndexMap.find(pAsAi->userIndex);
	if (it == g_idToIndexMap.end())
	{
		Log::Warning("[AI] User not found playerId={}", pAsAi->userIndex);
		return;
	}

	User* pUser = g_userPool.Get(it->second);
	if (!pUser) return;

	// Build SC_NpcChat (variable-length)
	const size_t totalSize = sizeof(SC_NpcChat) + textLen;
	std::vector<uint8_t> buf(totalSize, 0);
	auto* pResp  = reinterpret_cast<SC_NpcChat*>(buf.data());
	pResp->size  = static_cast<uint16_t>(totalSize);
	pResp->id    = static_cast<uint16_t>(PACKET_ID::SC_NPC_CHAT);

	if (textLen > 0)
		CopyMemory(buf.data() + sizeof(SC_NpcChat), textPtr, textLen);

	pUser->Send(std::span<const uint8_t>(buf.data(), buf.size()));
	Log::Info("[AI] NPC response sent playerId={} textLen={}", pAsAi->userIndex, textLen);
}

void GameServer::Run()
{
	User::RegisterHandlers();
	StartServer();
	StartNpcTick();
}

void GameServer::Stop()
{
	StopNpcTick();
	IOCPNetwork::Stop();
}

#include "pch.h"
#include "GameServer.h"

void GameServer::OnConnect(const uint32_t slotIndex)
{
	Log::Info("[OnConnect] SlotIndex({})", slotIndex);
}

void GameServer::OnClose(const uint32_t slotIndex)
{
	Log::Info("[OnClose] SlotIndex({})", slotIndex);
}

void GameServer::OnRecv(uint32_t slotIndex, const Packet& packet)
{
	User* pUser = GetUser(slotIndex);
	if (pUser) pUser->Dispatch(packet);
}

void GameServer::Run()
{
	User::RegisterHandlers();
	StartServer();
}

void GameServer::Stop()
{
	IOCPNetwork::Stop();
}

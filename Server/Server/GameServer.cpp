#include "pch.h"
#include "GameServer.h"

void GameServer::OnConnect(const uint32_t clientId)
{
	std::print("[OnConnect]: Id({})\n", clientId);
}

void GameServer::OnClose(const uint32_t clientId)
{
	std::print("[OnClose]: Id({})\n", clientId);
}

void GameServer::OnRecv(uint32_t clientId, const Packet& packet)
{
	User* pUser = GetUser(clientId);
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

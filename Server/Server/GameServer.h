#pragma once
#include "pch.h"
#include "IOCPNetwork.h"

class GameServer : public IOCPNetwork
{
public:
	GameServer() = default;
	virtual ~GameServer() = default;

	virtual void OnConnect(const uint32_t clientId) override
	{
		std::print("[OnConnect]: Id({})\n", clientId);
	}

	virtual void OnClose(const uint32_t clientId) override
	{
		std::print("[OnClose]: Id({})\n", clientId);
	}

	virtual void OnRecv(uint32_t clientId, const Packet& packet) override
	{
		User* pUser = GetUser(clientId);
		if (pUser) pUser->Dispatch(packet);
	}

	virtual void OnSend(uint32_t clientId) override {}

	void Run()
	{
		User::RegisterHandlers();
		StartServer();
	}

	void Stop()
	{
		IOCPNetwork::Stop();
	}
};

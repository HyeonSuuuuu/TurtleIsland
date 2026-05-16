#pragma once
#include "pch.h"
#include "IOCPNetwork.h"

class GameServer : public IOCPNetwork
{
public:
	GameServer() = default;
	virtual ~GameServer() = default;

	virtual void OnConnect(const uint32_t clientId) override;
	virtual void OnClose(const uint32_t clientId) override;
	virtual void OnRecv(uint32_t clientId, const Packet& packet) override;
	virtual void OnSend(uint32_t clientId) override {}

	void Run();
	void Stop();
};

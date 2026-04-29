export module GameServer;

import IOCPNetwork;
import Common;
import Define;
import User;
import "../../Common/Protocol.h";

export class GameServer : public IOCPNetwork
{
public:
	GameServer() = default;
	virtual ~GameServer() = default;

	virtual void OnConnect(const uint32 clientIndex) override
	{
		std::print("[OnConnect]: Index({})\n", clientIndex);
	}

	virtual void OnClose(const uint32 clientIndex) override
	{
		std::print("[OnClose]: Index({})\n", clientIndex);
	}

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

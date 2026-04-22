export module GameServer;

import IOCPNetwork;
import Common;
import Define;
import User;
import "../../Common/Protocol.h";



export class GameServer : public IOCPNetwork<User>
{
public:
	GameServer() = default;
	virtual ~GameServer() = default;

	virtual void OnConnect(const uint32 clientIndex) override
	{
		std::print("[Onconnect]: Index({})\n", clientIndex);
	}

	virtual void OnClose(const uint32 clientIndex) override
	{
		std::print("[OnClose]: Index({})\n", clientIndex);
	}

	void Run()
	{
		User::InitHandlers();

		StartServer();
	}

	void End()
	{
		DestroyThread();
	}
};

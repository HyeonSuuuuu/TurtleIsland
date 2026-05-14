#include "pch.h"
#include "GameServer.h"
#include "../utils/minidump/mdump.h"
#include "../../Common/Protocol.h"

constexpr uint16_t SERVER_PORT = NetConfig::GAME_SERVER_PORT;

int main()
{
	GameServer server;
	MiniDump::Begin();

	server.InitSocket();
	server.ConnectToAIServer("127.0.0.1", NetConfig::AI_SERVER_PORT);
	server.Listen(SERVER_PORT);
	server.Run();

	while (true)
	{
		std::string inputCmd;
		std::getline(std::cin, inputCmd);

		if (inputCmd == "quit")
			break;
	}

	server.Stop();

	MiniDump::End();
	return 0;
}

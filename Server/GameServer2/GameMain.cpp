#include "pch.h"
#include "GameServer.h"

// GameDev 게임 서버 포트 (Common/Protocol.h NetConfig::GAME_SERVER_PORT 와 동일)
constexpr int GAME_SERVER_PORT = 11021;

int main()
{
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);

	GameServer server;
	if (!server.InitSocket())              return -1;
	if (!server.Listen(GAME_SERVER_PORT))  return -1;
	server.Run();

	Log::Success("Server running — 'quit' to stop");

	std::string cmd;
	while (std::getline(std::cin, cmd))
	{
		if (cmd == "quit") break;
	}

	server.Stop();
	return 0;
}

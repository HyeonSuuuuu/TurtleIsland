import GameServer;
import Common;
import "../../Common/Protocol.h";

constexpr uint16 SERVER_PORT = NetConfig::GAME_SERVER_PORT;


int main()
{
	GameServer server;

	server.InitEnvironment();
	server.InitSocket();
	server.ConnectToAIServer("127.0.0.1", NetConfig::AI_SERVER_PORT);
	server.BindandListen(SERVER_PORT);
	server.Run();

	while (true)
	{
		std::string inputCmd;
		std::getline(std::cin, inputCmd);

		if (inputCmd == "quit")
		{
			break;
		}
	}

	server.End();
	return 0;
}
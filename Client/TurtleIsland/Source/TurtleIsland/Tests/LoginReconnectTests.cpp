#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/GameInstance.h"
#include "HAL/RunnableThread.h"
#include "Network/ClientTransport.h"
#include "Network/NetSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoginReconnectTest,
	"TurtleIsland.Network.Login.FirstClickReconnect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoginReconnectTest::RunTest(const FString& Parameters)
{
	UGameInstance* Instance = NewObject<UGameInstance>();
	UNetSubsystem* Net = NewObject<UNetSubsystem>(Instance);
	// Fail before opening a connection: this test never contacts a real account server.
	Net->SetServerIP(TEXT("invalid-test-address"));
	AddExpectedErrorPlain(TEXT("[Transport] Invalid server address"), EAutomationExpectedErrorFlags::Contains, 2);
	Net->RequestLogin(TEXT(" test-user "), TEXT(" test-password "));
	TestTrue(TEXT("First click remains pending after reconnect cleanup"), Net->bLoginPending);
	TestEqual(TEXT("Pending ID retained"), Net->PendingLoginID, FString(TEXT("test-user")));
	TestEqual(TEXT("Pending password retained"), Net->PendingLoginPW, FString(TEXT("test-password")));

	// Replace only the completed test transport; preserve the real RequestLogin state.
	if (Net->Thread)
	{
		Net->Thread->Kill(true);
		delete Net->Thread;
		Net->Thread = nullptr;
	}
	delete Net->Networker;
	Net->Networker = new FClientTransport();
	Net->PollPendingLogin(0.1f);
	TestTrue(TEXT("Request waits while connecting"), Net->bLoginPending);
	Net->Networker->bConnected.store(true);
	Net->PollPendingLogin(0.1f);
	TestFalse(TEXT("Connection completion sends without another click"), Net->bLoginPending);
	TArray<uint8> Bytes;
	TestTrue(TEXT("Login packet queued"), Net->Networker->SendQueue.Dequeue(Bytes));
	if (TestEqual(TEXT("Login packet size"), Bytes.Num(), static_cast<int32>(CS_LOGIN_PACKET_SIZE)))
	{
		const auto* Packet = reinterpret_cast<const CS_Login*>(Bytes.GetData());
		TestEqual(TEXT("Login packet type"), Packet->id, static_cast<uint16>(PACKET_ID::CS_LOGIN_REQUEST));
		TestEqual(TEXT("Login ID matches first click"),
			FClientPacketCodec::DecodeFixedUtf8(Packet->userID, sizeof(Packet->userID)), FString(TEXT("test-user")));
	}
	Net->PollPendingLogin(0.1f);
	TestTrue(TEXT("Further ticks do not resend"), Net->Networker->SendQueue.IsEmpty());
	Net->RequestLogin(TEXT("test-user"), TEXT("test-password"));
	TestFalse(TEXT("Connected request sends immediately"), Net->bLoginPending);
	TestTrue(TEXT("Connected request packet queued"), Net->Networker->SendQueue.Dequeue(Bytes));
	Net->ShutdownNetwork();

	Net->RequestLogin(TEXT("test-user"), TEXT("test-password"));
	Net->PollPendingLogin(Net->LoginConnectTimeoutSec);
	TestFalse(TEXT("Failed connection clears pending state"), Net->bLoginPending);
	TestNull(TEXT("Failed connection releases transport"), Net->Networker);
	Net->RequestLogin(TEXT(" "), TEXT("test-password"));
	TestFalse(TEXT("Empty ID does not start another login"), Net->bLoginPending);
	TestNull(TEXT("Empty ID does not open a connection"), Net->Networker);
	Net->ShutdownNetwork();
	return true;
}

#endif

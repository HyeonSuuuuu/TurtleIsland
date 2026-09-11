#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Network/ClientPacketCodec.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClientPacketCodecLobbyBoundsTest,
	"TurtleIsland.Network.ClientPacketCodec.LobbyBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClientPacketCodecLobbyBoundsTest::RunTest(const FString& Parameters)
{
	TArray<uint8> Packet;
	Packet.SetNumZeroed(sizeof(PacketHeader) + sizeof(int16));

	PacketHeader* Header = reinterpret_cast<PacketHeader*>(Packet.GetData());
	Header->size = static_cast<uint16>(Packet.Num());
	Header->id = static_cast<uint16>(PACKET_ID::SC_LOBBY_RESPONSE);

	int16* RoomCount = reinterpret_cast<int16*>(Packet.GetData() + sizeof(PacketHeader));
	*RoomCount = 1;

	TArray<FDecodedRoomInfo> Rooms;
	TestFalse(TEXT("Truncated lobby packet is rejected"), FClientPacketCodec::DecodeLobbyRooms(Packet, Rooms));
	TestEqual(TEXT("Rejected packet does not publish partial rooms"), Rooms.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClientPacketCodecUtf8ClampTest,
	"TurtleIsland.Network.ClientPacketCodec.Utf8Clamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClientPacketCodecUtf8ClampTest::RunTest(const FString& Parameters)
{
	char Buffer[5] = {};
	FClientPacketCodec::CopyStringToBuffer(Buffer, UE_ARRAY_COUNT(Buffer), TEXT("한글"));

	const FString Decoded = FClientPacketCodec::DecodeFixedUtf8(Buffer, UE_ARRAY_COUNT(Buffer));
	TestEqual(TEXT("UTF-8 copy stops before splitting the second Korean code point"), Decoded, FString(TEXT("한")));
	return true;
}

#endif

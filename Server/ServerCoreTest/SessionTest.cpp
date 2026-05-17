#include "pch.h"
#include "../Server/Session.h"
#include "../Server/Globals.h"

// ----------------------------------------------------
// SessionTest
// ----------------------------------------------------

TEST(SessionTest, InitSetsIndex)
{
	Session s;
	s.Init(42);
	EXPECT_EQ(s.GetId(), 42u);
}

TEST(SessionTest, PacketBufferRoundTrip)
{
	Session s;
	s.Init(0);

	PacketHeader header{};
	header.size = PACKET_HEADER_SIZE;
	header.id   = 200;

	std::span<const uint8_t> data{ reinterpret_cast<const uint8_t*>(&header), PACKET_HEADER_SIZE };
	s.PushPacket(data);

	auto result = s.PopPacket();
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->packetId, static_cast<uint16_t>(200));
	EXPECT_EQ(result->sessionIndex, 0u);
}

TEST(SessionTest, PacketBufferReturnsNulloptWhenEmpty)
{
	Session s;
	s.Init(1);
	auto result = s.PopPacket();
	EXPECT_FALSE(result.has_value());
}

TEST(SessionTest, PacketBufferCompaction)
{
	// 패킷 버퍼 크기가 8096바이트이고, 패킷 헤더 크기가 5바이트이므로, 1619개의 패킷을 쓰면 버퍼가 꽉 찹니다.
	// 1620개의 패킷이 잘써지는지 테스트

	Session s;
	s.Init(0);

	PacketHeader header{};
	header.size = PACKET_HEADER_SIZE;
	header.id   = 100;

	std::span<const uint8_t> data(reinterpret_cast<const uint8_t*>(&header), PACKET_HEADER_SIZE);

	for (int i = 0; i < 1619; ++i)
	{
		s.PushPacket(data);
		bool ok = s.PopPacket().has_value();
		EXPECT_TRUE(ok);
	}

	header.id = 999;
	s.PushPacket(data);
	auto result = s.PopPacket();
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->packetId, static_cast<uint16_t>(999));
}

// ----------------------------------------------------
// GlobalsTest
// ----------------------------------------------------

TEST(GlobalsTest, SessionCountMatchesMax)
{
	EXPECT_EQ(NetConfig::MAX_CLIENTS, 10000u);
}

TEST(GlobalsTest, IndexMappingAfterLogin)
{
	g_idToIndexMap.clear();

	g_idToIndexMap["player1"] = 0;
	g_idToIndexMap["player2"] = 1;

	EXPECT_EQ(g_idToIndexMap.at("player1"), 0u);
	EXPECT_EQ(g_idToIndexMap.at("player2"), 1u);
	EXPECT_EQ(g_idToIndexMap.count("unknown"), 0u);

	g_idToIndexMap.clear();
}

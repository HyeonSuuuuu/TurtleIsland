#pragma once
#include "Type.h"




namespace NetConfig
{
	inline constexpr uint16_t GAME_SERVER_PORT = 11021;
	inline constexpr uint16_t AI_SERVER_PORT = 4444;
	inline constexpr uint16_t MAX_CLIENTS = 10000;
}


// Protocol


enum class PACKET_ID : uint16_t
{
	// SYSTEM (1~99)
	SYS_USER_CONNECT = 11,
	SYS_USER_DISCONNECT = 12,
	SYS_END = 30,

	// DB (100~199)
	DB_END = 199,

	// Client To Server (200~299)
	CS_LOGIN_REQUEST = 201,

	CS_LOBBY_ENTER_REQUEST = 211,

	CS_ROOM_ENTER_REQUEST = 221,

	CS_ROOM_LEAVE_REQUEST = 231,

	CS_ROOM_CHAT_REQUEST = 241,

	CS_ROOM_START_REQUEST = 251,

	CS_MOVE = 261,

	CS_NPC_CHAT = 271,

	// Server To Client (300 ~ 399)
	SC_LOGIN_RESPONSE = 301,

	SC_LOBBY_RESPONSE = 311,

	SC_ROOM_ENTER_RESPONSE = 321,
	SC_ROOM_ENTER_NOTIFY = 322,

	SC_ROOM_LEAVE_NOTIFY = 331,

	SC_ROOM_CHAT_RESPONSE = 341,
	SC_ROOM_CHAT_NOTIFY = 342,

	SC_ROOM_START_NOTIFY = 351,

	SC_MOVE_NOTIFY = 361,

	SC_NPC_CHAT = 371,


	// AI To Server (3000 ~ 3099)
	AS_AI = 3001,

	// Server To AI (3100 ~ 3199)
	SA_AI = 3101,

};

enum class ERROR_CODE : uint8_t
{
	NONE = 0,

	// User Manager
	USER_LOGIN_USED_ALL = 11,
	/*	USER_MGR_INVALID_INDEX = 11,
		USER_MGR_INVALID_USER_UNIQUE_ID = 12,*/

		// Login
};

#pragma pack(push, 1)
struct PacketHeader
{
	uint16_t size;
	uint16_t id;
	uint8_t type; // 압축여부, 암호화 여부 등 속성을 알아내는 값
};
inline constexpr uint16_t PACKET_HEADER_SIZE = sizeof(PacketHeader);

// ----------------- Login -----------------
inline constexpr int32_t MAX_USER_ID_LEN = 32;
inline constexpr int32_t MAX_USER_PW_LEN = 32;
struct PlayerInfo
{
	uint32_t playerId; // session ID
	char name[MAX_USER_ID_LEN + 1];
};

struct CS_Login : public PacketHeader
{
	char userID[MAX_USER_ID_LEN + 1];
	char userPW[MAX_USER_PW_LEN + 1];
};
inline constexpr uint16_t CS_LOGIN_PACKET_SIZE = sizeof(CS_Login);

struct SC_Login : public PacketHeader
{
	uint8_t result;
	uint32_t playerId; // 클라이언트에서 자신 패킷 식별용
};
inline constexpr uint16_t SC_LOGIN_PACKET_SIZE = sizeof(SC_Login);

// --------------- Lobby ----------------
inline constexpr int32_t MAX_ROOM_TITLE_LEN = 64;
struct RoomInfo
{
	uint16_t roomId;
	char roomTitle[MAX_ROOM_TITLE_LEN];
	char hostName[MAX_USER_ID_LEN]; // 방장 이름만
	uint16_t userCount;
};

struct CS_LobbyEnter : public PacketHeader
{
	// Empty
};
inline constexpr uint16_t CS_LOBBY_ENTER_PACKET_SIZE = sizeof(CS_LobbyEnter);

struct SC_LobbyEnter : public PacketHeader
{
	int16_t roomCount; // 현재 방 개수
	RoomInfo roomList[1]; // 가변
};
inline constexpr uint16_t SC_LOBBY_ENTER_PACKET_SIZE = sizeof(SC_LobbyEnter);

// --------------- Room ----------------

struct CS_RoomEnter : public PacketHeader
{
	uint32_t roomIndex;
};
inline constexpr uint16_t CS_ROOM_ENTER_PACKET_SIZE = sizeof(CS_RoomEnter);

struct SC_RoomEnter : public PacketHeader
{
	uint8_t result;
};
inline constexpr uint16_t SC_ROOM_ENTER_PACKET_SIZE = sizeof(SC_RoomEnter);

struct SC_RoomEnterNotify : public PacketHeader
{
	char userID[MAX_USER_ID_LEN + 1];
	uint32_t userIndex;
};
inline constexpr uint16_t SC_ROOM_ENTER_NOTIFY_PACKET_SIZE = sizeof(SC_RoomEnterNotify);

struct CS_RoomLeave : public PacketHeader
{
};
inline constexpr uint16_t CS_ROOM_LEAVE_PACKET_SIZE = sizeof(CS_RoomLeave);

struct SC_RoomLeaveNotify : public PacketHeader
{
	uint32_t userIndex;
};
inline constexpr uint16_t SC_ROOM_LEAVE_NOTIFY_PACKET_SIZE = sizeof(SC_RoomLeaveNotify);


inline constexpr int32_t MAX_CHAT_MSG_LEN = 256;
struct CS_RoomChat : public PacketHeader
{
	char chatMsg[MAX_CHAT_MSG_LEN + 1];
};
inline constexpr uint16_t CS_ROOM_CHAT_PACKET_SIZE = sizeof(CS_RoomChat);

struct SC_RoomChatResponse : public PacketHeader
{
	uint8_t result;
};
inline constexpr uint16_t SC_ROOM_CHAT_RESPONSE_PACKET_SIZE = sizeof(SC_RoomChatResponse);

struct SC_RoomChatNotify : public PacketHeader
{
	uint32_t userIndex;
	char chatMsg[MAX_CHAT_MSG_LEN + 1];
};
inline constexpr uint16_t SC_ROOM_CHAT_NOTIFY_PACKET_SIZE = sizeof(SC_RoomChatNotify);

struct CS_RoomStart : public PacketHeader
{
};
inline constexpr uint16_t CS_ROOM_START_PACKET_SIZE = sizeof(CS_RoomStart);

struct SC_RoomStart : public PacketHeader
{
	uint8_t result;
};
inline constexpr uint16_t SC_ROOM_START_PACKET_SIZE = sizeof(SC_RoomStart);

//  --------------- InGame ----------------
struct CS_Move : public PacketHeader
{
	uint16_t inputFlag;
	float yaw;
};
inline constexpr uint16_t CS_MOVE_PACKET_SIZE = sizeof(CS_Move);

struct SC_MoveNotify : public PacketHeader
{
	uint32_t userIndex;
	uint16_t inputFlag;
	float x, z;   // 2D 좌표 (y는 사용 안 함)
	float yaw;
};
inline constexpr uint16_t SC_MOVE_NOTIFY_PACKET_SIZE = sizeof(SC_MoveNotify);


//  --------------- NPC ----------------
struct CS_NpcChat : public PacketHeader
{
	uint32_t userIndex;
	uint32_t npcIndex;
	// 가변 AI 텍스트
};
inline constexpr uint16_t CS_NPC_CHAT_PACKET_SIZE = sizeof(CS_NpcChat);

struct SA_AI: public PacketHeader
{
	uint32_t userIndex;
	uint32_t npcIndex;
	// 가변 AI 텍스트
};
inline constexpr uint16_t SA_AI_PACKET_SIZE = sizeof(SA_AI);

struct AS_AI : public PacketHeader
{
	uint32_t userIndex;
	// 가변 AI 텍스트
};
inline constexpr uint16_t AS_AI_PACKET_SIZE = sizeof(AS_AI);

struct SC_NpcChat : public PacketHeader
{
	// 가변 AI 텍스트
};
inline constexpr uint16_t SC_NPC_CHAT_PACKET_SIZE = sizeof(SC_NpcChat);

#pragma pack(pop)
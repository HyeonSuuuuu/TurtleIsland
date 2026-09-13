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

	CS_ROOM_CREATE_REQUEST = 222,

	CS_ROOM_LEAVE_REQUEST = 231,

	CS_ROOM_CHAT_REQUEST = 241,

	CS_ROOM_START_REQUEST = 251,

	CS_MOVE = 261,

	CS_NPC_CHAT = 271,

	CS_INGAME_READY = 281,   // 인게임 레벨 로딩 완료 → 현재 상태 요청

	CS_ATTACK = 291,         // 공격/스킬 시전 요청 (의도만. 판정은 전적으로 서버)
	CS_USE_ITEM = 292,

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

	SC_NPC_SPAWN = 381,
	SC_NPC_MOVE  = 382,
	SC_NPC_DESPAWN = 383,

	SC_ATTACK_NOTIFY = 391,  // 시전 성공 → 전원이 모션 재생
	SC_DAMAGE_NOTIFY = 392,  // 피해 결과 → 전원이 HP/연출 반영
	SC_PLAYER_STATE = 393,
	SC_ACTION_RESULT = 394,
	SC_HEALTH_STATE = 395,
	SC_RESPAWN_NOTIFY = 396,
	SC_PLAYER_INFO = 397,


	// AI To Server (3000 ~ 3099)
	AS_AI = 3001,
	AS_AI_AGENT = 3002, // AS_AI header + {context,say} JSON; never sent to a client verbatim
	AS_MCP = 3003,      // PacketHeader + one MCP JSON-RPC message

	// Server To AI (3100 ~ 3199)
	SA_AI = 3101,
	SA_AI_AGENT = 3102, // SA_AI header + {context,conversation,say} JSON
	SA_MCP = 3103,      // PacketHeader + one MCP JSON-RPC message

};

enum class ERROR_CODE : uint8_t
{
	NONE = 0,

	// User Manager
	USER_LOGIN_USED_ALL = 11,
	/*	USER_MGR_INVALID_INDEX = 11,
		USER_MGR_INVALID_USER_UNIQUE_ID = 12,*/

	// Login
	USER_LOGIN_FAIL = 21,           // 비밀번호 불일치 또는 DB 오류
	USER_ALREADY_LOGGED_IN = 22,    // 이미 접속 중인 계정

	// Room
	ROOM_TITLE_DUP = 31,            // 같은 제목의 방이 이미 있음
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
	uint8_t isNewAccount; // 1 = 이번 로그인에서 신규 가입됨, 0 = 기존 계정
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

struct CS_RoomCreate : public PacketHeader
{
	char roomTitle[MAX_ROOM_TITLE_LEN];
};
inline constexpr uint16_t CS_ROOM_CREATE_PACKET_SIZE = sizeof(CS_RoomCreate);

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

// CS_Move.inputFlag 비트. 클라와 서버가 같은 값을 봐야 하므로 여기서 한 번만 정의한다.
inline constexpr uint16_t INPUT_FLAG_W      = 1 << 0;
inline constexpr uint16_t INPUT_FLAG_A      = 1 << 1;
inline constexpr uint16_t INPUT_FLAG_S      = 1 << 2;
inline constexpr uint16_t INPUT_FLAG_D      = 1 << 3;
inline constexpr uint16_t GAMEPLAY_PROTOCOL_VERSION = 3;
inline constexpr uint8_t ENTITY_NONE = 0;
inline constexpr uint8_t ENTITY_PLAYER = 1;
inline constexpr uint8_t ENTITY_NPC = 2;
inline constexpr int32_t INVENTORY_SLOT_COUNT = 6;
inline constexpr int32_t PLAYER_SKILL_COUNT = 3;

struct CS_Move : public PacketHeader
{
	uint32_t lifeId;
	uint16_t inputFlag;
	float yaw;
	float x, y;   // 클라 실제 위치 (Unreal 월드 X, Y)
	float z, velocityZ;
	uint8_t airborne;
};
inline constexpr uint16_t CS_MOVE_PACKET_SIZE = sizeof(CS_Move);

// 인게임 로딩 완료 통지 + 실제 스폰 위치. 서버가 다른 플레이어/NPC 상태를 응답.
struct CS_IngameReady : public PacketHeader
{
	float x, y, yaw;   // 클라 실제 스폰 위치
	float z;
	uint16_t protocolVersion;
};
inline constexpr uint16_t CS_INGAME_READY_PACKET_SIZE = sizeof(CS_IngameReady);

struct SC_MoveNotify : public PacketHeader
{
	uint32_t userIndex;
	uint32_t lifeId;
	uint16_t inputFlag;
	float x, y;   // Unreal world position; player Z is relayed below.
	float yaw;
	float z, velocityZ;
	uint8_t airborne;
	int32_t hp, maxHP;
};
inline constexpr uint16_t SC_MOVE_NOTIFY_PACKET_SIZE = sizeof(SC_MoveNotify);

// Same pose payload, but reset/teleport instead of interpolating from the previous life.
struct SC_RespawnNotify : SC_MoveNotify {};
inline constexpr uint16_t SC_RESPAWN_NOTIFY_PACKET_SIZE = sizeof(SC_RespawnNotify);

// --------------- NPC (게임플레이) ----------------
struct SC_NpcSpawn : public PacketHeader
{
	uint32_t npcId;
	uint16_t npcType;
	float    x, y;   // 2D 좌표
	float    yaw;
	int32_t  hp;
	int32_t  maxHp;
};
inline constexpr uint16_t SC_NPC_SPAWN_PACKET_SIZE = sizeof(SC_NpcSpawn);

struct SC_NpcMove : public PacketHeader
{
	uint32_t npcId;
	float    x, y;   // 2D 좌표
	float    yaw;
};
inline constexpr uint16_t SC_NPC_MOVE_PACKET_SIZE = sizeof(SC_NpcMove);

struct SC_NpcDespawn : public PacketHeader
{
	uint32_t npcId;
};
inline constexpr uint16_t SC_NPC_DESPAWN_PACKET_SIZE = sizeof(SC_NpcDespawn);


// --------------- 전투 ----------------
// 클라는 판정하지 않는다. 의도만 보내고, 모션조차 서버 통지를 받은 뒤에 재생한다.
// Rejections return SC_ActionResult; accepted casts return SC_AttackNotify.

inline constexpr uint16_t SKILL_ID_BASIC_ATTACK = 0;

struct CS_Attack : public PacketHeader
{
	uint32_t lifeId;
	uint16_t skillId;    // 0 = 기본공격
	uint32_t targetId;   // Used only when targetKind != ENTITY_NONE (NPC 0 is valid).
	uint8_t targetKind;  // ENTITY_NONE selects targets by server range/facing.
	float    yaw;        // 시전 순간 바라본 방향 (서버가 가진 yaw 는 최대 한 주기 낡다)
};
inline constexpr uint16_t CS_ATTACK_PACKET_SIZE = sizeof(CS_Attack);

// 시전 성공 통지. 시전자 본인도 이걸 받고 나서야 모션을 재생한다.
struct SC_AttackNotify : public PacketHeader
{
	uint32_t casterId;
	uint16_t skillId;
	float    yaw;
	uint8_t casterKind;
	uint32_t targetId;
	uint8_t targetKind;
};
inline constexpr uint16_t SC_ATTACK_NOTIFY_PACKET_SIZE = sizeof(SC_AttackNotify);

// Damage is a separate result per target; current server rules apply it immediately.
// 광역기는 대상 수만큼 여러 번 온다.
struct SC_DamageNotify : public PacketHeader
{
	uint32_t targetId;
	uint32_t causerId;
	int32_t  damage;
	int32_t  remainHP;
	uint8_t  flags;      // 1=크리티컬 2=회피 4=막기
	uint8_t targetKind, causerKind;
	int32_t maxHP;
};
inline constexpr uint16_t SC_DAMAGE_NOTIFY_PACKET_SIZE = sizeof(SC_DamageNotify);

struct SC_HealthState : PacketHeader
{
	uint32_t playerId;
	int32_t hp, maxHP;
};
inline constexpr uint16_t SC_HEALTH_STATE_PACKET_SIZE = sizeof(SC_HealthState);

// Public identity only. Inventory, gold, XP and other private stats remain owner-only.
struct SC_PlayerInfo : PacketHeader
{
	uint32_t playerId, lifeId;
	int32_t level;
	char userName[MAX_USER_ID_LEN + 1];
};
inline constexpr uint16_t SC_PLAYER_INFO_PACKET_SIZE = sizeof(SC_PlayerInfo);

struct ItemSlot
{
	uint16_t itemId;
	uint16_t count;
};

struct CS_UseItem : PacketHeader
{
	uint32_t lifeId;
	uint8_t slot;
	uint16_t expectedItemId;
	uint32_t inventoryRevision;
};
inline constexpr uint16_t CS_USE_ITEM_PACKET_SIZE = sizeof(CS_UseItem);

// Sent to the owning player only. Cooldowns are remaining milliseconds, not client clock timestamps.
struct SC_PlayerState : PacketHeader
{
	uint32_t playerId;
	uint32_t lifeId;
	int32_t hp, maxHP, mp, maxMP;
	int32_t level, exp, expToNext, gold;
	int32_t attack, defense;
	float moveSpeed;
	uint32_t inventoryRevision;
	ItemSlot items[INVENTORY_SLOT_COUNT];
	uint32_t skillCooldownMs[PLAYER_SKILL_COUNT];
	uint32_t itemCooldownMs;
};
inline constexpr uint16_t SC_PLAYER_STATE_PACKET_SIZE = sizeof(SC_PlayerState);

enum class ActionResult : uint8_t { Ok, Invalid, Dead, Cooldown, NotEnoughMP, NoEffect, StaleInventory };
struct SC_ActionResult : PacketHeader
{
	uint16_t requestId;
	uint16_t actionId;
	ActionResult result;
};
inline constexpr uint16_t SC_ACTION_RESULT_PACKET_SIZE = sizeof(SC_ActionResult);

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

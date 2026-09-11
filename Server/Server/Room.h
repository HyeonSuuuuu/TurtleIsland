#pragma once
#include "pch.h"
#include "Define.h"
#include "NPC.h"
#include "../../Common/Protocol.h"
#include "../../Common/GameplayRules.h"

class User;

enum class RoomEnterResult : uint8_t
{
	Created = 0,
	Joined  = 1,
	Full    = 2,
	InGame  = 3,
};

class Room
{
public:
	Room() { m_userSlots.fill(INVALID_SESSION_INDEX); }

	// Enter: creates room if inactive, joins if active and not full.
	// On creation, uses `title` if non-empty, otherwise a default "<user>'s Room".
	// Returns result code. Thread-safe.
	RoomEnterResult Enter(uint16_t roomId, uint32_t slotIndex, std::string_view userName, std::string_view title = {});

	// Leave: removes user. Returns true if room is now empty.
	bool Leave(uint32_t slotIndex);

	// Send data to all users except excludeSlot.
	void Broadcast(const std::span<const uint8_t> data, uint32_t excludeSlot = INVALID_SESSION_INDEX);

	void StartGame();   // m_inGame = true + NPC 스폰

	// Thread-safe gameplay entry points. npcId is a room-local slot in [0, MAX_ROOM_NPCS).
	bool SpawnNpc(uint32_t npcId, uint16_t type, float x, float y, int32_t hp, float speed);
	bool DespawnNpc(uint32_t npcId);
	void PlayerReady(User& user, const CS_IngameReady& request);
	void MovePlayer(User& user, const CS_Move& request);
	void Attack(User& user, const CS_Attack& request);
	void UseItem(User& user, const CS_UseItem& request);

	// NPC (방 인스턴스 소유)
	void TickNpcs();    // 틱 스레드가 호출: AI/이동 갱신 + 방 broadcast
	void BroadcastNpcSpawns(uint32_t toSlot = INVALID_SESSION_INDEX); // 현재 NPC 스폰 통지

	// toSlot 유저에게 같은 방의 '다른' 플레이어들의 현재 위치를 SC_MoveNotify 로 전송(초기 스폰용)
	void SendPlayerStatesToUser(uint32_t toSlot);

	RoomInfo GetRoomInfo() const;

	// Lock-free reads — safe because m_isActive is atomic
	bool     IsActive()   const { return m_isActive.load(std::memory_order_relaxed); }
	bool     IsFull()     const { std::lock_guard lock(m_mutex); return m_userCount >= MAX_ROOM_USERS; }
	bool     IsInGame()   const { std::lock_guard lock(m_mutex); return m_inGame; }
	uint32_t GetUserCount() const { std::lock_guard lock(m_mutex); return m_userCount; }
	uint32_t GetHostSlot()  const { std::lock_guard lock(m_mutex); return m_hostSlot; }
	uint16_t GetRoomId()    const { return m_roomId; }

	// Iterate user slots under lock (excludes excludeSlot).
	template<typename Fn>
	void ForEachUser(Fn&& fn, uint32_t excludeSlot = INVALID_SESSION_INDEX) const
	{
		std::lock_guard lock(m_mutex);
		for (uint32_t i = 0; i < m_userCount; ++i)
		{
			if (m_userSlots[i] != excludeSlot)
				fn(m_userSlots[i]);
		}
	}

private:
	void DespawnNpcLocked(NPC& npc);
	void SendStateLocked(User& user, const GameplayRules::PlayerState& state, uint64_t now);
	void BroadcastGameplayLocked(std::span<const uint8_t> data, uint32_t exclude = INVALID_SESSION_INDEX);
	std::unordered_map<uint32_t, GameplayRules::PlayerState> m_playerStates;
	void SetHost(uint32_t slotIndex, std::string_view userName);

	uint16_t m_roomId   = 0;
	char     m_title[MAX_ROOM_TITLE_LEN]{};
	char     m_hostName[MAX_USER_ID_LEN]{};
	uint32_t m_hostSlot = INVALID_SESSION_INDEX;

	std::array<uint32_t, MAX_ROOM_USERS> m_userSlots{};
	uint32_t m_userCount = 0;

	std::array<NPC, MAX_ROOM_NPCS> m_npcs{};   // 방 인스턴스 NPC
	std::atomic<bool> m_isActive{ false };
	bool              m_inGame = false;

	mutable std::mutex m_mutex;
};

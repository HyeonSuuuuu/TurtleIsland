#pragma once
#include "pch.h"
#include "Define.h"
#include "NPC.h"
#include "../../Common/Protocol.h"

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

	// NPC (방 인스턴스 소유)
	void TickNpcs();    // 틱 스레드가 호출: AI/이동 갱신 + 방 broadcast
	void BroadcastNpcSpawns(uint32_t toSlot = INVALID_SESSION_INDEX); // 현재 NPC 스폰 통지

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

#include "pch.h"
#include "Room.h"
#include "Globals.h"
#include "MonsterScript.h"
#include <cmath>

RoomEnterResult Room::Enter(uint16_t roomId, uint32_t slotIndex, std::string_view userName, std::string_view title)
{
	std::lock_guard lock(m_mutex);

	if (!m_isActive)
	{
		// Create new room
		m_roomId   = roomId;
		m_isActive = true;
		m_inGame   = false;
		m_userCount = 0;
		m_userSlots.fill(INVALID_SESSION_INDEX);

		std::string roomTitle = title.empty() ? (std::string(userName) + "'s Room") : std::string(title);
		strncpy_s(m_title, roomTitle.c_str(), MAX_ROOM_TITLE_LEN - 1);
		m_title[MAX_ROOM_TITLE_LEN - 1] = '\0';

		SetHost(slotIndex, userName);
		m_userSlots[m_userCount++] = slotIndex;
		return RoomEnterResult::Created;
	}

	if (m_inGame)
		return RoomEnterResult::InGame;

	if (m_userCount >= MAX_ROOM_USERS)
		return RoomEnterResult::Full;

	m_userSlots[m_userCount++] = slotIndex;
	return RoomEnterResult::Joined;
}

bool Room::Leave(uint32_t slotIndex)
{
	std::lock_guard lock(m_mutex);

	// Find and remove
	bool found = false;
	for (uint32_t i = 0; i < m_userCount; ++i)
	{
		if (m_userSlots[i] == slotIndex)
		{
			for (uint32_t j = i; j < m_userCount - 1; ++j)
				m_userSlots[j] = m_userSlots[j + 1];
			m_userSlots[--m_userCount] = INVALID_SESSION_INDEX;
			found = true;
			break;
		}
	}

	if (!found) return false;

	if (m_userCount == 0)
	{
		m_isActive = false;
		m_inGame   = false;
		for (auto& npc : m_npcs) npc.Clear();   // 방 비면 NPC 정리
		return true;
	}

	// Promote new host if host left
	if (m_hostSlot == slotIndex)
	{
		m_hostSlot = m_userSlots[0];
		User* newHost = g_userPool.Get(m_userSlots[0]);
		if (newHost)
		{
			strncpy_s(m_hostName, newHost->GetUserId().c_str(), MAX_USER_ID_LEN - 1);
			m_hostName[MAX_USER_ID_LEN - 1] = '\0';
		}
	}
	return false;
}

void Room::Broadcast(const std::span<const uint8_t> data, uint32_t excludeSlot)
{
	std::lock_guard lock(m_mutex);
	for (uint32_t i = 0; i < m_userCount; ++i)
	{
		uint32_t slot = m_userSlots[i];
		if (slot == excludeSlot) continue;
		User* pUser = g_userPool.Get(slot);
		if (pUser) pUser->Send(data);
	}
}

void Room::StartGame()
{
	std::lock_guard lock(m_mutex);
	m_inGame = true;

	// NPC 스폰: 원점 주변 원형 배치 (좌표 2D x,y)
	constexpr int   SPAWN_COUNT  = 5;
	constexpr float SPAWN_RADIUS = 300.0f;
	const size_t    defCount     = g_monsterDefs.size();
	for (int i = 0; i < SPAWN_COUNT && i < static_cast<int>(MAX_ROOM_NPCS); ++i)
	{
		float ang = static_cast<float>(i) / SPAWN_COUNT * 6.2831853f;
		float sx  = std::cos(ang) * SPAWN_RADIUS;
		float sy  = std::sin(ang) * SPAWN_RADIUS;
		if (defCount > 0)
		{
			uint16_t type = static_cast<uint16_t>(i % defCount);
			const MonsterDef& def = g_monsterDefs[type];
			m_npcs[i].Spawn(static_cast<uint32_t>(i), type, sx, sy, def.hp, def.speed);
		}
		else
		{
			m_npcs[i].Spawn(static_cast<uint32_t>(i), 0, sx, sy, 100, 6.0f);   // Lua 미로드 폴백
		}
	}
}

void Room::TickNpcs()
{
	constexpr float CHASE_RANGE = 600.0f;   // 추격 시작 거리
	constexpr float STOP_DIST   = 60.0f;    // 추격 정지 거리(근접)

	std::vector<SC_NpcMove>              moves;
	std::array<uint32_t, MAX_ROOM_USERS> slots{};
	uint32_t userCnt = 0;

	{
		std::lock_guard lock(m_mutex);
		if (!m_inGame) return;

		for (uint32_t i = 0; i < m_userCount; ++i)
			slots[userCnt++] = m_userSlots[i];

		for (auto& npc : m_npcs)
		{
			if (!npc.IsActive()) continue;

			// 최근접 PC 탐색(CHASE_RANGE 내)
			float bestD2  = CHASE_RANGE * CHASE_RANGE;
			float tx = npc.m_anchorX, ty = npc.m_anchorY;
			bool  chasing = false;
			for (uint32_t i = 0; i < userCnt; ++i)
			{
				User* u = g_userPool.Get(slots[i]);
				if (!u) continue;
				float dx = u->x - npc.x, dy = u->y - npc.y;
				float d2 = dx * dx + dy * dy;
				if (d2 < bestD2) { bestD2 = d2; tx = u->x; ty = u->y; chasing = true; }
			}

			npc.m_state = chasing ? NpcState::Chase : NpcState::Return;

			// 목표(추격=PC / 비추격=앵커) 방향으로 연속 이동
			float dx = tx - npc.x, dy = ty - npc.y;
			float dist = std::sqrt(dx * dx + dy * dy);
			float arrive = chasing ? STOP_DIST : 4.0f;
			if (dist > arrive)
			{
				float inv = 1.0f / dist;
				npc.x += dx * inv * npc.m_speed;
				npc.y += dy * inv * npc.m_speed;
				npc.yaw = std::atan2(dy, dx) * 180.0f / 3.14159265f;
			}

			SC_NpcMove m{};
			m.size  = SC_NPC_MOVE_PACKET_SIZE;
			m.id    = static_cast<uint16_t>(PACKET_ID::SC_NPC_MOVE);
			m.npcId = npc.GetId();
			m.x = npc.x; m.y = npc.y; m.yaw = npc.yaw;
			moves.push_back(m);
		}
	}

	if (moves.empty() || userCnt == 0) return;

	for (uint32_t i = 0; i < userCnt; ++i)
	{
		User* u = g_userPool.Get(slots[i]);
		if (!u) continue;
		for (const auto& m : moves)
			u->Send(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&m), SC_NPC_MOVE_PACKET_SIZE));
	}
}

void Room::BroadcastNpcSpawns(uint32_t toSlot)
{
	std::vector<SC_NpcSpawn>             pkts;
	std::array<uint32_t, MAX_ROOM_USERS> slots{};
	uint32_t userCnt = 0;

	{
		std::lock_guard lock(m_mutex);
		for (auto& npc : m_npcs)
		{
			if (!npc.IsActive()) continue;
			SC_NpcSpawn s{};
			s.size    = SC_NPC_SPAWN_PACKET_SIZE;
			s.id      = static_cast<uint16_t>(PACKET_ID::SC_NPC_SPAWN);
			s.npcId   = npc.GetId();
			s.npcType = npc.m_type;
			s.x = npc.x; s.y = npc.y; s.yaw = npc.yaw;
			s.hp = npc.m_hp;
			pkts.push_back(s);
		}

		if (toSlot != INVALID_SESSION_INDEX)
			slots[userCnt++] = toSlot;
		else
			for (uint32_t i = 0; i < m_userCount; ++i)
				slots[userCnt++] = m_userSlots[i];
	}

	if (pkts.empty()) return;

	for (uint32_t i = 0; i < userCnt; ++i)
	{
		User* u = g_userPool.Get(slots[i]);
		if (!u) continue;
		for (const auto& s : pkts)
			u->Send(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&s), SC_NPC_SPAWN_PACKET_SIZE));
	}
}

RoomInfo Room::GetRoomInfo() const
{
	std::lock_guard lock(m_mutex);
	RoomInfo info{};
	info.roomId    = m_roomId;
	info.userCount = static_cast<uint16_t>(m_userCount);
	strncpy_s(info.roomTitle, m_title,    MAX_ROOM_TITLE_LEN - 1);
	strncpy_s(info.hostName,  m_hostName, MAX_USER_ID_LEN - 1);
	return info;
}

void Room::SetHost(uint32_t slotIndex, std::string_view userName)
{
	m_hostSlot = slotIndex;
	strncpy_s(m_hostName, userName.data(), MAX_USER_ID_LEN - 1);
	m_hostName[MAX_USER_ID_LEN - 1] = '\0';
}

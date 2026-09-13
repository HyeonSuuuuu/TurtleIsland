#include "pch.h"
#include "Room.h"
#include "Globals.h"
#include "MonsterScript.h"
#include "ObstacleMap.h"
#include <cmath>
#include <random>

namespace
{
	// Seconds from montage start at play rate 1: basic 0.30, Q 0.50, E 0.40.
	// Authoritative hit timing; clients never decide damage or report a trusted hit.
	constexpr std::array<uint32_t, PLAYER_SKILL_COUNT> AttackImpactDelayMs = {300, 500, 400};
	constexpr float PlayerRadius = 42.f; // APlayerAgent capsule XY radius.
	constexpr double MovementBurstSeconds = 0.25; // Bounded allowance for batched TCP arrivals.
}

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
	m_playerStates.erase(slotIndex);
	m_npcConversations.erase(slotIndex);
	m_pendingAttacks.erase(slotIndex);
	m_movementBudgets.erase(slotIndex);
	for (auto& npc : m_npcs)
		if (npc.m_attackTarget == slotIndex) { npc.m_attackImpactAt = 0; npc.m_attackTarget = INVALID_SESSION_INDEX; }

	if (m_userCount == 0)
	{
		m_isActive = false;
		m_inGame   = false;
		for (auto& npc : m_npcs) npc.Clear();   // 방 비면 NPC 정리
		m_npcSpawnSlots.fill({});
		m_pendingAttacks.clear();
		m_movementBudgets.clear();
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
	if (!m_isActive || m_inGame) return;
	m_inGame = true;

	// 플레이어를 인게임 스폰(월드좌표)으로 배치 — 장애물 그리드/클라 좌표와 일치
	for (uint32_t i = 0; i < m_userCount; ++i)
	{
		User* u = g_userPool.Get(m_userSlots[i]);
		if (!u) continue;
		m_playerStates[m_userSlots[i]] = GameplayRules::PlayerState{};
		float px = MapSpawn::PLAYER_X + static_cast<float>(i % 2) * 120.0f;
		float py = MapSpawn::PLAYER_Y + static_cast<float>(i / 2) * 120.0f;
		g_obstacleMap.SnapToFree(px, py, PlayerRadius);
		u->x = px; u->y = py; u->yaw = MapSpawn::PLAYER_YAW;
	}

	m_npcSpawnSlots.fill({});
	uint32_t slot = 0;
	for (size_t area = 0; area < g_monsterSpawnAreas.size(); ++area)
		for (uint32_t i = 0; i < g_monsterSpawnAreas[area].count && slot < m_npcs.size(); ++i)
			m_npcSpawnSlots[slot++].areaIndex = static_cast<int>(area);
	SpawnAreaNpcsLocked(GetTickCount64(), false);
}

void Room::SpawnAreaNpcsLocked(uint64_t now, bool broadcast)
{
	// Thread-local RNG is shared by rooms, but only each room's lock protects its slots.
	thread_local std::mt19937 random(std::random_device{}());
	std::uniform_real_distribution<float> unit(0.f, 1.f);
	for (uint32_t id = 0; id < m_npcs.size(); ++id)
	{
		auto& slot = m_npcSpawnSlots[id];
		if (m_npcs[id].IsActive() || slot.areaIndex < 0 || now < slot.readyAt) continue;
		const auto& area = g_monsterSpawnAreas[slot.areaIndex];
		bool spawned = false;
		if (g_obstacleMap.IsLoaded())
		{
			for (int attempt = 0; attempt < 64; ++attempt)
			{
				// sqrt gives uniform density across the disk, not just near the center.
				const float angle = unit(random) * 6.2831853f;
				const float radius = std::sqrt(unit(random)) * area.radius;
				const float x = area.x + std::cos(angle) * radius;
				const float y = area.y + std::sin(angle) * radius;
				const double dx = static_cast<double>(x) - area.x, dy = static_cast<double>(y) - area.y;
				if (dx * dx + dy * dy > static_cast<double>(area.radius) * area.radius) continue;
				if (!g_obstacleMap.CanMoveTo(x, y)) continue;
				const auto& def = g_monsterDefs[area.type];
				m_npcs[id].Spawn(id, area.type, x, y, def.hp, def.speed);
				if (broadcast) BroadcastNpcSpawnLocked(m_npcs[id]);
				spawned = true;
				break;
			}
		}
		if (!spawned)
		{
			if (slot.readyAt == 0)
				Log::Warning("[NPC] room={} area={} has no valid spawn position or map; retrying", m_roomId, slot.areaIndex);
			slot.readyAt = now + 1000;
		}
	}
}

void Room::TickNpcs()
{
	// Keep outgoing lifecycle and movement packets ordered, including slot reuse.
	std::lock_guard lock(m_mutex);
	constexpr float CHASE_RANGE = 600.0f;   // 추격 시작 거리
	constexpr float STOP_DIST   = 60.0f;    // 추격 정지 거리(근접)

	std::vector<SC_NpcMove>              moves;
	std::array<uint32_t, MAX_ROOM_USERS> slots{};
	uint32_t userCnt = 0;

	{
		if (!m_inGame) return;
		for (uint32_t i = 0; i < m_userCount; ++i)
			if (User* user = g_userPool.Get(m_userSlots[i]))
			{
				TickPlayerRespawnLocked(m_userSlots[i], GetTickCount64());
				ResolvePendingAttackLocked(*user, GetTickCount64());
			}
		SpawnAreaNpcsLocked(GetTickCount64(), true);

		for (uint32_t i = 0; i < m_userCount; ++i)
			slots[userCnt++] = m_userSlots[i];

		for (auto& npc : m_npcs)
		{
			if (!npc.IsActive()) continue;
			if (!npc.IsAlive())
			{
				DespawnNpcLocked(npc, true);
				continue;
			}

			// 최근접 PC 탐색(CHASE_RANGE 내)
			float bestD2  = CHASE_RANGE * CHASE_RANGE;
			float tx = npc.m_anchorX, ty = npc.m_anchorY;
			bool  chasing = false;
			User* target = nullptr;
			for (uint32_t i = 0; i < userCnt; ++i)
			{
				User* u = g_userPool.Get(slots[i]);
				if (!u) continue;
				auto state = m_playerStates.find(slots[i]);
				if (state == m_playerStates.end() || !state->second.ready || state->second.hp <= 0) continue;
				float dx = u->x - npc.x, dy = u->y - npc.y;
				float d2 = dx * dx + dy * dy;
				if (d2 < bestD2) { bestD2 = d2; tx = u->x; ty = u->y; chasing = true; target = u; }
			}

			npc.m_state = chasing ? NpcState::Chase : NpcState::Return;
			const uint64_t now = GetTickCount64();
			ResolveNpcAttackLocked(npc, now);
			const MonsterDef fallback;
			const auto& def = npc.m_type < g_monsterDefs.size() ? g_monsterDefs[npc.m_type] : fallback;
			if (target && bestD2 <= def.attackRange * def.attackRange && now >= npc.m_attackReadyAt)
			{
				auto& state = m_playerStates.at(target->GetSlotIndex());
				if (state.hp > 0 && !state.airborne)
				{
					npc.m_attackReadyAt = now + def.attackCooldownMs;
					npc.m_attackImpactAt = now + def.attackWindupMs;
					npc.m_attackTarget = target->GetSlotIndex();
					npc.yaw = std::atan2(target->y - npc.y, target->x - npc.x) * 180.f / 3.14159265f;
					SC_AttackNotify attack{};
					attack.size = sizeof(attack); attack.id = static_cast<uint16_t>(PACKET_ID::SC_ATTACK_NOTIFY);
					attack.casterId = npc.GetId(); attack.casterKind = ENTITY_NPC;
					attack.targetId = target->GetId(); attack.targetKind = ENTITY_PLAYER; attack.yaw = npc.yaw;
					BroadcastGameplayLocked({reinterpret_cast<const uint8_t*>(&attack), sizeof(attack)});
				}
			}

			// 목표(추격=PC / 비추격=앵커) 방향으로 연속 이동
			float dx = tx - npc.x, dy = ty - npc.y;
			float dist = std::sqrt(dx * dx + dy * dy);
			float arrive = chasing ? STOP_DIST : 4.0f;
			if (dist > arrive && now >= npc.m_attackReadyAt)
			{
				float inv = 1.0f / dist;
				float ncx = npc.x + dx * inv * npc.m_speed;
				float ncy = npc.y + dy * inv * npc.m_speed;
				// 장애물 충돌: 막힌 칸이면 이동 정지 (길찾기 없음 — 벽에 막힘)
				if (g_obstacleMap.CanTraverse(npc.x, npc.y, ncx, ncy)) { npc.x = ncx; npc.y = ncy; }
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

void Room::ResolveNpcAttackLocked(NPC& npc, uint64_t now)
{
	if (!npc.m_attackImpactAt || now < npc.m_attackImpactAt) return;
	const uint32_t slot = npc.m_attackTarget;
	npc.m_attackImpactAt = 0;
	npc.m_attackTarget = INVALID_SESSION_INDEX;
	const auto found = m_playerStates.find(slot);
	if (!npc.IsActive() || !npc.IsAlive() || slot >= MAX_CLIENTS || found == m_playerStates.end() ||
		std::find(m_userSlots.begin(), m_userSlots.begin() + m_userCount, slot) == m_userSlots.begin() + m_userCount) return;
	auto& state = found->second;
	if (!state.ready || state.hp <= 0 || state.airborne) return;
	User* target = g_userPool.Get(slot);
	const MonsterDef fallback;
	const auto& def = npc.m_type < g_monsterDefs.size() ? g_monsterDefs[npc.m_type] : fallback;
	const float dx = target->x - npc.x, dy = target->y - npc.y;
	const float distance = std::hypot(dx, dy);
	const float yaw = npc.yaw * 0.0174532925f;
	if (distance > def.attackRange || (distance > 1.f && (dx * std::cos(yaw) + dy * std::sin(yaw)) / distance < 0.2f) ||
		!g_obstacleMap.CanTraverse(npc.x, npc.y, target->x, target->y)) return;
	SC_DamageNotify damage{};
	damage.size = sizeof(damage); damage.id = static_cast<uint16_t>(PACKET_ID::SC_DAMAGE_NOTIFY);
	damage.targetId = target->GetId(); damage.targetKind = ENTITY_PLAYER;
	damage.causerId = npc.GetId(); damage.causerKind = ENTITY_NPC;
	damage.damage = std::max(1, def.attackDamage - state.defense);
	state.hp -= std::min(state.hp, damage.damage); damage.remainHP = state.hp; damage.maxHP = state.maxHP;
	if (state.hp == 0) TickPlayerRespawnLocked(slot, now);
	BroadcastGameplayLocked({reinterpret_cast<const uint8_t*>(&damage), sizeof(damage)});
	SendStateLocked(*target, state, now);
}

void Room::TickPlayerRespawnLocked(uint32_t slot, uint64_t now)
{
	auto found = m_playerStates.find(slot);
	if (!m_inGame || slot >= MAX_CLIENTS || found == m_playerStates.end() || !found->second.ready || found->second.hp > 0 ||
		std::find(m_userSlots.begin(), m_userSlots.begin() + m_userCount, slot) == m_userSlots.begin() + m_userCount) return;
	auto& state = found->second;
	if (!state.respawnReadyAt)
	{
		state.respawnReadyAt = now + GameplayRules::PlayerRespawnDelayMs;
		state.airborne = false; state.velocityZ = 0.f;
		m_pendingAttacks.erase(slot);
		m_movementBudgets.erase(slot);
		for (auto& npc : m_npcs)
			if (npc.m_attackTarget == slot) { npc.m_attackImpactAt = 0; npc.m_attackTarget = INVALID_SESSION_INDEX; }
		if (auto chat = m_npcConversations.find(slot); chat != m_npcConversations.end())
		{
			chat->second.context.clear();
			chat->second.quote = nullptr;
		}
		return;
	}
	if (now < state.respawnReadyAt) return;
	float x = MapSpawn::PLAYER_X, y = MapSpawn::PLAYER_Y;
	g_obstacleMap.SnapToFree(x, y, PlayerRadius);
	// Do not revive inside an obstacle or at a distant fallback position. Retry on the next tick.
	if (std::hypot(x - MapSpawn::PLAYER_X, y - MapSpawn::PLAYER_Y) > 150.f ||
		!g_obstacleMap.CanTraverse(x, y, x, y, PlayerRadius)) return;
	User& user = *g_userPool.Get(slot);
	user.x = x; user.y = y; user.yaw = MapSpawn::PLAYER_YAW;
	state.z = MapSpawn::PLAYER_GROUND_Z + GameplayRules::PlayerCapsuleHalfHeight + 2.f;
	state.hp = state.maxHP; state.mp = state.maxMP;
	state.airborne = false; state.velocityZ = 0.f;
	state.landingReadyAt = state.castReadyAt = state.respawnReadyAt = 0;
	++state.lifeId;
	m_movementBudgets[slot] = {now, state.moveSpeed * MovementBurstSeconds};
	SC_RespawnNotify packet{};
	packet.size = sizeof(packet); packet.id = static_cast<uint16_t>(PACKET_ID::SC_RESPAWN_NOTIFY);
	packet.userIndex = user.GetId(); packet.lifeId = state.lifeId;
	packet.x = user.x; packet.y = user.y; packet.z = state.z; packet.yaw = user.yaw;
	packet.hp = state.hp; packet.maxHP = state.maxHP;
	BroadcastGameplayLocked({reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)});
	SendStateLocked(user, state, now);
}

void Room::BroadcastNpcSpawns(uint32_t toSlot)
{
	// Snapshot and enqueue share the despawn lock so a stale spawn cannot resurrect an NPC.
	std::lock_guard lock(m_mutex);
	if (!m_inGame) return;
	std::vector<SC_NpcSpawn>             pkts;
	std::array<uint32_t, MAX_ROOM_USERS> slots{};
	uint32_t userCnt = 0;

	{
		for (auto& npc : m_npcs)
		{
			if (!npc.IsActive() || !npc.IsAlive()) continue;
			SC_NpcSpawn s{};
			s.size    = SC_NPC_SPAWN_PACKET_SIZE;
			s.id      = static_cast<uint16_t>(PACKET_ID::SC_NPC_SPAWN);
			s.npcId   = npc.GetId();
			s.npcType = npc.m_type;
			s.x = npc.x; s.y = npc.y; s.yaw = npc.yaw;
			s.hp = npc.m_hp;
			s.maxHp = npc.m_maxHp;
			pkts.push_back(s);
		}

		for (uint32_t i = 0; i < m_userCount; ++i)
			if (toSlot == INVALID_SESSION_INDEX || m_userSlots[i] == toSlot)
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

bool Room::SpawnNpc(uint32_t npcId, uint16_t type, float x, float y, int32_t hp, float speed)
{
	std::lock_guard lock(m_mutex);
	if (!m_inGame || npcId >= m_npcs.size() || m_npcs[npcId].IsActive() ||
		m_npcSpawnSlots[npcId].areaIndex >= 0 || hp <= 0 ||
		!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(speed) || speed < 0.f)
		return false;

	g_obstacleMap.SnapToFree(x, y);
	if (!g_obstacleMap.CanMoveTo(x, y)) return false;
	m_npcs[npcId].Spawn(npcId, type, x, y, hp, speed);
	BroadcastNpcSpawnLocked(m_npcs[npcId]);
	return true;
}

void Room::BroadcastNpcSpawnLocked(const NPC& npc)
{
	SC_NpcSpawn packet{};
	packet.size = SC_NPC_SPAWN_PACKET_SIZE;
	packet.id = static_cast<uint16_t>(PACKET_ID::SC_NPC_SPAWN);
	packet.npcId = npc.GetId();
	packet.npcType = npc.m_type;
	packet.x = npc.x;
	packet.y = npc.y;
	packet.yaw = npc.yaw;
	packet.hp = npc.m_hp;
	packet.maxHp = npc.m_maxHp;
	for (uint32_t i = 0; i < m_userCount; ++i)
		if (User* user = g_userPool.Get(m_userSlots[i]))
			user->Send(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)));
}

bool Room::DespawnNpc(uint32_t npcId)
{
	std::lock_guard lock(m_mutex);
	if (!m_inGame || npcId >= m_npcs.size())
		return false;
	if (!m_npcs[npcId].IsActive())
	{
		const bool pending = m_npcSpawnSlots[npcId].areaIndex >= 0;
		m_npcSpawnSlots[npcId] = {};
		return pending;
	}
	DespawnNpcLocked(m_npcs[npcId]);
	return true;
}

void Room::DespawnNpcLocked(NPC& npc, bool died)
{
	// A targeted wind-up must not hit a different NPC that later reuses this slot.
	std::erase_if(m_pendingAttacks, [&npc](const auto& entry)
	{
		return entry.second.request.targetKind == ENTITY_NPC && entry.second.request.targetId == npc.GetId();
	});
	auto& slot = m_npcSpawnSlots[npc.GetId()];
	if (died && slot.areaIndex >= 0)
		slot.readyAt = GetTickCount64() + g_monsterSpawnAreas[slot.areaIndex].respawnMs;
	else
		slot = {};
	SC_NpcDespawn packet{};
	packet.size = SC_NPC_DESPAWN_PACKET_SIZE;
	packet.id = static_cast<uint16_t>(PACKET_ID::SC_NPC_DESPAWN);
	packet.npcId = npc.GetId();
	npc.Clear();
	for (uint32_t i = 0; i < m_userCount; ++i)
		if (User* user = g_userPool.Get(m_userSlots[i]))
			user->Send(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)));
}

void Room::SendPlayerStatesToUser(uint32_t toSlot)
{
	std::lock_guard lock(m_mutex);
	if (toSlot >= MAX_CLIENTS) return;
	User* dst = g_userPool.Get(toSlot);
	if (!dst) return;

	for (uint32_t i = 0; i < m_userCount; ++i)
	{
		if (m_userSlots[i] == toSlot) continue;
		User* src = g_userPool.Get(m_userSlots[i]);
		if (!src) continue;

		SC_MoveNotify m{};
		m.size      = SC_MOVE_NOTIFY_PACKET_SIZE;
		m.id        = static_cast<uint16_t>(PACKET_ID::SC_MOVE_NOTIFY);
		m.userIndex = src->GetId();
		m.inputFlag = 0;
		m.x = src->x; m.y = src->y; m.yaw = src->yaw;
		if (const auto state = m_playerStates.find(src->GetSlotIndex()); state != m_playerStates.end())
		{
			if (!state->second.ready) continue;
			m.z = state->second.z; m.velocityZ = state->second.velocityZ; m.airborne = state->second.airborne;
			m.lifeId = state->second.lifeId;
			m.hp = state->second.hp; m.maxHP = state->second.maxHP;
			const SC_PlayerInfo info = MakePlayerInfo(*src, state->second);
			dst->Send({reinterpret_cast<const uint8_t*>(&info), sizeof(info)});
		}
		dst->Send(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&m), SC_MOVE_NOTIFY_PACKET_SIZE));
	}
}

void Room::BroadcastGameplayLocked(std::span<const uint8_t> data, uint32_t exclude)
{
	for (uint32_t i = 0; i < m_userCount; ++i)
	{
		const uint32_t slot = m_userSlots[i];
		auto state = m_playerStates.find(slot);
		if (slot == exclude || state == m_playerStates.end() || !state->second.ready) continue;
		if (User* user = g_userPool.Get(slot)) user->Send(data);
	}
}

SC_PlayerInfo Room::MakePlayerInfo(const User& user, const GameplayRules::PlayerState& state)
{
	SC_PlayerInfo info{};
	info.size = sizeof(info); info.id = static_cast<uint16_t>(PACKET_ID::SC_PLAYER_INFO);
	info.playerId = user.GetId(); info.lifeId = state.lifeId; info.level = state.level;
	strncpy_s(info.userName, user.GetUserId().c_str(), MAX_USER_ID_LEN);
	return info;
}

void Room::SendStateLocked(User& user, const GameplayRules::PlayerState& state, uint64_t now)
{
	SC_PlayerState packet{};
	packet.size = sizeof(packet); packet.id = static_cast<uint16_t>(PACKET_ID::SC_PLAYER_STATE);
	packet.playerId = user.GetId();
	packet.lifeId = state.lifeId;
	packet.hp = state.hp; packet.maxHP = state.maxHP; packet.mp = state.mp; packet.maxMP = state.maxMP;
	packet.level = state.level; packet.exp = state.exp; packet.expToNext = state.expToNext; packet.gold = state.gold;
	packet.attack = state.attack; packet.defense = state.defense; packet.moveSpeed = state.moveSpeed;
	packet.inventoryRevision = state.inventoryRevision;
	std::copy(state.items.begin(), state.items.end(), packet.items);
	for (size_t i = 0; i < state.skillReadyAt.size(); ++i)
		packet.skillCooldownMs[i] = static_cast<uint32_t>(state.skillReadyAt[i] > now ? state.skillReadyAt[i] - now : 0);
	packet.itemCooldownMs = static_cast<uint32_t>(state.itemReadyAt > now ? state.itemReadyAt - now : 0);
	user.Send({reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)});
	SC_HealthState health{};
	health.size = sizeof(health); health.id = static_cast<uint16_t>(PACKET_ID::SC_HEALTH_STATE);
	health.playerId = user.GetId(); health.hp = state.hp; health.maxHP = state.maxHP;
	BroadcastGameplayLocked({reinterpret_cast<const uint8_t*>(&health), sizeof(health)}, user.GetSlotIndex());
	const SC_PlayerInfo info = MakePlayerInfo(user, state);
	BroadcastGameplayLocked({reinterpret_cast<const uint8_t*>(&info), sizeof(info)});
}

void Room::PlayerReady(User& user, const CS_IngameReady& request)
{
	if (request.protocolVersion != GAMEPLAY_PROTOCOL_VERSION ||
		!GameplayRules::ValidPose(request.x, request.y, request.z, request.yaw, 0.f)) return;
	{
		std::lock_guard lock(m_mutex);
		auto found = m_playerStates.find(user.GetSlotIndex());
		if (!m_inGame || found == m_playerStates.end()) return;
		auto& state = found->second;
		// Ready is a snapshot request, not a reset or a teleport after initial entry.
		if (!state.ready)
		{
			// XY belongs to the room spawn, not an untrusted first client pose.
			if (!g_obstacleMap.CanTraverse(user.x, user.y, user.x, user.y, PlayerRadius)) return;
			user.yaw = request.yaw;
			state.z = request.z; state.ready = true;
			m_movementBudgets[user.GetSlotIndex()] = {GetTickCount64(), state.moveSpeed * MovementBurstSeconds};
		}
		SendStateLocked(user, state, GetTickCount64());
		SC_MoveNotify me{};
		me.size = sizeof(me); me.id = static_cast<uint16_t>(PACKET_ID::SC_MOVE_NOTIFY);
		me.userIndex = user.GetId(); me.x = user.x; me.y = user.y; me.yaw = user.yaw;
		me.lifeId = state.lifeId;
		me.z = state.z; me.velocityZ = state.velocityZ; me.airborne = state.airborne;
		me.hp = state.hp; me.maxHP = state.maxHP;
		user.Send({reinterpret_cast<const uint8_t*>(&me), sizeof(me)});
		BroadcastGameplayLocked({reinterpret_cast<const uint8_t*>(&me), sizeof(me)}, user.GetSlotIndex());
	}
	SendPlayerStatesToUser(user.GetSlotIndex());
	BroadcastNpcSpawns(user.GetSlotIndex());
}

void Room::MovePlayer(User& user, const CS_Move& request)
{
	std::lock_guard lock(m_mutex);
	auto found = m_playerStates.find(user.GetSlotIndex());
	if (!m_inGame || found == m_playerStates.end() || !found->second.ready) return;
	auto& state = found->second;
	const uint64_t now = GetTickCount64();
	const bool accepted = ApplyPlayerMoveLocked(user, request, now);
	SC_MoveNotify packet{};
	packet.size = sizeof(packet); packet.id = static_cast<uint16_t>(PACKET_ID::SC_MOVE_NOTIFY);
	packet.userIndex = user.GetId();
	packet.lifeId = state.lifeId;
	packet.inputFlag = accepted && now >= state.castReadyAt && now >= state.landingReadyAt ? request.inputFlag : 0;
	packet.x = user.x; packet.y = user.y; packet.yaw = user.yaw;
	packet.z = state.z; packet.velocityZ = state.velocityZ; packet.airborne = state.airborne;
	packet.hp = state.hp; packet.maxHP = state.maxHP;
	if (accepted)
		BroadcastGameplayLocked({reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)}, user.GetSlotIndex());
	else
		user.Send({reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)});
}

bool Room::ApplyPlayerMoveLocked(User& user, const CS_Move& request, uint64_t now)
{
	auto found = m_playerStates.find(user.GetSlotIndex());
	auto budget = m_movementBudgets.find(user.GetSlotIndex());
	if (!m_inGame || found == m_playerStates.end() || !found->second.ready || found->second.hp <= 0 ||
		request.lifeId != found->second.lifeId ||
		budget == m_movementBudgets.end() || !GameplayRules::ValidPose(request.x, request.y, request.z, request.yaw, request.velocityZ) ||
		request.airborne > 1) return false;
	auto& state = found->second;
	auto& credit = budget->second;
	const double elapsed = now > credit.updatedAt ? (now - credit.updatedAt) / 1000. : 0.;
	credit.updatedAt = std::max(now, credit.updatedAt);
	credit.distance = std::min(state.moveSpeed * MovementBurstSeconds, credit.distance + state.moveSpeed * elapsed);
	const double distance = std::hypot(static_cast<double>(request.x) - user.x, static_cast<double>(request.y) - user.y);
	const bool casting = now < state.castReadyAt;
	// Casting stops XY and new jumps, but must still accept gravity and landing updates.
	if (casting && (distance > 0. || (!state.airborne && (request.airborne || request.velocityZ > 0.f)) ||
		(state.airborne && request.airborne && request.velocityZ > state.velocityZ))) return false;
	if (now < state.landingReadyAt && (distance > 0. || request.airborne || request.velocityZ > 0.f)) return false;
	if (distance > credit.distance || !g_obstacleMap.CanTraverse(user.x, user.y, request.x, request.y, PlayerRadius)) return false;
	if (state.airborne && !request.airborne) state.landingReadyAt = now + GameplayRules::LandingRecoveryMs;
	credit.distance -= distance;
	user.x = request.x; user.y = request.y;
	if (!casting) user.yaw = request.yaw;
	state.z = request.z; state.velocityZ = request.velocityZ; state.airborne = request.airborne != 0;
	return true;
}

void Room::Attack(User& user, const CS_Attack& request)
{
	std::lock_guard lock(m_mutex);
	auto found = m_playerStates.find(user.GetSlotIndex());
	if (!m_inGame || found == m_playerStates.end() || !found->second.ready) return;
	auto& state = found->second;
	const uint64_t now = GetTickCount64();
	ActionResult result = ActionResult::Invalid;
	if (request.lifeId == state.lifeId && std::isfinite(request.yaw) && (request.targetKind == ENTITY_NONE ||
		(request.targetKind == ENTITY_NPC && request.targetId < m_npcs.size() && m_npcs[request.targetId].IsAlive())))
		result = m_pendingAttacks.contains(user.GetSlotIndex()) ? ActionResult::Cooldown :
			GameplayRules::UseSkill(state, request.skillId, now);
	SC_ActionResult response{};
	response.size = sizeof(response); response.id = static_cast<uint16_t>(PACKET_ID::SC_ACTION_RESULT);
	response.requestId = static_cast<uint16_t>(PACKET_ID::CS_ATTACK); response.actionId = request.skillId; response.result = result;
	user.Send({reinterpret_cast<const uint8_t*>(&response), sizeof(response)});
	if (result != ActionResult::Ok) { SendStateLocked(user, state, now); return; }

	user.yaw = request.yaw;
	SC_AttackNotify attack{};
	attack.size = sizeof(attack); attack.id = static_cast<uint16_t>(PACKET_ID::SC_ATTACK_NOTIFY);
	attack.casterId = user.GetId(); attack.casterKind = ENTITY_PLAYER; attack.skillId = request.skillId;
	attack.yaw = request.yaw; attack.targetKind = request.targetKind; attack.targetId = request.targetId;
	BroadcastGameplayLocked({reinterpret_cast<const uint8_t*>(&attack), sizeof(attack)});
	m_pendingAttacks[user.GetSlotIndex()] = {request, now + AttackImpactDelayMs[request.skillId]};
	SendStateLocked(user, state, now);
}

void Room::ResolvePendingAttackLocked(User& user, uint64_t now)
{
	const auto pending = m_pendingAttacks.find(user.GetSlotIndex());
	if (pending == m_pendingAttacks.end() || now < pending->second.impactAt) return;
	const CS_Attack request = pending->second.request;
	m_pendingAttacks.erase(pending); // Resolve each accepted swing once, including a miss.
	const auto state = m_playerStates.find(user.GetSlotIndex());
	if (!m_inGame || state == m_playerStates.end() || !state->second.ready || state->second.hp <= 0) return;
	ApplyAttackHitLocked(user, request, now);
}

void Room::ApplyAttackHitLocked(User& user, const CS_Attack& request, uint64_t now)
{
	auto& state = m_playerStates.at(user.GetSlotIndex());
	const auto& skill = GameplayRules::Skills[request.skillId];
	const float radians = std::remainder(request.yaw, 360.f) * 0.0174532925f;
	for (auto& npc : m_npcs)
	{
		if (!npc.IsActive() || !npc.IsAlive() || (request.targetKind == ENTITY_NPC && request.targetId != npc.GetId())) continue;
		const float dx = npc.x - user.x, dy = npc.y - user.y;
		const float distance = std::sqrt(dx * dx + dy * dy);
		if (distance > skill.range || (distance > 1.f && (dx * std::cos(radians) + dy * std::sin(radians)) / distance < skill.facingDot)) continue;
		SC_DamageNotify damage{};
		damage.size = sizeof(damage); damage.id = static_cast<uint16_t>(PACKET_ID::SC_DAMAGE_NOTIFY);
		damage.targetId = npc.GetId(); damage.targetKind = ENTITY_NPC;
		damage.causerId = user.GetId(); damage.causerKind = ENTITY_PLAYER;
		damage.damage = std::max(1, skill.damage + state.attack - 20);
		npc.m_hp -= std::min(npc.m_hp, damage.damage);
		damage.remainHP = npc.m_hp; damage.maxHP = npc.m_maxHp;
		BroadcastGameplayLocked({reinterpret_cast<const uint8_t*>(&damage), sizeof(damage)});
		if (!npc.IsAlive()) { GameplayRules::RewardKill(state); DespawnNpcLocked(npc, true); }
		if (request.skillId == SKILL_ID_BASIC_ATTACK) break;
	}
	SendStateLocked(user, state, now);
}

void Room::UseItem(User& user, const CS_UseItem& request)
{
	std::lock_guard lock(m_mutex);
	auto found = m_playerStates.find(user.GetSlotIndex());
	if (!m_inGame || found == m_playerStates.end() || !found->second.ready) return;
	const uint64_t now = GetTickCount64();
	SC_ActionResult response{};
	response.size = sizeof(response); response.id = static_cast<uint16_t>(PACKET_ID::SC_ACTION_RESULT);
	response.requestId = static_cast<uint16_t>(PACKET_ID::CS_USE_ITEM); response.actionId = request.slot;
	response.result = GameplayRules::UseItem(found->second, request, now);
	user.Send({reinterpret_cast<const uint8_t*>(&response), sizeof(response)});
	SendStateLocked(user, found->second, now);
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

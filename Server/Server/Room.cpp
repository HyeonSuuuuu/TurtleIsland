#include "pch.h"
#include "Room.h"
#include "Globals.h"
#include "MonsterScript.h"
#include "ObstacleMap.h"
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
	m_playerStates.erase(slotIndex);

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
		g_obstacleMap.SnapToFree(px, py);   // 벽에 끼지 않도록 보정
		u->x = px; u->y = py; u->yaw = 0.f;
	}

	// NPC 스폰: 아레나 개방 통로(NPC_ANCHOR) 주변 원형 배치 (좌표 2D x,y, 월드)
	constexpr int   SPAWN_COUNT  = 5;
	constexpr float SPAWN_RADIUS = 150.0f;
	const size_t    defCount     = g_monsterDefs.size();
	for (int i = 0; i < SPAWN_COUNT && i < static_cast<int>(MAX_ROOM_NPCS); ++i)
	{
		float ang = static_cast<float>(i) / SPAWN_COUNT * 6.2831853f;
		float sx  = MapSpawn::NPC_ANCHOR_X + std::cos(ang) * SPAWN_RADIUS;
		float sy  = MapSpawn::NPC_ANCHOR_Y + std::sin(ang) * SPAWN_RADIUS;
		g_obstacleMap.SnapToFree(sx, sy);   // 벽에 끼지 않도록 보정
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
			slots[userCnt++] = m_userSlots[i];

		for (auto& npc : m_npcs)
		{
			if (!npc.IsActive()) continue;
			if (!npc.IsAlive())
			{
				DespawnNpcLocked(npc);
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
			if (target && bestD2 <= 90.f * 90.f && now >= npc.m_attackReadyAt)
			{
				auto& state = m_playerStates.at(target->GetSlotIndex());
				if (!state.airborne)
				{
					npc.m_attackReadyAt = now + 1000;
					SC_AttackNotify attack{};
					attack.size = sizeof(attack); attack.id = static_cast<uint16_t>(PACKET_ID::SC_ATTACK_NOTIFY);
					attack.casterId = npc.GetId(); attack.casterKind = ENTITY_NPC;
					attack.targetId = target->GetId(); attack.targetKind = ENTITY_PLAYER; attack.yaw = npc.yaw;
					BroadcastGameplayLocked({reinterpret_cast<const uint8_t*>(&attack), sizeof(attack)});
					SC_DamageNotify damage{};
					damage.size = sizeof(damage); damage.id = static_cast<uint16_t>(PACKET_ID::SC_DAMAGE_NOTIFY);
					damage.targetId = target->GetId(); damage.targetKind = ENTITY_PLAYER;
					damage.causerId = npc.GetId(); damage.causerKind = ENTITY_NPC;
					damage.damage = std::min(state.hp, std::max(1, 10 - state.defense));
					state.hp -= damage.damage; damage.remainHP = state.hp; damage.maxHP = state.maxHP;
					BroadcastGameplayLocked({reinterpret_cast<const uint8_t*>(&damage), sizeof(damage)});
					SendStateLocked(*target, state, now);
				}
			}

			// 목표(추격=PC / 비추격=앵커) 방향으로 연속 이동
			float dx = tx - npc.x, dy = ty - npc.y;
			float dist = std::sqrt(dx * dx + dy * dy);
			float arrive = chasing ? STOP_DIST : 4.0f;
			if (dist > arrive)
			{
				float inv = 1.0f / dist;
				float ncx = npc.x + dx * inv * npc.m_speed;
				float ncy = npc.y + dy * inv * npc.m_speed;
				// 장애물 충돌: 막힌 칸이면 이동 정지 (길찾기 없음 — 벽에 막힘)
				if (g_obstacleMap.CanMoveTo(ncx, ncy)) { npc.x = ncx; npc.y = ncy; }
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
	if (!m_inGame || npcId >= m_npcs.size() || m_npcs[npcId].IsActive() || hp <= 0 ||
		!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(speed) || speed < 0.f)
		return false;

	g_obstacleMap.SnapToFree(x, y);
	m_npcs[npcId].Spawn(npcId, type, x, y, hp, speed);
	SC_NpcSpawn packet{};
	packet.size = SC_NPC_SPAWN_PACKET_SIZE;
	packet.id = static_cast<uint16_t>(PACKET_ID::SC_NPC_SPAWN);
	packet.npcId = npcId;
	packet.npcType = type;
	packet.x = x;
	packet.y = y;
	packet.hp = hp;
	packet.maxHp = hp;
	for (uint32_t i = 0; i < m_userCount; ++i)
		if (User* user = g_userPool.Get(m_userSlots[i]))
			user->Send(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)));
	return true;
}

bool Room::DespawnNpc(uint32_t npcId)
{
	std::lock_guard lock(m_mutex);
	if (!m_inGame || npcId >= m_npcs.size() || !m_npcs[npcId].IsActive())
		return false;
	DespawnNpcLocked(m_npcs[npcId]);
	return true;
}

void Room::DespawnNpcLocked(NPC& npc)
{
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
			m.hp = state->second.hp; m.maxHP = state->second.maxHP;
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

void Room::SendStateLocked(User& user, const GameplayRules::PlayerState& state, uint64_t now)
{
	SC_PlayerState packet{};
	packet.size = sizeof(packet); packet.id = static_cast<uint16_t>(PACKET_ID::SC_PLAYER_STATE);
	packet.playerId = user.GetId();
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
			user.x = request.x; user.y = request.y; user.yaw = request.yaw;
			state.z = request.z; state.ready = true;
		}
		SendStateLocked(user, state, GetTickCount64());
		SC_MoveNotify me{};
		me.size = sizeof(me); me.id = static_cast<uint16_t>(PACKET_ID::SC_MOVE_NOTIFY);
		me.userIndex = user.GetId(); me.x = user.x; me.y = user.y; me.yaw = user.yaw;
		me.z = state.z; me.velocityZ = state.velocityZ; me.airborne = state.airborne;
		me.hp = state.hp; me.maxHP = state.maxHP;
		BroadcastGameplayLocked({reinterpret_cast<const uint8_t*>(&me), sizeof(me)}, user.GetSlotIndex());
	}
	SendPlayerStatesToUser(user.GetSlotIndex());
	BroadcastNpcSpawns(user.GetSlotIndex());
}

void Room::MovePlayer(User& user, const CS_Move& request)
{
	if (!GameplayRules::ValidPose(request.x, request.y, request.z, request.yaw, request.velocityZ) || request.airborne > 1) return;
	std::lock_guard lock(m_mutex);
	auto found = m_playerStates.find(user.GetSlotIndex());
	if (!m_inGame || found == m_playerStates.end() || !found->second.ready || found->second.hp <= 0) return;
	auto& state = found->second;
	user.x = request.x; user.y = request.y; user.yaw = request.yaw;
	state.z = request.z; state.velocityZ = request.velocityZ; state.airborne = request.airborne != 0;
	SC_MoveNotify packet{};
	packet.size = sizeof(packet); packet.id = static_cast<uint16_t>(PACKET_ID::SC_MOVE_NOTIFY);
	packet.userIndex = user.GetId(); packet.inputFlag = request.inputFlag;
	packet.x = user.x; packet.y = user.y; packet.yaw = user.yaw;
	packet.z = state.z; packet.velocityZ = state.velocityZ; packet.airborne = state.airborne;
	packet.hp = state.hp; packet.maxHP = state.maxHP;
	BroadcastGameplayLocked({reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)}, user.GetSlotIndex());
}

void Room::Attack(User& user, const CS_Attack& request)
{
	std::lock_guard lock(m_mutex);
	auto found = m_playerStates.find(user.GetSlotIndex());
	if (!m_inGame || found == m_playerStates.end() || !found->second.ready) return;
	auto& state = found->second;
	const uint64_t now = GetTickCount64();
	ActionResult result = ActionResult::Invalid;
	if (std::isfinite(request.yaw) && (request.targetKind == ENTITY_NONE ||
		(request.targetKind == ENTITY_NPC && request.targetId < m_npcs.size() && m_npcs[request.targetId].IsAlive())))
		result = GameplayRules::UseSkill(state, request.skillId, now);
	SC_ActionResult response{};
	response.size = sizeof(response); response.id = static_cast<uint16_t>(PACKET_ID::SC_ACTION_RESULT);
	response.requestId = static_cast<uint16_t>(PACKET_ID::CS_ATTACK); response.actionId = request.skillId; response.result = result;
	user.Send({reinterpret_cast<const uint8_t*>(&response), sizeof(response)});
	if (result != ActionResult::Ok) { SendStateLocked(user, state, now); return; }

	SC_AttackNotify attack{};
	attack.size = sizeof(attack); attack.id = static_cast<uint16_t>(PACKET_ID::SC_ATTACK_NOTIFY);
	attack.casterId = user.GetId(); attack.casterKind = ENTITY_PLAYER; attack.skillId = request.skillId;
	attack.yaw = request.yaw; attack.targetKind = request.targetKind; attack.targetId = request.targetId;
	BroadcastGameplayLocked({reinterpret_cast<const uint8_t*>(&attack), sizeof(attack)});
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
		damage.damage = std::min(npc.m_hp, skill.damage + state.attack - 20);
		npc.m_hp -= damage.damage; damage.remainHP = npc.m_hp; damage.maxHP = npc.m_maxHp;
		BroadcastGameplayLocked({reinterpret_cast<const uint8_t*>(&damage), sizeof(damage)});
		if (!npc.IsAlive()) { GameplayRules::RewardKill(state); DespawnNpcLocked(npc); }
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

#include "pch.h"
#include "Room.h"
#include "User.h"
#include "MonsterScript.h"
#include "ObstacleMap.h"
#include "Globals.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition) throw std::runtime_error(message);
	}
}

struct RoomSpawnTests
{
	static void TestPlayerInfo()
	{
		User user;
		user.Login("NameplatePlayer");
		GameplayRules::PlayerState state;
		state.level = 7; state.lifeId = 3; state.gold = 987654;
		const SC_PlayerInfo info = Room::MakePlayerInfo(user, state);
		Check(info.size == SC_PLAYER_INFO_PACKET_SIZE && info.id == static_cast<uint16_t>(PACKET_ID::SC_PLAYER_INFO), "public identity header");
		Check(info.playerId == user.GetId() && info.lifeId == 3 && info.level == 7, "public identity and level");
		Check(std::string(info.userName) == "NameplatePlayer", "identity uses authenticated server name");
		state.gold = 1; state.exp = 500; state.items[0].count = 8;
		const SC_PlayerInfo changedPrivate = Room::MakePlayerInfo(user, state);
		Check(std::memcmp(&info, &changedPrivate, sizeof(info)) == 0, "private gold XP and items cannot leak into public packet");
		user.Login(std::string(MAX_USER_ID_LEN, 'W'));
		const SC_PlayerInfo longest = Room::MakePlayerInfo(user, state);
		Check(std::strlen(longest.userName) == MAX_USER_ID_LEN && longest.userName[MAX_USER_ID_LEN] == '\0', "maximum name is terminated");
		state.level = 8; state.lifeId = 4;
		const SC_PlayerInfo respawn = Room::MakePlayerInfo(user, state);
		Check(respawn.level == 8 && respawn.lifeId == 4, "public identity follows level-up and respawn");
		std::println("PASS: public player identity, level, private-state isolation, bounded name");
	}

	static void TestPlayerRespawn()
	{
		Check(g_obstacleMap.LoadConfigured("CollisionSettings.json"), "load active collision backend for respawn");
		Room room;
		room.m_inGame = true;
		room.m_userSlots[0] = 0; room.m_userCount = 1;
		User& user = *g_userPool.Get(0);
		user.x = MapSpawn::PLAYER_X; user.y = MapSpawn::PLAYER_Y;
		g_obstacleMap.SnapToFree(user.x, user.y, 42.f);
		auto& state = room.m_playerStates[0];
		state.ready = true; state.hp = 1; state.mp = 5;
		state.maxHP = 150; state.maxMP = 120;
		state.gold = 123; state.exp = 17; state.level = 4;
		state.skillReadyAt[2] = 9000; state.itemReadyAt = 8000;
		state.castReadyAt = 7000; state.landingReadyAt = 6500;
		state.z = 900; state.velocityZ = -300;
		room.m_pendingAttacks[0] = {};
		room.m_movementBudgets[0] = {1000, 125};
		auto& chat = room.m_npcConversations[0];
		chat.Begin("old-life-context", "hello", 1000);
		chat.relationship.affinity = 40; chat.quote = {{"unit_price", 30}};
		auto& npc = room.m_npcs[0];
		npc.Spawn(0, 0, user.x, user.y, 50, 0);
		npc.m_attackTarget = 0; npc.m_attackImpactAt = 1000;
		room.m_npcs[1].m_attackTarget = 0; room.m_npcs[1].m_attackImpactAt = 2000;
		room.ResolveNpcAttackLocked(npc, 1000);
		Check(state.hp == 0 && state.respawnReadyAt == 1000 + GameplayRules::PlayerRespawnDelayMs,
			"lethal NPC impact schedules server respawn");
		Check(room.m_pendingAttacks.empty() && room.m_movementBudgets.empty(), "death cancels attack and movement credit");
		Check(room.m_npcs[1].m_attackImpactAt == 0, "death cancels other NPC wind-ups targeting player");
		Check(chat.context.empty() && chat.quote.is_null() && chat.relationship.affinity == 40,
			"death invalidates AI capabilities but preserves relationship");
		Check(!state.airborne && state.velocityZ == 0, "death clears airborne motion");
		const auto deadline = state.respawnReadyAt;
		room.TickPlayerRespawnLocked(0, deadline - 1);
		Check(state.hp == 0 && state.lifeId == 0 && state.respawnReadyAt == deadline, "no early respawn or timer extension");
		Check(GameplayRules::UseSkill(state, 0, deadline - 1) == ActionResult::Dead, "dead player cannot attack");
		CS_UseItem item{}; item.expectedItemId = 1; item.inventoryRevision = state.inventoryRevision;
		Check(GameplayRules::UseItem(state, item, deadline - 1) == ActionResult::Dead, "dead player cannot use items");

		const auto validMap = g_obstacleMap;
		g_obstacleMap = ObstacleMap{};
		room.TickPlayerRespawnLocked(0, deadline);
		Check(state.hp == 0 && state.lifeId == 0, "invalid collision map delays respawn");
		g_obstacleMap = validMap;
		const float spawnX = MapSpawn::PLAYER_X;
		MapSpawn::PLAYER_X = 9000000.f;
		room.TickPlayerRespawnLocked(0, deadline);
		Check(state.hp == 0, "distant fallback cannot relocate respawn across the map");
		MapSpawn::PLAYER_X = spawnX;
		room.TickPlayerRespawnLocked(0, deadline);
		Check(state.hp == 150 && state.mp == 120 && state.lifeId == 1 && state.respawnReadyAt == 0, "deadline restores full HP/MP exactly once");
		Check(state.gold == 123 && state.exp == 17 && state.level == 4 && state.inventoryRevision == 1 &&
			state.items[0].count == 3 && state.items[1].count == 3, "respawn preserves progression and items");
		Check(state.skillReadyAt[2] == 9000 && state.itemReadyAt == 8000, "respawn cannot reset individual cooldowns");
		Check(state.castReadyAt == 0 && state.landingReadyAt == 0 && !state.airborne && state.velocityZ == 0,
			"respawn clears death, cast, landing and jump state");
		Check(std::hypot(user.x - MapSpawn::PLAYER_X, user.y - MapSpawn::PLAYER_Y) <= 150.f &&
			g_obstacleMap.CanTraverse(user.x, user.y, user.x, user.y, 42), "respawn at capsule-safe PlayerStart");
		Check(state.z == MapSpawn::PLAYER_GROUND_Z + GameplayRules::PlayerCapsuleHalfHeight + 2.f,
			"respawn height uses exported floor plus capsule clearance");
		room.TickPlayerRespawnLocked(0, deadline + 1);
		Check(state.lifeId == 1, "alive ticks do not repeat respawn");

		// Detached user fixture exercises handlers without a live IOCP session.
		Room actions;
		User actor; actor.x = user.x; actor.y = user.y;
		actions.m_inGame = true;
		auto& alive = actions.m_playerStates[actor.GetSlotIndex()]; alive = state;
		actions.m_movementBudgets[actor.GetSlotIndex()] = room.m_movementBudgets.at(0);
		CS_Move move{}; move.x = actor.x; move.y = actor.y; move.z = state.z;
		Check(!actions.ApplyPlayerMoveLocked(actor, move, deadline), "old-life move rejected after respawn");
		move.lifeId = alive.lifeId;
		Check(actions.ApplyPlayerMoveLocked(actor, move, deadline), "new-life move accepted after respawn");
		CS_Attack attack{}; attack.skillId = 1;
		actions.Attack(actor, attack);
		Check(actions.m_pendingAttacks.empty() && alive.mp == 120, "old-life attack cannot consume MP or create an impact");
		attack.lifeId = alive.lifeId;
		actions.Attack(actor, attack);
		Check(!actions.m_pendingAttacks.empty() && alive.mp == 100, "new-life attack works after respawn");
		alive.hp = 100;
		Check(GameplayRules::UseItem(alive, item, 10000) == ActionResult::Invalid, "old-life item request rejected");
		item.lifeId = alive.lifeId;
		Check(GameplayRules::UseItem(alive, item, 10000) == ActionResult::Ok, "new-life item request accepted");

		state.hp = 0;
		room.TickPlayerRespawnLocked(0, 10000);
		room.TickPlayerRespawnLocked(0, 10000 + GameplayRules::PlayerRespawnDelayMs);
		Check(state.lifeId == 2 && state.hp == state.maxHP, "second death has independent respawn cycle");
		state.hp = 0;
		room.TickPlayerRespawnLocked(0, 20000);
		room.Leave(0);
		room.TickPlayerRespawnLocked(0, 30000);
		Check(room.m_playerStates.empty(), "leaving cancels scheduled respawn");
		std::println("PASS: player death, respawn timing, resources, ground height, blocked spawn, old-life requests and leave");
	}

	static void TestNpcImpacts()
	{
		Room room;
		room.m_inGame = true;
		room.m_userSlots[0] = 0; room.m_userCount = 1;
		auto& state = room.m_playerStates[0];
		state.ready = true;
		User* user = g_userPool.Get(0);
		user->x = MapSpawn::NPC_ANCHOR_X; user->y = MapSpawn::NPC_ANCHOR_Y;
		auto& npc = room.m_npcs[0];
		npc.Spawn(0, 0, user->x, user->y, 50, 0);
		npc.m_attackTarget = 0; npc.m_attackImpactAt = 100;
		room.ResolveNpcAttackLocked(npc, 99);
		Check(state.hp == 100, "NPC wind-up does not cause early damage");
		room.ResolveNpcAttackLocked(npc, 100);
		Check(state.hp == 93, "NPC impact uses configured attack minus defense");
		room.ResolveNpcAttackLocked(npc, 101);
		Check(state.hp == 93, "NPC impact cannot repeat");
		npc.m_attackTarget = 0; npc.m_attackImpactAt = 200; state.airborne = true;
		room.ResolveNpcAttackLocked(npc, 200);
		Check(state.hp == 93, "jump dodges NPC impact");
		state.airborne = false; user->x += 1000;
		npc.m_attackTarget = 0; npc.m_attackImpactAt = 300;
		room.ResolveNpcAttackLocked(npc, 300);
		Check(state.hp == 93, "out-of-range target dodges NPC impact");
		user->x = npc.x;
		npc.m_attackTarget = 0; npc.m_attackImpactAt = 400; npc.m_hp = 0;
		room.ResolveNpcAttackLocked(npc, 400);
		Check(state.hp == 93, "dead NPC cannot finish attack");
		npc.m_attackTarget = 0; npc.m_attackImpactAt = 500;
		room.Leave(0);
		Check(npc.m_attackImpactAt == 0, "leave clears NPC pending impact");
		std::println("PASS: NPC wind-up, impact, damage, dodge, death and leave cancellation");
	}
	static void TestMovement()
	{
		const auto fixture = std::filesystem::temp_directory_path() /
			("TurtleIslandMovementTest-" + std::to_string(GetCurrentProcessId()) + ".dat");
		{ std::ofstream file(fixture); file << "5 5 100 0 0\n00000\n00100\n00100\n00100\n00000\n"; }
		Check(g_obstacleMap.Load(fixture.string().c_str()), "load movement fixture");
		Check(g_obstacleMap.CanTraverse(50, 50, 450, 50, 42), "clear corridor permits capsule");
		Check(!g_obstacleMap.CanTraverse(150, 250, 350, 250), "free endpoints cannot tunnel through wall");
		Check(!g_obstacleMap.CanTraverse(150, 150, 150, 450, 51), "radius blocks wall grazing");
		Check(!g_obstacleMap.CanTraverse(50, 50, 499, 50, 42), "capsule cannot cross map boundary");
		Check(!g_obstacleMap.CanTraverse(150, 50, 350, 150), "diagonal segment cannot cut wall corner");
		Room room;
		User user;
		room.m_inGame = true;
		user.x = 50; user.y = 50;
		auto& state = room.m_playerStates[user.GetSlotIndex()];
		CS_IngameReady ready{};
		ready.protocolVersion = GAMEPLAY_PROTOCOL_VERSION;
		ready.x = 450; ready.y = 450; ready.z = 96;
		room.PlayerReady(user, ready);
		Check(state.ready && user.x == 50 && user.y == 50, "ready cannot override server spawn XY");
		room.m_movementBudgets[user.GetSlotIndex()] = {1000, 125};
		CS_Move move{};
		move.x = 150; move.y = 50; move.z = 96;
		Check(room.ApplyPlayerMoveLocked(user, move, 1000), "normal move accepted");
		move.x = 200;
		Check(!room.ApplyPlayerMoveLocked(user, move, 1000) && user.x == 150, "packet spam cannot refill distance budget");
		Check(room.ApplyPlayerMoveLocked(user, move, 1100), "elapsed server time refills distance budget");
		move.x = 450;
		Check(!room.ApplyPlayerMoveLocked(user, move, 100000) && user.x == 200, "idle cannot accumulate teleport budget");
		move.x = 225; move.y = 150;
		Check(!room.ApplyPlayerMoveLocked(user, move, 100100) && user.y == 50, "wall crossing rejected");
		move.airborne = 1; move.z = 1000;
		Check(!room.ApplyPlayerMoveLocked(user, move, 100200), "airborne flag cannot bypass XY walls");
		move.x = std::numeric_limits<float>::quiet_NaN();
		Check(!room.ApplyPlayerMoveLocked(user, move, 100300), "non-finite move rejected");
		move.x = 200; move.y = 50; move.z = 196; move.velocityZ = 300;
		Check(room.ApplyPlayerMoveLocked(user, move, 100400) && state.airborne, "stationary jump remains supported");
		move.airborne = 0; move.z = 96; move.velocityZ = 0;
		Check(room.ApplyPlayerMoveLocked(user, move, 100600), "landing accepted");
		const uint64_t landingReadyAt = state.landingReadyAt;
		Check(landingReadyAt == 100600 + GameplayRules::LandingRecoveryMs, "server starts landing recovery");
		Check(room.ApplyPlayerMoveLocked(user, move, 100610) && state.landingReadyAt == landingReadyAt,
			"stationary ground packets do not extend recovery");
		move.x = 201;
		Check(!room.ApplyPlayerMoveLocked(user, move, landingReadyAt - 1) && user.x == 200, "landing blocks movement");
		move.x = 200; move.airborne = 1; move.velocityZ = 300;
		Check(!room.ApplyPlayerMoveLocked(user, move, landingReadyAt - 1), "landing blocks immediate re-jump");
		move.airborne = 0;
		Check(!room.ApplyPlayerMoveLocked(user, move, landingReadyAt - 1), "ground flag cannot bypass vertical launch lock");
		move.velocityZ = 0; move.x = 201;
		Check(room.ApplyPlayerMoveLocked(user, move, landingReadyAt), "movement resumes at recovery deadline");
		TestSkillMovement();
		{ std::ofstream file(fixture); file << "5 5 100 0 0\n00000\n"; }
		Check(!g_obstacleMap.Load(fixture.string().c_str()), "truncated grid rejected");
		Check(!g_obstacleMap.CanMoveTo(50, 50) && !g_obstacleMap.CanTraverse(50, 50, 60, 50), "invalid map fails closed");
		Check(!room.ApplyPlayerMoveLocked(user, move, 100500), "missing map prevents player movement");
		std::filesystem::remove(fixture);
		Check(g_obstacleMap.Load("map.dat"), "restore real map");
		for (int i = 0; i < 4; ++i)
		{
			float x = MapSpawn::PLAYER_X + (i % 2) * 120.f;
			float y = MapSpawn::PLAYER_Y + (i / 2) * 120.f;
			g_obstacleMap.SnapToFree(x, y, 42);
			Check(g_obstacleMap.CanTraverse(x, y, x, y, 42), "all player spawns have capsule clearance");
		}
		std::println("PASS: path collision, radius, boundary, server spawn, speed budget, jump, fail-closed map");
	}

	static void TestSkillMovement()
	{
		for (uint16_t id = 0; id < PLAYER_SKILL_COUNT; ++id)
		{
			Room room;
			User user;
			room.m_inGame = true;
			user.x = 50; user.y = 50;
			auto& state = room.m_playerStates[user.GetSlotIndex()];
			state.ready = true;
			room.m_movementBudgets[user.GetSlotIndex()] = {GetTickCount64(), 125};
			CS_Attack attack{};
			attack.skillId = id; attack.targetKind = ENTITY_NONE; attack.yaw = 90.f;
			room.Attack(user, attack);
			const uint64_t due = state.castReadyAt;
			const uint64_t start = due - GameplayRules::Skills[id].movementLockMs;
			Check(start <= GetTickCount64() && due > GetTickCount64(), "approved attack starts full cast recovery");
			CS_Move move{};
			move.x = 51; move.y = 50; move.z = 96;
			Check(!room.ApplyPlayerMoveLocked(user, move, start + 1) && user.x == 50, "every skill blocks XY from cast start");
			move.x = 50;
			Check(room.ApplyPlayerMoveLocked(user, move, start + 2) && user.yaw == 90.f,
				"stationary updates preserve cast facing");
			move.airborne = 1; move.velocityZ = 300;
			Check(!room.ApplyPlayerMoveLocked(user, move, start + 3), "every skill blocks a new jump");
			move.airborne = 0;
			Check(!room.ApplyPlayerMoveLocked(user, move, start + 4), "ground flag cannot bypass cast launch lock");
			const uint64_t impact = room.m_pendingAttacks.at(user.GetSlotIndex()).impactAt;
			Check(impact < due, "impact is earlier than full recovery");
			Resolve(room, user);
			move.velocityZ = 0; move.x = 51;
			Check(!room.ApplyPlayerMoveLocked(user, move, impact) && state.castReadyAt == due,
				"consuming the pending hit does not unlock movement");
			const auto mana = state.mp;
			attack.skillId = (id + 1) % PLAYER_SKILL_COUNT;
			room.Attack(user, attack);
			Check(state.mp == mana && state.castReadyAt == due && room.m_pendingAttacks.empty(),
				"different skill cannot interrupt recovery after impact or spend mana");
			Check(!room.ApplyPlayerMoveLocked(user, move, due - 1), "cast blocks movement until final millisecond");
			Check(room.ApplyPlayerMoveLocked(user, move, due) && user.x == 51, "movement resumes at cast deadline");
			Check(GameplayRules::UseSkill(state, attack.skillId, due) == ActionResult::Ok, "another skill can start after recovery");
		}

		Room room;
		User user;
		room.m_inGame = true;
		user.x = 50; user.y = 50;
		auto& state = room.m_playerStates[user.GetSlotIndex()];
		state.ready = true; state.airborne = true; state.z = 296; state.velocityZ = 200;
		room.m_movementBudgets[user.GetSlotIndex()] = {GetTickCount64(), 125};
		CS_Attack attack{};
		attack.skillId = 1; attack.targetKind = ENTITY_NONE;
		room.Attack(user, attack);
		const uint64_t due = state.castReadyAt;
		CS_Move move{};
		move.x = 50; move.y = 50; move.z = 300; move.velocityZ = 150; move.airborne = 1;
		Check(room.ApplyPlayerMoveLocked(user, move, due - 400), "airborne cast preserves existing upward momentum and gravity");
		move.velocityZ = 300;
		Check(!room.ApplyPlayerMoveLocked(user, move, due - 300), "airborne cast cannot launch another jump");
		move.z = 200; move.velocityZ = -100;
		Check(room.ApplyPlayerMoveLocked(user, move, due - 200), "cast accepts falling updates");
		move.z = 96; move.velocityZ = 0; move.airborne = 0;
		Check(room.ApplyPlayerMoveLocked(user, move, due - 100), "cast accepts landing");
		move.x = 51;
		Check(!room.ApplyPlayerMoveLocked(user, move, due), "cast expiry cannot bypass remaining landing recovery");
		Check(room.ApplyPlayerMoveLocked(user, move, state.landingReadyAt), "movement resumes after both recoveries end");
		std::println("PASS: all skill movement locks, jump prevention, facing, post-impact recovery, gravity and landing");
	}

	static void ResetSkillRecovery(GameplayRules::PlayerState& state)
	{
		state.skillReadyAt.fill(0);
		state.castReadyAt = 0;
	}

	static void Resolve(Room& room, User& user)
	{
		const auto pending = room.m_pendingAttacks.find(user.GetSlotIndex());
		Check(pending != room.m_pendingAttacks.end(), "accepted attack has pending impact");
		room.ResolvePendingAttackLocked(user, pending->second.impactAt);
		Check(!room.m_pendingAttacks.contains(user.GetSlotIndex()), "impact consumed once");
	}

	static void TestAttackImpacts()
	{
		Room room;
		room.m_isActive = true;
		room.m_inGame = true;
		User user;
		auto& state = room.m_playerStates[user.GetSlotIndex()];
		state.ready = true;
		state.attack = 37;
		room.SpawnNpc(0, 0, MapSpawn::NPC_ANCHOR_X, MapSpawn::NPC_ANCHOR_Y, 500, 0.f);
		auto& npc = room.m_npcs[0];
		user.x = npc.x - 100.f; user.y = npc.y;
		CS_Attack attack{};
		attack.targetKind = ENTITY_NPC; attack.targetId = 0;
		room.Attack(user, attack);
		Check(npc.m_hp == 500, "swing start does not damage");
		const auto due = room.m_pendingAttacks.at(user.GetSlotIndex()).impactAt;
		room.ResolvePendingAttackLocked(user, due - 1);
		Check(npc.m_hp == 500, "no damage before impact");
		Resolve(room, user);
		Check(npc.m_hp == 463, "basic hit damage equals attack stat");
		room.ResolvePendingAttackLocked(user, due + 1);
		Check(npc.m_hp == 463, "repeated impact tick cannot damage twice");

		ResetSkillRecovery(state);
		room.Attack(user, attack);
		npc.x += 1000.f;
		Resolve(room, user);
		Check(npc.m_hp == 463, "target moving out before impact is a miss");
		ResetSkillRecovery(state);
		room.Attack(user, attack);
		npc.x = user.x + 100.f;
		Resolve(room, user);
		Check(npc.m_hp == 426, "target entering range at impact can be hit");
		ResetSkillRecovery(state);
		attack.yaw = 180.f;
		room.Attack(user, attack);
		Resolve(room, user);
		Check(npc.m_hp == 426, "wrong-facing swing misses");

		ResetSkillRecovery(state);
		attack.yaw = 0.f; attack.skillId = 1;
		room.Attack(user, attack);
		const auto mana = state.mp;
		const auto firstImpact = room.m_pendingAttacks.at(user.GetSlotIndex()).impactAt;
		attack.skillId = 2;
		room.Attack(user, attack);
		Check(state.mp == mana && room.m_pendingAttacks.at(user.GetSlotIndex()).impactAt == firstImpact,
			"overlapping skill cannot spend mana or replace pending swing");
		Resolve(room, user);
		Check(npc.m_hp == 369, "Q retains skill bonus at impact");

		ResetSkillRecovery(state);
		attack.skillId = 0;
		room.Attack(user, attack);
		state.hp = 0;
		Resolve(room, user);
		Check(npc.m_hp == 369, "dead caster cannot finish wind-up");
		state.hp = 100;
		ResetSkillRecovery(state);
		room.Attack(user, attack);
		room.DespawnNpc(0);
		Check(room.m_pendingAttacks.empty(), "target despawn cancels targeted wind-up");
		room.SpawnNpc(0, 0, user.x + 100.f, user.y, 500, 0.f);
		room.ResolvePendingAttackLocked(user, firstImpact + 10000);
		Check(npc.m_hp == 500, "reused target ID cannot receive old hit");

		ResetSkillRecovery(state);
		room.Attack(user, attack);
		room.m_userSlots[0] = user.GetSlotIndex(); room.m_userCount = 1;
		room.Leave(user.GetSlotIndex());
		Check(room.m_pendingAttacks.empty(), "leave cancels pending attack");
		std::println("PASS: swing/impact timing, attack-stat damage, impact range/facing, single hit, cancellation");
	}

	static void Run()
	{
		LoadMonsterScript("Monster.lua");
		Check(g_monsterDefs.size() == 3 && g_monsterSpawnAreas.size() == 3, "default Lua config");
		const char8_t* expectedNames[] = {u8"\uc2a4\ud1a0\ucee4", u8"\ud0c0\ub974 \ud3ec\ub4dc", u8"\uc13c\ud2b8\ub9ac \uc544\uc774"};
		for (size_t type = 0; type < g_monsterDefs.size(); ++type)
			Check(g_monsterDefs[type].name == reinterpret_cast<const char*>(expectedNames[type]), "monster type uses its Korean game name");
		Check(g_obstacleMap.Load("map.dat"), "real obstacle map loaded");
		Room room;
		room.m_isActive = true;
		room.StartGame();
		// Expected slots follow the live Monster.lua areas in order.
		std::vector<uint16_t> expectedTypes;
		for (const auto& spawn : g_monsterSpawnAreas) expectedTypes.insert(expectedTypes.end(), spawn.count, spawn.type);
		Check(std::count_if(room.m_npcs.begin(), room.m_npcs.end(),
			[](const NPC& npc) { return npc.IsActive(); }) == static_cast<long long>(expectedTypes.size()), "initial area monsters match Lua counts");

		const auto& area = g_monsterSpawnAreas[0];
		float firstX = room.m_npcs[0].x, firstY = room.m_npcs[0].y;
		bool moved = false;
		for (int i = 0; i < 1000; ++i)
		{
			auto& npc = room.m_npcs[0];
			Check(npc.IsActive() && npc.m_hp == g_monsterDefs[0].hp && npc.m_maxHp == npc.m_hp, "restored HP");
			Check(npc.GetId() == 0 && npc.m_type == 0, "stable ID/type");
			const double dx = static_cast<double>(npc.x) - area.x, dy = static_cast<double>(npc.y) - area.y;
			Check(dx * dx + dy * dy <= static_cast<double>(area.radius) * area.radius, "position remains in area");
			Check(g_obstacleMap.CanMoveTo(npc.x, npc.y), "position avoids blocked cells");
			Check(npc.x == npc.m_anchorX && npc.y == npc.m_anchorY, "new return anchor");
			moved |= npc.x != firstX || npc.y != firstY;
			npc.m_hp = 0;
			room.TickNpcs();
			Check(!npc.IsActive(), "tick removes dead NPC");
			const uint64_t due = room.m_npcSpawnSlots[0].readyAt;
			room.SpawnAreaNpcsLocked(due - 1, true);
			Check(!npc.IsActive(), "no early respawn");
			room.SpawnAreaNpcsLocked(due, true);
		}
		Check(moved, "random locations vary");
		for (size_t slot = 1; slot < expectedTypes.size(); ++slot)
			Check(room.m_npcs[slot].m_type == expectedTypes[slot], "per-area count and type");
		Check(!room.SpawnNpc(0, 0, firstX, firstY, 10, 1.f), "occupied slot rejected");

		// Exercise the actual combat death path, without sockets or database access.
		// Reward values are pinned so balance edits in Monster.lua do not change this fixture.
		g_monsterDefs[0].gold = 10; g_monsterDefs[0].exp = 25;
		User user;
		user.x = room.m_npcs[0].x;
		user.y = room.m_npcs[0].y;
		auto& state = room.m_playerStates[user.GetSlotIndex()];
		state.ready = true;
		const auto gold = state.gold;
		const auto exp = state.exp;
		CS_Attack attack{};
		attack.targetKind = ENTITY_NPC; attack.targetId = 0;
		room.Attack(user, attack);
		Resolve(room, user);
		Check(room.m_npcs[0].IsAlive() && room.m_npcs[0].m_hp < room.m_npcs[0].m_maxHp, "nonlethal hit");
		Check(state.gold == gold && state.exp == exp, "damage alone grants no reward");
		ResetSkillRecovery(state);
		room.m_npcs[0].m_hp = 1;
		room.Attack(user, attack);
		Resolve(room, user);
		Check(!room.m_npcs[0].IsActive() && state.gold == gold + 10 && state.exp == exp + 25, "kill grants 10 gold and 25 XP");
		Check(room.m_npcSpawnSlots[0].readyAt >= GetTickCount64() + 9900, "death schedules 10-second respawn");
		ResetSkillRecovery(state);
		room.Attack(user, attack);
		room.TickNpcs();
		Check(state.gold == gold + 10 && state.exp == exp + 25, "duplicate attack and death tick grant no extra reward");
		for (int kills = 2; kills <= 4; ++kills)
		{
			room.SpawnAreaNpcsLocked(room.m_npcSpawnSlots[0].readyAt, true);
			Check(state.gold == gold + (kills - 1) * 10, "respawn itself grants no gold");
			user.x = room.m_npcs[0].x; user.y = room.m_npcs[0].y;
			room.m_npcs[0].m_hp = 1;
			ResetSkillRecovery(state);
			room.Attack(user, attack);
			Resolve(room, user);
			Check(state.gold == gold + kills * 10, "respawned monster grants gold on each kill");
			if (kills < 4) Check(state.exp == exp + kills * 25, "respawned monster grants XP on each kill");
		}
		Check(state.level == 2 && state.exp == 0 && state.expToNext == 150, "four kills level up once");
		Check(state.maxHP == 110 && state.maxMP == 105 && state.attack == 22, "level-up stats applied");
		std::println("PASS: kill gold/XP, no damage-only or duplicate reward, repeat rewards after respawn, level-up");
		Check(!room.SpawnNpc(0, 0, firstX, firstY, 10, 1.f), "pending area slot reserved");
		Check(room.DespawnNpc(0), "explicit despawn cancels pending respawn");
		room.SpawnAreaNpcsLocked(UINT64_MAX, true);
		Check(!room.m_npcs[0].IsActive(), "cancelled slot stays empty");
		Check(room.SpawnNpc(0, 0, firstX, firstY, 10, 1.f), "manual spawn after cancellation");
		room.m_npcs[0].m_hp = 0;
		room.TickNpcs();
		Check(room.m_npcSpawnSlots[0].areaIndex == -1, "manual spawn does not acquire area respawn");
		Check(state.gold == gold + 40 && state.exp == 0, "despawn and unattributed death grant no reward");

		// An entirely blocked/out-of-map area must retry, never snap outside its boundary.
		g_monsterSpawnAreas[0].x = 1000000000.f;
		g_monsterSpawnAreas[0].y = 1000000000.f;
		room.m_npcSpawnSlots[0] = {0, 0};
		room.SpawnAreaNpcsLocked(100, true);
		Check(!room.m_npcs[0].IsActive() && room.m_npcSpawnSlots[0].readyAt == 1100, "blocked area retries");
		room.SpawnAreaNpcsLocked(1099, true);
		Check(room.m_npcSpawnSlots[0].readyAt == 1100, "retry delay respected");
		g_obstacleMap = ObstacleMap{};
		room.SpawnAreaNpcsLocked(1100, true);
		Check(!room.m_npcs[0].IsActive() && room.m_npcSpawnSlots[0].readyAt == 2100, "missing map blocks area spawn");
		room.m_npcSpawnSlots[0] = {-1, 0};
		Check(!room.SpawnNpc(0, 0, firstX, firstY, 10, 1.f), "missing map also blocks manual spawn");

		room.m_userSlots[0] = 0;
		room.m_userCount = 1;
		Check(room.Leave(0), "last player leaves");
		for (size_t i = 0; i < room.m_npcs.size(); ++i)
			Check(!room.m_npcs[i].IsActive() && room.m_npcSpawnSlots[i].areaIndex == -1, "room teardown clears timers");
		LoadMonsterScript("Monster.lua");
		Check(g_obstacleMap.Load("map.dat"), "reload real map");
		room.Enter(1, 0, "test");
		room.StartGame();
		Check(room.m_npcs[0].IsActive(), "reused room spawns fresh monsters");
		room.Leave(0);
		Room secondRoom;
		secondRoom.m_isActive = true;
		secondRoom.StartGame();
		Check(secondRoom.m_npcs[0].IsActive() && !room.m_npcs[0].IsActive(), "rooms have independent spawn state");
		Check(secondRoom.DespawnNpc(0), "explicit removal of live area NPC");
		secondRoom.SpawnAreaNpcsLocked(GetTickCount64() + 20000, true);
		Check(!secondRoom.m_npcs[0].IsActive(), "live explicit removal cancels respawn");
		std::println("PASS: area bounds, obstacles, random positions, death, delay, HP, cancellation, room reuse");
	}
};

int main(int argc, char** argv)
{
	try
	{
		if (argc != 1)
		{
			Check(argc == 3 && std::string_view(argv[1]) == "--validate-script", "usage: --validate-script Monster.lua");
			Check(LoadMonsterScript(argv[2]), "candidate Lua must load without fallback or invalid SpawnAreas");
			std::println("PASS: candidate Lua, {} monster types, {} spawn areas", g_monsterDefs.size(), g_monsterSpawnAreas.size());
			return 0;
		}
		Check(MapSpawn::LoadPlayerStart("PlayerSpawn.json"), "exported PlayerStart loaded");
		Check(g_obstacleMap.Load("map.dat"), "exported map loaded");
		float safeX = MapSpawn::PLAYER_X, safeY = MapSpawn::PLAYER_Y;
		g_obstacleMap.SnapToFree(safeX, safeY, 42.f);
		Check(g_obstacleMap.CanTraverse(safeX, safeY, safeX, safeY, 42.f), "PlayerStart has nearby capsule clearance");
		Check(std::hypot(safeX - MapSpawn::PLAYER_X, safeY - MapSpawn::PLAYER_Y) <= 150.f,
			"PlayerStart correction stays within local spawn area");
		std::println("PASS: configured PlayerStart ({},{}) -> capsule-safe ({},{})",
			MapSpawn::PLAYER_X, MapSpawn::PLAYER_Y, safeX, safeY);
		const float spawnX = MapSpawn::PLAYER_X, spawnY = MapSpawn::PLAYER_Y;
		const auto invalidSpawn = std::filesystem::temp_directory_path() /
			("TurtleIslandPlayerStartTest-" + std::to_string(GetCurrentProcessId()) + ".json");
		{ std::ofstream file(invalidSpawn); file << R"({"x":10,"y":"invalid","yaw":0})"; }
		Check(!MapSpawn::LoadPlayerStart(invalidSpawn.string().c_str()), "malformed PlayerStart rejected");
		Check(MapSpawn::PLAYER_X == spawnX && MapSpawn::PLAYER_Y == spawnY, "invalid config cannot partially change spawn");
		std::filesystem::remove(invalidSpawn);
		Check(!MapSpawn::LoadPlayerStart(invalidSpawn.string().c_str()), "missing PlayerStart rejected");
		RoomSpawnTests::Run();
		RoomSpawnTests::TestAttackImpacts();
		RoomSpawnTests::TestMovement();
		RoomSpawnTests::TestNpcImpacts();
		RoomSpawnTests::TestPlayerRespawn();
		RoomSpawnTests::TestPlayerInfo();
		{
			const auto builtIn = GameplayRules::Skills;
			Check(LoadSkillScript("Skill.lua"), "Skill.lua loads");
			Check(GameplayRules::Skills[1].facingDot == -1.f && std::abs(GameplayRules::Skills[0].facingDot - 0.2f) < 0.01f &&
				GameplayRules::Skills[2].impactDelayMs == 400 && GameplayRules::Skills[0].movementLockMs == builtIn[0].movementLockMs,
				"Skill.lua angle/hit delay map to server rules; movement lock stays in code");
			GameplayRules::Skills = builtIn;
			std::println("PASS: Skill.lua player skill values");
		}
		const auto fixture = std::filesystem::temp_directory_path() /
			("TurtleIslandSpawnTest-" + std::to_string(GetCurrentProcessId()) + ".lua");
		const std::string monsters = "Monsters = {{name='Test',hp=50,speed=4}}\n";
		const std::array<const char*, 6> invalid = {
			"{type=0,count=101,x=0,y=0,radius=1,respawnSeconds=10}",
			"{type=1,count=1,x=0,y=0,radius=1,respawnSeconds=10}",
			"{type=0,count=1,x=0,y=0,radius=-1,respawnSeconds=10}",
			"{type=0,count=1,x=0/0,y=0,radius=1,respawnSeconds=10}",
			"{type=0,count=1,x=0,y=0,radius=1,respawnSeconds=0}",
			"{type=0,count=1.5,x=0,y=0,radius=1,respawnSeconds=10}"
		};
		for (const char* area : invalid)
		{
			{ std::ofstream file(fixture); file << monsters << "SpawnAreas = {" << area << "}"; }
			Check(!LoadMonsterScript(fixture.string().c_str()), "invalid Lua returns failure to the editor validator");
			Check(g_monsterSpawnAreas.empty(), "invalid config rejected");
		}
		const std::string area = "{type=0,count=" + std::to_string(MAX_ROOM_NPCS / 2) + ",x=-24750,y=-23150,radius=150,respawnSeconds=10}";
		{ std::ofstream file(fixture); file << monsters << "SpawnAreas = {" << area << "," << area << "}"; }
		Check(LoadMonsterScript(fixture.string().c_str()), "valid Lua returns success to the editor validator");
		Check(g_monsterSpawnAreas.size() == 2, "exactly MAX_ROOM_NPCS slots accepted");
		{ std::ofstream file(fixture); file << monsters << "SpawnAreas = {" << area << "," << area << "," << area << "}"; }
		LoadMonsterScript(fixture.string().c_str());
		Check(g_monsterSpawnAreas.empty(), "combined count above capacity rejected atomically");
		{ std::ofstream file(fixture); file << monsters << "SpawnAreas = {}"; }
		LoadMonsterScript(fixture.string().c_str());
		Check(g_monsterSpawnAreas.empty(), "explicit empty config disables spawning");
		{ std::ofstream file(fixture); file << monsters; }
		LoadMonsterScript(fixture.string().c_str());
		Check(g_monsterSpawnAreas.size() == 5, "legacy config has defaults");
		std::filesystem::remove(fixture);
		LoadMonsterScript(fixture.string().c_str());
		Check(g_monsterSpawnAreas.size() == 5 && g_monsterDefs.size() == 1, "missing Lua fallback");
		std::println("PASS: Lua validation, empty config, legacy config, missing Lua fallback");
		return 0;
	}
	catch (const std::exception& error)
	{
		std::println("FAIL: {}", error.what());
		return 1;
	}
}

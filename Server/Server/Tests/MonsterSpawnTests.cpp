#include "pch.h"
#include "Room.h"
#include "User.h"
#include "MonsterScript.h"
#include "ObstacleMap.h"
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
	static void Run()
	{
		LoadMonsterScript("Monster.lua");
		Check(g_monsterDefs.size() == 3 && g_monsterSpawnAreas.size() == 3, "default Lua config");
		Check(g_obstacleMap.Load("map.dat"), "real obstacle map loaded");
		Room room;
		room.m_isActive = true;
		room.StartGame();
		Check(std::count_if(room.m_npcs.begin(), room.m_npcs.end(),
			[](const NPC& npc) { return npc.IsActive(); }) == 5, "five initial area monsters");

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
		Check(room.m_npcs[1].m_type == 0 && room.m_npcs[2].m_type == 1 &&
			room.m_npcs[3].m_type == 1 && room.m_npcs[4].m_type == 2, "per-area count and type");
		Check(!room.SpawnNpc(0, 0, firstX, firstY, 10, 1.f), "occupied slot rejected");

		// Exercise the actual combat death path, without sockets or database access.
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
		Check(room.m_npcs[0].IsAlive() && room.m_npcs[0].m_hp < room.m_npcs[0].m_maxHp, "nonlethal hit");
		Check(state.gold == gold && state.exp == exp, "damage alone grants no reward");
		state.skillReadyAt.fill(0);
		room.m_npcs[0].m_hp = 1;
		room.Attack(user, attack);
		Check(!room.m_npcs[0].IsActive() && state.gold == gold + 10 && state.exp == exp + 25, "kill grants 10 gold and 25 XP");
		Check(room.m_npcSpawnSlots[0].readyAt >= GetTickCount64() + 9900, "death schedules 10-second respawn");
		state.skillReadyAt.fill(0);
		room.Attack(user, attack);
		room.TickNpcs();
		Check(state.gold == gold + 10 && state.exp == exp + 25, "duplicate attack and death tick grant no extra reward");
		for (int kills = 2; kills <= 4; ++kills)
		{
			room.SpawnAreaNpcsLocked(room.m_npcSpawnSlots[0].readyAt, true);
			Check(state.gold == gold + (kills - 1) * 10, "respawn itself grants no gold");
			user.x = room.m_npcs[0].x; user.y = room.m_npcs[0].y;
			room.m_npcs[0].m_hp = 1;
			state.skillReadyAt.fill(0);
			room.Attack(user, attack);
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
		g_monsterSpawnAreas[0].x = 0.f;
		g_monsterSpawnAreas[0].y = 0.f;
		room.m_npcSpawnSlots[0] = {0, 0};
		room.SpawnAreaNpcsLocked(100, true);
		Check(!room.m_npcs[0].IsActive() && room.m_npcSpawnSlots[0].readyAt == 1100, "blocked area retries");
		room.SpawnAreaNpcsLocked(1099, true);
		Check(room.m_npcSpawnSlots[0].readyAt == 1100, "retry delay respected");
		g_obstacleMap = ObstacleMap{};
		room.SpawnAreaNpcsLocked(1100, true);
		Check(!room.m_npcs[0].IsActive() && room.m_npcSpawnSlots[0].readyAt == 2100, "missing map blocks area spawn");

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

int main()
{
	try
	{
		RoomSpawnTests::Run();
		const auto fixture = std::filesystem::temp_directory_path() /
			("TurtleIslandSpawnTest-" + std::to_string(GetCurrentProcessId()) + ".lua");
		const std::string monsters = "Monsters = {{name='Test',hp=50,speed=4}}\n";
		const std::array<const char*, 6> invalid = {
			"{type=0,count=9,x=0,y=0,radius=1,respawnSeconds=10}",
			"{type=1,count=1,x=0,y=0,radius=1,respawnSeconds=10}",
			"{type=0,count=1,x=0,y=0,radius=-1,respawnSeconds=10}",
			"{type=0,count=1,x=0/0,y=0,radius=1,respawnSeconds=10}",
			"{type=0,count=1,x=0,y=0,radius=1,respawnSeconds=0}",
			"{type=0,count=1.5,x=0,y=0,radius=1,respawnSeconds=10}"
		};
		for (const char* area : invalid)
		{
			{ std::ofstream file(fixture); file << monsters << "SpawnAreas = {" << area << "}"; }
			LoadMonsterScript(fixture.string().c_str());
			Check(g_monsterSpawnAreas.empty(), "invalid config rejected");
		}
		const std::string area = "{type=0,count=4,x=-24750,y=-23150,radius=150,respawnSeconds=10}";
		{ std::ofstream file(fixture); file << monsters << "SpawnAreas = {" << area << "," << area << "}"; }
		LoadMonsterScript(fixture.string().c_str());
		Check(g_monsterSpawnAreas.size() == 2, "exactly eight slots accepted");
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

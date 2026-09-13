#include "pch.h"
#include "MonsterScript.h"
#include "include/lua.hpp"
#include "Define.h"
#include "ObstacleMap.h"
#include <cmath>

namespace
{
	void SetDefaultSpawnAreas()
	{
		g_monsterSpawnAreas.clear();
		for (uint32_t i = 0; i < 5; ++i)
			g_monsterSpawnAreas.push_back({static_cast<uint16_t>(i % g_monsterDefs.size()), 1,
				MapSpawn::NPC_ANCHOR_X, MapSpawn::NPC_ANCHOR_Y, 150.f, 10000});
	}

	bool ReadNumber(lua_State* L, const char* field, double& value)
	{
		lua_getfield(L, -1, field);
		const bool valid = lua_type(L, -1) == LUA_TNUMBER;
		value = valid ? lua_tonumber(L, -1) : 0.;
		lua_pop(L, 1);
		return valid && std::isfinite(value);
	}

	double ReadCombatValue(lua_State* L, const char* field, double fallback, double maximum)
	{
		lua_getfield(L, -1, field);
		const bool missing = lua_isnil(L, -1);
		lua_pop(L, 1);
		if (missing) return fallback;
		double value;
		if (ReadNumber(L, field, value) && value >= 1 && value <= maximum && std::floor(value) == value) return value;
		Log::Warning("[LUA] invalid monster {}; using {}", field, fallback);
		return fallback;
	}

	void LoadSpawnAreas(lua_State* L)
	{
		lua_getglobal(L, "SpawnAreas");
		if (lua_isnil(L, -1)) { lua_pop(L, 1); return; } // Legacy scripts keep the default area.
		g_monsterSpawnAreas.clear();
		uint32_t total = 0;
		bool valid = lua_istable(L, -1);
		const size_t count = valid ? lua_rawlen(L, -1) : 0;
		for (size_t i = 1; valid && i <= count; ++i)
		{
			lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
			double type, amount, x, y, radius, seconds;
			valid = lua_istable(L, -1) && ReadNumber(L, "type", type) &&
				ReadNumber(L, "count", amount) && ReadNumber(L, "x", x) && ReadNumber(L, "y", y) &&
				ReadNumber(L, "radius", radius) && ReadNumber(L, "respawnSeconds", seconds) &&
				type >= 0 && type < g_monsterDefs.size() && type <= UINT16_MAX && std::floor(type) == type &&
				amount >= 1 && amount <= MAX_ROOM_NPCS - total && std::floor(amount) == amount &&
				std::abs(x) <= 10000000 && std::abs(y) <= 10000000 && radius > 0 && radius <= 1000000 &&
				seconds >= 0.001 && seconds <= 86400;
			if (valid)
			{
				g_monsterSpawnAreas.push_back({static_cast<uint16_t>(type), static_cast<uint32_t>(amount),
					static_cast<float>(x), static_cast<float>(y), static_cast<float>(radius),
					static_cast<uint64_t>(std::ceil(seconds * 1000.))});
				total += static_cast<uint32_t>(amount);
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
		if (!valid)
		{
			g_monsterSpawnAreas.clear();
			Log::Warning("[LUA] invalid SpawnAreas (types, fields or total count > {}); area spawning disabled", MAX_ROOM_NPCS);
		}
	}
}

void LoadMonsterScript(const char* path)
{
	g_monsterDefs = {{"Fallback", 100, 6.f}};
	SetDefaultSpawnAreas();
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);

	if (luaL_dofile(L, path) != LUA_OK)
	{
		Log::Warning("[LUA] '{}' load fail: {}", path, lua_tostring(L, -1));
		lua_close(L);
		return;
	}

	lua_getglobal(L, "Monsters");
	if (lua_istable(L, -1))
	{
		g_monsterDefs.clear();
		int count = static_cast<int>(lua_rawlen(L, -1));
		for (int i = 1; i <= count; ++i)
		{
			lua_rawgeti(L, -1, i);   // Monsters[i]
			if (lua_istable(L, -1))
			{
				MonsterDef def;
				lua_getfield(L, -1, "name");  if (lua_isstring(L, -1)) def.name = lua_tostring(L, -1);        lua_pop(L, 1);
				lua_getfield(L, -1, "hp");    if (lua_isnumber(L, -1)) def.hp = static_cast<int>(lua_tointeger(L, -1)); lua_pop(L, 1);
				lua_getfield(L, -1, "speed"); if (lua_isnumber(L, -1)) def.speed = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
				if (def.hp <= 0) def.hp = 50;
				if (!std::isfinite(def.speed) || def.speed < 0.f) def.speed = 6.f;
				def.attackDamage = static_cast<int>(ReadCombatValue(L, "attackDamage", 10, 100000));
				def.attackRange = static_cast<float>(ReadCombatValue(L, "attackRange", 90, 2000));
				def.attackWindupMs = static_cast<uint32_t>(ReadCombatValue(L, "attackWindupMs", 350, 60000));
				def.attackCooldownMs = static_cast<uint32_t>(ReadCombatValue(L, "attackCooldownMs", 1000, 60000));
				def.attackCooldownMs = std::max(def.attackCooldownMs, def.attackWindupMs + 1);
				g_monsterDefs.push_back(def);
			}
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
	if (g_monsterDefs.empty()) g_monsterDefs = {{"Fallback", 100, 6.f}};
	SetDefaultSpawnAreas();
	LoadSpawnAreas(L);
	lua_close(L);

	Log::Success("[LUA] monsters loaded: {}", g_monsterDefs.size());
	Log::Info("[LUA] spawn areas loaded: {}", g_monsterSpawnAreas.size());
}

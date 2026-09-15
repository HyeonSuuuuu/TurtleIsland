#include "pch.h"
#include "MonsterScript.h"
#include "include/lua.hpp"
#include "Define.h"
#include "ObstacleMap.h"
#include "../../Common/GameplayRules.h"
#include <algorithm>
#include <cmath>
#include <type_traits>

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

	// 없으면 out(기본값) 유지, 범위 밖·정수형에 소수면 경고 후 유지.
	template<typename T>
	void ReadField(lua_State* L, const char* field, T& out, std::type_identity_t<T> minimum, std::type_identity_t<T> maximum)
	{
		lua_getfield(L, -1, field);
		const int type = lua_type(L, -1);
		const double value = type == LUA_TNUMBER ? lua_tonumber(L, -1) : 0.;
		lua_pop(L, 1);
		if (type == LUA_TNIL) return;
		if (type == LUA_TNUMBER && std::isfinite(value) && value >= minimum && value <= maximum &&
			(!std::is_integral_v<T> || std::floor(value) == value))
			out = static_cast<T>(value);
		else
			Log::Warning("[LUA] invalid {}; using {}", field, out);
	}

	bool LoadSpawnAreas(lua_State* L)
	{
		lua_getglobal(L, "SpawnAreas");
		if (lua_isnil(L, -1)) { lua_pop(L, 1); return true; } // Legacy scripts keep the default area.
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
		return valid;
	}
}

bool LoadMonsterScript(const char* path)
{
	g_monsterDefs = {{"Fallback", 100, 6.f}};
	SetDefaultSpawnAreas();
	lua_State* L = luaL_newstate();
	if (!L) return false;
	luaL_openlibs(L);

	if (luaL_dofile(L, path) != LUA_OK)
	{
		Log::Warning("[LUA] '{}' load fail: {}", path, lua_tostring(L, -1));
		lua_close(L);
		return false;
	}

	lua_getglobal(L, "Monsters");
	bool validDefinitions = lua_istable(L, -1) && lua_rawlen(L, -1) > 0;
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
				ReadField(L, "hp", def.hp, 1, 1000000);
				ReadField(L, "speed", def.speed, 0, 1000);
				ReadField(L, "attackDamage", def.attackDamage, 1, 100000);
				ReadField(L, "attackRange", def.attackRange, 1, 2000);
				ReadField(L, "attackWindupMs", def.attackWindupMs, 1, 60000);
				ReadField(L, "attackCooldownMs", def.attackCooldownMs, 1, 60000);
				ReadField(L, "gold", def.gold, 0, 1000000);
				ReadField(L, "exp", def.exp, 0, 1000000);
				for (auto [field, text] : {std::pair{"habitat", &def.habitat}, {"looks", &def.looks}, {"lore", &def.lore}})
				{
					lua_getfield(L, -1, field);
					if (lua_type(L, -1) == LUA_TSTRING)
					{
						*text = lua_tostring(L, -1);
						if (text->size() > MonsterLoreBytes)
						{
							Log::Warning("[LUA] Monsters[{}].{} is over {} bytes; ignored", i, field, MonsterLoreBytes);
							text->clear();
						}
					}
					lua_pop(L, 1);
				}
				def.attackCooldownMs = std::max(def.attackCooldownMs, def.attackWindupMs + 1);
				g_monsterDefs.push_back(def);
			}
			else validDefinitions = false;
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
	if (g_monsterDefs.empty())
	{
		g_monsterDefs = {{"Fallback", 100, 6.f}};
		validDefinitions = false;
	}
	SetDefaultSpawnAreas();
	const bool validAreas = LoadSpawnAreas(L);
	lua_close(L);

	Log::Success("[LUA] monsters loaded: {}", g_monsterDefs.size());
	Log::Info("[LUA] spawn areas loaded: {}", g_monsterSpawnAreas.size());
	return validDefinitions && validAreas;
}

bool LoadSkillScript(const char* path)
{
	lua_State* L = luaL_newstate();
	if (!L) return false;
	if (luaL_dofile(L, path) != LUA_OK)
	{
		Log::Warning("[LUA] '{}' load fail: {}", path, lua_tostring(L, -1));
		lua_close(L);
		return false;
	}
	lua_getglobal(L, "Skills");
	const bool valid = lua_istable(L, -1);
	const char* names[PLAYER_SKILL_COUNT] = {"Attack", "Q", "E"};
	for (int i = 0; valid && i < PLAYER_SKILL_COUNT; ++i)
	{
		lua_getfield(L, -1, names[i]);
		if (lua_istable(L, -1))
		{
			auto& skill = GameplayRules::Skills[i];
			// 360도 = 전방위. 판정은 전방 벡터와의 cos(각도/2) 비교다.
			double angle = std::acos(std::clamp(static_cast<double>(skill.facingDot), -1., 1.)) * 360. / 3.14159265358979;
			ReadField(L, "damage", skill.damage, 0, 100000);
			ReadField(L, "mana", skill.mana, 0, 100000);
			ReadField(L, "cooldownMs", skill.cooldownMs, 0, 600000);
			ReadField(L, "range", skill.range, 1, 10000);
			ReadField(L, "angle", angle, 1, 360);
			ReadField(L, "hitDelayMs", skill.impactDelayMs, 0, 5000);
			skill.facingDot = static_cast<float>(std::cos(angle * 3.14159265358979 / 360.));
		}
		lua_pop(L, 1);
	}
	lua_close(L);
	if (!valid) Log::Warning("[LUA] '{}' has no Skills table; using built-in skill values", path);
	else Log::Success("[LUA] skills loaded (Attack/Q/E damage {}/{}/{})",
		GameplayRules::Skills[0].damage, GameplayRules::Skills[1].damage, GameplayRules::Skills[2].damage);
	return valid;
}

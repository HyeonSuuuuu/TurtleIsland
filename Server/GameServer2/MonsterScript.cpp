#include "pch.h"
#include "MonsterScript.h"
#include "include/lua.hpp"

void LoadMonsterScript(const char* path)
{
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
				g_monsterDefs.push_back(def);
			}
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
	lua_close(L);

	Log::Success("[LUA] monsters loaded: {}", g_monsterDefs.size());
}

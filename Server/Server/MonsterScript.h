#pragma once
#include "pch.h"

// Monster.lua 로 적재되는 몬스터 정의. index = monster type.
struct MonsterDef
{
	std::string name;
	int   hp    = 50;
	float speed = 6.0f;   // units/tick
};

inline std::vector<MonsterDef> g_monsterDefs;

void LoadMonsterScript(const char* path);   // 서버 시작 시 1회 (Monster.lua -> g_monsterDefs)

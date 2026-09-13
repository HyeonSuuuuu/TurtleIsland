#pragma once
#include "pch.h"

// Monster.lua 로 적재되는 몬스터 정의. index = monster type.
struct MonsterDef
{
	std::string name;
	int   hp    = 50;
	float speed = 6.0f;   // units/tick
	int attackDamage = 10;
	float attackRange = 90.f;
	uint32_t attackWindupMs = 350;
	uint32_t attackCooldownMs = 1000;
};

inline std::vector<MonsterDef> g_monsterDefs;

struct MonsterSpawnArea
{
	uint16_t type = 0; // Zero-based index into Monsters.
	uint32_t count = 1;
	float x = 0.f, y = 0.f, radius = 150.f;
	uint64_t respawnMs = 10000;
};

inline std::vector<MonsterSpawnArea> g_monsterSpawnAreas;

// Returns false on load/validation failure; runtime fallback behavior is preserved.
bool LoadMonsterScript(const char* path);   // 서버 시작 시 1회 (Monster.lua -> g_monsterDefs)

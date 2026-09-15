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

	int gold = 10;
	int exp = 25;

	// What a local knows, for the shopkeeper AI (get_monster_info). UTF-8, each at most MonsterLoreBytes.
	std::string habitat, looks, lore;
};
inline constexpr size_t MonsterLoreBytes = 240; // keeps one monster's tool result inside an MCP message

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
bool LoadSkillScript(const char* path);     // Skill.lua -> GameplayRules::Skills (이동 잠금 시간 제외)

-- GameDev 몬스터 정의 (방 인스턴스 스폰). 배열 index = monster type
--   name : 표시 이름 / hp : 체력 / speed : 이동 속도(units/tick)
Monsters = {
  { name = "스토커", hp = 50,  speed = 4.0, attackDamage = 10, attackRange = 90, attackWindupMs = 300, attackCooldownMs = 1000, gold = 10, exp = 25 },
  { name = "타르 포드", hp = 80,  speed = 7.0, attackDamage = 15, attackRange = 110, attackWindupMs = 900, attackCooldownMs = 2400, gold = 10, exp = 25 },
  { name = "센트리 아이", hp = 150, speed = 5.0, attackDamage = 20, attackRange = 100, attackWindupMs = 350, attackCooldownMs = 1100, gold = 10, exp = 25 },
}

-- Unreal world XY, circular areas. type is ZERO-based (0 = Monsters[1]).
-- Total count across all areas must not exceed MAX_ROOM_NPCS (currently 8).
-- Death removes the monster immediately; respawn uses a new random position.
-- BEGIN UNREAL SPAWN AREAS
-- Generated from Lv3_Ingame MonsterSpawnRegion actors. World XY, centimeters.
SpawnAreas = {
    { type = 0, count = 2, x = -581, y = 3221, radius = 1000, respawnSeconds = 10 },
    { type = 1, count = 2, x = 7966, y = 5144, radius = 3000, respawnSeconds = 10 },
    { type = 2, count = 2, x = 7966, y = 5144, radius = 3000, respawnSeconds = 10 },
}
-- END UNREAL SPAWN AREAS

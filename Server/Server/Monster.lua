-- GameDev 몬스터 정의 (방 인스턴스 스폰). 배열 index = monster type
--   name : 표시 이름 / hp : 체력 / speed : 이동 속도(units/tick)
Monsters = {
  { name = "스토커", hp = 50,  speed = 4.0, attackDamage = 10, attackRange = 90, attackWindupMs = 300, attackCooldownMs = 1000, gold = 50, exp = 25 },
  { name = "타르 포드", hp = 80,  speed = 7.0, attackDamage = 15, attackRange = 110, attackWindupMs = 900, attackCooldownMs = 2400, gold = 100, exp = 50 },
  { name = "센트리 아이", hp = 150, speed = 5.0, attackDamage = 20, attackRange = 100, attackWindupMs = 350, attackCooldownMs = 1100, gold = 200, exp = 80 },
  { name = "몬스터9", hp = 250, speed = 5.0, attackDamage = 25, attackRange = 120, attackWindupMs = 600, attackCooldownMs = 1800, gold = 300, exp = 120 },
  { name = "웜", hp = 400, speed = 3.0, attackDamage = 35, attackRange = 150, attackWindupMs = 300, attackCooldownMs = 2000, gold = 500, exp = 200 },
}

-- Unreal world XY, circular areas. type is ZERO-based (0 = Monsters[1]).
-- Total count across all areas must not exceed MAX_ROOM_NPCS (currently 100).
-- Death removes the monster immediately; respawn uses a new random position.
-- BEGIN UNREAL SPAWN AREAS
-- Generated from Lv3_Ingame MonsterSpawnRegion actors. World XY, centimeters.
SpawnAreas = {
    { type = 0, count = 4, x = -581, y = 3221, radius = 1000, respawnSeconds = 10 },
    { type = 1, count = 4, x = 7966, y = 5144, radius = 3000, respawnSeconds = 10 },
    { type = 2, count = 4, x = 7966, y = 5144, radius = 3000, respawnSeconds = 10 },
}
-- END UNREAL SPAWN AREAS

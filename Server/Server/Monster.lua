-- GameDev 몬스터 정의 (방 인스턴스 스폰). 배열 index = monster type
--   name : 표시 이름 / hp : 체력 / speed : 이동 속도(units/tick)
Monsters = {
  { name = "Slime", hp = 50,  speed = 4.0 },
  { name = "Wolf",  hp = 80,  speed = 7.0 },
  { name = "Bear",  hp = 150, speed = 5.0 },
}

-- Unreal world XY, circular areas. type is ZERO-based (0 = Monsters[1]).
-- Total count across all areas must not exceed MAX_ROOM_NPCS (currently 8).
-- Death removes the monster immediately; respawn uses a new random position.
SpawnAreas = {
  { type = 0, count = 2, x = -24750, y = -23150, radius = 150, respawnSeconds = 10 },
  { type = 1, count = 2, x = -24750, y = -23150, radius = 150, respawnSeconds = 10 },
  { type = 2, count = 1, x = -24750, y = -23150, radius = 150, respawnSeconds = 10 },
}

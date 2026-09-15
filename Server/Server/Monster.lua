-- GameDev 몬스터 정의 (방 인스턴스 스폰). 배열 index = monster type
--   name : 표시 이름 / hp : 체력 / speed : 이동 속도(units/tick)
--   habitat, looks, lore : 상점 AI 가 주민으로서 아는 서식지·생김새·소문 (각 240바이트 이하, 한글 약 80자)
Monsters = {
  { name = "스토커", hp = 50,  speed = 4.0, attackDamage = 10, attackRange = 90, attackWindupMs = 300, attackCooldownMs = 1000, gold = 50, exp = 25,
      habitat = "마을 바로 옆 풀숲",
      looks = "사람만 한 앙상한 괴물. 긴 팔 끝에 갈고리 같은 발톱이 달렸고 몸을 웅크린 채 뚝뚝 끊기듯 걷는다.",
      lore = "혼자 다니는 사람을 몰래 뒤따르다 덮친다. 약한 편이라 초보 사냥꾼의 첫 상대. 발톱은 갈고리날 재료로 쓴다." },
  { name = "타르 포드", hp = 80,  speed = 7.0, attackDamage = 15, attackRange = 110, attackWindupMs = 900, attackCooldownMs = 2400, gold = 100, exp = 50,
      habitat = "마을에서 조금 떨어진 끈적한 늪지",
      looks = "검은 타르로 뒤덮인 알주머니 같은 몸에 다리와 촉수가 달렸다. 등에 알이 불룩하게 붙어 있다.",
      lore = "느려 보여도 생각보다 빠르게 달려와 걷어찬다. 둥지를 틀면 금방 불어나서 둥지 부수는 일이 끊이지 않는다." },
  { name = "센트리 아이", hp = 150, speed = 5.0, attackDamage = 20, attackRange = 100, attackWindupMs = 350, attackCooldownMs = 1100, gold = 200, exp = 80,
      habitat = "타르 포드 늪지, 둥지 근처",
      looks = "바퀴 같은 몸통 위에 커다란 눈알 하나가 달렸고 주위로 촉수가 흔들린다.",
      lore = "한자리를 지키며 다가오는 걸 노려보다 촉수로 후려친다. 늪지 둥지를 지키는 파수꾼이라는 소문이 있다." },
  { name = "크러셔", hp = 250, speed = 5.0, attackDamage = 25, attackRange = 120, attackWindupMs = 600, attackCooldownMs = 1800, gold = 300, exp = 120,
      habitat = "섬 깊숙한 곳",
      looks = "집채만 한 몸집에 단단한 등껍질을 두른 괴물. 머리에 번뜩이는 눈이 박혀 있다.",
      lore = "두 팔로 내리찍어 무엇이든 부순다. 껍질이 워낙 단단해서 카라페이스 피어서 같은 관통 무기가 잘 먹힌다." },
  { name = "데스웜", hp = 400, speed = 3.0, attackDamage = 35, attackRange = 150, attackWindupMs = 300, attackCooldownMs = 2000, gold = 500, exp = 200,
      habitat = "섬 깊숙한 곳, 크러셔가 사는 땅 밑",
      looks = "땅속을 파고 다니는 거대한 벌레. 입 안에 이빨이 빼곡하고 촉수가 꿈틀댄다.",
      lore = "땅속에 숨었다 튀어나와 문다. 섬에서 가장 위험한 놈. 이빨이 아주 단단해 방어구 재료로 비싸게 팔린다." },
}

-- Unreal world XY, circular areas. type is ZERO-based (0 = Monsters[1]).
-- Total count across all areas must not exceed MAX_ROOM_NPCS (currently 100).
-- Death removes the monster immediately; respawn uses a new random position.
-- BEGIN UNREAL SPAWN AREAS
-- Generated from Lv3_Ingame MonsterSpawnRegion actors. World XY, centimeters.
SpawnAreas = {
    { type = 0, count = 5, x = -581, y = 3221, radius = 1000, respawnSeconds = 10 },
    { type = 3, count = 5, x = 17300, y = 9077, radius = 5000, respawnSeconds = 10 },
    { type = 4, count = 5, x = 17300, y = 9077, radius = 5000, respawnSeconds = 10 },
    { type = 1, count = 5, x = 7966, y = 5144, radius = 3000, respawnSeconds = 10 },
    { type = 2, count = 4, x = 7966, y = 5144, radius = 3000, respawnSeconds = 10 },
}
-- END UNREAL SPAWN AREAS

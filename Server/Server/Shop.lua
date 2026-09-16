-- 상점 상품. 서버 재시작 시 반영. 잘못되면 상점이 꺼진다.
--   minPrice : 흥정으로 내려갈 수 있는 최저가 (호감도 100일 때)
--   avgPrice : 평소 판매가 (흥정 없이 살 때 가격)
--   maxPrice : 상인이 부를 수 있는 최고가
-- 정수, minPrice <= avgPrice <= maxPrice
ShopPotions = {
  HealthPotion = { minPrice = 20, avgPrice = 100, maxPrice = 120 }, -- 회복 포션 (HP +50)
  ManaPotion   = { minPrice = 20, avgPrice = 100, maxPrice = 120 }, -- 마나 포션 (MP +50)
}

-- 호감도 (상인에게 친절하게 말하면 AI 가 올려준다). 시연용으로 잘 오르게 설정.
--   start            : 방 입장 시 호감도 (0 ~ 100)
--   maxStep          : 한 번 대화로 오르내릴 수 있는 최대치
--   cooldownSeconds  : 한 번 바뀐 뒤 다시 바뀔 수 있을 때까지
--   maxGainPerWindow : windowSeconds 동안 오를 수 있는 총량
-- 없으면 기본값(25 / 2 / 60초 / 5분에 +10).
ShopAffinity = { start = 60, maxStep = 20, cooldownSeconds = 0, maxGainPerWindow = 100, windowSeconds = 300 }

-- 장비: 인벤토리 6칸에 들고 있으면 능력치가 오른다(한 칸에 1개, 같은 장비 중복 가능).
--   id        : 3 ~ 65535, 아이콘 T_UI_ShopItem_<id>_* 와 같은 번호 (상점 칸 아이콘으로 상품이 연결됨)
--   name      : 표시 이름 (UTF-8 39바이트 이하)
--   hp, attack, defense, moveSpeed : 올라가는 능력치 (음수 가능, 이동속도는 최소 100)
--   흥정 범위는 우선 평소가의 70% ~ 120%
-- 상품은 포션 포함 최대 12개.
ShopEquipment = {
  { id = 1002, name = "사냥꾼의 단도",      minPrice = 40,   avgPrice = 200,   maxPrice = 240,   hp = 100,  attack = 15,  defense = 0,   moveSpeed = 0 },
  { id = 1003, name = "스토커의 갈고리날",  minPrice = 170,  avgPrice = 850,   maxPrice = 1020,  hp = 100,  attack = 40,  defense = 0,   moveSpeed = 100 }, -- 이속 위주
  { id = 1004, name = "웜의 단단한 이빨",   minPrice = 500,  avgPrice = 2500,  maxPrice = 3000,  hp = 2000, attack = 35,  defense = 50,  moveSpeed = -50 }, -- 탱커 중반 주력
  { id = 1005, name = "정화의 은빛 말뚝",   minPrice = 500,  avgPrice = 2500,  maxPrice = 3000,  hp = 0,    attack = 55,  defense = 30,  moveSpeed = 50 },  -- 중반 주력 무기
  { id = 1006, name = "카라페이스 피어서",  minPrice = 800,  avgPrice = 4000,  maxPrice = 4800,  hp = 0,    attack = 75,  defense = 0,   moveSpeed = 0 },
  { id = 1007, name = "가디언의 수호",      minPrice = 1100, avgPrice = 5500,  maxPrice = 6600,  hp = 5000, attack = 50,  defense = 100, moveSpeed = 0 },   -- 탱커 종결
  { id = 1008, name = "네스트 브레이커",    minPrice = 2400, avgPrice = 12000, maxPrice = 14400, hp = 3000, attack = 110, defense = 30,  moveSpeed = 150 }, -- 최종 종결
}

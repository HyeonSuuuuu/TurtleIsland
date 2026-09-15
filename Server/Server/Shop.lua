-- 상점 포션 가격(G). 서버 재시작 시 반영. 잘못되면 상점이 꺼진다.
--   minPrice : 흥정으로 내려갈 수 있는 최저가 (호감도 100일 때)
--   avgPrice : 평소 판매가 (흥정 없이 살 때 가격)
--   maxPrice : 상인이 부를 수 있는 최고가
-- 1 ~ 100000 정수, minPrice <= avgPrice <= maxPrice
ShopPotions = {
  HealthPotion = { minPrice = 70, avgPrice = 100, maxPrice = 120 }, -- 회복 포션 (HP +50)
  ManaPotion   = { minPrice = 70, avgPrice = 100, maxPrice = 120 }, -- 마나 포션 (MP +50)
}

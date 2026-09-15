-- 플레이어 스킬 판정값 (서버 권위). 서버 재시작 시 반영. 빠진 값은 코드 기본값을 쓴다.
--   damage     : 기본 피해. 실제 피해 = damage + 공격력 - 20 (최소 1)
--   mana       : MP 소모
--   cooldownMs : 재사용 대기(ms)
--   range      : 사거리(cm, 캐릭터 중심 기준)
--   angle      : 앞쪽 판정 부채꼴 각도(도). 360 = 전방위
--   hitDelayMs : 시전 후 피해가 들어가는 시점(ms). 모션의 타격 프레임에 맞출 것
-- 모션 길이(이동 잠금)는 애니메이션과 맞물려 있어 Common/GameplayRules.h 에 있다.
Skills = {
  Attack = { damage = 20, mana = 0,  cooldownMs = 500,  range = 300, angle = 157, hitDelayMs = 300 }, -- 평타(좌클릭)
  Q      = { damage = 40, mana = 20, cooldownMs = 4000, range = 350, angle = 200, hitDelayMs = 500 },
  E      = { damage = 60, mana = 30, cooldownMs = 6000, range = 500, angle = 360, hitDelayMs = 400 },
}

# 작업 계획 — NPC + Lua 시스템 이식 (SwordRPG → GameDev)

> SwordRPG(iocp_game_server2)의 NPC/Lua 구조를 GameDev에 입힌다.
> GameDev는 **Room 인스턴스(최대 4인) · 연속좌표(float x, y) 이동** 모델이므로,
> SwordRPG의 **그리드 전용 코드(섹터 AOI / 그리드 A*)는 제외**하고 패턴만 가져와 재구성한다.

---

## 1. 전제 / 현황 (확인됨)
- **좌표 규약: 서버는 2D `float x, y` 만 사용** (NPC·플레이어 공통). `z`는 미사용 — 높이/3D는 **언리얼 클라가 처리**.
- GameObject: `float x, y, yaw` 로 정리(기존 `z` 제거 가능)
- 이동: 연속 — `CS_Move`(inputFlag + yaw), 서버가 speed로 `x, y` 위치 적분 후 방 broadcast
- 저장: `EBRPool<User> g_userPool`, `g_objects[MAX_CLIENTS + MAX_NPCS]`(NPC 슬롯 예약됨), `MAX_NPCS=200000`
- 콘텐츠: **Room 인스턴스** (`g_rooms[MAX_ROOMS]`, `Room::Broadcast`), 게임 시작 = `Room::StartGame()`
- 네트워크: `GameServer : IOCPNetwork`(가상 On*), 핸들러 맵 `User::s_handlers`
- **없는 것**: NPC 게임플레이 패킷(있는 건 `CS/SC_NPC_CHAT` AI챗뿐), 타이머/틱 인프라, Lua 라이브러리

### 선결 정리 항목
- 현재 플레이어 이동(`OnMove`/`SC_MoveNotify`)이 `x, z`를 쓰면 **`x, y`로 정렬하고 `z` 제거**. GameObject의 `z` 필드도 제거. 서버 전체를 2D(x,y)로 통일(구현 1순위).

---

## 2. 핵심 설계 결정 (Room 기반)
1. **NPC 소유 = Room** — 방마다 인스턴스이므로 NPC도 방이 보유(`Room`이 NPC 목록 소유). 전역 `g_objects` NPC 슬롯은 오픈월드용이라 이번엔 미사용(방-로컬 보관이 단순·안전).
2. **틱 = 전용 타이머 스레드 1개** — 활성(InGame) 방들을 주기(예: 150ms) 순회하며 NPC AI/이동 갱신 후 방 broadcast.
3. **이동 = 연속 스티어링 (float x, y)** — 그리드 A* 대신 목표 방향으로 `pos += dir * speed * dt`(추격/배회). 충돌은 단순 경계 클램프부터.
4. **가시성 = 방 broadcast** — 섹터/AOI 불필요(4인 방).
5. **Lua** — 몬스터 정의(데이터)는 부팅 시 1회 공유 로드, 보스 행동 스크립트가 필요하면 방별 `lua_State`(방=인스턴스).
6. **프로토콜 추가는 `Common/Protocol.h`(클라 공유)** — 서버부터 추가, 클라(TurtleIsland)는 후속.

---

## 3. SwordRPG → GameDev 재사용 매핑
| SwordRPG | 가져갈 것 | GameDev 적응 |
|---|---|---|
| `Npc.h`/`NpcAI` (FSM, 스폰) | FSM 골격(IDLE/WANDER/CHASE), 스폰 | 그리드 A* 제거 → 연속 스티어링(x,y), 방 단위 |
| `World.h` 섹터/near_ids | ✗ (제외) | 방 broadcast가 대체 |
| `TimerThread` | 틱 루프 아이디어 | 활성 방 순회 틱 스레드로 단순화 |
| `MonsterScript`(Monster.lua) | Lua 데이터 로딩 프레임워크 | 존/반경 대신 방 스폰 파라미터 |
| `Boss.cpp`(Lua 패턴) | C API 등록 + boss_tick | 방별 VM (선택) |
| `Pool.h`/`Ebr.h` | ✗ (GameDev EBRPool 이미 있음) | — |

---

## 코드 스타일 (camelCase — GameDev 컨벤션 준수)
> SwordRPG의 `snake_case`는 쓰지 않는다. 이식 시 GameDev 기존 스타일로 변환.
- 타입/enum/메서드: **PascalCase** (`NPC`, `NpcState`, `SpawnNpcs`, `IsVisible`)
- 멤버 변수: **`m_camelCase`** (`m_hp`, `m_target`, `m_anchorX`)
- 지역/매개변수: **camelCase** (`slotIndex`, `roomIdx`, `npcId`)
- 상수: **SCREAMING_SNAKE** (`MAX_ROOM_NPCS`)
- 패킷 구조체/ID: **`CS_/SC_` 접두** (`SC_NpcSpawn`, `PACKET_ID::SC_NPC_SPAWN`)
- 변환 예: `npc_step_to_cell→StepToCell`, `pick_zone_weighted→PickSpawnPos`, `is_visible→IsVisible`

---

## Phase 0 — 좌표 2D 통일 (z 제거, 선결)
**목표**: 서버 전체를 `float x, y` 2D로 통일하고 `z`를 제거.

**파일**: `GameObject.h`, `User.cpp`(`OnMove`), `Common/Protocol.h`(`CS_Move`/`SC_MoveNotify`)

- [ ] `GameObject`의 `z` 필드 제거(`x, y, yaw`만 유지)
- [ ] 플레이어 이동/통지 좌표 `x, z` → `x, y`로 정렬, `z` 제거
- [ ] `SC_MoveNotify` 등 좌표 필드 `x, y`로 통일(z 미포함)

**검증**: 빌드 통과 / 클라 이동 정상(언리얼 측 높이는 클라 처리)

---

## Phase 1 — NPC 기반 (방 단위 스폰)
**목표**: 게임 시작 시 방에 NPC가 생기고 클라가 스폰을 수신.

**파일**: 신규 `NPC.h`/`NPC.cpp`, `Room.h`/`Room.cpp` 수정, `Define.h`, `Common/Protocol.h`

- [ ] `NPC : GameObject` 신규 — `hp`, `state(IDLE/WANDER/CHASE)`, `target`, `anchorX/anchorY`, `monsterType`, `lastMove` + `Clear()`
- [ ] `Define.h`: `MAX_ROOM_NPCS`(예: 16)
- [ ] `Room`에 NPC 보관: `std::array<NPC, MAX_ROOM_NPCS>` + 활성 카운트(+ 보호 락)
- [ ] `Room::SpawnNpcs()` — `StartGame()` 시 호출, 방 안 `x, y` 좌표에 N마리 배치
- [ ] `Common/Protocol.h`: `SC_NpcSpawn`(npcId, type, **x, y**, yaw, hp) — 단건 또는 리스트
- [ ] `StartGame()`에서 스폰 후 `SC_NpcSpawn` 방 broadcast (+ 신규 입장자에게 현재 NPC 전송)

**검증**: 빌드 통과 / DummyClient로 방 시작 → 스폰 패킷 수신 로그 확인

---

## Phase 2 — NPC 틱 & 연속 이동
**목표**: NPC가 방 안에서 움직이고 클라가 반영.

**파일**: 신규 `NpcTick.h`/`.cpp`(틱 스레드), `Room` 수정, `GameMain.cpp`, `Common/Protocol.h`

- [ ] 틱 스레드: 활성(InGame) 방 순회 → 각 방 NPC AI 1틱(150ms)
- [ ] NPC AI: IDLE↔WANDER(앵커 반경 배회), 근처 PC 있으면 CHASE(최근접 추격) — 전부 `x, y` 연속좌표
- [ ] 이동: `pos += dir * speed * dt`, 방 경계 클램프
- [ ] `Common/Protocol.h`: `SC_NpcMove`(npcId, **x, y**, yaw) — 가능하면 방 NPC 일괄(가변 길이)
- [ ] 틱 결과 방 broadcast(이동분만)
- [ ] 동시성: 틱 스레드 NPC 갱신 vs 워커(전투) 접근 → 방 단위 락으로 보호
- [ ] `GameMain.cpp`: 틱 스레드 start/stop

**검증**: NPC가 방에서 배회/추격 / 다중 클라가 같은 위치로 보간 / 방 종료 시 틱 정지

---

## Phase 3 — Lua 몬스터 정의
**목표**: 재컴파일 없이 몬스터 스탯/스폰을 Lua로 조정.

**파일**: 신규 `MonsterScript.h`/`.cpp`, `Monster.lua`, `Server.vcxproj`(lua 연결)

- [ ] `Server.vcxproj`에 lua include/lib 추가 (SwordRPG `include/lua.hpp` + `lua55.lib` 참고)
- [ ] `MonsterScript`: `Monster.lua` 로드 → `MonsterDef`(name/hp/speed/aggro 등) 맵
- [ ] 방 스폰이 `MonsterDef` 기반(타입/스탯)으로 동작
- [ ] (선택) 보스: 방별 `lua_State` + `boss_tick` + C API(`npc_pos`/`spawn_minion`/`aoe` 등) — SwordRPG `Boss.cpp` 참고
- [ ] 데이터 파일 출력 폴더 복사(vcxproj `<None> CopyToOutputDirectory`)

**검증**: Monster.lua 수정 → 재빌드 없이(서버 재시작) 스폰 스탯 변경 확인

---

## Phase 4 — 전투 (설계 의존 · 보류 가능)
> GameDev(TurtleIsland) 게임플레이가 SwordRPG와 다를 수 있어 별도 설계 후 진행.
- [ ] 공격/피격/사망/리스폰 + 보상 정의 (게임 기획 확인 필요)
- [ ] `CS_Attack` / `SC_NpcHit` / `SC_NpcDespawn` 등 프로토콜
- [ ] SwordRPG `Combat.cpp` 패턴(원자적 데미지·막타 보상) 참고

---

## 관통 사항 / 주의
- **좌표**: 서버는 2D `float x, y`만(z 제거). 높이/3D는 언리얼 클라 담당. (Phase 0 선결)
- **프로토콜**: `Common/Protocol.h` 추가분은 **클라(TurtleIsland)도 핸들링 필요** — 서버 먼저, 클라 후속. PACKET_ID는 기존 대역(CS_ 26x / SC_ 36x) 빈 번호 배정.
- **인코딩**: 모든 `.cpp/.h` **UTF-8 BOM + LF** (Server/CLAUDE.md 규칙) — 수정 후 BOM 복구 필수.
- **단순성 우선**: 방 4인·바운드된 NPC라 과도한 최적화(섹터/락프리) 불필요 — 방 락 + 틱 스레드로 충분.
- **빌드 검증**: 각 STEP 후 `Server.sln` 빌드 통과 확인.

## 시작 순서
Phase 0(좌표 통일) → 1 → 2 → 3 (각 Phase 빌드 검증). 전투(Phase 4)는 기획 확정 후.

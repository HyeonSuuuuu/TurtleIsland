# 작업 계획 — EBR + Lock-Free Stack 도입

## 현재 상태
- 세션 재사용: `mutex` + `queue` + 2초 타임스탬프 딜레이 방식
- 클라이언트 저장: `std::array<User, MAX_CLIENTS> g_users` (값 배열)
- IOCP 완료 키: `Session*` 포인터
- NPC: 미구현

---

## STEP 1 — `InitClear()` → `Clear()` + `UserState` atomic화

**파일**: `GameObject.h`, `Session.h`, `User.h`

- [ ] `GameObject.h`: 순수 가상 `InitClear()` → `Clear()`
- [ ] `Session.h`: `InitClear()` → `Clear()` 구현
- [ ] `User.h`: `InitClear()` → `Clear()`, `m_userState` → `std::atomic<UserState>`, `m_stateLock` 제거

---

## STEP 2 — Lock-Free Free Stack

**파일**: 새 `FreeStack.h`

```cpp
std::atomic<int32_t> g_free_next[MAX_PLAYERS];
std::atomic<int32_t> g_free_top{ -1 };

void Push(int32_t index);   // CAS 루프
int32_t Pop();              // CAS 루프, -1 = 만석
void Init();                // 서버 시작 시 전체 push
```

- [ ] `FreeStack.h` 신규 생성

---

## STEP 3~5 — EBR (Epoch-Based Reclamation)

**파일**: 새 `EBR.h`

- 전역 epoch 카운터 (`std::atomic<uint64_t>`)
- 스레드별 로컬 epoch (`thread_local`)
- 스레드 등록/해제: `RegisterThread()`, `UnregisterThread()`
- 임계 구간: `Enter()`, `Exit()`
- 회수 큐: `Retire(uint32_t index)`, `TryReclaim()`
- reclaim 시: `g_idToIndex` 에서 erase → `Clear()` → `FreeStack::Push()`

- [ ] `EBR.h` 신규 생성

---

## STEP 6 — NPC 클래스 추가

**파일**: 새 `NPC.h`, `Define.h`

```cpp
class NPC : public GameObject {
    void Clear() override { /* AI 상태 초기화 */ }
};
```

- [ ] `NPC.h` 신규 생성
- [ ] `Define.h`: `IOOperation::NPCMOVE` 추가

---

## STEP 7 — 컨테이너 전환 (g_clients 단일 배열)

**파일**: `Define.h`, `Globals.h`, `IOCPNetwork.h`

```
인덱스 [0,          MAX_PLAYERS)            → User 슬롯
인덱스 [MAX_PLAYERS, MAX_PLAYERS+MAX_NPCS)  → NPC 슬롯
```

- [ ] `Define.h`: `MAX_PLAYERS`, `MAX_NPCS` 추가 (`MAX_CLIENTS` 대체)
- [ ] `Globals.h`:
  - `GameObject* g_clients[MAX_PLAYERS + MAX_NPCS]` 추가
  - `g_idToIndex` 맵 추가 (게임 ID → 슬롯 인덱스, shared_mutex 방식)
  - `InitClients()` 추가
- [ ] `IOCPNetwork.h`: `g_users` 배열 제거

**객체 생명주기**
```
[시작]   Init(i)             — 1회, 버퍼 할당 및 슬롯 인덱스 고정
[연결]   FreeStack::Pop()   → RegisterSocket() → state = Connected
[로그인] g_idToIndex.Insert(gameId, idx)
[해제]   Close()            → EBR::Retire(idx)
[EBR]    g_idToIndex 에서 erase → Clear() → state = None → FreeStack::Push(idx)
[재사용] Pop() 후 RegisterSocket() 바로 호출 (Init 재호출 불필요)
```

---

## STEP 8 — IOCP 키 = 슬롯 인덱스

**파일**: `Session.h`, `IOCPNetwork.h`

- [ ] `Session.h` — `RegisterIOCP`: `(ULONG_PTR)this` → `(ULONG_PTR)m_id`
- [ ] `IOCPNetwork.h` — `GetQueuedCompletionStatus` 수신 변수: `Session*` → `uint32_t idx`

---

## STEP 9 — WorkerThread 수정

**파일**: `IOCPNetwork.h`

- [ ] `EBR::RegisterThread()` / `UnregisterThread()` 호출
- [ ] GQCS 루프 내 `EBR::Enter()` / `EBR::Exit()` 감싸기
- [ ] ACCEPT: `FreeStack::Pop()` 으로 슬롯 획득, 만석 시 거부
- [ ] RECV/SEND: `g_clients[idx]` 직접 접근
- [ ] `NPCMOVE` 분기 추가
- [ ] 루프 말미 `EBR::TryReclaim()` 호출

---

## STEP 10 — Disconnect 수정

**파일**: `IOCPNetwork.h`

- [ ] `AcquireSession()`, `ReleaseSession()` 제거
- [ ] `m_reuseQueue`, `m_reuseLock`, `REUSE_WAIT_MS` 제거
- [ ] `Disconnect()`: `Close()` → `EBR::Retire(idx)` 로 교체

---

## STEP 11 — 초기화 순서 변경

**파일**: `GameMain.cpp`

```cpp
EBR::Init();       // epoch 초기화
InitClients();     // g_clients[] 생성 + FreeStack 초기화
// worker thread 생성 → 각 스레드에서 EBR::RegisterThread()
```

- [ ] `GameMain.cpp` 초기화 순서 반영

---

## 검증

- [ ] 각 STEP 후 컴파일 확인
- [ ] DummyClient로 다중 접속 → 접속/해제 반복하여 슬롯 재사용 확인
- [ ] worker thread 수 늘려 동시 접속/해제 집중 테스트
- [ ] `state == Retiring` 동안 패킷 처리가 차단되는지 확인
- [ ] `MAX_PLAYERS` 초과 접속 시 연결 거부 로그 확인

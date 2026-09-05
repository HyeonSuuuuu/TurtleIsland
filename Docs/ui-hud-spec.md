# TurtleIsland — HUD 디자인 스펙

피그마 목업(`겜공졸작` 파일 → `메인 HUD` 페이지)을 UMG로 옮기기 위한 실측 수치 모음.
목업 이미지는 `Docs/ui-mockups/` 참고.

---

## 1. 색상 — 선형 변환이 필수

`색상 가이드` 페이지 기준.

| 이름 | hex | 용도 |
|---|---|---|
| 메인 패널색 | `#0B2025` | 패널·바 프레임 내부 (알파 0.90~0.92) |
| 슬롯 배경 | `#13292F` | 슬롯·인벤 칸 (패널에서 한 단계 띄움) |
| 버튼색 | `#69595D` | 패널·슬롯 테두리 |
| Disabled 40% | `#574A4D` | 바 프레임 테두리 (채움이 튀도록 죽임) |
| 메인 글씨 | `#428984` | 레벨 배지 테두리, MP 게이지 — **큰 면적 전용** |
| 강조 글씨 | `#5AA9A2` | 채팅 강조·작은 수치 (아래 명암비 항목) |
| 보조 글씨 | `#DADADA` | 모든 본문·수치 |
| HP 게이지 | `#A8383C` | |
| XP 게이지 | `#B08A46` | |

### ⚠ `unreal.LinearColor(hex/255)` 는 틀립니다

Slate 틴트는 **선형 공간**에서 곱해집니다. hex를 255로 나눠 넣으면 의도보다 훨씬 밝게 렌더됩니다.

실측 (`#8040C0` 입력):

| 방식 | 렌더 결과 |
|---|---|
| sRGB → 선형 변환 | `#8635C6` ✅ |
| `hex/255` 원값 | `#C290E4` ❌ |

```python
def srgb(h, a=1.0):
    """'0B2025' 같은 hex를 Slate가 의도대로 표시하는 LinearColor로."""
    def lin(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return unreal.LinearColor(lin(int(h[0:2], 16)), lin(int(h[2:4], 16)), lin(int(h[4:6], 16)), a)
```

> `Content/Python/apply_carbon_theme.py` 의 `DS` 클래스는 전부 `hex/255` 로 되어 있어
> 주석의 hex보다 밝게 나오고 있음. 예: 라임골드 `#A8C22B` → 실제 `#D4E17B` 근처.

### ⚠ Border 는 색을 한 곳에만 넣는다

`UBorder` 는 `Background`(SlateBrush) 의 `TintColor` 와 `BrushColor` 를 **곱한다.**
둘 다 `#0B2025` 를 넣으면 제곱돼서 사실상 검정이 된다.

| 설정 | 렌더 |
|---|---|
| 브러시 TintColor 만 | `#385F66` |
| BrushColor 만 | `#385F66` (동일) |
| **둘 다** | **`#000B0B`** ← 검정 |

**브러시는 흰색으로 두고 `BrushColor` 하나로만 색을 준다.**

### ⚠ CapturePreview 의 색은 믿지 말 것

`CapturePreview` 가 뱉는 PNG 는 선형 → sRGB 인코딩을 **한 번 더** 적용한다.
입력 `0.0035` → 예측 `59`, 실측 `0x3B`; 입력 `0.0144` → 예측 `99`, 실측 `0x63` — 오차 1 이내로 재현된다.
`#000000` 만 정확한 이유는 0 이 어떤 감마에서도 0 이기 때문.

**색 확인은 UMG 디자이너나 PIE 에서 한다.** 프리뷰에 맞춰 보정하면 인게임이 틀어진다.
저장된 값의 검증은 `LinearColor` → sRGB hex 역산으로 한다(그건 정확하다).

### 명암비

`#428984` 를 `#0B2025` 위 11px 본문에 쓰면 명암비 **4.09** 로 WCAG AA(4.5) 미달.
작은 글씨의 강조는 `#5AA9A2`(6.1) 를 쓸 것. `#428984` 는 테두리·게이지 같은 큰 면적에만.

---

## 2. Kenney Fantasy UI Borders — 9-slice 마진 실측표

`Content/UI/Textures/Kenney/{Panel,Border}` — 알파로 모서리 장식 크기를 잰 값.
Brush `Margin` 은 프랙션이라 x1(48px)·x2(96px) 어느 쪽이든 동일하게 적용된다.

| 마진 | 해당 변형 |
|---|---|
| `0.0833` (8px) | **015** |
| `0.1667` (16px) | 004 |
| `0.2083` (20px) | 001, 009 |
| `0.2500` (24px) | 000, 003, 008, 010, 011, 012, **013**, 014, 028, 030 |
| `0.2917` (28px) | 002, 005, 006, **007**, 019, 022, 023, 031 |
| `0.3333` (32px) | 018, 021, 024, 027, 029 |
| `0.3750` (36px) | **016**, 017, 026 |
| `0.4583` (44px) | 020, 025 |

**HUD 에서 쓰는 것**
- `013` (0.25) — 메인 패널 / 채팅 / 미니맵. 장식 있는 테두리
- `015` (0.0833) — 슬롯 / 바 / 입력칸. 가장 단순

**주의**
- `021`, `022`, `026` 은 십자·비정형 → 가로로 긴 패널에 부적합
- `020`, `025` 는 코너 44px → 48px 이하 슬롯에서 코너끼리 붙음
- **`015` 는 「테두리선 4px + 간격 4px + 내부」의 이중선 구조.**
  높이 20px 이하에 쓰면 속이 빈 것처럼 보이므로, 게이지 *채움* 에는 실루엣 대신 단색 사각형을 쓸 것

`apply_carbon_theme.py` 의 `kenney_brush(margin=0.3)` 기본값은 대부분 틀림:
`004` 는 0.3 이 실제(0.1667)보다 커서 테두리 두께가 위젯 크기 따라 변하고,
`016` 은 0.3 이 실제(0.375)보다 작아서 코너 장식이 늘어난다.

---

## 3. 구조 — 테두리를 살리려면 2겹

Kenney 는 흰색 실루엣이라 색은 엔진에서 입힌다. `Panel` 은 채워진 형태, `Border` 는 테두리만(내부 투명).

UMG `Border` 위젯은 배경 브러시가 **하나뿐**이라, 어두운 내부 + 밝은 테두리를 동시에 내려면 2겹이 필요하다.

```
Overlay
├ Image  ← Panel-013,  tint 메인 패널색 (알파 0.92)   … 내부
└ Image  ← Border-013, tint 버튼색                    … 테두리
```

한 겹만 쓰면 장식이 실루엣으로만 보이고 선 색이 죽는다.

---

## 4. 메인 HUD 레이아웃 (피그마 최종안, 590×170 기준)

모서리 장식이 24px 를 차지하므로 **내용은 24px 안쪽**에 둘 것.

| 요소 | 위치 | 크기 |
|---|---|---|
| 레벨 배지 (원형 링) | 24, 30 | 76×76 |
| 스킬 3칸 (한 판 + 구분선) | 110, 12 | 174×58 · 칸 58 |
| 증강 3칸 | 296, 12 | 칸 30, **스텝 40 (글로우 여유)** |
| HP 바 | 110, 76 | 296×22 |
| MP 바 | 110, 102 | 296×22 |
| 스탯 (아이콘+숫자) | 24, 128 | 글리프 20px, 숫자 14px bold, 스텝 68 |
| 인벤 3×2 (한 판 + 구분선) | 420, 22 | 138×92 · 칸 46 |
| XP 바 (전체폭) | 24, 154 | 534×6 |

게이지 채움 인셋: 프레임 `015` 기준 좌우 8px, 상하 4px.

**증강 글로우** — UMG Image 에는 글로우가 없다. 슬롯 뒤에 `T_UI_Glow_Soft`(흰색, 등급색 Tint)
를 깔고 Opacity 를 애니메이션하는 방식. 스텝을 40px 로 벌린 이유가 이것.

---

## 4b. 디자이너에서 넣을 크기 — 위젯 이름 ↔ 값

구조와 색은 스크립트가 넣는다. **크기·정렬만 디자이너에서 잡는다.**
(`set_desired_size_override` / `SizeBox` 조합으로 원격 지정이 계속 실패해서 수동으로 넘김.)

| 위젯 | 넣을 값 |
|---|---|
| `StatusPanel` | 650×188, 하단중앙 앵커, 하단 여백 20 |
| `HudRoot` (슬롯) | Padding 26 / 16 |
| `LevelSize` | **SizeBox** W 80 · H 80 |
| `SkillLMB/Q/E` 의 `*Bg` | 58×58 · 슬롯 간격 6 |
| `Skill*Icon` | 44×44, 가운데, 아래 여백 8 (키 자리) |
| `Aug*Frame` | 30×30 |
| `Aug*Glow` | 46×46 (프레임보다 크게 — 번져야 함) |
| `Aug*Icon` | 22×22 |
| `AugSilver/Gold/Prism` 슬롯 | **간격 10** (글로우가 이웃을 덮지 않게) |
| `HPSize` / `MPSize` | **SizeBox** H 22 |
| `XPSize` | **SizeBox** H 6 |
| `Inv*Bg` | 46×46 |
| `Inv*Icon` | 32×32 |
| `InventoryGrid` | SlotPadding 2, 3열 2행 |
| `Stat*Icon` | 20×20 |

정렬 규칙: 바·배경은 `Fill/Fill`, 아이콘은 `Center/Center`,
키·수량 텍스트는 `Right/Bottom`.

---

## 5. 현재 `WBP_MainHUD` 위젯 이름

```
ChatPanel     [Border]  └ ChatBox [VerticalBox]
                           ├ ChatLogScrollBox [ScrollBox] └ MessageContainer [VerticalBox]
                           └ ChatInput [EditableText]
CombatPanel   [Border]  └ CombatLog [ScrollBox] └ CombatLog_Box └ MessageContainer_1
StatusPanel   [Border]  └ HPProgressBar [ProgressBar] / LvText [TextBlock]
```

HP 는 `GetHPRatio` → `SetPercent` 로 Tick 에서 갱신 (`TurtleCharacter`).
**MP·XP·스탯·스킬 슬롯·인벤토리·증강은 아직 없음.**

---

## 6. 폰트 — FontFace 가 아니라 Font 를 넣어야 한다

`.ttf` 를 임포트하면 애셋이 **두 개** 생긴다.

| 애셋 | 클래스 | 용도 |
|---|---|---|
| `IBMPlexSans-Bold` | `FontFace` | 원본 글리프 데이터 |
| `IBMPlexSans-Bold_Font` | `Font` | **Slate 가 요구하는 것** |

`SlateFontInfo.font_object` 에 `FontFace` 를 넣으면 글자가
`A Basic LATIN 007F 0000` 같은 쓰레기로 렌더된다.

경로도 헷갈린다 — **IBM Plex 의 `Font` 는 `Fonts/UI/` 가 아니라 `Fonts/` 바로 아래에 있다.**

```
/Game/Fonts/IBMPlexSans-Bold_Font              ← Font   (O)
/Game/Fonts/UI/IBMPlexSans-Bold                ← FontFace (X)
/Game/Fonts/Korean/NotoSansKR-Regular_Font     ← Font   (O)
```

`apply_carbon_theme.font_asset(name, subdir)` 이 후보 경로를 훑고 클래스까지 검사한다.

**용도 구분** — IBM Plex 에는 한글 글리프가 없다. 숫자·영문 전용으로만 쓰고,
한글이 섞일 수 있는 곳은 전부 `NotoSansKR-*_Font` 를 쓸 것.

---

## 7. VibeUE MCP 주의사항

- `WidgetService.GetHierarchy` / `WidgetBlueprintExists` 는 **정상 경로에도 빈 값/false 반환**(플러그인 버그).
  위젯 이름은 `.uasset` 문자열 테이블에서 뽑고 `unreal.find_object(WidgetTree, name)` 으로 검증할 것.
- `unreal.WidgetTree` 는 파이썬에 메서드·프로퍼티를 **하나도 노출하지 않음**. `root_widget` 접근 불가.
- `EditorAssetLibrary.load_asset` 은 None 을 반환할 때가 있음. `unreal.load_asset("<path>.<name>")` 사용.
- **`CapturePreview` 는 base64 가 아니라 파일 경로만 반환**한다(페이로드 0KB). 스크린샷은 이걸로만 쓸 것.
  뷰포트 캡처 계열은 base64 를 JSON 문자열로 돌려줘서 1MB = 약 33만 토큰이 된다.
- ⚠ **`CapturePreview` 는 위젯에 따라 에디터를 하드 크래시시킨다.**
  `WBP_MainHUD` 는 4회 연속 정상, `WBP_Login` 에 걸자마자 사망:
  ```
  UnrealEditor-VibeUE.dll!UWidgetService::CapturePreview()
    Plugins/VibeUE/Source/VibeUE/Private/PythonAPI/UWidgetService.cpp:2608
  ```
  플레이어 컨텍스트가 필요한 위젯을 단독 생성하려다 터지는 것으로 보인다.
  **처음 보는 위젯에는 쓰지 말 것.** 검증은 속성 값을 읽어 되돌려 계산하는 쪽이 안전하다
  (`LinearColor` → sRGB hex 역산). 플러그인 소스가 프로젝트 안에 있으니
  2608 줄에 null 가드를 넣으면 크래시 대신 에러 반환으로 바꿀 수 있다.
- `describe_toolset` 은 툴셋당 40~70KB. 세션당 1회만.
- ⚠ **`AddComponent` 는 이미 있는 이름을 막지 않는다.** 실패 대신 부모에 한 번 더 등록해서
  하위 트리가 통째로 중복된다. 생성 전에 반드시 `WidgetExists` 로 확인할 것
  (`Tools/mcp/build_*.py` 가 그렇게 되어 있다).
  중복을 `remove_child_at` 으로 지우면 자식 자리에 None 이 남아 위젯이 깨진다 — 실제로 한 번 깨뜨렸다.
  복구는 `git checkout` 후 `unreal.EditorLoadingAndSavingUtils.reload_packages()`.
- ⚠ **`ReparentWidget` 을 같은 부모로 호출하면 no-op** 이다. `True` 를 반환하지만 순서는 안 바뀐다.
  **자식 순서는 원격으로 못 바꾼다 — 디자이너에서 드래그해야 한다.**
- ⚠ `SlateBrush.ImageSize` 는 `DeprecateSlateVector2D` 라 파이썬에서 못 만들고 못 고친다.
  `Image.set_desired_size_override()` 도 조용히 무시된다.
  **크기는 `SetBrush` 도구에 `"(X=58.000000,Y=58.000000)"` 문자열로 넘기는 것만 통한다**
  (`Tools/mcp/set_brush_sizes.py`).

호스트에서 에디터를 직접 조종하려면 `Tools/mcp/` 참고.

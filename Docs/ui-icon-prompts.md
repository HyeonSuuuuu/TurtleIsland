# TurtleIsland — UI 아이콘 생성 프롬프트 모음

이미지 생성 AI(GPT 등)에 **블록 하나를 통째로 복사해서** 붙여넣으면 됩니다.

## 뽑는 순서

1. `T_Skill_Attack` 을 **먼저** 뽑아 배경 슬레이트 색과 선 두께를 확정한다.
2. 나머지는 **1번 이미지를 첨부한 채로** 요청한다. 각 블록 맨 위 `[세트 유지]` 자리에 아래 한 줄을 넣는다.

```
Same art style, same dark slate background color, same stroke weight as the attached icon. Keep it a matching set.
```

3. 1번을 뽑을 때는 `[세트 유지]` 줄을 지우고 요청한다.

## UE 임포트 설정 (필수)

- Compression: `UserInterface2D`
- sRGB: **켜기**
- Mip Gen Settings: `NoMipmaps`

프레임/테두리는 생성하지 말 것 — 기존 Kenney `Border_x2` 패널을 9-slice로 얹는 편이 깔끔하다.

---

# 1. 스킬 아이콘

## T_Skill_Attack — 기본공격 (휘두르기)

```
A single minimal flat game icon for a fantasy action RPG HUD.

Subject: one thick off-white crescent stroke sweeping downward from
upper-left to lower-right, with a simple flat sword silhouette behind
it. Off-white only, no accent color.

Style: flat vector pictogram. Bold simple shapes, thick confident
strokes, high contrast. Exactly 3 colors total and no more: a dark
slate background, one off-white shape, one accent color. NO gradients,
NO shading, NO texture, NO glow, NO painted detail. Must read
instantly as a silhouette at 32x32 pixels.

Composition: perfectly square 1:1, one centered symbol filling about
70% of the frame, even margin all around, flat solid dark background.
No frame, no border, no vignette.

Strict rules: NO text, NO letters, NO numbers, NO watermark, NO UI
frame, NO character, NO realistic rendering, NO background scenery.
Flat straight-on view.

Output: 1024x1024, PNG.
```

## T_Skill_Q — 가로베기

```
[세트 유지]

A single minimal flat game icon for a fantasy action RPG HUD.

Subject: one long thin amber crescent stroke spanning the full width
of the frame, perfectly level and horizontal, with a small flat sword
silhouette at its left end.

Style: flat vector pictogram. Bold simple shapes, thick confident
strokes, high contrast. Exactly 3 colors total and no more: a dark
slate background, one off-white shape, one accent color. NO gradients,
NO shading, NO texture, NO glow, NO painted detail. Must read
instantly as a silhouette at 32x32 pixels.

Composition: perfectly square 1:1, one centered symbol filling about
70% of the frame, even margin all around, flat solid dark background.
No frame, no border, no vignette.

Strict rules: NO text, NO letters, NO numbers, NO watermark, NO UI
frame, NO character, NO realistic rendering, NO background scenery.
Flat straight-on view.

Output: 1024x1024, PNG.
```

## T_Skill_E — 회전 돌진

```
[세트 유지]

A single minimal flat game icon for a fantasy action RPG HUD.

Subject: two or three teal curved strokes coiling inward like a spiral
vortex, a small flat sword silhouette at the center, suggesting a
spinning charge.

Style: flat vector pictogram. Bold simple shapes, thick confident
strokes, high contrast. Exactly 3 colors total and no more: a dark
slate background, one off-white shape, one accent color. NO gradients,
NO shading, NO texture, NO glow, NO painted detail. Must read
instantly as a silhouette at 32x32 pixels.

Composition: perfectly square 1:1, one centered symbol filling about
70% of the frame, even margin all around, flat solid dark background.
No frame, no border, no vignette.

Strict rules: NO text, NO letters, NO numbers, NO watermark, NO UI
frame, NO character, NO realistic rendering, NO background scenery.
Flat straight-on view.

Output: 1024x1024, PNG.
```

---

# 2. 인벤토리 아이템

## T_Item_Longsword — 공격력 小

```
[세트 유지]

A single minimal flat game icon for a fantasy action RPG HUD.

Subject: a simple flat longsword pointing straight up, blade
off-white, crossguard and pommel in warm amber accent.

Style: flat vector pictogram. Bold simple shapes, thick confident
strokes, high contrast. Exactly 3 colors total and no more: a dark
slate background, one off-white shape, one accent color. NO gradients,
NO shading, NO texture, NO glow, NO painted detail. Must read
instantly as a silhouette at 32x32 pixels.

Composition: perfectly square 1:1, one centered symbol filling about
70% of the frame, even margin all around, flat solid dark background.
No frame, no border, no vignette.

Strict rules: NO text, NO letters, NO numbers, NO watermark, NO UI
frame, NO character, NO realistic rendering, NO background scenery.
Flat straight-on view.

Output: 1024x1024, PNG.
```

## T_Item_Greatsword — 공격력 大

```
[세트 유지]

A single minimal flat game icon for a fantasy action RPG HUD.

Subject: a broad two-handed greatsword pointing straight up, thick
heavy blade in off-white, wrapped hilt in warm amber accent, visibly
bulkier than a normal sword.

Style: flat vector pictogram. Bold simple shapes, thick confident
strokes, high contrast. Exactly 3 colors total and no more: a dark
slate background, one off-white shape, one accent color. NO gradients,
NO shading, NO texture, NO glow, NO painted detail. Must read
instantly as a silhouette at 32x32 pixels.

Composition: perfectly square 1:1, one centered symbol filling about
70% of the frame, even margin all around, flat solid dark background.
No frame, no border, no vignette.

Strict rules: NO text, NO letters, NO numbers, NO watermark, NO UI
frame, NO character, NO realistic rendering, NO background scenery.
Flat straight-on view.

Output: 1024x1024, PNG.
```

## T_Item_Armor — 방어력

```
[세트 유지]

A single minimal flat game icon for a fantasy action RPG HUD.

Subject: a flat chest armor breastplate seen from the front,
symmetrical, off-white plate with steel-blue accent trim along the
edges.

Style: flat vector pictogram. Bold simple shapes, thick confident
strokes, high contrast. Exactly 3 colors total and no more: a dark
slate background, one off-white shape, one accent color. NO gradients,
NO shading, NO texture, NO glow, NO painted detail. Must read
instantly as a silhouette at 32x32 pixels.

Composition: perfectly square 1:1, one centered symbol filling about
70% of the frame, even margin all around, flat solid dark background.
No frame, no border, no vignette.

Strict rules: NO text, NO letters, NO numbers, NO watermark, NO UI
frame, NO character, NO realistic rendering, NO background scenery.
Flat straight-on view.

Output: 1024x1024, PNG.
```

## T_Item_Boots — 이동속도

```
[세트 유지]

A single minimal flat game icon for a fantasy action RPG HUD.

Subject: a single flat side-view boot, off-white leather with a small
green accent wing on the ankle.

Style: flat vector pictogram. Bold simple shapes, thick confident
strokes, high contrast. Exactly 3 colors total and no more: a dark
slate background, one off-white shape, one accent color. NO gradients,
NO shading, NO texture, NO glow, NO painted detail. Must read
instantly as a silhouette at 32x32 pixels.

Composition: perfectly square 1:1, one centered symbol filling about
70% of the frame, even margin all around, flat solid dark background.
No frame, no border, no vignette.

Strict rules: NO text, NO letters, NO numbers, NO watermark, NO UI
frame, NO character, NO realistic rendering, NO background scenery.
Flat straight-on view.

Output: 1024x1024, PNG.
```

## T_Item_Potion_HP — 체력 회복

```
[세트 유지]

A single minimal flat game icon for a fantasy action RPG HUD.

Subject: a flat rounded potion flask with a short cork neck, off-white
glass outline, filled with solid crimson red liquid.

Style: flat vector pictogram. Bold simple shapes, thick confident
strokes, high contrast. Exactly 3 colors total and no more: a dark
slate background, one off-white shape, one accent color. NO gradients,
NO shading, NO texture, NO glow, NO painted detail. Must read
instantly as a silhouette at 32x32 pixels.

Composition: perfectly square 1:1, one centered symbol filling about
70% of the frame, even margin all around, flat solid dark background.
No frame, no border, no vignette.

Strict rules: NO text, NO letters, NO numbers, NO watermark, NO UI
frame, NO character, NO realistic rendering, NO background scenery.
Flat straight-on view.

Output: 1024x1024, PNG.
```

## T_Item_Ring — 특수

```
[세트 유지]

A single minimal flat game icon for a fantasy action RPG HUD.

Subject: a flat circular ring seen face-on, off-white band with a
single violet gemstone at the top.

Style: flat vector pictogram. Bold simple shapes, thick confident
strokes, high contrast. Exactly 3 colors total and no more: a dark
slate background, one off-white shape, one accent color. NO gradients,
NO shading, NO texture, NO glow, NO painted detail. Must read
instantly as a silhouette at 32x32 pixels.

Composition: perfectly square 1:1, one centered symbol filling about
70% of the frame, even margin all around, flat solid dark background.
No frame, no border, no vignette.

Strict rules: NO text, NO letters, NO numbers, NO watermark, NO UI
frame, NO character, NO realistic rendering, NO background scenery.
Flat straight-on view.

Output: 1024x1024, PNG.
```

---

# 3. HUD 공용 아이콘

## T_Icon_Gold — 골드 표시

```
[세트 유지]

A single minimal flat game icon for a fantasy action RPG HUD.

Subject: a small stack of three flat coins seen face-on, off-white
outlines filled with warm amber, slightly overlapping.

Style: flat vector pictogram. Bold simple shapes, thick confident
strokes, high contrast. Exactly 3 colors total and no more: a dark
slate background, one off-white shape, one accent color. NO gradients,
NO shading, NO texture, NO glow, NO painted detail. Must read
instantly as a silhouette at 32x32 pixels.

Composition: perfectly square 1:1, one centered symbol filling about
70% of the frame, even margin all around, flat solid dark background.
No frame, no border, no vignette.

Strict rules: NO text, NO letters, NO numbers, NO watermark, NO UI
frame, NO character, NO realistic rendering, NO background scenery.
Flat straight-on view.

Output: 1024x1024, PNG.
```

## T_Icon_SlotEmpty — 인벤토리 빈칸

```
[세트 유지]

A single minimal flat game icon for a fantasy action RPG HUD.

Subject: a plain empty square outline with a slightly thick off-white
stroke and softly rounded corners, completely hollow inside, nothing
else at all.

Style: flat vector pictogram. Bold simple shapes, thick confident
strokes, high contrast. Exactly 3 colors total and no more: a dark
slate background, one off-white shape, one accent color. NO gradients,
NO shading, NO texture, NO glow, NO painted detail. Must read
instantly as a silhouette at 32x32 pixels.

Composition: perfectly square 1:1, one centered symbol filling about
70% of the frame, even margin all around, flat solid dark background.
No frame, no border, no vignette.

Strict rules: NO text, NO letters, NO numbers, NO watermark, NO UI
frame, NO character, NO realistic rendering, NO background scenery.
Flat straight-on view.

Output: 1024x1024, PNG.
```

## T_Icon_Shop — 상점

```
[세트 유지]

A single minimal flat game icon for a fantasy action RPG HUD.

Subject: a flat market stall awning with a simple scalloped canopy
edge, off-white structure with warm amber stripes.

Style: flat vector pictogram. Bold simple shapes, thick confident
strokes, high contrast. Exactly 3 colors total and no more: a dark
slate background, one off-white shape, one accent color. NO gradients,
NO shading, NO texture, NO glow, NO painted detail. Must read
instantly as a silhouette at 32x32 pixels.

Composition: perfectly square 1:1, one centered symbol filling about
70% of the frame, even margin all around, flat solid dark background.
No frame, no border, no vignette.

Strict rules: NO text, NO letters, NO numbers, NO watermark, NO UI
frame, NO character, NO realistic rendering, NO background scenery.
Flat straight-on view.

Output: 1024x1024, PNG.
```

## T_Icon_Bag — 인벤토리 버튼

```
[세트 유지]

A single minimal flat game icon for a fantasy action RPG HUD.

Subject: a flat satchel bag seen from the front with a single flap and
strap, off-white body with a warm amber buckle.

Style: flat vector pictogram. Bold simple shapes, thick confident
strokes, high contrast. Exactly 3 colors total and no more: a dark
slate background, one off-white shape, one accent color. NO gradients,
NO shading, NO texture, NO glow, NO painted detail. Must read
instantly as a silhouette at 32x32 pixels.

Composition: perfectly square 1:1, one centered symbol filling about
70% of the frame, even margin all around, flat solid dark background.
No frame, no border, no vignette.

Strict rules: NO text, NO letters, NO numbers, NO watermark, NO UI
frame, NO character, NO realistic rendering, NO background scenery.
Flat straight-on view.

Output: 1024x1024, PNG.
```

---

# 4. 시스템 로그 표식 (선택)

## T_Log_LevelUp — 레벨업

```
[세트 유지]

A single minimal flat game icon for a fantasy action RPG HUD.

Subject: a single bold upward chevron arrow, off-white, with a short
amber accent line beneath it.

Style: flat vector pictogram. Bold simple shapes, thick confident
strokes, high contrast. Exactly 3 colors total and no more: a dark
slate background, one off-white shape, one accent color. NO gradients,
NO shading, NO texture, NO glow, NO painted detail. Must read
instantly as a silhouette at 32x32 pixels.

Composition: perfectly square 1:1, one centered symbol filling about
70% of the frame, even margin all around, flat solid dark background.
No frame, no border, no vignette.

Strict rules: NO text, NO letters, NO numbers, NO watermark, NO UI
frame, NO character, NO realistic rendering, NO background scenery.
Flat straight-on view.

Output: 1024x1024, PNG.
```

## T_Log_Loot — 아이템 획득

```
[세트 유지]

A single minimal flat game icon for a fantasy action RPG HUD.

Subject: a small flat downward arrow dropping into an open box,
off-white box with an amber arrow.

Style: flat vector pictogram. Bold simple shapes, thick confident
strokes, high contrast. Exactly 3 colors total and no more: a dark
slate background, one off-white shape, one accent color. NO gradients,
NO shading, NO texture, NO glow, NO painted detail. Must read
instantly as a silhouette at 32x32 pixels.

Composition: perfectly square 1:1, one centered symbol filling about
70% of the frame, even margin all around, flat solid dark background.
No frame, no border, no vignette.

Strict rules: NO text, NO letters, NO numbers, NO watermark, NO UI
frame, NO character, NO realistic rendering, NO background scenery.
Flat straight-on view.

Output: 1024x1024, PNG.
```

## T_Log_Kill — 처치

```
[세트 유지]

A single minimal flat game icon for a fantasy action RPG HUD.

Subject: a simple flat skull seen face-on, off-white, with hollow dark
eye sockets and a single crimson accent mark on the forehead.

Style: flat vector pictogram. Bold simple shapes, thick confident
strokes, high contrast. Exactly 3 colors total and no more: a dark
slate background, one off-white shape, one accent color. NO gradients,
NO shading, NO texture, NO glow, NO painted detail. Must read
instantly as a silhouette at 32x32 pixels.

Composition: perfectly square 1:1, one centered symbol filling about
70% of the frame, even margin all around, flat solid dark background.
No frame, no border, no vignette.

Strict rules: NO text, NO letters, NO numbers, NO watermark, NO UI
frame, NO character, NO realistic rendering, NO background scenery.
Flat straight-on view.

Output: 1024x1024, PNG.
```

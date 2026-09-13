# Server Shape Collision

## Status and scope

The Box2D 3.1.0 backend is enabled for the reviewed Lv3_Ingame map.
`Server/Server/CollisionSettings.json` uses `shapes` mode. Exporting a candidate
alone never changes the running server,
map assets, the legacy `map.dat`, or `PlayerSpawn.json`.

Current Lv3_Ingame audit: 40,843 candidate convex parts, 82,316 dry ground cells,
and zero unresolved entries. The user's puddle/well collision edits resolved all
22 complex-as-simple entries. Read-only Blueprint graph and instance-option
inspection classified 35 of the original 137 Movable components as static.
The user approved freezing the remaining door/shutter/sign/gate actors in their
current poses. The real candidate passes the server loader and capsule-safe spawn
check. Two-PC visual/play testing remains; no production session was restarted.

The 35 reviewed components belong to 18 actors in Ingame_Env: 11 fireplace
decorations (22 components), five flag stands (8), Forest_ISM (4), and the fake
window support (1). They received `ServerCollisionStatic` actor tags; transforms,
collision shapes, Mobility, Blueprint assets, and material wind effects are unchanged.
Fireplace instances have both dynamic shutter options disabled. Flag gameplay code
only updates material primitive data; native forest instances have no movement logic.

The remaining 102 entries (83 door/shutter components, 15 signs, four gate/support
components) were resolved on 72 placed actors, without editing their shared
Blueprint assets. Dynamic Door, Dynamic shutter, and Wind options are disabled;
mesh physics simulation and thrusters are disabled. These actors carry the reviewed
static tag. Existing open angles and all 778 mesh transforms/materials were verified
after reloading the saved map.

Eight doors changed orientation when their construction scripts switched from
dynamic to static. Those door meshes were baked to independent StaticMeshActors,
retaining the original mesh, materials and transform. The building's `Remove door`
option prevents duplicates. These actors have the `_FixedDoor` label suffix and
`ServerCollisionFrozenDoor` tag. Buildings/windows/signs otherwise retain their
Blueprint structure. Future interactive doors require server-side dynamic support;
do not simply re-enable local physics while using this static export.

The pre-review map and user-edited SM_Disk are backed up under
`Saved/Collision/BeforeStaticReview-20260913-034723/`. Detailed local inspection
outputs are `BehaviorAudit.json`, `CollisionOptions.json`, and
`StaticReviewApplied.json` in `Saved/Collision/`.
The pre-freeze map/candidate are in `Saved/Collision/BeforeFreeze-20260913-183712/`.
`FrozenCollisionActors.json` and `FrozenDoorReplacements.json` retain the original
poses and replacement mapping for local verification. `CollisionSettings.previous.json`
records the previous grid mode when applying the new scene.

- `grid`: existing navigation grid, useful for rollback.
- `shadow`: both backends run; the grid decision wins and disagreements are logged.
- `shapes`: convex static XY obstacles plus a separate ground coverage mask.

Shape mode does **not** AND its result with the old obstacle grid. Ground sampling
uses Landscape collision or explicitly tagged ground meshes, slope <= 45 degrees,
dry ocean surface queries, five samples per cell and <= 45 cm local height change.
NavMesh is used only to find the PlayerStart ground layer and export bounds.
The ground grid remains 100 cm, but fences are polygons, not blocked grid cells.

Player movement sweeps a 42 cm circle over the entire requested segment. Start/end
overlap, outside-map positions, non-finite input and invalid exports are blocked.
Each query has its own Box2D cache and the shared static tree is immutable.
Validated convex vertices go directly into Box2D GJK query proxies, which use the
same distance/shape-cast implementation as polygon queries. The rigid-body hull
builder is not used: its 2 cm point welding would collapse small bevels and thin
projected parts. No collider-padding or grid expansion is added. Concave,
self-intersecting, collinear and malformed inputs are still rejected.
Existing movement speed budgets, landing locks, packets and client CMC are retained.
NPC movement and spawn checks use the selected backend; existing point-radius NPC
and attack line-of-sight semantics are retained for this first stage.

## Editor workflow

1. Build `TurtleIslandEditor` with UE 5.8, open the game level and stop PIE.
2. Select **Tools > Box2D Collision: Export Candidate**.
3. Inspect `Client/TurtleIsland/Saved/Collision/CollisionAudit.json` and
   **Tools > Box2D Collision: Preview**. Cyan lines are candidate collider outlines;
   an incomplete preview is not an approved server scene.
4. Resolve every error, save edited assets/levels and export again.
5. Run `Tools/Test-ShapeCollision.ps1` to build the server validator and tests.
6. Select **Tools > Box2D Collision: Validate and Apply** and confirm.
7. Rebuild/restart the server to copy `CollisionMap.json` and settings to its output.

Apply requires a complete candidate and successful validation by the same Box2D
loader the server uses. It backs up existing scene/settings under `Saved/Collision`,
publishes the scene, then changes the mode. Cancelling or a validation failure keeps
active files untouched. These are startup files, not a live reload mechanism.
For an explicitly pre-approved headless batch, Python may call
`collision_scene.apply_scene(confirm=False)`; all level/completeness/server checks
still run. The normal Tools menu continues to require the confirmation dialog.

## Classifying geometry

The exporter reads Pawn-blocking query collision from registered components,
including Blueprint children and ISM/HISM world transforms. Supported simple
geometry: rotated/scaled boxes, convex bodies, spheres and capsules. Convex parts
have 3-8 vertices. Curves use a circumscribed 32-gon (at most 0.49% radial excess).
Complex-as-simple, deformed splines, unsupported primitives and unresolved moving
bodies produce errors; there is no broad AABB fallback.
Explicit `NavArea_Null` volumes are reported as requiring a shape/ground exclusion,
so authored no-walk restrictions cannot silently disappear during migration.

Actor tags apply to all its components; component tags allow finer control:

- `ServerCollisionGround`: an actual walkable floor mesh. It contributes ground
  traces, not an XY obstacle. Do not tag a whole building containing walls.
- `ServerCollisionStatic`: explicitly reviewed as never moving during gameplay,
  despite its Unreal Mobility setting. Simulating physics still produces an error.
- `ServerCollisionIgnore`: reviewed exclusion or a collider replaced by explicit
  simple collision elsewhere. Never use it just to silence an unknown obstacle.

Set Mobility to Static when that is the correct asset setup. Do not freeze working
gates/doors as static. Give complex meshes simple convex collision in the Static
Mesh Editor when appropriate, then confirm client and server outlines agree.
`unreal.CollisionExportLibrary.audit_collision_behavior()` returns a read-only JSON
report of loaded actors, component physics/tick state, attachment parents, and
Blueprint/level graph nodes with pin connections, including inherited Blueprints.
Inspect instance options and BeginPlay logic before approving a static tag: the
editor's current physics state alone does not prove that an object stays still.
Objects with no actual physics body (such as render-only water meshes) are recorded
in the audit instead of being mistaken for blocking geometry.

This is a **single ground-layer static XY** model. The vertical band is the sampled
local ground +45..192 cm, within 200 cm of the PlayerStart ground layer. Multilevel
bridges, jumping over low walls, moving doors, dynamic collisions, and NPC-specific
capsule sizes need additional rules and are not solved by switching to Box2D.
Sliding response and obstacle-aware NPC pathfinding are also outside this stage.

## Verification

```powershell
# From the repository root; no live server or database needed.
& .\Tools\Test-ShapeCollision.ps1

# Editor Session Frontend / console:
# Automation RunTests TurtleIsland

# Validate a real candidate; failure does not enable it.
& .\Server\Server\x64\CollisionTests\ServerCollisionTests.exe --validate .\Client\TurtleIsland\Saved\Collision\CollisionMap.candidate.json
```

Server tests cover thin/rotated fences, high-speed crossing, initial overlap,
rounded corners, terrain holes, bounds, invalid input, concurrent queries, backend
selection, relative config paths and shadow/rollback behavior. The Unreal projection
test generates `Saved/ShapeExportFixture.json` for a native-export/server-load round
trip. Python tests in `Content/Python/Tests/test_collision_scene.py` verify staged
publication, rejection and cancellation without an Unreal process.

Before rollout, test both PCs with identical code/assets: walk close to each side of
a fence, along it, through gates, around corners and near the shoreline; then test
spawn/respawn and landing. In `shadow`, disagreements are evidence to inspect, not
automatic authorization to change the map. `shapes` intentionally does not simulate
Unreal physics. A wide simple collider will still be wide after export.

Rollback: set `mode` to `grid` and restart the server. Keep legacy grid/spawn files
with the deployment. Missing or invalid startup data disables gameplay movement.

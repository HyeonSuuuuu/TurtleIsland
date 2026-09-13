# Editor-authored monster spawn regions

The game server still owns random positions, death and respawn timing. Unreal's
`MonsterSpawnRegion` actors only author the circular regions exported to Lua.
They are hidden in PIE, excluded from cooked maps, and have no collision or
navigation effect. Player respawn continues to use the separately exported
`PlayerStart`; these tools do not change it.

## Edit and apply

1. Open `/Game/Lv3_Ingame/Lv3_Ingame`. Load and show both `Ingame_Env` and
   `Ingame_Gameplay`. Stop PIE before using the tools.
2. Find the region in the World Outliner under `MonsterSpawns`, or use
   **Tools > Monster Spawns: Add Region** to add one to `Ingame_Gameplay`.
   The new marker starts near PlayerStart, projected onto navigation ground.
3. Move the actor to set its center. Set **Monster Spawn > Radius** in centimeters.
   Keep actor scale at `(1, 1, 1)` and pitch/roll at zero. Its green circle shows
   the XY boundary; actor Z only positions the editor preview above the terrain.
   Game View hides editor-only previews, so turn it off when editing regions.
4. Edit **Monsters** entries: **Monster Type**, **Count**, **Respawn Seconds**.
   One region can contain several monster types. **Enabled** excludes/includes
   the entire region in exports. Renaming the actor is safe.
5. Save the level, then use **Tools > Monster Spawns: Validate and Export Lua**.
   Review the locations and total count in the confirmation dialog.
6. Rebuild and restart the C++ game server. This is not a live-reload command.
   A server on another PC also needs the updated `Monster.lua` beside its exe.

Type IDs are zero-based: `0` is `Monsters[1]` in Lua. The matching client class
is configured in Project Settings > Network Settings > NpcClasses. Names, HP,
speed and attack stats remain in `Server/Server/Monster.lua`; the exporter never
changes them. Current type IDs are 0, 1 and 2. The room capacity is currently
**8 monsters total across all enabled regions**, not 8 per region.

| Type | Blueprint | Game name | Single basic attack |
| --- | --- | --- | --- |
| 0 | BP_ServerMonster1 | Stalker | Anim_Monster1_Hit_Left_Both |
| 1 | BP_ServerMonster3 | Tar Pod | Anim_Monster3_Kick1 |
| 2 | BP_ServerMonster5 | Sentry Eye | Anim_Monster5_HitBoth |

The actual labels are Korean, stored in each blueprint's **Monster Display Name**
and in `Monster.lua`. The game HUD shows the name and server HP above each monster.
`Content/Python/configure_monster_catalog.py` reapplies these presentation defaults
without changing meshes, capsule dimensions, scale or other animation slots.
Each attack plays once; only the server's single impact applies damage. Existing
attack damage, range, wind-up/cooldown, movement and death/respawn behavior remain unchanged.

The existing settings are center `(-20, 1600)`, radius `150 cm`, types `0 x2`,
`1 x2`, `2 x1`, and `10 seconds` respawn delay. Editing a region's Z does not
change spawn height: the server uses XY and the client resolves ground height.

## Validation and files

- The export reads all enabled regions in the loaded map, not only selection.
  Wrong maps, PIE, hidden/missing sublevels, tilted/scaled markers, invalid
  numbers, missing client type mappings and room-capacity overflow are rejected.
- The server's actual Lua loader validates the candidate before publication,
  including type IDs against `Monsters`. Build its validator once with
  `Tools/Test-ShapeCollision.ps1`; rerun after updating validator code.
- No enabled regions means `SpawnAreas = {}`. The confirmation explicitly warns
  that this disables all area spawns. An enabled region with an empty monster
  list is an error, not an implicit deletion.
- Only the block between `-- BEGIN UNREAL SPAWN AREAS` and
  `-- END UNREAL SPAWN AREAS` is managed. Keep those exact marker lines and do
  not assign `SpawnAreas` again elsewhere. Lua outside this block is preserved.
- Candidate and audit: `Client/TurtleIsland/Saved/MonsterSpawns/Monster.candidate.lua`
  and `regions.json`. Previous source: `Monster.previous.lua` in that folder.
  Publication replaces `Server/Server/Monster.lua` atomically after validation
  and confirmation, and rejects concurrent edits to the source or regions.
- The circle is a sampling boundary, not a guarantee every point is walkable.
  The server still rejects blocked/out-of-map points using its collision data.
  If no valid position is found it retries later, without spawning outside the
  area. Export collision separately after changing obstacles or terrain.

Python tests run without the editor:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe' -m unittest discover -s Client/TurtleIsland/Content/Python/Tests -v
```

Unreal automation: `TurtleIsland.Editor.MonsterSpawnRegion`.
Native validator: `Server/Server/x64/SpawnTests/ServerSpawnTests.exe --validate-script <candidate.lua>`.

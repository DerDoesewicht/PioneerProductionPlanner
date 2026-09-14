# Buildable Planner Terminal

Version 1.0.7 includes the completed terminal assets below
`/SFPFactoryPlanner/Terminal` and registers their unlock schematic from the
native root game-world module.

The runtime bridge remains available in `USFPPlannerBlueprintLibrary`:

- `Open Planner`
- `Close Planner`
- `Toggle Planner`

## Asset layout

The package contains these assets:

1. `Build_SFPPlannerTerminal` — Blueprint child of native `FGBuildable`
2. `Desc_SFPPlannerTerminal` — Blueprint child of `FGBuildingDescriptor`
3. `Recipe_SFPPlannerTerminal` — `FGRecipe`
4. `Schematic_SFPPlannerTerminal` — `FGSchematic`
5. `M_SFPPlannerScreen` — unlit screen material using the supplied texture
6. `Mesh/SM_SFPPlannerTerminal_r2` — original floor-console main mesh
7. `Mesh/SM_SFPPlannerTerminalScreen_r3` — separate screen overlay
8. `Mesh/TEX_T_SFPPlannerScreen` and the required `SFP_R2_*`/`SFP_R3_*`
   materials
9. `SourceAssets/Terminal` — r13 premium OBJ/MTL, 18 PBR maps, 2K display,
   menu icon and the in-editor setup script

## Buildable

- `TerminalMesh` is a real `StaticMeshComponent` using the original
  `SM_SFPPlannerTerminal_r2` floor-console mesh.
- Its collision profile is `BlockAll` with query and physics collision enabled,
  which supplies the hit target needed for use and Build Gun dismantling.
- `TerminalScreen` uses `SM_SFPPlannerTerminalScreen_r3` with
  `M_SFPPlannerScreen`; it is visual only and uses `NoCollision`.
- Nanite is disabled in both terminal mesh assets. Both Blueprint components
  also disallow Nanite so the low-poly console cannot request material shader
  permutations that are absent from the packaged materials.
- The Blueprint is usable. At runtime `USFPGameWorldModule` assigns the native
  `ASFPPlannerTerminalHologram` to its own CDO before the build recipe can be
  used. r34 does not traverse or mutate Blueprint SCS component templates.
- The safe hologram blocks FactoryGame instancing, coloring and customization
  conversion for both imported mesh components. It forces non-Nanite rendering,
  selects FactoryGame's simplified hologram material path, hides
  `TerminalScreen` only for placement and gives the body the engine's
  default surface material. The completed buildable retains all authored
  materials and the cyan planner screen.
- The normal use interaction obtains the using player's controller and calls
  `Open Planner`. On a listen or dedicated server, r35 routes this request only
  to that controller's owning client through the registered remote call object.
- The inherited buildable interaction prompt uses the terminal's display name.

The installer copies the stable assets and does not run an Unreal Python
commandlet. For the new geometry and PBR surfaces, run
`SourceAssets/Terminal/setup-terminal-r13.py` once through **Tools -> Execute
Python Script** in the open editor, then compile `Build_SFPPlannerTerminal` and
Save All. Despite the compatibility filename, r17 contains the crash-, texture-
and slot-safe importer: it never deletes material expressions, never calls the
unsupported `UObject.post_edit_change()`, and creates every run in a new
`PBR/Generated_r17_XX` staging folder. Fresh source meshes preserve all material
groups; LOD 0 and explicit section mappings are then transferred to the stable
eight-slot assets. Normal-map, mask and sRGB settings are verified and saved
individually. The script preserves the asset IDs, disables Nanite,
recreates the main box collision and removes collision from the visual screen. The verifier
checks the saved Blueprint references, collision profiles, usable interface,
screen material and texture. No HUB, coupon-shop or EasyCheat content is
referenced.

The planner bridge opens Slate UI immediately for a local controller. For an
authoritative remote controller, it sends a reliable client RPC through that
player's `USFPPlannerRemoteCallObject`. The dedicated server never constructs
the planner window, and another multiplayer client never receives the request.

## Descriptor and build recipe

- Point `Desc_SFPPlannerTerminal.mBuildableClass` at
  `Build_SFPPlannerTerminal`.
- `USFPGameWorldModule` prefers the r13 `TEX_T_PFP_TerminalIcon` for the
  descriptor's small/big icon fields and both milestone icon paths. The
  packaged `TEX_T_SFPPlannerScreen` remains a safe fallback, so a white tile is
  never intentionally selected.
- Use a valid building category and subcategory so opening the build menu cannot
  fail. The HUB/progression category is a suitable temporary location.
- `Recipe_SFPPlannerTerminal` costs 10 Iron Plates and 20 Cable.
- Set its producer to `FGBuildGun` and its single product to
  `Desc_SFPPlannerTerminal`.

## Unlock and registration

- The public Tier 1 milestone `Schematic_SFPPlannerTerminal` costs one Iron
  Plate and unlocks `Recipe_SFPPlannerTerminal`.
- `USFPGameWorldModule` loads and registers
  `Schematic_SFPPlannerTerminal` through its `mSchematics` list.
- After changing C++ or any terminal asset, save all assets, restart the editor
  if C++ was rebuilt, then package only `SFPFactoryPlanner` with Alpakit.

The terminal does not replace the configurable shortcut. F8 is the default;
players may bind another key/chord or disable it. The terminal remains available
regardless of that setting.

After an in-game test, `diagnose-runtime-crash.sh FactoryGame.log` verifies that
the crash-safe hologram was assigned without SCS traversal and instantiated and that the former SFP
Nanite-material warning and fatal D3D12 PSO signature are absent.

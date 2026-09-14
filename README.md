# Pioneer Production Planner 1.3.0

Production-graph and usability release for large Vanilla and Satisfactory Plus factory plans.

Key changes in 1.3.0:

- production graph flows left to right with aggregated machine cards and dedicated material ports;
- machine-group input/output rates and total power are displayed without changing solver values;
- transported materials receive deterministic high-contrast route colours;
- graph zoom extends down to 5% and FIT GRAPH can frame the complete factory;
- long node headings wrap to two lines and are safely truncated only when necessary;
- graph text uses zoom-dependent LOD so large overview views remain readable;
- all 1.2.1 solver fixes, plan compatibility, multiplayer/server support and Satisfactory Plus integration remain included;
- server and clients must use the exact same 1.3.0 build.

The following feature history describes the included earlier versions.

Native in-game production planner for vanilla Satisfactory, Satisfactory Plus,
and other loaded recipe mods. Version 1.0.7 reads the active runtime recipe
manager directly; no HTTP server, log parser, or Ficsit Remote Monitoring recipe
endpoint is involved. Satisfactory Plus is supported but is not a dependency.

## Open the planner

Press `F8` after loading a save. The same key closes the planner again. F8 is
only the default: click the `HOTKEY` control in the planner header and press a
new key combination, or select `OFF` to disable the shortcut. The choice is
stored in the local game user settings. The terminal and `/sfpplanner open`
remain available even when the hotkey is disabled.

The included floor-standing `Factory Planner Terminal` is a compact original
SFP console with a separate emissive screen showing the planner interface. It
uses neither the HUB terminal nor the coupon shop and has no EasyCheat content
or dependency. It can be unlocked as a Tier 1 HUB milestone, built for 10 Iron
Plates and 20 Cable, and used with the normal interaction key. Blocking
query/physics collision on the main mesh supports both interaction and Build
Gun dismantling; the visual screen overlay has no collision. The Blueprint functions
`Open Planner`, `Close Planner`, and `Toggle Planner` remain available for
extensions.

Release r14 contains the Linux-editor import hotfix. The compatibility-named
`SourceAssets/Terminal/setup-terminal-r13.py` no longer deletes material
expressions; each execution creates a fresh `PBR/Generated_r14_XX` material set
and safely reuses the packaged screen material. This avoids the UE 5.6
`Assertion failed: !IsRooted()` crash seen during the earlier r13 import.

Release r15 also fixes UE retaining only five material slots on direct OBJ
reimport. It imports fresh staging meshes below `PBR/Generated_r15_XX`, copies
LOD 0 into the stable assets, reconstructs all eight main slots and explicitly
remaps every section without changing Blueprint or save-game asset identities.

Release r16 removes the final unsupported UE 5.6 Python call
`StaticMesh.post_edit_change()`. The slot-safe transfer remains unchanged, but
the stable meshes are now persisted directly through `EditorAssetLibrary`.
Retries use a fresh `PBR/Generated_r16_XX` folder and do not touch incomplete
r14/r15 staging assets.

Release r17 also removes `Texture2D.post_edit_change()`, verifies and persists
the required normal-map, mask and sRGB settings, and disables Nanite before the
staging meshes are imported. Only assets touched by the current run are saved;
the expensive recursive validation of every old staging folder is gone.

Release r19 includes the persistent construction progress introduced in r18 and
fixes its UE 5.6 checked-format compilation failure. Construction progress remains
directly integrated into the production
graph. Every node has a clickable completion box, completed nodes turn green,
and the graph reports completed versus total nodes. The automatic current plan
is saved after every toggle; named plans retain the progress when saved. Schema
5 remains backwards compatible with all schema-1 through schema-4 plans.

Release r20 adds the Satisfactory Plus/KLib modular miner as a real production
alternative. KAPI data assets and the installed drill/waste modules are read by
reflection only when present. A configured miner can therefore produce ingots
directly on an impure, normal or pure resource node; both belt outputs and the
waste stream (for example residue sludge) are included. The graph shows the
attached node locally, and construction totals include the miner and its modules.
KAPI and KLib remain optional, so Vanilla/SML still loads the same plugin.

Release r21 adds a dedicated power-supply tab. It discovers fuel generators and
their fuels from the active build-recipe catalog, observes current-save unlocks,
and plans to a requested net output plus reserve. The result includes whole
generator count and final clock setting, fuel and supplemental-resource rates,
the complete upstream production chain, nuclear waste/by-products, gross power,
chain self-consumption, and real net power. Power plans use persistence schema 6
while schema 1 through 5 remain loadable. The F8 input handler now defers opening
or closing the viewport UI until the next game tick, avoiding Slate-tree mutation
inside key-event dispatch.

Release r22 fixes the diagnosed F8 startup assertion caused by empty product
arrays in modded runtime recipes. The planner no longer calls FactoryGame's
unchecked `GetBuildableClassFromRecipe` helper. A shared guarded resolver now
validates recipe classes, every product descriptor and each resulting buildable
before cataloguing generators, modular miners, construction costs or transport
tiers; malformed foreign recipes are skipped safely.

Release r23 rebuilds the production graph for large multi-product sites. The
layout now uses labelled columns from external resources through each
production stage to the final products, orders nodes around their connected
neighbours, assigns separate sorted ports to parallel edges and reserves
non-overlapping label lanes between stages. Each solid material keeps one
deterministic colour throughout the graph, while power, fluids, gases and local
modular-miner links use dedicated colours. Completion boxes and saved progress
remain fully compatible.

Release r24 fixes routing infrastructure generated solely from transport
capacity. A single logical 350/min connection on three Mk.2 belts now remains
three parallel lines; it no longer creates a false three-input fusionator and
another splitter. Splitters require multiple distinct consumer nodes and
mergers require multiple distinct producer nodes. Belt/lift counts and costs
remain based on `RequiredLines`, while redundant junction costs disappear.

Release r25 adds the modular miner's drill-only raw-resource route. A tier-1
drill on a normal node is modelled as two 30/min outputs, or 60/min per miner.
For example, 416.25/min now produces 6.9375 equivalent miners: seven physical
miners on seven normal nodes, with six at 100% and the last at 93.75%. The
graph therefore shows the resource nodes and modular miners explicitly instead
of presenting the entire requirement as one ambiguous external source. Existing
direct-processing routes and Vanilla/SML compatibility are unchanged.

Release r26 keeps every capacity-derived transport lane continuous from its
producer to its consumer. A 350/min solid flow over Mk.2 transport is labelled
as three parallel lines including average load versus capacity per line.
Aggregated machine groups no longer fabricate mergers; routing hardware is
created only for real multi-endpoint splits or merges whose line count changes.

Release r27 automatically follows the active game language. German (`de-*`)
uses the complete German planner UI and German number separators. English and
all other languages use the complete English UI and English number separators.
This covers tabs, controls, graph headings and details, status/error messages,
power and resource summaries, saved legacy plan text, and chat feedback. Item,
recipe, machine and buildable names remain sourced from the game's localized
runtime catalog.

Release r28 is the public 1.0.0 branding build. The visible name is now
`Pioneer Production Planner`, while the stable technical mod reference remains
`SFPFactoryPlanner`. Graph columns are calculated from the real material-flow
topology, so ordinary shared-production and routing edges stay between their
producer and consumer instead of being drawn beyond the final-product column.
Only genuine recipe cycles retain a reverse route.

Release r29 keeps the fast per-world catalog cache but verifies its known and
available recipe sets whenever the planner is reopened. If research changed
the live unlock state, the catalog is rebuilt once before the window appears.
Newly unlocked KLib products therefore show up with the unlocked-only filter
after closing and reopening F8; reloading the save is no longer necessary.
This fix is published as Mod Manager version `1.0.1` with numeric version `1`,
which matches the SemVer major required by FICSIT.app.

Release r30 also discovers uniform-rate fluid extractors from their construction
recipes. FactoryGame stores their throughput on the extractor building rather
than in an ordinary production recipe, so earlier releases could show water or
algae mass as a locked external source even when a compatible extractor was
unlocked. The planner now reads the loaded extractor's cycle time, fluid amount
per cycle, allowed resource, power and live unlock state and creates a direct
source route. This supports the `AB_FluidExtras` Mini Water Extractor and KLib
bio-water extractor at their current runtime rates without hardcoding those
rates or requiring either mod. r30 also aggregates repeated descriptors within
one recipe and prefers an unlocked compatible machine when a recipe lists
multiple producers, correcting the additional catalog anomalies found in the
provided 1.0.1 runtime export.

Release r31 is cumulative and keeps all r30 fixes. Its graph layout right-aligns
short feeder chains against their first real consumer, so water, sand, or any
other direct ingredient appears immediately before the machine that uses it
instead of crossing the plan from the global source column. Wider stage and row
spacing fans out complex factories. Full-width section bands distinguish start,
direct input, production, routing, and final-output areas. Real splitters and
mergers now carry a one-to-three or three-to-one flow schematic plus coloured
ports at every connected material edge.

Release r32 lets large graphs use the available canvas instead of shrinking the
whole factory to an unreadable thumbnail. It starts at a readable minimum zoom,
keeps the graph pannable, increases node and stage spacing, and distributes
sparse columns across the full branch height. Every logical edge now terminates
at its own visible source and target port. A splitter or merger continues those
actual colour-matched input and output lines through its body to the junction;
each branch then runs to its individual machine or storage endpoint. Final
products, by-products, and generator waste are shown as explicit storage nodes
with their own input ports.

Release r33 expands power planning beyond `AFGBuildableGeneratorFuel`. It
discovers fuel-free solar, wind, water and geothermal generators and optional
Refined Power modular components from their live construction recipes. A
modular plan contains distinct heater, boiler, boiler platform, turbine,
converter platform, generator, steam cooler, exhaust cooler and cooling
platform costs. Fuel, water, high-pressure steam, low-pressure steam, flue gas,
rotational energy and electric output are connected as real graph edges. The
catalog reads loaded output, RPM, steam, fuel and item-class properties through
reflection, so Refined Power remains optional. Variable renewable output is
shown as a maximum and Automatic prefers a stable available configuration.

Release r34 makes the planner shortcut user-configurable without adding a
ConfigLib or other mod dependency. Single keys and chords such as `Ctrl+F9`
can be captured in the planner header or set by chat command; the binding may
also be disabled and reset to F8. The global input preprocessor no longer
consumes a matching key, so another mod is not blocked merely by registration
order. Opening and closing still happen on the next game tick. r34 also stops
traversing or mutating Blueprint SimpleConstructionScript component templates
during module/world startup. Only the spawned SFP hologram instance is
sanitized. This reduces load-order coupling with camera, equipment, hand-slot,
sign and other buildable mods while retaining the safe terminal hologram.

Release r35 is a cumulative multiplayer and dedicated-server update that
already includes every r34 change. A root game-instance module registers a
replicated `UFGRemoteCallObject` for each player. Server-side terminal and chat
interactions route open, close, toggle, hotkey status, and hotkey changes to the
owning client instead of trying to construct Slate UI on the server. Dedicated
servers skip Slate input initialization entirely. Planner hotkeys, the automatic
last plan and personal named plans remain local to each client; runtime exports
remain on the machine executing the export. Exact `1.0.7` remote-version
metadata rejects other public versions. Because older test builds also used
`1.0.7`, server and clients must use the same generated r35.5 ZIP.

Source revision r35.3 also keeps all terminal Blueprint and texture loads out
of the native GameWorld module CDO constructor. The terminal schematic is
loaded immediately before SML handles construction of an actual game world;
the main-menu Server Manager therefore never receives the terminal dependency
graph during startup. Dedicated servers register the schematic but skip the
client-only hologram and icon CDO decoration. The public and remote version
remain exactly `1.0.7` for this verification build.

Source revision r35.4 adds opt-in community server plans without replacing the
personal plan store. The plan bar switches between `PERSONAL` and `SERVER`.
Server plans are validated and stored authoritatively below
`Saved/SFPFactoryPlanner/ServerPlans`; clients never receive a server file path.
Every connected player may read a plan and update its current revision, while
only its creator may delete it. Exact revision checks reject stale writes.
Progress checkboxes on a loaded server plan synchronize automatically; a newly
calculated graph remains local until `SAVE` is pressed. Larger JSON graphs are
transferred in ordered, bounded reliable-RPC chunks.

Source revision r35.5 keeps that protocol and save schema unchanged while
removing the unavailable `Misc/LexToString.h` dependency found by Alpakit's
LinuxServer Shipping target. Server revision numbers are formatted explicitly
as portable 64-bit values on Windows, Windows Server and Linux Server.

## In-game commands

After loading a save, open chat and use:

```text
/sfpplanner open
/sfpplanner close
/sfpplanner export
/sfpplanner hotkey F9
/sfpplanner hotkey Ctrl+F9
/sfpplanner hotkey off
/sfpplanner hotkey reset
```

`/sfpplanner` without an action opens the planner. Aliases are `/sfp` and
`/sfp-export`.

## Planner UI

- localized product search across all currently loaded mods
- persistent rebindable open/close hotkey with optional modifiers and an off switch
- any number of end products with an independent target rate
- one combined solve that merges shared intermediate production branches
- active-by-default filter for recipes and transport tiers unlocked in the current save
- separate runtime-derived selectors for conveyor belts and conveyor lifts
- solid-line capacity based on the slower selected belt/lift tier, preventing hidden lift bottlenecks
- deterministic automatic recipe selection plus manual per-product recipe overrides
- recursive machine, ingredient, power, by-product, and source calculation
- optional modular-miner raw extraction and direct-production routes with node purity, two outputs, modules, and waste stream
- direct water/algae-extractor routes derived from loaded building data, including compatible modded subclasses
- dedicated net-power planning with reserve, automatic or explicit generator/operating-mode selection, classic fuel chains, fuel-free generators, complete Refined Power modular steam/exhaust chains, supplemental resources, waste, gross output, self-use, and real net output
- optional input-budget mode: enter every available external resource and calculate the maximum reachable output
- grouped list of all required production machines
- separate machine, infrastructure, and combined construction-material totals
- configurable average connection length for transparent conveyor/pipe material estimates
- cycle protection for feedback recipes
- runtime-derived conveyor and pipeline recommendations, line counts, and estimated metres
- runtime game-data names for splitter, fusionator/merger, and pipe-junction nodes where a flow branches or joins
- consumer-adjacent feeder branches instead of long avoidable source-to-machine crossings
- port-wired splitters/mergers with an explicit line to each machine or storage endpoint
- visible storage endpoints for final products, by-products, and generator waste
- full-screen native Slate graph
- per-node construction completion boxes with persistent completed/total progress
- mouse-wheel zoom, middle/right mouse drag, readable double-click alignment, and Escape close
- distinct colours for solids, liquids, and gases
- unambiguous compact German numbers (`12,5` versus `12.500`) throughout the visible planner
- wider, three-line path labels with a background for unobstructed item, rate, and transport descriptions
- automatic persistence of the active production graph across planner and game restarts
- any number of named plans with save/overwrite, load, and delete controls
- saved recipe choices, belt/lift tiers, unlock filter, connection-length estimate, available input budgets, and node completion progress per plan
- cached graph layout/text, no permanent widget tick, off-screen culling, and reduced detail rendering at overview zoom for lower CPU/GPU overhead

Machine counts are displayed as whole buildings to construct. Each group shows
the exact clocking split, for example four at 100% plus one at 16.7%, instead
of a fractional machine count. Power remains labelled as base power because
overclocking mods can replace the game's power curve.

## Runtime export schema 1.1

`/sfpplanner export` writes the existing recipe/item/mod data plus:

- unique production-machine metadata
- base and idle power
- solid input/output connection counts
- pipe connection counts
- conveyor speed and exact capacity using the runtime item spacing
- pipeline flow limits and capacity per minute

The file is stored below the game's project Saved directory under
`SFPFactoryPlanner/Exports`.

The most recent production plan is stored as
`SFPFactoryPlanner/Plans/LastPlan.json` below the same Saved directory.

In multiplayer the automatic last plan and the `PERSONAL` named-plan store are
local to each client, so players retain independent hotkeys and personal plan
files. The opt-in `SERVER` store is different: its validated plans live below
the authority's `SFPFactoryPlanner/ServerPlans` directory and are synchronized
through the multiplayer bridge. A dedicated-server `/sfpplanner export` is
written below that server's Saved directory.

Personal named plans are stored separately below
`SFPFactoryPlanner/Plans/Saved`. Enter a name, click `Save`, then select it from
the list whenever it is needed again. Switch the plan bar to `SERVER` to use
community plans instead.

## Planning boundary

The graph is a logical production plan, not a 3D blueprint. Machine groups and
material flows are exact for the selected recipes. Multiple selected end
products share one graph and common intermediate demands are merged. Additional
recipe outputs are shown as explicit by-product sinks. Cyclic chains are stopped and marked for manual
feedback. Splitter and merger counts are calculated from the number of graph
branches. Conveyor and pipe build materials are estimates because the planner
cannot know the final 3D route length; change `Geschätzte Länge je Verbindung`
before calculating to match the intended factory layout. The selected conveyor
lift limits throughput, but lift count and construction material are not guessed
because the logical graph does not know the factory's vertical height.

## Win64 packaging path check

Run `./diagnose-win64-paths.sh <Alpakit-log>` if Alpakit reports duplicate Unreal
types or `LNK1104`. Engine and project must both use `/mnt/SFPMod`: mixing roots
duplicates Unreal headers, while the long `/mnt/AllesAndere/Satisfactory-Modding`
project path makes the OnlineIntegrationPostSplashScreen output 261 characters.
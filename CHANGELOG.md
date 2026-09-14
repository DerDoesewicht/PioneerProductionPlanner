# Pioneer Production Planner 1.3.0

- Reworked production graph into a left-to-right factory-flow view with clearer machine cards, input/output hierarchy and larger production-stage spacing.
- Preserve aggregated machine groups and show aggregate material rates, machine count, clock distribution and total power without changing solver calculations.
- Add deterministic high-contrast connection colours per transported material across machines, splitters/mergers, pipeline junctions and storage.
- Extend graph zoom to 5% and make FIT GRAPH/double-click frame the complete graph.
- Wrap long recipe/node headings to two lines; extremely long second lines end with an ellipsis instead of overflowing the card.
- Add zoom-dependent graph LOD: full card details from 30%, compact titles from 15%, clean connection overview below 15% with hover orientation retained.
- Keep all 1.2.1 solver, route-selection, persistence, multiplayer and plan-schema behaviour unchanged.

# Pioneer Production Planner 1.2.1

- r56: Give each transported material a deterministic high-contrast route colour across machines, routing nodes and storage; liquids and gases are no longer all forced into one colour per form.
- r55: Rework the production graph cards, material ports, interaction and spacing without changing solver values.
- Fix available-input capacity planning to use the real balanced withdrawal of the selected input.
- Prefer supply-connected automatic recipe routes using the shortest production distance to the selected input.
- Prevent automatic pack/unpack conversion loops from masquerading as primary liquid or gas production.
- Preserve explicit user-selected unpackaging routes.
- Treat plans with only fixed outputs as feasibility checks.
- Replace generic unbounded-solver failures with finite diagnostic plans and clearer status text.
- Includes all 1.2.0 features and the r51 power-label baseline.

# Pioneer Production Planner 1.1.0 – Teststand r42

- Vollständige Modular-Miner-Konfigurationen mit Pflichtmodulen und Betriebsflüssigkeiten.
- Direktabbaurouten für Schwefel, Uran und Kerrkristall bei gültiger Ausstattung ergänzt.
- Flüssigkeitszufuhr, Modulboni, Modulbaukosten und Leistung berücksichtigt.
- UI nach Bezugswegen unterteilt; Quelle, Miner, Reinheit, Module und Flüssigkeit auswählbar.
- Automatische Annahmen gekennzeichnet; eigene Auswahl pro Plan gespeichert.
- Runtime-Export um Bezugswege und Herkunft der Anschlussdaten erweitert.
- Vorherige Fixes für Materialbilanz, Graph, Fortschritt und Mausklicks enthalten.

Status: Paketlokale Tests bestanden; Unreal-Build und In-Game-Abnahme ausstehend.

## 1.0.12

- Geöffneter Planner sperrt Spieleingaben; Klicks auf freie Graphflächen werden abgefangen.
- Baufortschritt-Fix aus 1.0.11 enthalten.

# 1.0.11

Fix completion carry-over between different plans. Preserve unchanged same-plan progress and each loaded plan’s own saved state.

# 1.0.10 — Route labels

Compact material names and rates on horizontal/vertical graph connections, with collision-aware placement. Includes previous 1.0.9 fixes.

Richtungspfeile: größere Pfeile am Zielport und zusätzliche Pfeile entlang horizontaler, vertikaler und rückwärts laufender Leitungen. Pfeile folgen der tatsächlichen Quell-/Zielrichtung, behalten die Materialfarbe und werden nicht über das eigene Leitungslabel gezeichnet. Sichtbarkeit im Spiel noch testen.

# 1.0.9-r38.2

Disable unverified synthetic Modular Miner gangue production for every input, preventing dirt/peat/clay substitution. Preserve actual game recipes, raw extraction, coproduct reuse and return-flow balancing.

# 1.0.9 — r38 Material balance

Reuses coproducts, balances return-flow losses, rebuilds material connections and removes redundant production. Updates machines, power, transport and costs; includes the dirt/gangue hotfix. Recalculate and save old plans. Startup stock remains a separate requirement.

# 1.0.9 — Dirt/Gangue hotfix

Exclude invalid Ficsit Farming dirt → gangue Modular Miner conversion from the recipe catalog. Raw extraction is preserved. Recalculate and save affected plans after updating. Matching client/server version 1.0.9 required.

# Changelog

## 1.0.8 – Relevant recipes and production-chain fixes

- Restrict recipe choices to the supply chain of the selected end products.
- Show end-product recipes first, including products with a single recipe.
- Clear outdated recipe choices after changing targets or recipe selections.
- Prefer direct production recipes over unrelated factories producing the
  requested material as a secondary output. Retain secondary-output sources
  when no eligible primary-output source exists.
- Fix unwanted production branches in the reported Satisfactory Plus steel plan.
- Prevent programmatic dropdown updates from creating unintended overrides.
- Include the CoreOnline dependency required for multiplayer player IDs.
- Preserve personal/shared plans, per-player controls and all earlier fixes.
- Require matching 1.0.8 installations on the server and all clients.

After updating an existing plan, use AUTOMATIC and recalculate, then save.
The steel-plan fix and Linux shared server plans were confirmed in game.
Windows dedicated-server runtime testing is still pending.

## 1.0.7 r36 – Relevant recipe selection

- Limit recipe rows to positive-flow ancestors of the calculated end products;
  unrelated nodes and disposal-only branches do not populate the selector.
- Show end-product choices first, including products with only one recipe.
- Clear stale recipe rows when end products are added or removed. Recalculate
  to populate the new chain.
- Prefer recipes whose first runtime output is the requested item in both
  automatic selection and the dropdown. Secondary-output routes remain a
  fallback when no eligible primary-output source exists. This avoids choosing
  an unrelated metal factory just for steam when direct steam production exists.
- In automatic solving, respect recursion exclusions before deciding whether a
  primary source exists. Stored secondary overrides are superseded when an
  eligible primary source exists. Existing graphs require recalculation.
- Ignore programmatic combo selection events instead of recording them as user
  overrides. Old graphs with excluded recipes display a recalculation prompt.
- Include the CoreOnline link dependency needed by shared server plans.
- Keep the public version at 1.0.7 for this test package. Install the exact same
  newly generated ZIP on all peers. Unreal compilation and the actual Plus
  steel plan still require in-game verification.

## 1.0.7 r35.5 – Alpakit LinuxServer compile fix

- Remove the unavailable `Misc/LexToString.h` include from the shared-plan
  persistence, networking and UI code.
- Format 64-bit server-plan revisions explicitly with portable `%lld` values,
  so all four Alpakit Shipping targets compile against the installed
  Satisfactory Unreal distribution.
- Keep the r35.4 community-server-plan behavior and public Mod Manager version
  `1.0.7` unchanged. Server and every client must use the same newly generated
  r35.5 ZIP.

## 1.0.7 r35.4 – Community server plans

- Add an explicit `PERSONAL` / `SERVER` switch to the existing plan bar.
  Personal named plans and the automatic last-plan backup remain client-local.
- Store shared plans authoritatively below the dedicated server's
  `Saved/SFPFactoryPlanner/ServerPlans` directory and expose only validated plan
  JSON through each player's registered Remote Call Object.
- Synchronize the shared-plan catalog and loaded-plan construction progress to
  connected players. Recalculated graphs stay local until `SAVE` is pressed.
- Protect concurrent edits with exact server revisions: stale clients are told
  to reload instead of silently overwriting a newer plan.
- Allow every connected player to read and edit a shared plan while restricting
  deletion to its original creator. Persist creator and last-editor metadata on
  the server without sending the creator's account ID to clients.
- Limit shared catalogs, targets, graph size and payload size, sanitize all file
  names, write JSON through a temporary file, and split large network transfers
  into ordered 8,192-character reliable RPC chunks.
- Keep public Mod Manager version `1.0.7`, its exact remote-version gate and all
  r35.3 Server Manager startup protections unchanged. Server and clients must
  use the same generated r35.4 ZIP because older test builds also reported
  `1.0.7`.

## 1.0.7 r35.3 – Server Manager startup hotfix

- Stop synchronously loading terminal Blueprint classes, descriptors and
  textures from the native GameWorld module CDO constructor. That early load
  could partially initialize Satisfactory's Server Manager widget packages and
  leave newly added dedicated servers invisible with an `EntryWidgetClass`
  fallback error.
- Load the terminal schematic only when an actual game world enters its
  construction phase, before SML performs default schematic registration.
- On dedicated servers, register gameplay content without loading or mutating
  client-only hologram and menu-icon CDOs.
- Keep public Mod Manager version `1.0.7`, exact remote parity, the r35
  per-player RCO bridge and all r35.1/r35.2 build corrections unchanged.

## 1.0.7 r35.2 – Linux dedicated-server Shipping build

- Build `FactoryServer Linux Shipping` instead of the unsupported Development
  configuration used by the first helper script. This matches the installed
  engine distribution and the successful Alpakit LinuxServer release target.
- Add a package regression check that prevents the unsupported Development
  command from returning. Public Mod Manager version and required client/server
  parity remain exactly `1.0.7`.

## 1.0.7 r35.1 – Unreal compile hotfix

- Match Unreal's real `FLifetimeProperty` and `FKeyEvent` tag declarations so
  Linux Clang and the Microsoft C++ ABI warning policy compile without
  `-Wmismatched-tags` failures.
- Replace the unavailable `TArray::Mid()` call in the hotkey chat command with
  an allocation-free argument concatenation loop compatible with the current
  Starter Project API.
- Use brace initialization for persisted `FKey` values to prevent the most
  vexing parse under Clang. The public Mod Manager version remains `1.0.7` and
  all r35 multiplayer metadata is unchanged.

## 1.0.7 r35

- Ship one cumulative package containing all fixes and features through r34;
  no r34 installation is required before applying or installing 1.0.7.
- Register `USFPPlannerRemoteCallObject` from a root game-instance module so
  SML supplies a network-owned bridge for every player controller.
- Route server-side terminal Blueprint calls and `/sfpplanner open`, `close`,
  `hotkey` requests to the owning client through reliable client RPCs. A
  request can no longer open UI for the wrong multiplayer player.
- Keep direct execution for a listen host or standalone local controller while
  preserving the existing terminal Blueprint assets and callable API.
- Skip global Slate input-processor setup on dedicated servers. Hotkeys and
  plan JSON remain client-local; a server-side runtime export is explicitly
  written to the server's Saved directory.
- Require the exact remote version `1.0.7` and mark the mod required remotely,
  preventing unsupported mixed r34/r35 multiplayer installations.
- Add a one-command Linux `FactoryServer` build wrapper, dedicated multiplayer
  log diagnostics, and a two-player server test checklist. Release packaging
  is documented for Windows, Windows Server, and Linux Server targets.

## 1.0.6 r34

- Make the planner open/close shortcut persistent and user-configurable while
  keeping F8 as the default. Capture a new key or modifier chord directly in
  the planner header, disable it with `OFF`, and keep terminal/chat access
  available at all times.
- Add `/sfpplanner hotkey F9`, `/sfpplanner hotkey Ctrl+F9`,
  `/sfpplanner hotkey off` and `/sfpplanner hotkey reset` as recovery paths
  when another mod already owns the default key.
- Stop consuming matched key-down events in the global Slate input
  preprocessor. Planner toggling remains deferred to the next game tick, but
  other mods and the game now receive the same event regardless of input
  preprocessor registration order.
- Register the input preprocessor after engine initialization when Slate was
  not yet ready during module startup, and always unregister the matching
  delegate/preprocessor during shutdown.
- Remove all startup traversal and mutation of Blueprint
  `SimpleConstructionScript`/`USCS_Node` component templates. Assign only the
  SFP terminal's hologram class at startup and harden only components on the
  spawned hologram instance.
- Extend runtime diagnostics for save-load failures in
  `AFGCharacterPlayer::SpawnEquipment` ->
  `USCS_Node::GetActualComponentTemplate`. The supplied report crashes before
  any planner hotkey path and contains no SFP callstack frame, so r34 records
  this as an equipment-Blueprint/load-order crash rather than claiming an
  unproven culprit.
- Publish the cumulative update as Mod Manager version `1.0.6` with numeric
  version `1`; retain the stable mod reference `SFPFactoryPlanner`, schema 1–6
  compatibility and SML `^3.12.0` as the only required dependency.

## 1.0.5 r33

- Expand the runtime power catalog beyond fuel-generator subclasses. Discover
  fuel-free solar, wind, water, geothermal and other loaded generator classes
  from their construction recipes and mark placement-/weather-dependent output
  explicitly as a maximum.
- Discover Refined Power modular generators, turbines, heaters, boilers,
  coolers and required platforms without adding a hard Refined Power, KLib or
  Satisfactory Plus dependency.
- Read generator output/RPM, turbine steam demand, allowed heater fuels,
  efficiency multipliers, water/steam/flue-gas descriptors and live unlock
  state from the loaded runtime classes. Retain guarded fallbacks for localized
  boiler-rate descriptions and known Refined Power tiers.
- Build a real modular graph chain: fuel to heater, heat and water to boiler,
  high-pressure steam to turbine, direct rotational coupling to generator,
  electric output to the grid, low-pressure steam to the cooling tower and
  flue gas to the chimney. Nuclear heater waste still terminates at a storage
  endpoint.
- Treat continuous heater by-products independently from fuel-item throughput:
  use a reflected per-minute rate when the loaded class exposes one and guarded
  Refined Power operating points for legacy tick-based heater classes.
- Include heater, boiler, turbine, generator, steam/exhaust coolers and their
  boiler/converter/cooling platforms in machine counts, self-consumption and
  construction-material totals.
- Add the fuel-free operating mode to the UI, show per-set fuel consumption and
  modular configuration details, and prefer stable generation over variable
  renewables when Automatic selects a setup.
- Publish the cumulative update as Mod Manager version `1.0.5` with numeric
  version `1`; retain the stable mod reference `SFPFactoryPlanner`, schema 1–6
  compatibility and SML as the only required dependency.

## 1.0.4 r32

- Keep the initial and double-click graph view at a readable minimum zoom;
  intentionally pan across large factories instead of fitting them into an
  unreadable full-graph thumbnail.
- Increase node size and stage/row spacing substantially, and distribute sparse
  columns across the full height occupied by the busiest branch column.
- Give every logical edge its own visible output and input port on sources,
  machines, routing buildings, and storage endpoints.
- Replace the detached splitter/merger pictogram with colour-matched internal
  wiring generated from the node's actual incoming and outgoing graph edges.
  Each branch remains visible from its producer through the junction to its
  individual consumer.
- Present every final product, by-product, and generator-waste leaf as a visible
  storage endpoint with a dedicated input line, without changing plan schemas
  or persistent completion identities.
- Publish the cumulative update as Mod Manager version `1.0.4` with numeric
  version `1`; keep the stable mod reference `SFPFactoryPlanner` and SML as the
  only required dependency.

## 1.0.3 r31

- Ship one cumulative package containing every r30 runtime-catalog fix and the
  new graph presentation; no earlier patch is required alongside it.
- Right-align every shorter acyclic feeder chain against its first actual
  consumer. Direct water, sand, ore, or other ingredients now appear in the
  column immediately before the machine that consumes them whenever topology
  permits.
- Keep a long connection only for a genuine provider shared by consumers in
  different stages; all ordinary edges remain strictly left to right.
- Draw real splitters and mergers with directional one-to-three/three-to-one
  schematics, coloured edge ports, arrowed connections, and their runtime
  in-game names and exact lane counts.
- Increase horizontal and vertical spacing, use full-width alternating section
  bands, and label resource/start, direct-input, production, routing, and
  final/byproduct sections explicitly.
- Publish the cumulative graph update as Mod Manager version `1.0.3` with
  numeric version `1`.

## 1.0.2 r30

- Discover base-game and modded water-pump subclasses, plus the compatible KLib
  bio-water extractor, from their unlocked construction recipes even when they
  provide no ordinary production recipe.
- Read extraction cycle time, raw fluid amount per cycle, supported resource,
  machine power and live unlock state from each loaded extractor CDO.
- Register the resulting liquid extraction as a real direct-source route, so
  water no longer falls back to a misleading locked packaging or byproduct
  recipe.
- Support the `AB_FluidExtras` Mini Water Extractor at its runtime-defined
  `75 m³/min` and the KLib algae-mass extractor at its runtime-defined rate
  without hardcoding either rate or adding a required dependency.
- Aggregate repeated ingredient or product descriptors inside one recipe before
  calculating rates. This corrects seven exported Satisfactory Plus repaint
  recipes that list the same output twice and previously appeared at half rate
  with duplicate recipe choices.
- Prefer an unlocked compatible factory when one recipe lists several producers,
  and mark a recipe unavailable when its known construction recipe is still
  locked. This prevents a locked A.I. Fluid Packer or refinery from masking an
  already unlocked alternative machine.
- Publish the fix as Mod Manager update `1.0.2` with numeric version `1`.

## 1.0.1 r29

- Compare the cached recipe catalog with the live recipe manager whenever the
  planner is reopened in the same world.
- Rebuild product, recipe, transport, generator and optional modular-miner
  availability only when recipe registration or unlock state actually changed.
- Make newly researched KLib products such as Caterium heatsinks appear with
  the unlocked-only filter after closing and reopening F8, without a save reload.
- Keep KAPI and KLib optional runtime integrations.
- Publish this fix as Mod Manager update `1.0.1` with numeric version `1`,
  matching the SemVer major as required by FICSIT.app.

## 1.0.0 r28

- Publish under the visible name `Pioneer Production Planner` while retaining
  the save- and update-compatible mod reference `SFPFactoryPlanner`.
- Set the release metadata to version `1.0.0`, numeric version `1`, non-beta,
  game build `>=502094`, with only SML `^3.12.0` as a required dependency.
- Rank graph nodes from the actual directed material-flow topology so every
  ordinary producer, routing node and consumer appears from left to right.
- Remove the confusing label lanes beyond the final-product column for normal
  shared flows; reserve reverse routing exclusively for genuine recipe cycles.
- Update the terminal display source and packaged icon to the final public
  branding.

## 0.5.0 r27

- Follow the active game language automatically: German for `de-*`, English
  for English and every unsupported language as a stable fallback.
- Localize all planner-owned Slate controls, graph headings/details,
  status/error messages, summaries, saved legacy text and chat feedback while
  retaining runtime-localized game names.
- Switch thousands and decimal separators together with the language.
- Add a static localization regression test covering both the UI display
  boundary and representative solver/graph output.

## 0.5.0 r26

- Preserve every capacity-derived conveyor/lift or pipe lane from producer to
  consumer and show total throughput plus average load/capacity per line.
- Never create a merger solely because one logical node represents multiple
  physical machines; grouped machines already expose their required parallel
  lanes.
- Create splitters/mergers only at real multi-endpoint topology changes where
  the required input and output line counts differ.
- Keep a 350/min Mk.2 connection as three lines end to end, without inserting
  an impossible one-line bottleneck.

## 0.5.0 r25

- Add the missing drill-only modular-miner extraction recipes for raw resources.
- Model a normal tier-1 node as two 30/min belt outputs, or 60/min per miner.
- Show 416.25/min as 6.9375 equivalent miners: seven physical miners and seven
  normal nodes, with the last miner at 93.75%, instead of one aggregated
  external-source node.
- Treat direct self-extraction as a terminal resource link rather than a recipe
  cycle, prefer the normal-node variant for automatic selection, and retain the
  r20 direct-processing, r24 routing and Vanilla/SML paths unchanged.

## 0.5.0 r24

- Distinguish transport capacity lanes from actual graph endpoints when
  constructing routing infrastructure.
- Keep a single 350/min connection on three Mk.2 belts as three parallel belts
  instead of fabricating a three-input fusionator followed by a splitter.
- Create a splitter only for multiple distinct consumers and a merger only for
  multiple distinct producers; retain `RequiredLines` for transport counts and
  costs.
- Remove the corresponding impossible bottleneck and redundant infrastructure
  costs while preserving r23 graph layout, node completion and schema 1–6
  compatibility.

## 0.5.0 r23

- Rebuild the production graph as labelled left-to-right columns for external
  resources, production stages and final products.
- Order nodes by connected-neighbour barycentres to reduce crossings without
  changing solver output or persisted node identities.
- Give every incoming and outgoing connection an individual sorted node port,
  then route labels through non-overlapping lanes in the corresponding stage
  gap instead of stacking every line at the node centre.
- Keep a deterministic colour per solid item and dedicated colours for power,
  liquids, gases and local modular-miner links; add a compact legend and graph
  navigation help.
- Retain completion checkboxes, completed-state carry-forward and schema-6 plan
  compatibility unchanged.

## 0.5.0 r22

- Fix the reproduced F8 startup assertion `Array index out of bounds: 0 into an
  array of size 0` from modded runtime recipe catalogs.
- Stop calling FactoryGame's unsafe `GetBuildableClassFromRecipe`, which
  unconditionally indexes the first recipe product. Building recipes are now
  resolved only after validating the recipe, every product descriptor and the
  resulting buildable class.
- Apply the same guarded resolver to power generators, modular miners,
  construction costs and transport tiers, so empty, null or non-building
  products from any loaded content mod are skipped without warnings or crashes.

## 0.5.0 r21

- Add a dedicated power-supply tab with requested net MW, safety reserve, and
  automatic or explicit generator/fuel selection.
- Discover fuel generators, supported fuels, supplemental resources, energy
  values and unlock state from the active runtime catalog; modded generators
  remain optional and require no new hard plugin dependency.
- Calculate whole generator count and final underclocked unit, complete upstream
  fuel/cooling chains, by-products and nuclear waste, gross production, chain
  self-consumption, real net output and remaining reserve.
- Add schema-6 persistence and graph rendering for power plans while retaining
  schema-1 through schema-5 compatibility.
- Defer F8 open/close handling to the next game tick so viewport widgets are not
  inserted or removed while Slate is dispatching the key event.

## 0.5.0 r20

- Detect KAPI modular-miner descriptions and KLib miner/module buildables at
  runtime without adding either plugin as a hard dependency.
- Add direct-ingot modular-miner alternatives for impure, normal and pure
  resource nodes, including drill tier, waste/smelter module, both belt outputs,
  exact KLib cycle scaling and the resulting waste by-product stream.
- Show the attached resource node as a local graph input instead of inventing
  an ore conveyor, and include miner plus attached modules in construction costs.
- Preserve Vanilla/SML behavior when KAPI/KLib are absent.

## 0.5.0 r19

- Fix UE 5.6 Linux/server compilation of the node-completion status message by
  using literal checked format strings in both branches.
- Refresh installed source timestamps so UnrealBuildTool cannot package a stale
  r17/r18 module DLL.

## 0.5.0

- r18 adds an interactive completion checkbox to every production-graph node.
  Completed nodes use a clear green state, the graph and planner status show
  completed/total progress, and every toggle is written to the automatic
  current-plan snapshot. Named plans persist the same state when saved.
- Plan persistence schema 5 stores the completion flag per node while schema
  1–4 plans continue to load with all nodes open. Matching completion states
  are carried forward when rates, transport tiers or recipes are recalculated.
- r17 removes the remaining unsupported `Texture2D.post_edit_change()` call.
  Normal maps, ORM masks and sRGB states are now verified and saved directly
  through `EditorAssetLibrary`, so incorrect default texture compression can no
  longer silently survive the import.
- The importer no longer recursively saves and validates the complete terminal
  tree, which had grown to more than one hundred assets after retries and made
  the editor look frozen. It saves only touched assets, emits visible progress
  messages and disables Nanite in the staging import data before mesh creation.
- r16 removes the unsupported `StaticMesh.post_edit_change()` calls that stopped
  the UE 5.6 Linux editor after the r15 slot transfer had succeeded. Mesh changes
  are now persisted directly with `EditorAssetLibrary.save_loaded_asset`; each
  retry uses a new `PBR/Generated_r16_XX` staging folder and ignores partial r15
  assets.
- r15 fixes UE 5.6 retaining only five material slots when the premium OBJ was
  imported directly over the stable mesh. The importer now creates fresh source
  meshes in a unique `PBR/Generated_r15_XX` staging folder, copies LOD 0 through
  `StaticMeshEditorSubsystem`, rebuilds all eight named slots and explicitly
  remaps every mesh section. The stable Blueprint and save-game asset IDs remain
  unchanged.
- The staging MTL files now use only portable Wavefront directives, preventing
  the importer from dropping the Metal, ScreenGlow or ScreenUI identities.
- r14 fixes the Linux Unreal Editor assertion `!IsRooted()` triggered by
  `DeleteAllMaterialExpressions`. The importer never deletes or clears material
  expressions now. Every run builds a fresh material set below a unique
  `PBR/Generated_r14_XX` folder and leaves incomplete earlier imports untouched.
- The stable packaged screen material is no longer rebuilt through Python; its
  referenced 2K texture is replaced safely in place.
- r13 completely rebuilds the floor terminal presentation: 102 authored parts,
  fully bevelled hard-surface panels, structural rails, service hardware,
  controls, realistic feet and a recessed 2K screen replace the toy-like glossy
  silhouette while retaining the proven footprint and asset identity.
- Added six complete 1K PBR texture sets (Base Color, tangent Normal and packed
  AO/Roughness/Metallic), physically plausible powder coat, brushed metal and
  rubber values, and a deterministic Unreal Editor setup script. The script
  imports/reimports every source, creates the material graphs, disables Nanite,
  rebuilds blocking main collision and keeps the overlay collision-free.
- Added a dedicated square terminal icon, wired it as the preferred HUB/build-
  menu image with the packaged screen as a fallback, and added professional
  concept/model previews for release review.
- r12 renames the visible mod to `Pioneer Factory Planner` while preserving the
  technical mod reference `SFPFactoryPlanner` for update and save compatibility.
- r12 explicitly supports both vanilla/SML and Satisfactory Plus: all recipes,
  machines and transport tiers come from the active runtime recipe manager;
  Satisfactory Plus remains optional.
- Added separate selectors for conveyor belts and conveyor lifts. Their build
  recipes and unlock states are read from the active save, locked tiers are
  identified in the UI, and the slower selected tier sets each solid line's
  capacity.
- Added schema-4 persistence for the unlock filter and selected belt/lift tiers.
  Older schema-1/2/3 plans continue to load and choose a valid runtime fallback.
- Added combined multi-product production sites: each end product has its own
  rate, shared intermediate demands are merged into one graph, and raw-input
  budget scaling preserves the selected output mix.
- Added schema-3 persistence for multiple end products with automatic
  reconstruction of schema-1/2 single-product plans.
- Replaced fractional-machine wording by buildable whole-machine counts and
  explicit clocking such as `4 × 100% + 1 × 16.7%` in the graph and machine tab.
- Routing nodes now use the buildable names loaded from the active game data
  (including Satisfactory Plus splitters/fusionators) instead of invented
  distributor/collector labels.
- Rebuilt the planner window as four switchable full-area tabs for planning,
  machines, resources/costs, and the production graph. Added a dark industrial
  Satisfactory-inspired orange/yellow/cyan theme and a full-width graph view.
- Added the r10 high-detail terminal source model with a chamfered base,
  tapered service cabinet, vents, control deck, layered monitor bezel, guards,
  fasteners and rear service details while preserving the existing footprint,
  origin and all eight Unreal material slots.
- Replaced the white HUB/build-menu fallback tiles at runtime by assigning the
  packaged planner texture to the descriptor's small/big icons and both
  schematic icon paths.
- Fixed an Unreal unity-build collision between the terminal world-module and
  hologram reflection helpers by moving them into distinct named namespaces.
- Added a dedicated crash-safe `AFGBuildableHologram` for the terminal. It
  blocks FactoryGame instance/color/customization conversion on the imported
  meshes, selects FactoryGame's simplified hologram material path, forces
  non-Nanite rendering, hides the decorative screen overlay only
  while placing, and uses the engine default surface material for the placement
  body. The installed Blueprint CDO is patched automatically at runtime.
- Replaced the provisional cube/HUB/coupon-shop experiments with a native
  `FGBuildable` and an original compact SFP floor-console mesh.
- Added a separate emissive screen overlay showing the SFP Factory Planner UI.
- Added blocking query/physics collision on the main mesh, `NoCollision` on the
  visual screen, and the usable flag required for normal interaction and Build
  Gun dismantling, without an EasyCheat dependency.
- Removed the unreliable Unreal-Python terminal repair; completed assets are
  installed and validated directly.
- Disabled Nanite in both low-poly terminal meshes and disallowed it on their
  Blueprint components. This removed the missing-Nanite shader warnings; r8's
  dedicated placement hologram addresses the remaining D3D12 PSO `80070057`.
- Corrected the startup log version from 0.4.2 to 0.5.0 and added a runtime-log
  diagnostic for the known Nanite/PSO crash signature.
- Added manual alternative-recipe selection for every produced item in the active plan.
- Persisted recipe overrides in named and automatic plans while retaining schema-1 compatibility.
- Persisted entered external-input budgets and the connection-length estimate with each plan.
- Added explicit splitter/merger and pipe distributor/collector graph nodes with directional arrows.
- Added automatic graph fit/centering with a wider zoom range for large plans.
- Added configurable average connection length, required transport lines, estimated conveyor/pipe metres, and infrastructure counts.
- Split construction costs into production machines, estimated infrastructure, and combined totals.
- Kept all calculations event-driven and retained cached layout/text, off-screen culling, and zoom-dependent detail rendering.
- Added an early diagnostic for mixed Unreal Engine roots in Win64 Alpakit logs.

## 0.4.2

- Added the complete buildable Factory Planner Terminal content assets.
- Added a HUB/progression building descriptor and Build Gun recipe costing 10 Iron Plates and 20 Cable.
- Added a Tier 1 milestone with a valid icon and public-build inclusion that unlocks the terminal recipe.
- Registered the terminal schematic from the native root game-world module so the recipe is discoverable and cooked reliably.
- Kept F8 as a zero-cost fallback when the terminal is unavailable.

## 0.4.1

- Added compact German number formatting with decimal commas, thousands dots, and trimmed trailing zeroes.
- Increased graph column spacing and added three-line backed path labels so connection descriptions remain readable.
- Added a grouped list of whole machines required by the calculated recipe branches.
- Added summed build-material costs for all production machines discovered from runtime construction recipes.
- Added an all-input budget mode whose limiting external resource determines the maximum reachable output.
- Kept all new graph and summary work event-driven to avoid permanent Slate tick or per-frame allocations.

## 0.4.0

- Added multiple named production plans with save, overwrite, load, and delete controls.
- Kept the automatic last-plan snapshot independently for instant reopening.
- Added safe filenames, plan-name validation, and corrupted-file isolation.
- Cached graph layout and display text instead of recreating it every frame.
- Culled off-screen nodes and edges to reduce Slate draw work on large plans.
- Added solve-duration telemetry to the planner status.
- Documented the prepared Blueprint bridge and required assets for a buildable planner terminal.

## 0.3.0

- Added automatic JSON persistence for the active production graph.
- Restored the last plan when reopening the planner or restarting the game.
- Added explicit save and delete buttons for the persisted plan.
- Deferred mouse cursor and UI focus activation until after the chat command closes.

## 0.2.2

- Cached the runtime recipe catalog for instant reopen and hitch-free close.
- Restored game input, mouse state, and viewport focus explicitly when closing the planner.
- Switched the overlay to game-and-UI input mode to avoid a stuck UI-only state.
- Added catalog initialization timing to the game log.

## 0.2.1

- Fixed Unreal unity-build collisions between internal solver and exporter helper functions.

## 0.2.0

- Added `/sfpplanner open` and `/sfpplanner close`.
- Added a native full-screen Slate calculator UI.
- Added localized product search, target-rate input, and unlock filtering.
- Added recursive recipe solving with raw-source and cycle handling.
- Added machine count, uniform clock, base-power, and by-product calculation.
- Added a zoomable/pannable directed production graph.
- Added runtime-derived conveyor and pipeline tier recommendations.
- Extended runtime export schema to 1.1 with machine, connector, and transport metadata.
- Preserved `/sfpplanner export` and its per-entry crash-safety checks.

## 0.1.0

- Added `/sfpplanner export` in-game chat command.
- Added crash-safe runtime recipe, item, producer, and mod metadata export.
- Added structured warnings for invalid recipe and item references.
- Added exact raw quantities plus normalized fluid/gas quantities and rates.
- Added Blueprint-callable exporter API for the upcoming planner UI.

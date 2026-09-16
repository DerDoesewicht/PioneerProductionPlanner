# Pioneer Production Planner 1.5.1

## Power-planning fixes

- Fixed power-plan persistence so a newly selected generator or fuel can no longer be written into an older calculated result.
- Enforces that a successful power result uses the exact manually selected generator and fuel.
- Marks pending power settings and failed recalculations directly in the grid summary while retaining the last successful result for reference.
- Preserves the concrete solver error when a selected generator/fuel chain cannot be completed.
- Extends self-consumption convergence for very large and power-intensive chains such as Ficsonium Fuel Rod production.
- Accepts a power result only after the requested net output including its configured reserve is actually reached.
- Stabilizes large material-balance objectives without changing recipe quantities or source constraints.
- Recipe output and grouped raw-resource names now resolve from the current runtime language instead of retaining cached German labels in English games.
- Clarifies that using the current factory demand replaces the manual net-power target.

## Included from 1.5.0 – World-aware resource planning

- Reads standard resource nodes and placed extractors from the authoritative loaded world.
- Shows total, occupied and free counts for impure, normal and pure nodes.
- Uses currently free nodes as automatic limits for ordinary factory plans and power/fuel plans.
- Adds an option to include already occupied sources when existing extraction should be shared.
- Keeps explicit per-purity limits as manual overrides; `0` still excludes that purity.
- Shows required, available and missing source counts in extraction settings.
- Transfers the server inventory to the owning multiplayer client instead of relying on client-side actor visibility.
- Excludes portable miners, water extractors and resource-well/fracking actors from standard-node occupancy matching.

## Included from 1.4.5-r8 – Automatic Purity Selection Fix

- Automatic purity allocation now runs without requiring a previously saved source mix.
- The selected representative purity no longer limits automatic allocation to that purity.
- Hides the manual **Purity** selector in normal and grouped extraction cards while automatic allocation is enabled.
- Disabling automatic allocation restores the manual selector as a fallback.
- Applies to ordinary production plans and power/fuel production plans.

## Included from 1.4.5-r7 – Automatic Resource Source Mix

- Automatically calculates the required pure, normal and impure resource-node mix.
- Uses pure nodes first, followed by normal and impure nodes, minimizing the number of required sources by default.
- Shows **Required** counts per purity in the extraction settings.
- Adds an independent availability limit for each purity; setting a limit to `0` excludes it.
- Recalculating redistributes unmet extraction demand across the remaining allowed purities.
- Applies to normal production plans, power/fuel plans, Vanilla ores, Crude Oil and compatible Satisfactory Plus extraction routes.
- Existing r4-r6 saved source counts retain their previous hard-cap meaning.

## Included from 1.4.5-r6 – Power Plan Editing

- Active power and fuel-production plans can now be recalculated directly from the Planning tab.
- Recipe, extraction and machine changes made while viewing a power plan are passed back into the power solver.
- The Planning-tab action changes to **Recalculate Power & Fuel Production** while a power plan is active.
- No unrelated final product is required for this workflow.
- Explicitly adding a final-product target still switches the action back to normal multi-product planning.

## Included from 1.4.5-r5 – Clear Product Selection

- The final-product catalog now starts without an automatically highlighted first entry.
- Added **Clear Selection** to remove the current product highlight explicitly.
- Calculating a production plan no longer silently adds a highlighted catalog item; products must be confirmed with **+ Add Target**.
- Loading or calculating a power-production plan no longer retains an unrelated default product selection.
- Added matching German and English UI text.

## Included from 1.4.5-r4 – Mixed Resource Sources

- Added per-resource counts for impure, normal and pure deposits.
- Applies mixed-source planning to Vanilla miners, Satisfactory Plus ores, modular-miner processing routes and Crude Oil extractors.
- Enforces the combined source capacity instead of silently creating additional deposits.
- Shows separate purity groups and used/available source counts in the graph.
- Reports an external shortfall together with alternative additional pure, normal or impure deposit counts.
- Saves the source mix in personal and multiplayer plans and restores it when a plan is loaded.

## Included from 1.4.5-r3

- Registers the vanilla Oil Extractor as a direct Crude Oil source.
- Adds impure, normal and pure oil-node routes using runtime extractor rates.
- Shows Oil Extractor counts and extraction power in the plan and graph.
- Keeps manual MW fuel planning active without a calculated factory plan.
- Includes the r2 checked-format compilation fix.

- Adds a power-target mode that reuses the current factory plan's calculated power demand.
- Keeps manual net-power targets available.
- Makes complete production planning for the selected generator fuel explicit in the Power Supply tab.
- Iteratively includes fuel-production self-consumption and the configured safety reserve.
- Adds matching German and English UI text.

# Pioneer Production Planner 1.4.1

- Resolves production-graph route names from current runtime item descriptors, including previously saved S+ plans.
- Keeps graph headings, input/output labels and navigation help synchronized with the active game language.
- Validates BurnerManufacturer fuel demand against the Burner Smelter's in-game supported-fuel rates.

- Releases the cumulative machine configuration, clocking, Somersloop, BurnerManufacturer, generator-overclock and Alien Power Augmenter planning features.
- Releases the three-column planning workflow and personal/server-synchronized multiplayer plans.
- Releases robust Vanilla miner runtime discovery and per-resource separation from S+ Modular Miner routes.
- Supports Mining-Head-only raw S+ extraction and groups ore/miner controls into the immediate processing card for all S+ ores.
- Includes all production-graph improvements from the 1.3.x test series.

## Feedback Loop Graph

- Routes connections returning to the same or an earlier stage through dedicated lanes above the production graph.
- Marks return lanes and labels in gold while retaining each material's route colour.
- Adds explicit **↩ RÜCKFÜHRUNG** / **↩ RETURN FLOW** labels and backward direction markers.
- Keeps multiple feedback loops separated and avoids the former far-right detour.
- Uses topology only, so Vanilla, Satisfactory Plus and compatible mod recipes work automatically.
- Does not change solver values, machine counts, material balances or saved-plan data.

# Pioneer Production Planner 1.3.1-r13 – Miner Catalog Separation

- Prevents Vanilla Miner Mk.1/Mk.2/Mk.3 from appearing beside the Modular Miner for the same modular-system resource.
- Detects ownership from an actual Modular Miner raw route instead of the resource descriptor path, because Satisfactory Plus can reuse and rename Vanilla descriptors.
- Keeps Vanilla miners available normally in games without matching modular raw routes.
- Groups the raw ore/miner controls into the immediate processing card for every S+ ore; this is topology-based, not Siderite-specific.
- Leaves shared raw sources as separate cards when they feed more than one processing node.

# Pioneer Production Planner 1.3.1-r12 – Raw Mining Head-Only Routes

- Restores Satisfactory Plus raw-resource routes using only a compatible Mining Head.
- Applies `mNeededModules` only to processed modular-miner outputs.
- Keeps optional Fluid and Booster combinations available without making them mandatory.
- Automatic planning now prefers standard recipes over Crush/Smelter processing inside the miner; those direct routes remain available by explicit selection.

# Pioneer Production Planner 1.3.1-r11 – Vanilla Miner Filter Bypass

- Keeps the seven extractor candidates discovered by r10 and fixes the later filter stage that produced zero standard-miner routes.
- Bypasses generic form, node-type and allowlist restrictions only for the exact Vanilla Miner Mk.1/Mk.2/Mk.3 classes.
- Keeps generic restrictions for water, oil and modded extractors.
- Adds per-miner filter and route-count diagnostics to `/sfpplanner export`.

# Pioneer Production Planner 1.3.1-r10 – Standard Miner Runtime Discovery

- Resolves standard miners from their live construction-recipe descriptors instead of relying on guessed canonical buildable paths.
- Preserves the exact `MinerMK1` / `MinerMk2` / `MinerMk3` package capitalization required on Linux.
- Loads unresolved derived miner classes through the correct resource-extractor base class.
- Adds standard-miner discovery diagnostics to `/sfpplanner export`.
- Retains every r8 and earlier 1.3.1 fix.

# Pioneer Production Planner 1.3.1-r8 – Miner Fuel Routes Fix

- Restores robust Vanilla Miner Mk.1/Mk.2/Mk.3 direct extraction discovery.
- Prevents raw-resource Conversion routes from replacing the normal miner path when basic extraction is available.
- Preserves real Turbofuel/Rocket Fuel production chains and blocks automatic Unpackage loops.
- Adds English `AUTO LAYOUT` localization and a third generator graph status row.

# Pioneer Production Planner 1.3.1-r7 – Miner Restore & Graph Footer

- Fixed missing vanilla Miner Mk.1/Mk.2/Mk.3 extraction routes for standard raw resources.
- Fixed empty/absent miner allow-list handling.
- Direct extraction now sorts before recipe/conversion routes.
- Graph machine cards now display power/fuel and clock/Somersloop/boost on separate footer rows.
- Retains r6 three-column planning and all earlier 1.3.1/1.3.0 fixes.

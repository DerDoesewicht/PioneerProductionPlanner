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

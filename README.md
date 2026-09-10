# Pioneer Production Planner

> Plan production chains and power systems directly inside Satisfactory — from desired products or the resources you already have.

[![Production planning](screenshots/1-Planung.png)](screenshots/1-Planung.png)

Pioneer Production Planner turns the recipes, machines, unlocks and transport tiers available in your loaded save into a production plan. Calculate several final products together, choose how resources are obtained, inspect machine counts and clock speeds, and follow the complete chain in an interactive graph.

Vanilla is supported. Satisfactory Plus, KAPI/KLib and Industrial Evolution are optional integrations, not required dependencies.

## New in 1.2.0

- **Plan from available inputs.** Enter your available resource rates and calculate achievable final-product rates for the selected recipe chains.
- **Adjust the result.** Set desired rates for individual products and recalculate the remaining free outputs within your input limits.
- **Industrial Evolution machines.** Select compatible Mk.2 and Mk.3 production machines under **More settings → Production machines**, directly below conveyor belt and lift settings.
- **More compact planning.** Additional settings are grouped together, with machine selections shared by steps that offer the same compatible variants.
- **Clear guidance.** English and German instructions explain when to calculate, choose machines, recalculate and save.
- **Recipe-dependent power.** Variable-power machines use runtime recipe data and power curves. Recipe ranges and approximate cycle averages help distinguish average demand from peaks.
- **Clearer power labels.** “Power demand at planned clock speeds” replaces the ambiguous “base power” label.

## Two ways to plan

### Start with final products

Choose one or more products and their target rates per minute. The planner calculates the required production chain, shared intermediates, raw inputs and machines.

### Start with available inputs

Enable **Plan from available inputs**, enter your supply rates, and choose the outputs you want. For example, two Siterite Ore supplies at 150/min give a total budget of 300/min for an iron-products plan in Satisfactory Plus.

Free outputs are increased together. You can set individual output rates as fixed wishes and recalculate what remains achievable. The result depends on your selected recipes and available inputs; this mode does not search every possible recipe combination for a global optimum.

## Recipes, extraction and machine selection

Choose the supply method for each relevant product: manufacturing recipes, conversion or direct extraction where available. For extraction, check the resource source, miner, purity, modules/drill head and operating fluid against your actual factory.

For production machine variants:

1. Choose your final products or available inputs.
2. Calculate the plan once.
3. Open **More settings → Production machines** and select the desired variant.
4. Recalculate, then save the plan.

The selectors apply to compatible steps in the current plan. With the unlock filter enabled, only unlocked options are offered. Processing modules inside a Modular Miner remain part of resource extraction settings.

A faster machine does not necessarily reduce total power demand: if production speed and power rise by the same factor, fewer machines can produce the same output at the same total consumption.

## Features

- Multiple final products with separate target rates.
- Shared intermediate production and by-product accounting.
- Automatic recipe selection and manual overrides.
- Whole machines to build, with clock-speed allocation.
- Separate conveyor belt and lift tiers.
- Continuous parallel transport routes and routing buildings where needed.
- Resource demand, input limits and estimated construction costs.
- Net power planning, including fuel chains, self-consumption and reserve settings.
- Zoomable production graph with material/rate labels and completion checkboxes.
- Named personal plans and shared community plans on supported multiplayer setups.
- English and German interface, following the game language.

## Screenshots

### Production graph

[![Production graph overview](screenshots/Graphenansicht-1.png)](screenshots/Graphenansicht-1.png)

[![Machine and route details](screenshots/Graphenansicht-2.png)](screenshots/Graphenansicht-2.png)

### Machines

[![Machine overview](screenshots/Uebersicht.png)](screenshots/Uebersicht.png)

### Resources and construction costs

[![Resources and costs](screenshots/Resources.png)](screenshots/Resources.png)

### Power planning

[![Power planning](screenshots/Stromrechner.png)](screenshots/Stromrechner.png)

### In-game terminal

[![Planner terminal](screenshots/Modterminal.png)](screenshots/Modterminal.png)

Screenshots may show an earlier interface revision or optional mod content.

## Getting started

1. Install Pioneer Production Planner through Satisfactory Mod Manager.
2. Load your save.
3. Press **F8** by default, or use the planner terminal.

The hotkey can be changed or disabled in the planner header. To use the terminal, complete the **Factory Planner Terminal** milestone in HUB Tier 1, build it for **10 Iron Plates and 20 Cable**, and interact using the normal Use key.

```text
/sfpplanner open
/sfpplanner close
/sfpplanner export
/sfpplanner hotkey F9
/sfpplanner hotkey off
/sfpplanner hotkey reset
```

## Compatibility

| Component | Support |
| --- | --- |
| Satisfactory | Game build `>=502094` |
| Satisfactory Mod Loader | `^3.12.0` required |
| Vanilla | Supported |
| Compatible content mods | Runtime discovery |
| Satisfactory Plus | Optional |
| KAPI / KLib Modular Miner | Optional |
| Industrial Evolution | Optional compatible Mk.2/Mk.3 production machines |
| Interface | English and German |

Optional content appears only when its mod and required runtime data are available. This does not guarantee compatibility with every machine or recipe from every mod.

## Planning notes

The graph is a logical production plan, not a three-dimensional blueprint. Transport lengths and construction costs depend on the selected average connection length and remain estimates.

For variable-power recipes, the power total uses approximate cycle averages. Instantaneous peaks can be higher; allow for them when sizing your power supply.

After updating to 1.2.0, recalculate existing plans and save them to refresh machine data and graph labels.

## Support and bug reports

Report reproducible bugs and feature requests through [GitHub Issues](https://github.com/DerDoesewicht/PioneerProductionPlanner/issues).

Please include:

- Game build, SML version and planner version.
- Installed content mods.
- Reproduction steps and screenshots.
- Relevant game/build log or crash report.
- A runtime export from `/sfpplanner export` for missing recipes, machines or resource sources.

Discord: `derdoesewicht`

## Mod identity

- Public name: **Pioneer Production Planner**
- Technical mod reference: `SFPFactoryPlanner`

This project is not affiliated with or endorsed by Coffee Stain Studios.

<img src="assets/planner-logo.png" alt="Pioneer Production Planner logo" width="128">

[Deutsch](README.de.md)

# Pioneer Production Planner

**Plan your factory. Follow the flow. Build it in Satisfactory.**

Pioneer Production Planner brings production chains, power planning and a personal build checklist into the game. Set your targets, explore the graph and turn useful by-products into the next production step.

**Version 1.7.3 · German & English interface · Mod reference: `SFPFactoryPlanner`**

![The redesigned planning interface with multiple production targets](screenshots/1.7.3/planning.png)

## New in 1.7.3

- Redesigned interface with a fixed left navigation, consistent controls and a new logo.
- Change recipes directly from graph machine cards. Inspect ingredients, products and by-products per minute before applying the choice and recalculating.
- A continuous left-to-right graph with standard or open spacing. Use **Readable** to return to the start at a useful zoom, or **Fit Graph** for the whole factory.
- Saved personal views and spacing. Recipe changes preserve zoom and focus where possible; a failed calculation keeps the previous plan.

## Plan around your goals

Plan several products together or work from available resource limits. Review machine counts, clock speeds, power demand and construction materials. Select alternative recipes, use the unlock filter and adjust production amplification where supported.

Transport settings include belts, lifts and pipelines, with compatible tiers discovered from the running game. Transport figures are planning estimates, not a measurement of routes you have built.

![Resources, construction costs and transport estimates](screenshots/1.7.3/resources.png)

## Understand every production step

Follow material flows from left to right, zoom into machine cards and arrange the view to suit your factory. Recipe previews show rates for the selected step's current demand, including the machine count and clock speed.

Applying a recipe creates a manual override for that product throughout the plan and recalculates the production chain.

![Left-to-right overview of a production graph](screenshots/1.7.3/graph-overview.png)

![Detailed machine card with material rates, machine count and power](screenshots/1.7.3/graph-detail.png)

## Give by-products a purpose

Select a by-product in the graph or open the by-products section to explore compatible processing recipes. Preview a suggestion, then add a production branch. Compatible fuel options can also be used for a power branch.

Proposals and added processing branches are visually separated so you can distinguish the original production chain from its extensions.

![Clickable processing suggestion beside a by-product](screenshots/1.7.3/byproduct-suggestions.png)

![Added by-product processing branch beside the main graph](screenshots/1.7.3/byproduct-processing.png)

Added branches are changed through the by-product selection; their graph recipe dialog is for inspection. **Recalculating the main plan replaces appended branches.** Named saved plans preserve the branches they contain.

## Plan power alongside production

Choose generators and fuels, set the desired net output and reserve, or use the factory's calculated demand. Include the fuel production chain and its own consumption, and calculate the maximum possible net power for the selected constraints.

![Power planning with generator, fuel and net output settings](screenshots/1.7.3/power-planning.png)

## Build at your own pace

The personal build list tracks partial progress and provides manual corrections, completion controls and an optional transparent HUD. HUD pages switch manually, so you decide which part stays visible.

Load or calculate a plan whenever you like. **Start the build project at the actual construction site** to capture the existing machines as a baseline. Automatic tracking then counts matching new machines within your selected area; existing machines can be included explicitly. Demolition and manual corrections let you adjust progress as the site changes.

Build progress belongs to each player, including when a plan design is shared. Machine detection counts buildings; clock and amplification settings remain instructions to configure yourself.

![Personal build list with area detection and manual progress controls](screenshots/1.7.3/build-list.png)

## Getting started

1. Install Pioneer Production Planner through Satisfactory Mod Manager.
2. Open the planner with your configured shortcut or enter `/sfpplanner open` in chat. Use `/sfpplanner hotkey reset` if you need to restore the default shortcut.
3. Choose your products and rates, check recipes and settings, then calculate.
4. Inspect the graph, resources and power needs. Save the plan when you are ready.
5. At your build site, start the build project and enable the HUD if wanted.

**New Project** resets the current working plan so you can start again. Named saved plans and shared plans remain available.

## Compatibility and support

Recipes and compatible machine data are read from the running game. The screenshots include modded content, including Satisfactory Plus; those items are examples and are not required for every plan. Support depends on the data exposed by each mod, and mod-provided item names may retain their original language.

For multiplayer, clients and server must all use **1.7.3**. Plan designs can be shared while personal views and build progress stay with each player.

Found a problem? [Open an issue](https://github.com/DerDoesewicht/PioneerProductionPlanner/issues) with the mod version, installed mods, reproduction steps and a screenshot or log where useful.

## Development

Development used AI assistance. The new logo was generated with AI. The screenshots show the in-game interface.

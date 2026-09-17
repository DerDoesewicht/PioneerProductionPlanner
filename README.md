# Pioneer Production Planner 1.5.2

In-game production and net-power planner for Vanilla Satisfactory, Satisfactory Plus and compatible recipe mods.

Version 1.5.2 adds a world-resource-limited maximum net-power calculation for the selected generator, fuel and extraction configuration. The search now starts at a physically buildable generator scale instead of a hard-coded 0.1 MW probe, rejects zero-generator results and can bracket downward when the current target already exceeds the available world resources.

The generator/fuel persistence and large self-powered-chain fixes from 1.5.1 remain included. Ficsonium and other power-intensive chains continue their self-consumption calculation until the requested net output including reserve is reached.

World-aware resource planning and explicit fuel-production planning from 1.5.0 remain included. Select Turbofuel, Ficsonium Fuel Rod or another compatible generator fuel, then calculate the complete production chain either for a manual net-power target or for the demand of the current factory plan.

Vanilla Oil Extractors are included as direct Crude Oil sources with impure, normal and pure node rates. Their required building count and extraction power are part of the resulting plan.

The planner reads standard resource nodes and placed extractors from the authoritative loaded world. It shows total, occupied and free counts per purity, uses free nodes by default, and lets you include occupied sources or override individual purity limits before recalculating.

The production graph uses material-specific colours, visible ports, routing buildings, storage endpoints and dedicated **↩ RETURN FLOW** lanes above the production stages for refinery and mod-production feedback loops.

Graph route names and headings now follow the active game language, including existing saved plans whose item labels originated from S+ runtime data.

Client and server must use exactly version **1.5.2** for multiplayer plans.

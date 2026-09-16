# Pioneer Production Planner 1.5.1

In-game production and net-power planner for Vanilla Satisfactory, Satisfactory Plus and compatible recipe mods.

Version 1.5.1 fixes generator/fuel selection persistence and large self-powered fuel chains. Ficsonium and other power-intensive production chains now continue their self-consumption calculation until the requested net output including reserve is actually reached. Detailed failures remain visible without replacing the last successful result.

World-aware resource planning and explicit fuel-production planning from 1.5.0 remain included. Select Turbofuel, Ficsonium Fuel Rod or another compatible generator fuel, then calculate the complete production chain either for a manual net-power target or for the demand of the current factory plan.

Vanilla Oil Extractors are included as direct Crude Oil sources with impure, normal and pure node rates. Their required building count and extraction power are part of the resulting plan.

The planner reads standard resource nodes and placed extractors from the authoritative loaded world. It shows total, occupied and free counts per purity, uses free nodes by default, and lets you include occupied sources or override individual purity limits before recalculating.

The production graph uses material-specific colours, visible ports, routing buildings, storage endpoints and dedicated **↩ RETURN FLOW** lanes above the production stages for refinery and mod-production feedback loops.

Graph route names and headings now follow the active game language, including existing saved plans whose item labels originated from S+ runtime data.

Client and server must use exactly version **1.5.1** for multiplayer plans.

#pragma once
#include <algorithm>
#include <cmath>

// KLib KLMMBuildableMiner: node cycle 2 / (purity + positive module buff);
// fluid task is a separate one-second task, raw inventory fluid units / 1000.
namespace SFPMinerRates
{

template<class Loadout, class Requirements, class Matches>
bool RequiredPresent(const Loadout& installed, const Requirements& required, Matches matches)
{
    for (const auto& requirement : required)
    {
        bool found = false;
        for (const auto& module : installed) if (matches(module, requirement)) { found = true; break; }
        if (!found) return false;
    }
    return true;
}
inline double Cycle(double purity, double bonus, double wasteMultiplier)
{
    return 2.0 / std::max(0.5, purity + (bonus > 0.0 ? bonus * wasteMultiplier : 0.0));
}
inline double Output(double amount, double cycle, int outputs = 2) { return amount * 60.0 * outputs / cycle; }
inline double OperatingFluid(double rawPerSecond, double purity) { return rawPerSecond * purity * 0.06; }
inline double Waste(double amount, double cycle) { return amount * 120.0 / (cycle * 3.0); }
}

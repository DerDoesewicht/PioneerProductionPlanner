#pragma once
#include <cmath>
namespace SFPStandardMinerRates
{
inline bool Supports(bool Solid, bool Restricted, bool Listed)
{
    return Solid && (!Restricted || Listed);
}
inline double Rate(double Amount, double CycleSeconds, int Purity)
{
    if (!std::isfinite(Amount) || !std::isfinite(CycleSeconds) || Amount <= 0 || CycleSeconds <= 0 || Purity < 0 || Purity > 2) { return 0; }
    const double Multipliers[] = {0.5, 1.0, 2.0};
    const double Result = Amount * 60.0 / CycleSeconds * Multipliers[Purity];
    return std::isfinite(Result) ? Result : 0;
}
}

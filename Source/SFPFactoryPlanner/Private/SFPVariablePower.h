#pragma once
#include <algorithm>
#include <cmath>

namespace SFPVariablePower
{
struct Range { double Minimum = 0, Maximum = 0, Mean = 0; };
inline bool RecipeRange(double Constant, double Factor, double CurveMin,
    double CurveMax, double CurveMean, Range& Out)
{
    if (!std::isfinite(Constant) || !std::isfinite(Factor)
        || !std::isfinite(CurveMin) || !std::isfinite(CurveMax)
        || !std::isfinite(CurveMean) || CurveMin > CurveMax) return false;
    double A = Constant + Factor * CurveMin;
    double B = Constant + Factor * CurveMax;
    Out = {std::min(A, B), std::max(A, B), Constant + Factor * CurveMean};
    return std::isfinite(Out.Minimum) && std::isfinite(Out.Maximum)
        && std::isfinite(Out.Mean) && Out.Minimum >= -0.001
        && Out.Mean >= Out.Minimum - 0.001 && Out.Mean <= Out.Maximum + 0.001;
}
inline double ClockedPower(double PerMachine, double Machines, double Exponent)
{
    if (Machines <= 0) return 0;
    const double Full = std::floor(Machines);
    return PerMachine * (Full + std::pow(Machines - Full, Exponent));
}

struct ConfiguredClocking
{
    int BuiltMachines = 0;
    int FullClockMachines = 0;
    double PartialClock = 0.0;
    double Power = 0.0;
};

/**
 * Distribute a 100%-clock cycle-equivalent machine count over physical machines
 * capped at MaxClock. ProductionBoost affects power only here; the caller is
 * responsible for reducing required production cycles by the boost.
 */
inline ConfiguredClocking ClockedPowerConfigured(
    double PerMachine,
    double CycleEquivalentMachines,
    double MaxClock,
    double PowerExponent,
    double ProductionBoost,
    double ProductionBoostPowerExponent)
{
    ConfiguredClocking Out;
    if (!std::isfinite(CycleEquivalentMachines) || CycleEquivalentMachines <= 0.0) return Out;
    const double SafeClock = std::isfinite(MaxClock) && MaxClock > 0.0 ? MaxClock : 1.0;
    const double SafePowerExponent = std::isfinite(PowerExponent) && PowerExponent > 0.0 ? PowerExponent : 1.0;
    const double SafeBoost = std::isfinite(ProductionBoost) && ProductionBoost > 0.0 ? ProductionBoost : 1.0;
    const double SafeBoostExponent = std::isfinite(ProductionBoostPowerExponent) && ProductionBoostPowerExponent > 0.0
        ? ProductionBoostPowerExponent : 1.0;

    Out.FullClockMachines = static_cast<int>(std::floor(CycleEquivalentMachines / SafeClock + 1e-9));
    double Remainder = CycleEquivalentMachines - static_cast<double>(Out.FullClockMachines) * SafeClock;
    if (Remainder < 1e-9) Remainder = 0.0;
    if (Remainder > SafeClock) Remainder = SafeClock;
    Out.PartialClock = Remainder;
    Out.BuiltMachines = Out.FullClockMachines + (Remainder > 0.0 ? 1 : 0);
    if (Out.BuiltMachines <= 0) Out.BuiltMachines = 1;

    const double BoostMultiplier = std::pow(SafeBoost, SafeBoostExponent);
    Out.Power = PerMachine * BoostMultiplier
        * (static_cast<double>(Out.FullClockMachines) * std::pow(SafeClock, SafePowerExponent)
            + (Remainder > 0.0 ? std::pow(Remainder, SafePowerExponent) : 0.0));
    return Out;
}
struct AlienAugmentation
{
    double BaseBeforeMultiplier = 0.0;
    double Multiplier = 1.0;
    double GrossPower = 0.0;
    double Contribution = 0.0;
};

/** Apply Alien Power Augmenter generation without baking vanilla constants into the math helper. */
inline AlienAugmentation ApplyAlienAugmentation(
    double GeneratorGrossPower,
    int PassiveCount,
    int FueledCount,
    double BasePowerPerAugmenter,
    double PassiveBoostPerAugmenter,
    double FueledBoostPerAugmenter)
{
    AlienAugmentation Out;
    const double SafeGenerator = std::isfinite(GeneratorGrossPower) ? std::max(0.0, GeneratorGrossPower) : 0.0;
    const int SafePassive = std::max(0, PassiveCount);
    const int SafeFueled = std::max(0, FueledCount);
    const double SafeBase = std::isfinite(BasePowerPerAugmenter) ? std::max(0.0, BasePowerPerAugmenter) : 0.0;
    const double SafePassiveBoost = std::isfinite(PassiveBoostPerAugmenter) ? std::max(0.0, PassiveBoostPerAugmenter) : 0.0;
    const double SafeFueledBoost = std::isfinite(FueledBoostPerAugmenter) ? std::max(0.0, FueledBoostPerAugmenter) : 0.0;
    const double AugmenterBase = static_cast<double>(SafePassive + SafeFueled) * SafeBase;
    Out.BaseBeforeMultiplier = SafeGenerator + AugmenterBase;
    Out.Multiplier = 1.0 + static_cast<double>(SafePassive) * SafePassiveBoost
        + static_cast<double>(SafeFueled) * SafeFueledBoost;
    Out.GrossPower = Out.BaseBeforeMultiplier * Out.Multiplier;
    Out.Contribution = std::max(0.0, Out.GrossPower - SafeGenerator);
    return Out;
}

/** Generator MW required before augmentation to reach a requested total gross output. */
inline double GeneratorGrossForTarget(
    double RequiredTotalGrossPower,
    int PassiveCount,
    int FueledCount,
    double BasePowerPerAugmenter,
    double PassiveBoostPerAugmenter,
    double FueledBoostPerAugmenter)
{
    const AlienAugmentation ZeroGenerator = ApplyAlienAugmentation(
        0.0, PassiveCount, FueledCount, BasePowerPerAugmenter,
        PassiveBoostPerAugmenter, FueledBoostPerAugmenter);
    const double Required = std::isfinite(RequiredTotalGrossPower) ? std::max(0.0, RequiredTotalGrossPower) : 0.0;
    if (ZeroGenerator.Multiplier <= 0.0) return Required;
    return std::max(0.0, Required / ZeroGenerator.Multiplier
        - (ZeroGenerator.BaseBeforeMultiplier));
}

}

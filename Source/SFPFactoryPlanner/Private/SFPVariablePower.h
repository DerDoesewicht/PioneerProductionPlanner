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
}

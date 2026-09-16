#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Engine-independent continuous material balance. Rates describe steady state;
// recycled material may still require an initial charge to start the factory.
namespace SFPMaterialBalance
{
using Vector = std::vector<double>;
using Matrix = std::vector<Vector>;

// Two-phase simplex: maximise c*x subject to A*x <= b, x >= 0.
class Tableau
{
    static constexpr double Epsilon = 1e-9;
    int Rows, Cols, Pivots = 0;
    std::vector<int> Basic, Nonbasic;
    Matrix D;
    bool Pivot(int R, int S)
    {
        if (++Pivots > 20000) return false;
        const double Inv = 1.0 / D[R][S];
        for (int I = 0; I < Rows + 2; ++I) if (I != R)
            for (int J = 0; J < Cols + 2; ++J) if (J != S)
                D[I][J] -= D[R][J] * D[I][S] * Inv;
        for (int J = 0; J < Cols + 2; ++J) if (J != S) D[R][J] *= Inv;
        for (int I = 0; I < Rows + 2; ++I) if (I != R) D[I][S] *= -Inv;
        D[R][S] = Inv;
        std::swap(Basic[R], Nonbasic[S]);
        return true;
    }
    bool Optimise(int Phase)
    {
        const int Objective = Phase == 1 ? Rows + 1 : Rows;
        for (;;)
        {
            int S = -1;
            // Bland's rule avoids cycling on degenerate recipe systems.
            for (int J = 0; J <= Cols; ++J)
                if (!(Phase == 2 && Nonbasic[J] == -1) && D[Objective][J] < -Epsilon
                    && (S == -1 || Nonbasic[J] < Nonbasic[S])) S = J;
            if (S == -1) return true;
            int R = -1;
            for (int I = 0; I < Rows; ++I)
            {
                if (D[I][S] <= Epsilon) continue;
                if (R == -1) { R = I; continue; }
                const double Left = D[I][Cols + 1] / D[I][S];
                const double Right = D[R][Cols + 1] / D[R][S];
                if (Left < Right - Epsilon || (std::abs(Left - Right) <= Epsilon && Basic[I] < Basic[R])) R = I;
            }
            if (R == -1 || !Pivot(R, S)) return false;
        }
    }
public:
    Tableau(const Matrix& A, const Vector& B, const Vector& C)
        : Rows(static_cast<int>(B.size())), Cols(static_cast<int>(C.size())),
          Basic(Rows), Nonbasic(Cols + 1), D(Rows + 2, Vector(Cols + 2))
    {
        for (int I = 0; I < Rows; ++I)
        {
            for (int J = 0; J < Cols; ++J) D[I][J] = A[I][J];
            Basic[I] = Cols + I; D[I][Cols] = -1; D[I][Cols + 1] = B[I];
        }
        for (int J = 0; J < Cols; ++J) { Nonbasic[J] = J; D[Rows][J] = -C[J]; }
        Nonbasic[Cols] = -1; D[Rows + 1][Cols] = 1;
    }
    bool Solve(Vector& X)
    {
        if (Rows == 0 || Cols == 0) return false;
        int R = 0;
        for (int I = 1; I < Rows; ++I) if (D[I][Cols + 1] < D[R][Cols + 1]) R = I;
        if (D[R][Cols + 1] < -Epsilon)
        {
            if (!Pivot(R, Cols) || !Optimise(1) || std::abs(D[Rows + 1][Cols + 1]) > 1e-7) return false;
            for (int I = 0; I < Rows; ++I) if (Basic[I] == -1)
            {
                int S = -1;
                for (int J = 0; J < Cols; ++J)
                    if (std::abs(D[I][J]) > Epsilon && (S == -1 || Nonbasic[J] < Nonbasic[S])) S = J;
                if (S != -1 && !Pivot(I, S)) return false;
            }
        }
        if (!Optimise(2)) return false;
        X.assign(Cols, 0);
        for (int I = 0; I < Rows; ++I) if (Basic[I] >= 0 && Basic[I] < Cols) X[Basic[I]] = D[I][Cols + 1];
        return true;
    }
};

// Net is production minus consumption for each item and each activity.
// First minimise external supply (including extraction), then machine load.
inline bool Solve(const Matrix& Net, const Vector& Demand, const Vector& ExternalCost,
                  const Vector& ActivityCost, Vector& Rates)
{
    if (Net.empty() || ExternalCost.empty() || Net.size() != Demand.size()
        || ExternalCost.size() != ActivityCost.size() || Net.size() > 1024 || ExternalCost.size() > 512) return false;
    Matrix A = Net;
    Vector B = Demand;
    for (size_t I = 0; I < A.size(); ++I)
    {
        auto& Row = A[I];
        if (Row.size() != ExternalCost.size() || !std::isfinite(B[I])) return false;
        for (double& V : Row)
        {
            if (!std::isfinite(V)) return false;
            V = -V;
        }
        B[I] = -B[I];
    }

    // Huge power plans combine raw-resource rates in the hundreds of millions
    // per minute with deliberately expensive external fallback sources.
    // Normalise objective coefficients so their absolute magnitude cannot
    // destabilise the tableau; their ordering and optimum remain unchanged.
    Vector ExternalObjective = ExternalCost;
    double ExternalScale = 1.0;
    for (double V : ExternalObjective)
    {
        if (!std::isfinite(V) || V < 0) return false;
        ExternalScale = std::max(ExternalScale, std::abs(V));
    }
    for (double& V : ExternalObjective) V /= ExternalScale;
    Vector C = ExternalObjective;
    for (double& V : C) V = -V;
    if (!Tableau(A, B, C).Solve(Rates)) return false;
    double Cost = 0;
    for (size_t J = 0; J < Rates.size(); ++J) Cost += ExternalObjective[J] * Rates[J];
    Vector CostRow = ExternalObjective;
    double CostRowScale = std::max(1.0, std::abs(Cost));
    for (double V : CostRow) CostRowScale = std::max(CostRowScale, std::abs(V));
    for (double& V : CostRow) V /= CostRowScale;
    A.push_back(CostRow);
    B.push_back((Cost + 1e-8 * std::max(1.0, std::abs(Cost))) / CostRowScale);

    C = ActivityCost;
    double ActivityScale = 1.0;
    for (double V : C)
    {
        if (!std::isfinite(V) || V <= 0) return false;
        ActivityScale = std::max(ActivityScale, std::abs(V));
    }
    for (double& V : C) V = -V / ActivityScale;
    if (!Tableau(A, B, C).Solve(Rates)) return false;
    for (double& V : Rates)
    {
        if (!std::isfinite(V) || V < -1e-7) return false;
        V = std::max(0.0, V);
    }
    for (size_t I = 0; I < Net.size(); ++I)
    {
        double Actual = 0, Magnitude = std::abs(Demand[I]);
        for (size_t J = 0; J < Rates.size(); ++J) { Actual += Net[I][J] * Rates[J]; Magnitude += std::abs(Net[I][J] * Rates[J]); }
        if (Actual + 1e-7 * std::max(1.0, Magnitude) < Demand[I]) return false;
    }
    return true;
}
}

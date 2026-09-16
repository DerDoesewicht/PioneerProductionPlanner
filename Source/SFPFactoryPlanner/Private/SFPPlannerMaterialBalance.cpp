#include "SFPVariablePower.h"
#include "SFPPlannerSolver.h"
#include "SFPMaterialBalance.h"
#include "SFPNumberFormatting.h"

bool FSFPPlannerSolver::BalanceMaterials(FSFPPlanResult& Result) const
{
    if (Result.Warnings.ContainsByPredicate([](const FString& W) { return W.StartsWith(TEXT("Plan bei ")); }))
    {
        Result.ErrorMessage = TEXT("Produktionskette unvollständig: Rechenlimit erreicht. Ziele verkleinern oder Rezeptwahl anpassen.");
        return false;
    }
    struct FPool
    {
        FSFPPlanEdge Example;
        TMap<int32, double> Supply;
        TMap<int32, double> Demand;
    };
    TMap<FString, FPool> Pools;
    TArray<FSFPPlanEdge> Preserved;
    TMap<int32, int32> VariableByNode;
    TArray<int32> NodesByVariable;
    for (const FSFPPlanNode& Node : Result.Nodes)
    {
        const bool bRecipeMachine = Node.Type == ESFPPlanNodeType::Machine
            && Recipes.ContainsByPredicate([&](const FSFPPlannerRecipe& R) { return R.ClassPath == Node.RecipeClassPath; });
        if (bRecipeMachine || Node.Type == ESFPPlanNodeType::Source || Node.Type == ESFPPlanNodeType::Cycle)
        {
            VariableByNode.Add(Node.Id, NodesByVariable.Num());
            NodesByVariable.Add(Node.Id);
        }
    }
    if (NodesByVariable.IsEmpty()) return true;
    for (const FSFPPlanEdge& Edge : Result.Edges)
    {
        if (Edge.Form != TEXT("solid") && Edge.Form != TEXT("liquid") && Edge.Form != TEXT("gas"))
        {
            Preserved.Add(Edge);
            continue;
        }
        // Raw node-to-miner input must not be replaced by the miner's own output.
        const FString Key = Edge.TransportKind == TEXT("resource_node")
            ? FString::Printf(TEXT("raw:%d:%s"), Edge.TargetNodeId, *Edge.ItemClassPath)
            : Edge.ItemClassPath;
        FPool& Pool = Pools.FindOrAdd(Key);
        Pool.Example = Edge;
        Pool.Supply.FindOrAdd(Edge.SourceNodeId) += Edge.RatePerMinute;
        if (Result.Nodes[Edge.TargetNodeId].Type != ESFPPlanNodeType::Byproduct)
            Pool.Demand.FindOrAdd(Edge.TargetNodeId) += Edge.RatePerMinute;
    }
    TArray<FString> Keys;
    Pools.GetKeys(Keys);
    Keys.Sort();
    const int32 N = NodesByVariable.Num();
    SFPMaterialBalance::Matrix Net(Keys.Num(), SFPMaterialBalance::Vector(N, 0));
    SFPMaterialBalance::Vector Demand(Keys.Num(), 0), External(N, 0), Activity(N, 1), Scale;
    for (int32 J = 0; J < N; ++J)
    {
        const FSFPPlanNode& Node = Result.Nodes[NodesByVariable[J]];
        Activity[J] = FMath::Max(0.001, Node.MachineCount);
    }
    for (int32 I = 0; I < Keys.Num(); ++I)
    {
        const FPool& Pool = Pools[Keys[I]];
        for (const auto& Pair : Pool.Supply)
        {
            if (const int32* J = VariableByNode.Find(Pair.Key))
            {
                Net[I][*J] += Pair.Value;
                const auto Type = Result.Nodes[Pair.Key].Type;
                if (Type == ESFPPlanNodeType::Source || Type == ESFPPlanNodeType::Cycle)
                    External[*J] += Pair.Value * FMath::Max(0.001, Result.Nodes[Pair.Key].SourceCostMultiplier);
            }
            else Demand[I] -= Pair.Value;
        }
        for (const auto& Pair : Pool.Demand)
        {
            if (const int32* J = VariableByNode.Find(Pair.Key)) Net[I][*J] -= Pair.Value;
            else Demand[I] += Pair.Value;
        }
    }
    // Sum external withdrawals per supplied item, including all shared consumers.
    // Net*x >= Demand encodes -withdrawal >= -capacity without changing coproduct balance.
    if (Result.bEnforceSupplyLimits)
    for (const auto& Limit : Result.SuppliedInputRates)
    {
        SFPMaterialBalance::Vector Row(N, 0);
        for (int32 J = 0; J < N; ++J)
        {
            const FSFPPlanNode& Node = Result.Nodes[NodesByVariable[J]];
            if ((Node.Type == ESFPPlanNodeType::Source || Node.Type == ESFPPlanNodeType::Cycle)
                && Node.ClassPath == Limit.Key) Row[J] = -Node.RatePerMinute;
        }
        Net.push_back(Row); Demand.push_back(-Limit.Value);
    }
    // Mixed resource selections impose the configured deposit-count/clock
    // upper bound. Their deliberately expensive fallback source is therefore
    // used only after every configured extraction group reaches its limit.
    for (int32 J = 0; J < N; ++J)
    {
        const FSFPPlanNode& Node = Result.Nodes[NodesByVariable[J]];
        if (Node.Type != ESFPPlanNodeType::Machine || Node.MachineCount <= KINDA_SMALL_NUMBER)
        {
            continue;
        }
        if (Node.MaximumMachineCount > KINDA_SMALL_NUMBER)
        {
            SFPMaterialBalance::Vector Row(N, 0);
            Row[J] = -Node.MachineCount;
            Net.push_back(Row);
            Demand.push_back(-Node.MaximumMachineCount);
        }
    }
    if (!SFPMaterialBalance::Solve(Net, Demand, External, Activity, Scale))
    {
        Result.ErrorMessage = TEXT("Materialbilanz nicht lösbar oder Rechenlimit erreicht. Plan nicht als versorgt verwenden.");
        Result.bSuccess = false;
        return false;
    }
    // KLib's fluid-consumption task has its own 1 s clock. Underclocking
    // ore production does not proportionally underclock that task. Charge the
    // operating fluid for each physical miner, then resolve upstream supply.
    TMap<int32, double> OperatingFluidDemand;
    TMap<int32, FString> OperatingFluidPath;
    TMap<int32, double> OperatingFluidRate;
    for (const FSFPPlanNode& Node : Result.Nodes)
    {
        const FSFPPlannerRecipe* Recipe = Recipes.FindByPredicate([&](const FSFPPlannerRecipe& R) { return R.ClassPath == Node.RecipeClassPath; });
        if (Recipe && Recipe->FluidRatePerMinute > 0 && Recipe->Ingredients.Num() > 1)
        {
            OperatingFluidPath.Add(Node.Id, Recipe->Ingredients[1].ClassPath);
            OperatingFluidRate.Add(Node.Id, Recipe->FluidRatePerMinute);
        }
    }
    bool bFluidStable = OperatingFluidRate.IsEmpty();
    for (int32 Pass = 0; Pass < 32 && !bFluidStable; ++Pass)
    {
        bool bChanged = false;
        for (const auto& Pair : OperatingFluidRate)
        {
            const int32* J = VariableByNode.Find(Pair.Key);
            const double Count = J ? Result.Nodes[Pair.Key].MachineCount * Scale[*J] : 0;
            const double Physical = Count > 1e-8 ? FMath::CeilToInt(Count - 1e-7) : 0;
            const double Required = Physical * Pair.Value;
            const double* Previous = OperatingFluidDemand.Find(Pair.Key);
            if (!Previous || FMath::Abs(*Previous - Required) > 1e-7) bChanged = true;
            OperatingFluidDemand.Add(Pair.Key, Required);
        }
        if (!bChanged) { bFluidStable = true; break; }
        auto FluidNet = Net;
        auto FluidDemand = Demand;
        for (int32 I = 0; I < Keys.Num(); ++I)
        {
            const FPool& Pool = Pools[Keys[I]];
            for (const auto& Pair : Pool.Demand)
            {
                const FString* FluidPath = OperatingFluidPath.Find(Pair.Key);
                const int32* J = VariableByNode.Find(Pair.Key);
                if (FluidPath && *FluidPath == Keys[I] && J)
                {
                    FluidNet[I][*J] += Pair.Value; // remove proportional demand
                    FluidDemand[I] += OperatingFluidDemand[Pair.Key];
                }
            }
        }
        if (!SFPMaterialBalance::Solve(FluidNet, FluidDemand, External, Activity, Scale)) break;
    }
    if (!bFluidStable)
    {
        Result.bSuccess = false;
        Result.ErrorMessage = TEXT("Betriebsflüssigkeit nicht stabil bilanzierbar. Rezeptwahl oder Ziele anpassen.");
        return false;
    }
    auto ConsumerDemand = [&](const FString& Key, int32 Id, double Original) -> double
    {
        const FString* FluidPath = OperatingFluidPath.Find(Id);
        if (FluidPath && *FluidPath == Key) return OperatingFluidDemand[Id];
        const int32* J = VariableByNode.Find(Id);
        return Original * (J ? Scale[*J] : 1.0);
    };
    auto NodeScale = [&](int32 Id) -> double
    {
        const int32* J = VariableByNode.Find(Id);
        return J ? Scale[*J] : 1.0;
    };
    TArray<FSFPPlanEdge> Balanced = Preserved;
    // Keep metadata and node IDs until power planning has finished using them.
    for (FSFPPlanNode& Node : Result.Nodes)
        if (Node.Type == ESFPPlanNodeType::Byproduct) Node.RatePerMinute = 0;
    auto Connect = [&](const FPool& Pool, int32 From, int32 To, double Rate)
    {
        if (Rate <= 1e-8) return;
        FSFPPlanEdge Edge;
        Edge.SourceNodeId = From;
        Edge.TargetNodeId = To;
        Edge.ItemName = Pool.Example.ItemName;
        Edge.ItemClassPath = Pool.Example.ItemClassPath;
        Edge.Form = Pool.Example.Form;
        Edge.RatePerMinute = Rate;
        if (Pool.Example.TransportKind == TEXT("resource_node"))
        {
            Edge.bLocalRoutingLink = true;
            Edge.TransportKind = TEXT("resource_node");
            Edge.TransportLabel = TEXT("Direkt am Rohstoffknoten");
        }
        else AddTransportAdvice(Edge, Result);
        Balanced.Add(MoveTemp(Edge));
    };
    for (const FString& Key : Keys)
    {
        const FPool& Pool = Pools[Key];
        TArray<int32> Sources, Consumers;
        Pool.Supply.GetKeys(Sources);
        Pool.Demand.GetKeys(Consumers);
        Sources.Sort(); Consumers.Sort();
        TMap<int32, double> Remaining;
        for (int32 Id : Sources) Remaining.Add(Id, Pool.Supply[Id] * NodeScale(Id));
        for (int32 Consumer : Consumers)
        {
            double Need = ConsumerDemand(Key, Consumer, Pool.Demand[Consumer]);
            for (int32 Source : Sources)
            {
                const double Used = FMath::Min(Need, Remaining[Source]);
                Connect(Pool, Source, Consumer, Used);
                Need -= Used;
                Remaining[Source] -= Used;
            }
            if (Need > 1e-5 * FMath::Max(1.0, ConsumerDemand(Key, Consumer, Pool.Demand[Consumer])))
            {
                Result.ErrorMessage = TEXT("Materialbilanz: verbleibender ungedeckter Bedarf; Berechnung abgebrochen.");
                return false;
            }
        }
        double Surplus = 0;
        for (int32 Id : Sources) Surplus += Remaining[Id];
        if (Surplus > 1e-6)
        {
            FSFPPlanNode Waste;
            Waste.Id = Result.Nodes.Num();
            Waste.Type = ESFPPlanNodeType::Byproduct;
            Waste.Title = Pool.Example.ItemName;
            Waste.ClassPath = Pool.Example.ItemClassPath;
            Waste.RatePerMinute = Surplus;
            Waste.Detail = FString::Printf(TEXT("Lagereingang Nebenprodukt: %s/min"), *FSFPNumberFormatting::Decimal(Surplus));
            const int32 Id = Result.Nodes.Add(MoveTemp(Waste));
            for (int32 Source : Sources) Connect(Pool, Source, Id, Remaining[Source]);
        }
    }
    for (int32 J = 0; J < N; ++J)
    {
        FSFPPlanNode& Node = Result.Nodes[NodesByVariable[J]];
        const double Factor = Scale[J];
        if (Node.Type == ESFPPlanNodeType::Machine)
        {
            Result.TotalEquivalentMachines += Node.MachineCount * (Factor - 1.0);
            Result.TotalBasePowerMW += Node.PowerMW * (Factor - 1.0);
        }
        Node.RatePerMinute *= Factor;
        Node.MachineCount *= Factor;
        const double RoundedCount = std::round(Node.MachineCount);
        if (RoundedCount > 0.0 && FMath::Abs(Node.MachineCount - RoundedCount) < 1e-7) Node.MachineCount = RoundedCount;
        Node.PowerMW *= Factor;
        if (Node.Type == ESFPPlanNodeType::Machine)
        {
            const FSFPPlannerRecipe* Recipe = Recipes.FindByPredicate([&](const FSFPPlannerRecipe& R) { return R.ClassPath == Node.RecipeClassPath; });
            if (Recipe == nullptr) continue;
            const double LinearPower = Node.PowerMW;
            const double ConfiguredClock = FMath::Max(0.001, Node.ConfiguredClockPercent / 100.0);
            const SFPVariablePower::ConfiguredClocking Clocking = SFPVariablePower::ClockedPowerConfigured(
                Recipe->BasePowerMW,
                Node.MachineCount,
                ConfiguredClock,
                Recipe->PowerExponent,
                Node.ProductionBoost,
                Recipe->MachineConfig.ProductionBoostPowerExponent);
            Node.PowerMW = Clocking.Power;
            Node.BuiltMachineCount = FMath::Max(1, Clocking.BuiltMachines);
            Node.FullClockMachineCount = FMath::Max(0, Clocking.FullClockMachines);
            Node.PartialClockPercent = FMath::Max(0.0, Clocking.PartialClock * 100.0);
            if (!Node.bFuelPowered)
            {
                Result.TotalBasePowerMW += Node.PowerMW - LinearPower;
            }
            const FString ClockingText = Node.PartialClockPercent > 0.05
                ? Node.FullClockMachineCount > 0
                    ? FString::Printf(TEXT("%d × %s%% + 1 × %s%%"),
                        Node.FullClockMachineCount,
                        *FSFPNumberFormatting::Decimal(Node.ConfiguredClockPercent, 1),
                        *FSFPNumberFormatting::Decimal(Node.PartialClockPercent, 1))
                    : FString::Printf(TEXT("1 × %s%%"), *FSFPNumberFormatting::Decimal(Node.PartialClockPercent, 1))
                : FString::Printf(TEXT("%d × %s%%"),
                    Node.BuiltMachineCount,
                    *FSFPNumberFormatting::Decimal(Node.ConfiguredClockPercent, 1));
            const FString LimitLine = Node.MaximumMachineCount > KINDA_SMALL_NUMBER
                ? FString::Printf(TEXT("\nGemischte Vorkommen: höchstens %s Maschinenäquivalente"),
                    *FSFPNumberFormatting::Decimal(Node.MaximumMachineCount, 2))
                : FString();
            Node.Detail = FString::Printf(TEXT("%s\n%s\n%d Maschinen gebaut | %s%s\n%s bei geplanter Taktung: %s MW"),
                *Recipe->DisplayName,
                *Recipe->ConfigurationDetail,
                Node.BuiltMachineCount,
                *ClockingText,
                *LimitLine,
                Node.bFuelPowered ? TEXT("Brennleistung") : TEXT("Strombedarf"),
                *FSFPNumberFormatting::Decimal(Node.PowerMW, 2));
        }
        else if (Node.Type == ESFPPlanNodeType::Source || Node.Type == ESFPPlanNodeType::Cycle)
        {
            const bool bRaw = Balanced.ContainsByPredicate([&](const FSFPPlanEdge& E) { return E.SourceNodeId == Node.Id && E.TransportKind == TEXT("resource_node"); });
			int32 UsedSourceCount = 0;
			if (bRaw && Node.MaximumMachineCount > KINDA_SMALL_NUMBER)
			{
				if (const FSFPPlanEdge* RawEdge = Balanced.FindByPredicate([&](const FSFPPlanEdge& Edge)
				{
					return Edge.SourceNodeId == Node.Id && Edge.TransportKind == TEXT("resource_node");
				}))
				{
					if (Result.Nodes.IsValidIndex(RawEdge->TargetNodeId))
						UsedSourceCount = Result.Nodes[RawEdge->TargetNodeId].BuiltMachineCount;
				}
			}
            Node.Detail = bRaw && Node.MaximumMachineCount > KINDA_SMALL_NUMBER
				? FString::Printf(TEXT("Gemischte Vorkommen: %d von %d genutzt | Rohstoffabbau: %s/min"),
					UsedSourceCount,
                    FMath::RoundToInt(Node.MaximumMachineCount),
                    *FSFPNumberFormatting::Decimal(Node.RatePerMinute))
                : bRaw
                ? FString::Printf(TEXT("Rohstoffabbau: %s/min"), *FSFPNumberFormatting::Decimal(Node.RatePerMinute))
                : FString::Printf(TEXT("Externe Zufuhr erforderlich: %s/min"), *FSFPNumberFormatting::Decimal(Node.RatePerMinute));
        }
    }
    Result.Edges = MoveTemp(Balanced);
    Result.Warnings.RemoveAll([](const FString& Warning)
    {
        return (Warning.StartsWith(TEXT("Kreislauf bei ")) && Warning.Contains(TEXT("wurde begrenzt")))
            || (Warning.StartsWith(TEXT("Für ")) && Warning.Contains(TEXT("blieb nur eine zyklische Rezeptkette")));
    });
    for (const FSFPPlanNode& Node : Result.Nodes)
        if (Node.Type == ESFPPlanNodeType::Cycle && Node.RatePerMinute > 1e-6)
            Result.Warnings.AddUnique(FString::Printf(TEXT("Rückführung deckt nicht den gesamten Bedarf: %s, %s/min externe Zufuhr erforderlich."),
                *Node.Title, *FSFPNumberFormatting::Decimal(Node.RatePerMinute)));
    // A directed cycle is a steady-state return path, not proof of cold start.
    TArray<int32> InDegree;
    InDegree.Init(0, Result.Nodes.Num());
    for (const FSFPPlanEdge& E : Result.Edges) if (E.RatePerMinute > 1e-8) ++InDegree[E.TargetNodeId];
    TArray<int32> Queue;
    for (int32 I = 0; I < InDegree.Num(); ++I) if (InDegree[I] == 0) Queue.Add(I);
    for (int32 I = 0; I < Queue.Num(); ++I)
        for (const FSFPPlanEdge& E : Result.Edges)
            if (E.RatePerMinute > 1e-8 && E.SourceNodeId == Queue[I] && --InDegree[E.TargetNodeId] == 0) Queue.Add(E.TargetNodeId);
    if (Queue.Num() < Result.Nodes.Num())
        Result.Warnings.AddUnique(TEXT("Rückführung bilanziert. Zum Anfahren können Startmaterial, Puffer und geregelte Rücklaufzufuhr erforderlich sein."));
    return true;
}

void FSFPPlannerSolver::CompactMaterialGraph(FSFPPlanResult& Result) const
{
    TSet<int32> Used;
    for (const FSFPPlanEdge& E : Result.Edges) if (E.RatePerMinute > 1e-8) { Used.Add(E.SourceNodeId); Used.Add(E.TargetNodeId); }
    TMap<int32, int32> Remap;
    TArray<FSFPPlanNode> Nodes;
    for (FSFPPlanNode& Node : Result.Nodes)
    {
        if (!Used.Contains(Node.Id) && Node.Type != ESFPPlanNodeType::Target) continue;
        Remap.Add(Node.Id, Nodes.Num());
        Node.Id = Nodes.Num();
        Nodes.Add(MoveTemp(Node));
    }
    Result.Edges.RemoveAll([](const FSFPPlanEdge& E) { return E.RatePerMinute <= 1e-8; });
    for (FSFPPlanEdge& E : Result.Edges) { E.SourceNodeId = Remap[E.SourceNodeId]; E.TargetNodeId = Remap[E.TargetNodeId]; }
    Result.Nodes = MoveTemp(Nodes);
}

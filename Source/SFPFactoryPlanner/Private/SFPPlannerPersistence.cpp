#include "SFPPlannerPersistence.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	constexpr int32 SFPPlanFileSchemaVersion = 9;
	constexpr int32 SFPMaxPlanNameLength = 80;
	constexpr int32 SFPMaxSharedPlans = 100;
	constexpr int32 SFPMaxSharedPlanNodes = 5000;
	constexpr int32 SFPMaxSharedPlanEdges = 20000;
	constexpr int32 SFPMaxSharedPlanTargets = 32;

	struct FSFPSharedPlanMetadata
	{
		int64 Revision = 0;
		FString OwnerId;
		FString OwnerName;
		FString UpdatedBy;
		FDateTime ModifiedAt;
	};

	bool IsFiniteInt32(const double Value)
	{
		return FMath::IsFinite(Value)
			&& Value >= static_cast<double>(MIN_int32)
			&& Value <= static_cast<double>(MAX_int32);
	}

	TSharedRef<FJsonObject> PersistedNodeToJson(const FSFPPlanNode& Node)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("id"), Node.Id);
		Json->SetNumberField(TEXT("type"), static_cast<uint8>(Node.Type));
		Json->SetBoolField(TEXT("completed"), Node.bCompleted);
		Json->SetNumberField(TEXT("depth"), Node.Depth);
		Json->SetStringField(TEXT("title"), Node.Title);
		Json->SetStringField(TEXT("detail"), Node.Detail);
		Json->SetStringField(TEXT("classPath"), Node.ClassPath);
		Json->SetStringField(TEXT("producedItemClassPath"), Node.ProducedItemClassPath);
		Json->SetStringField(TEXT("recipeClassPath"), Node.RecipeClassPath);
		Json->SetNumberField(TEXT("ratePerMinute"), Node.RatePerMinute);
		Json->SetNumberField(TEXT("machineCount"), Node.MachineCount);
		Json->SetNumberField(TEXT("builtMachineCount"), Node.BuiltMachineCount);
		Json->SetNumberField(TEXT("fullClockMachineCount"), Node.FullClockMachineCount);
		Json->SetNumberField(TEXT("configuredClockPercent"), Node.ConfiguredClockPercent);
		Json->SetNumberField(TEXT("partialClockPercent"), Node.PartialClockPercent);
		Json->SetNumberField(TEXT("maximumMachineCount"), Node.MaximumMachineCount);
		Json->SetNumberField(TEXT("sourceCostMultiplier"), Node.SourceCostMultiplier);
		Json->SetNumberField(TEXT("somersloopCount"), Node.SomersloopCount);
		Json->SetNumberField(TEXT("productionBoost"), Node.ProductionBoost);
		Json->SetNumberField(TEXT("powerMW"), Node.PowerMW);
		Json->SetBoolField(TEXT("fuelPowered"), Node.bFuelPowered);
		Json->SetStringField(TEXT("fuelClassPath"), Node.FuelClassPath);
		Json->SetStringField(TEXT("fuelDisplayName"), Node.FuelDisplayName);
		Json->SetStringField(TEXT("fuelForm"), Node.FuelForm);
		Json->SetNumberField(TEXT("fuelEnergyValueMJ"), Node.FuelEnergyValueMJ);
		Json->SetNumberField(TEXT("fuelRatePerMinute"), Node.FuelRatePerMinute);
		Json->SetNumberField(TEXT("infrastructureCount"), Node.InfrastructureCount);
		return Json;
	}

	TSharedRef<FJsonObject> PersistedEdgeToJson(const FSFPPlanEdge& Edge)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("sourceNodeId"), Edge.SourceNodeId);
		Json->SetNumberField(TEXT("targetNodeId"), Edge.TargetNodeId);
		Json->SetStringField(TEXT("itemName"), Edge.ItemName);
		Json->SetStringField(TEXT("itemClassPath"), Edge.ItemClassPath);
		Json->SetStringField(TEXT("form"), Edge.Form);
		Json->SetNumberField(TEXT("ratePerMinute"), Edge.RatePerMinute);
		Json->SetStringField(TEXT("transportLabel"), Edge.TransportLabel);
		Json->SetStringField(TEXT("transportClassPath"), Edge.TransportClassPath);
		Json->SetStringField(TEXT("transportKind"), Edge.TransportKind);
		Json->SetNumberField(TEXT("requiredLines"), Edge.RequiredLines);
		Json->SetNumberField(TEXT("estimatedLengthMeters"), Edge.EstimatedLengthMeters);
		Json->SetBoolField(TEXT("localRoutingLink"), Edge.bLocalRoutingLink);
		return Json;
	}

	TSharedRef<FJsonObject> ConstructionCostToJson(const FSFPConstructionCost& Cost)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("itemClassPath"), Cost.ItemClassPath);
		Json->SetStringField(TEXT("displayName"), Cost.DisplayName);
		Json->SetStringField(TEXT("form"), Cost.Form);
		Json->SetNumberField(TEXT("amount"), Cost.Amount);
		return Json;
	}

	TSharedRef<FJsonObject> PlanTargetToJson(const FSFPPlanTarget& Target)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("itemClassPath"), Target.ItemClassPath);
		Json->SetStringField(TEXT("displayName"), Target.DisplayName);
		Json->SetStringField(TEXT("form"), Target.Form);
		Json->SetNumberField(TEXT("ratePerMinute"), Target.RatePerMinute);
		return Json;
	}

	bool JsonToPlanTarget(const TSharedPtr<FJsonObject>& Json, FSFPPlanTarget& OutTarget)
	{
		if (!Json.IsValid()
			|| !Json->TryGetStringField(TEXT("itemClassPath"), OutTarget.ItemClassPath)
			|| OutTarget.ItemClassPath.IsEmpty()
			|| !Json->TryGetNumberField(TEXT("ratePerMinute"), OutTarget.RatePerMinute)
			|| !FMath::IsFinite(OutTarget.RatePerMinute)
			|| OutTarget.RatePerMinute <= 0.0)
		{
			return false;
		}
		Json->TryGetStringField(TEXT("displayName"), OutTarget.DisplayName);
		Json->TryGetStringField(TEXT("form"), OutTarget.Form);
		return true;
	}

	bool JsonToPersistedNode(const TSharedPtr<FJsonObject>& Json, FSFPPlanNode& OutNode)
	{
		if (!Json.IsValid())
		{
			return false;
		}

		double Number = 0.0;
		if (!Json->TryGetNumberField(TEXT("id"), Number) || !IsFiniteInt32(Number))
		{
			return false;
		}
		OutNode.Id = FMath::RoundToInt(Number);
		if (Json->TryGetNumberField(TEXT("type"), Number) && IsFiniteInt32(Number))
		{
			OutNode.Type = static_cast<ESFPPlanNodeType>(FMath::Clamp(FMath::RoundToInt(Number), 0, 7));
		}
		Json->TryGetBoolField(TEXT("completed"), OutNode.bCompleted);
		if (Json->TryGetNumberField(TEXT("depth"), Number) && IsFiniteInt32(Number))
		{
			OutNode.Depth = FMath::Max(0, FMath::RoundToInt(Number));
		}
		Json->TryGetStringField(TEXT("title"), OutNode.Title);
		Json->TryGetStringField(TEXT("detail"), OutNode.Detail);
		Json->TryGetStringField(TEXT("classPath"), OutNode.ClassPath);
		Json->TryGetStringField(TEXT("producedItemClassPath"), OutNode.ProducedItemClassPath);
		Json->TryGetStringField(TEXT("recipeClassPath"), OutNode.RecipeClassPath);
		Json->TryGetNumberField(TEXT("ratePerMinute"), OutNode.RatePerMinute);
		Json->TryGetNumberField(TEXT("machineCount"), OutNode.MachineCount);
		if (Json->TryGetNumberField(TEXT("builtMachineCount"), Number) && IsFiniteInt32(Number))
		{
			OutNode.BuiltMachineCount = FMath::Max(0, FMath::RoundToInt(Number));
		}
		if (Json->TryGetNumberField(TEXT("fullClockMachineCount"), Number) && IsFiniteInt32(Number))
		{
			OutNode.FullClockMachineCount = FMath::Max(0, FMath::RoundToInt(Number));
		}
		Json->TryGetNumberField(TEXT("configuredClockPercent"), OutNode.ConfiguredClockPercent);
		Json->TryGetNumberField(TEXT("partialClockPercent"), OutNode.PartialClockPercent);
		Json->TryGetNumberField(TEXT("maximumMachineCount"), OutNode.MaximumMachineCount);
		Json->TryGetNumberField(TEXT("sourceCostMultiplier"), OutNode.SourceCostMultiplier);
		if (Json->TryGetNumberField(TEXT("somersloopCount"), Number) && IsFiniteInt32(Number))
		{
			OutNode.SomersloopCount = FMath::Max(0, FMath::RoundToInt(Number));
		}
		Json->TryGetNumberField(TEXT("productionBoost"), OutNode.ProductionBoost);
		Json->TryGetNumberField(TEXT("powerMW"), OutNode.PowerMW);
		Json->TryGetBoolField(TEXT("fuelPowered"), OutNode.bFuelPowered);
		Json->TryGetStringField(TEXT("fuelClassPath"), OutNode.FuelClassPath);
		Json->TryGetStringField(TEXT("fuelDisplayName"), OutNode.FuelDisplayName);
		Json->TryGetStringField(TEXT("fuelForm"), OutNode.FuelForm);
		Json->TryGetNumberField(TEXT("fuelEnergyValueMJ"), OutNode.FuelEnergyValueMJ);
		Json->TryGetNumberField(TEXT("fuelRatePerMinute"), OutNode.FuelRatePerMinute);
		if (Json->TryGetNumberField(TEXT("infrastructureCount"), Number) && IsFiniteInt32(Number))
		{
			OutNode.InfrastructureCount = FMath::Max(0, FMath::RoundToInt(Number));
		}
		return true;
	}

	bool JsonToPersistedEdge(const TSharedPtr<FJsonObject>& Json, FSFPPlanEdge& OutEdge)
	{
		if (!Json.IsValid())
		{
			return false;
		}

		double Number = 0.0;
		if (!Json->TryGetNumberField(TEXT("sourceNodeId"), Number) || !IsFiniteInt32(Number))
		{
			return false;
		}
		OutEdge.SourceNodeId = FMath::RoundToInt(Number);
		if (!Json->TryGetNumberField(TEXT("targetNodeId"), Number) || !IsFiniteInt32(Number))
		{
			return false;
		}
		OutEdge.TargetNodeId = FMath::RoundToInt(Number);
		Json->TryGetStringField(TEXT("itemName"), OutEdge.ItemName);
		Json->TryGetStringField(TEXT("itemClassPath"), OutEdge.ItemClassPath);
		Json->TryGetStringField(TEXT("form"), OutEdge.Form);
		Json->TryGetNumberField(TEXT("ratePerMinute"), OutEdge.RatePerMinute);
		Json->TryGetStringField(TEXT("transportLabel"), OutEdge.TransportLabel);
		Json->TryGetStringField(TEXT("transportClassPath"), OutEdge.TransportClassPath);
		Json->TryGetStringField(TEXT("transportKind"), OutEdge.TransportKind);
		if (Json->TryGetNumberField(TEXT("requiredLines"), Number) && IsFiniteInt32(Number))
		{
			OutEdge.RequiredLines = FMath::Max(0, FMath::RoundToInt(Number));
		}
		Json->TryGetNumberField(TEXT("estimatedLengthMeters"), OutEdge.EstimatedLengthMeters);
		Json->TryGetBoolField(TEXT("localRoutingLink"), OutEdge.bLocalRoutingLink);
		return true;
	}

	bool JsonToConstructionCost(const TSharedPtr<FJsonObject>& Json, FSFPConstructionCost& OutCost)
	{
		if (!Json.IsValid())
		{
			return false;
		}
		Json->TryGetStringField(TEXT("itemClassPath"), OutCost.ItemClassPath);
		Json->TryGetStringField(TEXT("displayName"), OutCost.DisplayName);
		Json->TryGetStringField(TEXT("form"), OutCost.Form);
		return !OutCost.DisplayName.IsEmpty()
			&& Json->TryGetNumberField(TEXT("amount"), OutCost.Amount)
			&& OutCost.Amount >= 0.0;
	}

	bool ValidatePlan(const FSFPPlanResult& Plan, FString& OutError)
	{
		if (!Plan.bSuccess || Plan.Nodes.IsEmpty() || Plan.Edges.IsEmpty())
		{
			OutError = TEXT("Es ist kein berechneter Produktionsplan vorhanden");
			return false;
		}
		return true;
	}

	bool ValidateSharedPlanPayload(const FSFPPlanResult& Plan, FString& OutError)
	{
		if (Plan.Supplies.Num() > 32 || Plan.InputPlanningTargets.Num() > 32 || Plan.FixedOutputRates.Num() > 32
            || Plan.Targets.Num() > SFPMaxSharedPlanTargets
			|| Plan.Nodes.Num() > SFPMaxSharedPlanNodes
			|| Plan.Edges.Num() > SFPMaxSharedPlanEdges
			|| Plan.RecipeOverrides.Num() > 5000
			|| Plan.MachineSettings.Num() > 5000
			|| Plan.ResourceSourceMixes.Num() > 5000
			|| Plan.AvailableInputRates.Num() > 5000
			|| Plan.Warnings.Num() > 512)
		{
			OutError = FString::Printf(
				TEXT("Der Multiplayer-Plan ist zu groß (%d/%d Ziele, %d/%d Knoten, %d/%d Verbindungen)"),
				Plan.Targets.Num(),
				SFPMaxSharedPlanTargets,
				Plan.Nodes.Num(),
				SFPMaxSharedPlanNodes,
				Plan.Edges.Num(),
				SFPMaxSharedPlanEdges);
			return false;
		}
		if (Plan.PassiveAlienPowerAugmenters < 0
			|| Plan.FueledAlienPowerAugmenters < 0
			|| Plan.PassiveAlienPowerAugmenters > 10000
			|| Plan.FueledAlienPowerAugmenters > 10000)
		{
			OutError = TEXT("Der Multiplayer-Plan enthält eine ungültige Anzahl Alien Power Augmenter");
			return false;
		}

		const double ScalarValues[] = {
			Plan.TargetRatePerMinute,
			Plan.TotalEquivalentMachines,
			Plan.TotalBasePowerMW,
			Plan.RequestedNetPowerMW,
			Plan.PowerReservePercent,
			Plan.GeneratorBasePowerMW,
			Plan.GeneratorPowerMW,
			Plan.EquivalentGeneratorCount,
			Plan.ConfiguredGeneratorClockPercent,
			Plan.PartialGeneratorClockPercent,
			Plan.BaseGeneratorGrossPowerMW,
			Plan.AlienPowerAugmenterBaseMW,
			Plan.AlienPowerMultiplier,
			Plan.AlienPowerContributionMW,
			Plan.AlienPowerMatrixRatePerMinute,
			Plan.GrossPowerMW,
			Plan.SelfConsumptionPowerMW,
			Plan.NetPowerMW,
			Plan.ReservePowerMW,
			Plan.FuelRatePerMinute,
			Plan.SupplementalRatePerMinute,
			Plan.WasteRatePerMinute,
			Plan.EstimatedConnectionLengthMeters,
			Plan.EstimatedConveyorMeters,
			Plan.EstimatedPipelineMeters
		};
		for (const double Value : ScalarValues)
		{
			if (!FMath::IsFinite(Value) || FMath::Abs(Value) > 1.0e15)
			{
				OutError = TEXT("Der Multiplayer-Plan enthält einen ungültigen Zahlenwert");
				return false;
			}
		}

		for (const FSFPPlanTarget& Target : Plan.Targets)
		{
			if (Target.ItemClassPath.Len() > 1024
				|| Target.DisplayName.Len() > 256
				|| Target.Form.Len() > 64
				|| !FMath::IsFinite(Target.RatePerMinute)
				|| Target.RatePerMinute <= 0.0
				|| Target.RatePerMinute > 1.0e12)
			{
				OutError = TEXT("Der Multiplayer-Plan enthält ein ungültiges Ziel");
				return false;
			}
		}

		TSet<int32> NodeIds;
		NodeIds.Reserve(Plan.Nodes.Num());
		for (const FSFPPlanNode& Node : Plan.Nodes)
		{
			if (Node.Id == INDEX_NONE
				|| NodeIds.Contains(Node.Id)
				|| Node.Title.Len() > 256
				|| Node.Detail.Len() > 2048
				|| Node.ClassPath.Len() > 1024
				|| Node.ProducedItemClassPath.Len() > 1024
				|| Node.RecipeClassPath.Len() > 1024
				|| !FMath::IsFinite(Node.RatePerMinute)
				|| !FMath::IsFinite(Node.MachineCount)
				|| !FMath::IsFinite(Node.ConfiguredClockPercent)
				|| !FMath::IsFinite(Node.PartialClockPercent)
				|| !FMath::IsFinite(Node.MaximumMachineCount)
				|| !FMath::IsFinite(Node.SourceCostMultiplier)
				|| !FMath::IsFinite(Node.ProductionBoost)
				|| !FMath::IsFinite(Node.PowerMW)
				|| !FMath::IsFinite(Node.FuelEnergyValueMJ)
				|| !FMath::IsFinite(Node.FuelRatePerMinute)
				|| FMath::Abs(Node.RatePerMinute) > 1.0e12
				|| FMath::Abs(Node.MachineCount) > 1.0e9
				|| Node.BuiltMachineCount < 0 || Node.BuiltMachineCount > 1000000
				|| Node.FullClockMachineCount < 0 || Node.FullClockMachineCount > 1000000
				|| Node.ConfiguredClockPercent < 0.0 || Node.ConfiguredClockPercent > 100000.0
				|| Node.PartialClockPercent < 0.0 || Node.PartialClockPercent > 100000.0
				|| Node.MaximumMachineCount < 0.0 || Node.MaximumMachineCount > 1.0e9
				|| Node.SourceCostMultiplier < 0.001 || Node.SourceCostMultiplier > 1.0e9
				|| Node.SomersloopCount < 0 || Node.SomersloopCount > 1024
				|| Node.ProductionBoost <= 0.0 || Node.ProductionBoost > 10000.0
				|| FMath::Abs(Node.PowerMW) > 1.0e12
				|| Node.FuelClassPath.Len() > 1024
				|| Node.FuelDisplayName.Len() > 256
				|| Node.FuelForm.Len() > 64
				|| Node.FuelEnergyValueMJ < 0.0 || Node.FuelEnergyValueMJ > 1.0e15
				|| Node.FuelRatePerMinute < 0.0 || Node.FuelRatePerMinute > 1.0e12
				|| Node.InfrastructureCount < 0
				|| Node.InfrastructureCount > 1000000)
			{
				OutError = TEXT("Der Multiplayer-Plan enthält einen ungültigen oder doppelten Knoten");
				return false;
			}
			NodeIds.Add(Node.Id);
		}

		for (const FSFPPlanEdge& Edge : Plan.Edges)
		{
			if (!NodeIds.Contains(Edge.SourceNodeId)
				|| !NodeIds.Contains(Edge.TargetNodeId)
				|| Edge.ItemName.Len() > 256
				|| Edge.ItemClassPath.Len() > 1024
				|| Edge.Form.Len() > 64
				|| Edge.TransportLabel.Len() > 256
				|| Edge.TransportClassPath.Len() > 1024
				|| Edge.TransportKind.Len() > 64
				|| !FMath::IsFinite(Edge.RatePerMinute)
				|| Edge.RatePerMinute < 0.0
				|| Edge.RatePerMinute > 1.0e12
				|| Edge.RequiredLines < 0
				|| Edge.RequiredLines > 1000000
				|| !FMath::IsFinite(Edge.EstimatedLengthMeters)
				|| Edge.EstimatedLengthMeters < 0.0
				|| Edge.EstimatedLengthMeters > 1.0e9)
			{
				OutError = TEXT("Der Multiplayer-Plan enthält eine ungültige Verbindung");
				return false;
			}
		}

		for (const TPair<FString, double>& Input : Plan.AvailableInputRates)
		{
			if (Input.Key.Len() > 1024
				|| !FMath::IsFinite(Input.Value)
				|| Input.Value < 0.0
				|| Input.Value > 1.0e12)
			{
				OutError = TEXT("Der Multiplayer-Plan enthält ein ungültiges Rohstofflimit");
				return false;
			}
		}
		for (const TPair<FString, FString>& Override : Plan.RecipeOverrides)
		{
			if (Override.Key.Len() > 1024 || Override.Value.Len() > 1024)
			{
				OutError = TEXT("Der Multiplayer-Plan enthält einen ungültigen Rezeptpfad");
				return false;
			}
		}
		for (const TPair<FString, FSFPMachinePlanSettings>& Pair : Plan.MachineSettings)
		{
			const FSFPMachinePlanSettings& Settings = Pair.Value;
			if (Pair.Key.Len() > 1024
				|| Settings.FuelClassPath.Len() > 1024
				|| !FMath::IsFinite(Settings.ClockPercent)
				|| Settings.ClockPercent < 0.1
				|| Settings.ClockPercent > 100000.0
				|| Settings.SomersloopCount < 0
				|| Settings.SomersloopCount > 1024)
			{
				OutError = TEXT("Der Multiplayer-Plan enthält eine ungültige Maschinenkonfiguration");
				return false;
			}
		}
		for (const TPair<FString, FSFPResourceSourceMix>& Pair : Plan.ResourceSourceMixes)
		{
			const FSFPResourceSourceMix& Mix = Pair.Value;
			if (Pair.Key.Len() > 1024
				|| Mix.ImpureCount < 0 || Mix.ImpureCount > 100000
				|| Mix.NormalCount < 0 || Mix.NormalCount > 100000
				|| Mix.PureCount < 0 || Mix.PureCount > 100000)
			{
				OutError = TEXT("Der Multiplayer-Plan enthält einen ungültigen Vorkommensmix");
				return false;
			}
		}
		const auto ValidateCosts = [&OutError](const TArray<FSFPConstructionCost>& Costs) -> bool
		{
			if (Costs.Num() > 10000)
			{
				OutError = TEXT("Der Multiplayer-Plan enthält zu viele Baukosteneinträge");
				return false;
			}
			for (const FSFPConstructionCost& Cost : Costs)
			{
				if (Cost.ItemClassPath.Len() > 1024
					|| Cost.DisplayName.Len() > 256
					|| Cost.Form.Len() > 64
					|| !FMath::IsFinite(Cost.Amount)
					|| Cost.Amount < 0.0
					|| Cost.Amount > 1.0e15)
				{
					OutError = TEXT("Der Multiplayer-Plan enthält ungültige Baukosten");
					return false;
				}
			}
			return true;
		};
		if (!ValidateCosts(Plan.MachineConstructionCosts)
			|| !ValidateCosts(Plan.InfrastructureConstructionCosts)
			|| !ValidateCosts(Plan.ConstructionCosts))
		{
			return false;
		}
		for (const FString& Warning : Plan.Warnings)
		{
			if (Warning.Len() > 4096)
			{
				OutError = TEXT("Der Multiplayer-Plan enthält einen zu langen Hinweistext");
				return false;
			}
		}
		return true;
	}

	bool SerializePlanToJson(
		const FString& PlanName,
		const FSFPPlanResult& Plan,
		const FSFPSharedPlanMetadata* SharedMetadata,
		FString& OutJson,
		FString& OutError)
	{
		OutJson.Reset();
		OutError.Reset();
		if (!ValidatePlan(Plan, OutError))
		{
			return false;
		}

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("schemaVersion"), SFPPlanFileSchemaVersion);
		if (!PlanName.IsEmpty())
		{
			Root->SetStringField(TEXT("planName"), PlanName);
		}
		const FDateTime SavedAt = SharedMetadata != nullptr && SharedMetadata->ModifiedAt.GetTicks() > 0
			? SharedMetadata->ModifiedAt
			: FDateTime::UtcNow();
		Root->SetStringField(TEXT("savedAtUtc"), SavedAt.ToIso8601());
		if (SharedMetadata != nullptr)
		{
			// Ticks exceed JSON's exact integer range, so the concurrency token is a string.
			Root->SetStringField(
				TEXT("serverRevision"),
				FString::Printf(TEXT("%lld"), static_cast<long long>(SharedMetadata->Revision)));
			Root->SetStringField(TEXT("serverOwnerId"), SharedMetadata->OwnerId);
			Root->SetStringField(TEXT("serverOwnerName"), SharedMetadata->OwnerName);
			Root->SetStringField(TEXT("serverUpdatedBy"), SharedMetadata->UpdatedBy);
			Root->SetStringField(TEXT("serverModifiedAtUtc"), SavedAt.ToIso8601());
		}
		Root->SetStringField(TEXT("targetName"), Plan.TargetName);
		Root->SetNumberField(TEXT("targetRatePerMinute"), Plan.TargetRatePerMinute);
		Root->SetNumberField(TEXT("totalEquivalentMachines"), Plan.TotalEquivalentMachines);
		Root->SetNumberField(TEXT("totalBasePowerMW"), Plan.TotalBasePowerMW);
		Root->SetBoolField(TEXT("powerProductionPlan"), Plan.bPowerProductionPlan);
        Root->SetBoolField(TEXT("inputPlanning"), Plan.bInputPlanning);
        Root->SetBoolField(TEXT("inputFeasible"), Plan.bInputFeasible);
        Root->SetStringField(TEXT("inputStatus"), Plan.bInputPlanning ? Plan.ErrorMessage : FString());
        TArray<TSharedPtr<FJsonValue>> Supplies;
        for (const auto& Supply : Plan.Supplies)
        {
            auto O = MakeShared<FJsonObject>();
            O->SetStringField(TEXT("path"), Supply.ItemClassPath);
            O->SetStringField(TEXT("name"), Supply.DisplayName);
            O->SetStringField(TEXT("form"), Supply.Form);
            O->SetNumberField(TEXT("lines"), Supply.Lines);
            O->SetNumberField(TEXT("rate"), Supply.RatePerLine);
            Supplies.Add(MakeShared<FJsonValueObject>(O));
        }
        Root->SetArrayField(TEXT("inputSupplies"), Supplies);
        TArray<TSharedPtr<FJsonValue>> Wishes;
        for (const auto& Target : Plan.InputPlanningTargets)
        {
            auto O = MakeShared<FJsonObject>();
            O->SetStringField(TEXT("path"), Target.ItemClassPath);
            O->SetStringField(TEXT("name"), Target.DisplayName);
            O->SetStringField(TEXT("form"), Target.Form);
            O->SetNumberField(TEXT("rate"), Target.RatePerMinute);
            O->SetBoolField(TEXT("fixed"), Plan.FixedOutputRates.Contains(Target.ItemClassPath));
            Wishes.Add(MakeShared<FJsonValueObject>(O));
        }
        Root->SetArrayField(TEXT("inputWishes"), Wishes);

		Root->SetNumberField(TEXT("requestedNetPowerMW"), Plan.RequestedNetPowerMW);
		Root->SetNumberField(TEXT("powerReservePercent"), Plan.PowerReservePercent);
		Root->SetStringField(TEXT("requestedGeneratorClassPath"), Plan.RequestedGeneratorClassPath);
		Root->SetStringField(TEXT("requestedFuelClassPath"), Plan.RequestedFuelClassPath);
		Root->SetNumberField(TEXT("passiveAlienPowerAugmenters"), Plan.PassiveAlienPowerAugmenters);
		Root->SetNumberField(TEXT("fueledAlienPowerAugmenters"), Plan.FueledAlienPowerAugmenters);
		Root->SetStringField(TEXT("selectedGeneratorClassPath"), Plan.SelectedGeneratorClassPath);
		Root->SetStringField(TEXT("selectedGeneratorDisplayName"), Plan.SelectedGeneratorDisplayName);
		Root->SetStringField(TEXT("selectedFuelClassPath"), Plan.SelectedFuelClassPath);
		Root->SetStringField(TEXT("selectedFuelDisplayName"), Plan.SelectedFuelDisplayName);
		Root->SetStringField(TEXT("selectedFuelForm"), Plan.SelectedFuelForm);
		Root->SetNumberField(TEXT("generatorBasePowerMW"), Plan.GeneratorBasePowerMW);
		Root->SetNumberField(TEXT("generatorPowerMW"), Plan.GeneratorPowerMW);
		Root->SetNumberField(TEXT("equivalentGeneratorCount"), Plan.EquivalentGeneratorCount);
		Root->SetNumberField(TEXT("builtGeneratorCount"), Plan.BuiltGeneratorCount);
		Root->SetNumberField(TEXT("fullClockGeneratorCount"), Plan.FullClockGeneratorCount);
		Root->SetNumberField(TEXT("configuredGeneratorClockPercent"), Plan.ConfiguredGeneratorClockPercent);
		Root->SetNumberField(TEXT("partialGeneratorClockPercent"), Plan.PartialGeneratorClockPercent);
		Root->SetNumberField(TEXT("baseGeneratorGrossPowerMW"), Plan.BaseGeneratorGrossPowerMW);
		Root->SetNumberField(TEXT("alienPowerAugmenterBaseMW"), Plan.AlienPowerAugmenterBaseMW);
		Root->SetNumberField(TEXT("alienPowerMultiplier"), Plan.AlienPowerMultiplier);
		Root->SetNumberField(TEXT("alienPowerContributionMW"), Plan.AlienPowerContributionMW);
		Root->SetStringField(TEXT("alienPowerMatrixItemClassPath"), Plan.AlienPowerMatrixItemClassPath);
		Root->SetStringField(TEXT("alienPowerMatrixDisplayName"), Plan.AlienPowerMatrixDisplayName);
		Root->SetNumberField(TEXT("alienPowerMatrixRatePerMinute"), Plan.AlienPowerMatrixRatePerMinute);
		Root->SetBoolField(TEXT("maximumPowerPlan"), Plan.bMaximumPowerPlan);
		Root->SetBoolField(TEXT("maximumPowerSearchCapped"), Plan.bMaximumPowerSearchCapped);
		Root->SetStringField(TEXT("maximumPowerLimitingResourceClassPath"), Plan.MaximumPowerLimitingResourceClassPath);
		Root->SetStringField(TEXT("maximumPowerLimitingResourceDisplayName"), Plan.MaximumPowerLimitingResourceDisplayName);
		Root->SetNumberField(TEXT("maximumPowerLimitingResourceCapacityPerMinute"), Plan.MaximumPowerLimitingResourceCapacityPerMinute);
		Root->SetNumberField(TEXT("grossPowerMW"), Plan.GrossPowerMW);
		Root->SetNumberField(TEXT("selfConsumptionPowerMW"), Plan.SelfConsumptionPowerMW);
		Root->SetNumberField(TEXT("netPowerMW"), Plan.NetPowerMW);
		Root->SetNumberField(TEXT("reservePowerMW"), Plan.ReservePowerMW);
		Root->SetNumberField(TEXT("fuelRatePerMinute"), Plan.FuelRatePerMinute);
		Root->SetStringField(TEXT("supplementalItemClassPath"), Plan.SupplementalItemClassPath);
		Root->SetStringField(TEXT("supplementalDisplayName"), Plan.SupplementalDisplayName);
		Root->SetStringField(TEXT("supplementalForm"), Plan.SupplementalForm);
		Root->SetNumberField(TEXT("supplementalRatePerMinute"), Plan.SupplementalRatePerMinute);
		Root->SetStringField(TEXT("wasteItemClassPath"), Plan.WasteItemClassPath);
		Root->SetStringField(TEXT("wasteDisplayName"), Plan.WasteDisplayName);
		Root->SetStringField(TEXT("wasteForm"), Plan.WasteForm);
		Root->SetNumberField(TEXT("wasteRatePerMinute"), Plan.WasteRatePerMinute);
		Root->SetNumberField(TEXT("maxDepth"), Plan.MaxDepth);
		Root->SetNumberField(TEXT("estimatedConnectionLengthMeters"), Plan.EstimatedConnectionLengthMeters);
		Root->SetNumberField(TEXT("splitterCount"), Plan.SplitterCount);
		Root->SetNumberField(TEXT("mergerCount"), Plan.MergerCount);
		Root->SetNumberField(TEXT("conveyorLineCount"), Plan.ConveyorLineCount);
		Root->SetNumberField(TEXT("pipelineLineCount"), Plan.PipelineLineCount);
		Root->SetNumberField(TEXT("estimatedConveyorMeters"), Plan.EstimatedConveyorMeters);
		Root->SetNumberField(TEXT("estimatedPipelineMeters"), Plan.EstimatedPipelineMeters);
		Root->SetBoolField(TEXT("onlyAvailableRecipes"), Plan.bOnlyAvailableRecipes);
		Root->SetStringField(TEXT("selectedConveyorClassPath"), Plan.SelectedConveyorClassPath);
		Root->SetStringField(TEXT("selectedConveyorDisplayName"), Plan.SelectedConveyorDisplayName);
		Root->SetNumberField(TEXT("selectedConveyorCapacityPerMinute"), Plan.SelectedConveyorCapacityPerMinute);
		Root->SetStringField(TEXT("selectedConveyorLiftClassPath"), Plan.SelectedConveyorLiftClassPath);
		Root->SetStringField(TEXT("selectedConveyorLiftDisplayName"), Plan.SelectedConveyorLiftDisplayName);
		Root->SetNumberField(TEXT("selectedConveyorLiftCapacityPerMinute"), Plan.SelectedConveyorLiftCapacityPerMinute);

		TArray<TSharedPtr<FJsonValue>> Targets;
		Targets.Reserve(Plan.Targets.Num());
		for (const FSFPPlanTarget& Target : Plan.Targets)
		{
			if (!Target.ItemClassPath.IsEmpty() && Target.RatePerMinute > 0.0)
			{
				Targets.Add(MakeShared<FJsonValueObject>(PlanTargetToJson(Target)));
			}
		}
		Root->SetArrayField(TEXT("targets"), MoveTemp(Targets));

		TArray<TSharedPtr<FJsonValue>> Nodes;
		Nodes.Reserve(Plan.Nodes.Num());
		for (const FSFPPlanNode& Node : Plan.Nodes)
		{
			Nodes.Add(MakeShared<FJsonValueObject>(PersistedNodeToJson(Node)));
		}
		Root->SetArrayField(TEXT("nodes"), MoveTemp(Nodes));

		TArray<TSharedPtr<FJsonValue>> Edges;
		Edges.Reserve(Plan.Edges.Num());
		for (const FSFPPlanEdge& Edge : Plan.Edges)
		{
			Edges.Add(MakeShared<FJsonValueObject>(PersistedEdgeToJson(Edge)));
		}
		Root->SetArrayField(TEXT("edges"), MoveTemp(Edges));

		TArray<TSharedPtr<FJsonValue>> ConstructionCosts;
		ConstructionCosts.Reserve(Plan.ConstructionCosts.Num());
		for (const FSFPConstructionCost& Cost : Plan.ConstructionCosts)
		{
			ConstructionCosts.Add(MakeShared<FJsonValueObject>(ConstructionCostToJson(Cost)));
		}
		Root->SetArrayField(TEXT("constructionCosts"), MoveTemp(ConstructionCosts));

		TArray<TSharedPtr<FJsonValue>> MachineConstructionCosts;
		for (const FSFPConstructionCost& Cost : Plan.MachineConstructionCosts)
		{
			MachineConstructionCosts.Add(MakeShared<FJsonValueObject>(ConstructionCostToJson(Cost)));
		}
		Root->SetArrayField(TEXT("machineConstructionCosts"), MoveTemp(MachineConstructionCosts));

		TArray<TSharedPtr<FJsonValue>> InfrastructureConstructionCosts;
		for (const FSFPConstructionCost& Cost : Plan.InfrastructureConstructionCosts)
		{
			InfrastructureConstructionCosts.Add(MakeShared<FJsonValueObject>(ConstructionCostToJson(Cost)));
		}
		Root->SetArrayField(TEXT("infrastructureConstructionCosts"), MoveTemp(InfrastructureConstructionCosts));

		TSharedRef<FJsonObject> RecipeOverrides = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Pair : Plan.RecipeOverrides)
		{
			RecipeOverrides->SetStringField(Pair.Key, Pair.Value);
		}
		Root->SetObjectField(TEXT("recipeOverrides"), RecipeOverrides);

		TSharedRef<FJsonObject> MachineSettings = MakeShared<FJsonObject>();
		for (const TPair<FString, FSFPMachinePlanSettings>& Pair : Plan.MachineSettings)
		{
			TSharedRef<FJsonObject> Settings = MakeShared<FJsonObject>();
			Settings->SetNumberField(TEXT("clockPercent"), Pair.Value.ClockPercent);
			Settings->SetNumberField(TEXT("somersloopCount"), Pair.Value.SomersloopCount);
			Settings->SetStringField(TEXT("fuelClassPath"), Pair.Value.FuelClassPath);
			MachineSettings->SetObjectField(Pair.Key, Settings);
		}
		Root->SetObjectField(TEXT("machineSettings"), MachineSettings);

		TSharedRef<FJsonObject> ResourceSourceMixes = MakeShared<FJsonObject>();
		for (const TPair<FString, FSFPResourceSourceMix>& Pair : Plan.ResourceSourceMixes)
		{
			TSharedRef<FJsonObject> Mix = MakeShared<FJsonObject>();
			Mix->SetBoolField(TEXT("enabled"), Pair.Value.bEnabled);
			Mix->SetBoolField(TEXT("useOccupiedSources"), Pair.Value.bUseOccupiedSources);
			Mix->SetBoolField(TEXT("impureLimited"), Pair.Value.bImpureLimited);
			Mix->SetBoolField(TEXT("normalLimited"), Pair.Value.bNormalLimited);
			Mix->SetBoolField(TEXT("pureLimited"), Pair.Value.bPureLimited);
			Mix->SetNumberField(TEXT("impureCount"), Pair.Value.ImpureCount);
			Mix->SetNumberField(TEXT("normalCount"), Pair.Value.NormalCount);
			Mix->SetNumberField(TEXT("pureCount"), Pair.Value.PureCount);
			ResourceSourceMixes->SetObjectField(Pair.Key, Mix);
		}
		Root->SetObjectField(TEXT("resourceSourceMixes"), ResourceSourceMixes);

		TSharedRef<FJsonObject> AvailableInputRates = MakeShared<FJsonObject>();
		for (const TPair<FString, double>& Pair : Plan.AvailableInputRates)
		{
			AvailableInputRates->SetNumberField(Pair.Key, Pair.Value);
		}
		Root->SetObjectField(TEXT("availableInputRates"), AvailableInputRates);

		TArray<TSharedPtr<FJsonValue>> Warnings;
		Warnings.Reserve(Plan.Warnings.Num());
		for (const FString& Warning : Plan.Warnings)
		{
			Warnings.Add(MakeShared<FJsonValueString>(Warning));
		}
		Root->SetArrayField(TEXT("warnings"), MoveTemp(Warnings));

		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
		if (!FJsonSerializer::Serialize(Root, Writer))
		{
			OutError = TEXT("Der Produktionsplan konnte nicht als JSON serialisiert werden");
			return false;
		}
		return true;
	}

	bool SavePlanToPath(
		const FString& Path,
		const FString& PlanName,
		const FSFPPlanResult& Plan,
		FString& OutError,
		const FSFPSharedPlanMetadata* SharedMetadata = nullptr)
	{
		FString JsonText;
		if (!SerializePlanToJson(PlanName, Plan, SharedMetadata, JsonText, OutError))
		{
			return false;
		}

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		const FString TemporaryPath = Path + TEXT(".tmp");
		if (!FFileHelper::SaveStringToFile(
			JsonText,
			*TemporaryPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Produktionsplan konnte nicht gespeichert werden: %s"), *Path);
			return false;
		}
		if (!IFileManager::Get().Move(*Path, *TemporaryPath, true, true))
		{
			IFileManager::Get().Delete(*TemporaryPath, false, true, true);
			OutError = FString::Printf(TEXT("Produktionsplan konnte nicht atomar gespeichert werden: %s"), *Path);
			return false;
		}
		return true;
	}

	TSharedPtr<FSFPPlanResult> DeserializePlanFromJson(
		const FString& JsonText,
		FString& OutError,
		FString* OutPlanName = nullptr,
		FSFPSharedPlanMetadata* OutSharedMetadata = nullptr)
	{
		OutError.Reset();
		if (OutPlanName != nullptr)
		{
			OutPlanName->Reset();
		}
		if (OutSharedMetadata != nullptr)
		{
			*OutSharedMetadata = FSFPSharedPlanMetadata();
		}

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = TEXT("Gespeicherter Produktionsplan enthält ungültiges JSON");
			return nullptr;
		}

		double SchemaVersion = 0.0;
		if (!Root->TryGetNumberField(TEXT("schemaVersion"), SchemaVersion)
			|| !IsFiniteInt32(SchemaVersion)
			|| FMath::RoundToInt(SchemaVersion) < 1
			|| FMath::RoundToInt(SchemaVersion) > SFPPlanFileSchemaVersion)
		{
			OutError = TEXT("Gespeicherter Produktionsplan hat eine nicht unterstützte Version");
			return nullptr;
		}
		const int32 LoadedSchemaVersion = FMath::RoundToInt(SchemaVersion);
		if (OutPlanName != nullptr)
		{
			Root->TryGetStringField(TEXT("planName"), *OutPlanName);
		}
		if (OutSharedMetadata != nullptr)
		{
			FString RevisionText;
			if (Root->TryGetStringField(TEXT("serverRevision"), RevisionText))
			{
				OutSharedMetadata->Revision = FCString::Atoi64(*RevisionText);
			}
			Root->TryGetStringField(TEXT("serverOwnerId"), OutSharedMetadata->OwnerId);
			Root->TryGetStringField(TEXT("serverOwnerName"), OutSharedMetadata->OwnerName);
			Root->TryGetStringField(TEXT("serverUpdatedBy"), OutSharedMetadata->UpdatedBy);
			FString ModifiedAtText;
			if (Root->TryGetStringField(TEXT("serverModifiedAtUtc"), ModifiedAtText))
			{
				FDateTime::ParseIso8601(*ModifiedAtText, OutSharedMetadata->ModifiedAt);
			}
		}

		TSharedPtr<FSFPPlanResult> Plan = MakeShared<FSFPPlanResult>();
		double SummaryNumber = 0.0;
		Plan->bSuccess = true;
		Root->TryGetStringField(TEXT("targetName"), Plan->TargetName);
		Root->TryGetNumberField(TEXT("targetRatePerMinute"), Plan->TargetRatePerMinute);
		Root->TryGetNumberField(TEXT("totalEquivalentMachines"), Plan->TotalEquivalentMachines);
		Root->TryGetNumberField(TEXT("totalBasePowerMW"), Plan->TotalBasePowerMW);
		Root->TryGetBoolField(TEXT("powerProductionPlan"), Plan->bPowerProductionPlan);
        Root->TryGetBoolField(TEXT("inputPlanning"), Plan->bInputPlanning);
        Root->TryGetBoolField(TEXT("inputFeasible"), Plan->bInputFeasible);
        if (Plan->bInputPlanning) Root->TryGetStringField(TEXT("inputStatus"), Plan->ErrorMessage);
        const TArray<TSharedPtr<FJsonValue>>* InputValues = nullptr;
        if (Root->TryGetArrayField(TEXT("inputSupplies"), InputValues))
        {
            if (InputValues->Num() > 32) { OutError = TEXT("Zu viele Eingänge im Plan."); return nullptr; }
            for (const auto& V : *InputValues)
            {
                if (!V.IsValid() || V->Type != EJson::Object) { OutError = TEXT("Ungültiger Eingang."); return nullptr; }
                const auto O = V->AsObject(); FSFPPlanSupply Supply; double Lines = 0;
                if (!O->TryGetStringField(TEXT("path"), Supply.ItemClassPath) || Supply.ItemClassPath.IsEmpty() || Supply.ItemClassPath.Len() > 2048
                    || !O->TryGetNumberField(TEXT("lines"), Lines) || !IsFiniteInt32(Lines) || Lines < 1 || Lines > 1000 || Lines != static_cast<double>(static_cast<int32>(Lines))
                    || !O->TryGetNumberField(TEXT("rate"), Supply.RatePerLine) || !FMath::IsFinite(Supply.RatePerLine) || Supply.RatePerLine < 0 || Supply.RatePerLine > 1000000)
                    { OutError = TEXT("Ungültige Eingangsmenge."); return nullptr; }
                Supply.Lines = static_cast<int32>(Lines);
                O->TryGetStringField(TEXT("name"), Supply.DisplayName); O->TryGetStringField(TEXT("form"), Supply.Form);
                Plan->Supplies.Add(Supply);
                Plan->SuppliedInputRates.FindOrAdd(Supply.ItemClassPath) += Supply.Lines * Supply.RatePerLine;
            }
        }
        InputValues = nullptr;
        if (Root->TryGetArrayField(TEXT("inputWishes"), InputValues))
        {
            if (InputValues->Num() > 32) { OutError = TEXT("Zu viele Ausgaben im Plan."); return nullptr; }
            TSet<FString> SeenWishes;
            for (const auto& V : *InputValues)
            {
                if (!V.IsValid() || V->Type != EJson::Object) { OutError = TEXT("Ungültige Ausgabe."); return nullptr; }
                const auto O = V->AsObject(); FSFPPlanTarget Target; bool Fixed = false;
                if (!O->TryGetStringField(TEXT("path"), Target.ItemClassPath) || Target.ItemClassPath.IsEmpty() || Target.ItemClassPath.Len() > 2048 || SeenWishes.Contains(Target.ItemClassPath)
                    || !O->TryGetNumberField(TEXT("rate"), Target.RatePerMinute) || !FMath::IsFinite(Target.RatePerMinute) || Target.RatePerMinute < 0 || Target.RatePerMinute > 1000000)
                    { OutError = TEXT("Ungültige Zielmenge."); return nullptr; }
                SeenWishes.Add(Target.ItemClassPath);
                O->TryGetStringField(TEXT("name"), Target.DisplayName); O->TryGetStringField(TEXT("form"), Target.Form);
                O->TryGetBoolField(TEXT("fixed"), Fixed);
                Plan->InputPlanningTargets.Add(Target);
                if (Fixed) Plan->FixedOutputRates.Add(Target.ItemClassPath, Target.RatePerMinute);
            }
        }
        if (Plan->bInputPlanning && (Plan->Supplies.IsEmpty() || Plan->InputPlanningTargets.IsEmpty()))
            { OutError = TEXT("Eingangsplan unvollständig."); return nullptr; }

		Root->TryGetNumberField(TEXT("requestedNetPowerMW"), Plan->RequestedNetPowerMW);
		Root->TryGetNumberField(TEXT("powerReservePercent"), Plan->PowerReservePercent);
		Root->TryGetBoolField(TEXT("maximumPowerPlan"), Plan->bMaximumPowerPlan);
		Root->TryGetBoolField(TEXT("maximumPowerSearchCapped"), Plan->bMaximumPowerSearchCapped);
		Root->TryGetStringField(TEXT("maximumPowerLimitingResourceClassPath"), Plan->MaximumPowerLimitingResourceClassPath);
		Root->TryGetStringField(TEXT("maximumPowerLimitingResourceDisplayName"), Plan->MaximumPowerLimitingResourceDisplayName);
		Root->TryGetNumberField(TEXT("maximumPowerLimitingResourceCapacityPerMinute"), Plan->MaximumPowerLimitingResourceCapacityPerMinute);
		Root->TryGetStringField(TEXT("requestedGeneratorClassPath"), Plan->RequestedGeneratorClassPath);
		Root->TryGetStringField(TEXT("requestedFuelClassPath"), Plan->RequestedFuelClassPath);
		if (Root->TryGetNumberField(TEXT("passiveAlienPowerAugmenters"), SummaryNumber) && IsFiniteInt32(SummaryNumber))
		{
			Plan->PassiveAlienPowerAugmenters = FMath::Max(0, FMath::RoundToInt(SummaryNumber));
		}
		if (Root->TryGetNumberField(TEXT("fueledAlienPowerAugmenters"), SummaryNumber) && IsFiniteInt32(SummaryNumber))
		{
			Plan->FueledAlienPowerAugmenters = FMath::Max(0, FMath::RoundToInt(SummaryNumber));
		}
		Root->TryGetStringField(TEXT("selectedGeneratorClassPath"), Plan->SelectedGeneratorClassPath);
		Root->TryGetStringField(TEXT("selectedGeneratorDisplayName"), Plan->SelectedGeneratorDisplayName);
		Root->TryGetStringField(TEXT("selectedFuelClassPath"), Plan->SelectedFuelClassPath);
		Root->TryGetStringField(TEXT("selectedFuelDisplayName"), Plan->SelectedFuelDisplayName);
		Root->TryGetStringField(TEXT("selectedFuelForm"), Plan->SelectedFuelForm);
		Root->TryGetNumberField(TEXT("generatorBasePowerMW"), Plan->GeneratorBasePowerMW);
		Root->TryGetNumberField(TEXT("generatorPowerMW"), Plan->GeneratorPowerMW);
		Root->TryGetNumberField(TEXT("equivalentGeneratorCount"), Plan->EquivalentGeneratorCount);
		SummaryNumber = 0.0;
		if (Root->TryGetNumberField(TEXT("builtGeneratorCount"), SummaryNumber) && IsFiniteInt32(SummaryNumber))
		{
			Plan->BuiltGeneratorCount = FMath::Max(0, FMath::RoundToInt(SummaryNumber));
		}
		SummaryNumber = 0.0;
		if (Root->TryGetNumberField(TEXT("fullClockGeneratorCount"), SummaryNumber) && IsFiniteInt32(SummaryNumber))
		{
			Plan->FullClockGeneratorCount = FMath::Max(0, FMath::RoundToInt(SummaryNumber));
		}
		Root->TryGetNumberField(TEXT("configuredGeneratorClockPercent"), Plan->ConfiguredGeneratorClockPercent);
		Root->TryGetNumberField(TEXT("partialGeneratorClockPercent"), Plan->PartialGeneratorClockPercent);
		if (!FMath::IsFinite(Plan->ConfiguredGeneratorClockPercent) || Plan->ConfiguredGeneratorClockPercent <= 0.0)
		{
			Plan->ConfiguredGeneratorClockPercent = 100.0;
		}
		if (Plan->GeneratorBasePowerMW <= KINDA_SMALL_NUMBER && Plan->GeneratorPowerMW > KINDA_SMALL_NUMBER)
		{
			Plan->GeneratorBasePowerMW = Plan->GeneratorPowerMW * 100.0 / Plan->ConfiguredGeneratorClockPercent;
		}
		Root->TryGetNumberField(TEXT("baseGeneratorGrossPowerMW"), Plan->BaseGeneratorGrossPowerMW);
		Root->TryGetNumberField(TEXT("alienPowerAugmenterBaseMW"), Plan->AlienPowerAugmenterBaseMW);
		Root->TryGetNumberField(TEXT("alienPowerMultiplier"), Plan->AlienPowerMultiplier);
		Root->TryGetNumberField(TEXT("alienPowerContributionMW"), Plan->AlienPowerContributionMW);
		Root->TryGetStringField(TEXT("alienPowerMatrixItemClassPath"), Plan->AlienPowerMatrixItemClassPath);
		Root->TryGetStringField(TEXT("alienPowerMatrixDisplayName"), Plan->AlienPowerMatrixDisplayName);
		Root->TryGetNumberField(TEXT("alienPowerMatrixRatePerMinute"), Plan->AlienPowerMatrixRatePerMinute);
		Root->TryGetNumberField(TEXT("grossPowerMW"), Plan->GrossPowerMW);
		Root->TryGetNumberField(TEXT("selfConsumptionPowerMW"), Plan->SelfConsumptionPowerMW);
		Root->TryGetNumberField(TEXT("netPowerMW"), Plan->NetPowerMW);
		Root->TryGetNumberField(TEXT("reservePowerMW"), Plan->ReservePowerMW);
		Root->TryGetNumberField(TEXT("fuelRatePerMinute"), Plan->FuelRatePerMinute);
		Root->TryGetStringField(TEXT("supplementalItemClassPath"), Plan->SupplementalItemClassPath);
		Root->TryGetStringField(TEXT("supplementalDisplayName"), Plan->SupplementalDisplayName);
		Root->TryGetStringField(TEXT("supplementalForm"), Plan->SupplementalForm);
		Root->TryGetNumberField(TEXT("supplementalRatePerMinute"), Plan->SupplementalRatePerMinute);
		Root->TryGetStringField(TEXT("wasteItemClassPath"), Plan->WasteItemClassPath);
		Root->TryGetStringField(TEXT("wasteDisplayName"), Plan->WasteDisplayName);
		Root->TryGetStringField(TEXT("wasteForm"), Plan->WasteForm);
		Root->TryGetNumberField(TEXT("wasteRatePerMinute"), Plan->WasteRatePerMinute);
		if (Root->TryGetNumberField(TEXT("maxDepth"), SchemaVersion) && IsFiniteInt32(SchemaVersion))
		{
			Plan->MaxDepth = FMath::Max(0, FMath::RoundToInt(SchemaVersion));
		}
		Root->TryGetNumberField(TEXT("estimatedConnectionLengthMeters"), Plan->EstimatedConnectionLengthMeters);
		if (Plan->EstimatedConnectionLengthMeters <= 0.0)
		{
			Plan->EstimatedConnectionLengthMeters = 10.0;
		}
		SummaryNumber = 0.0;
		if (Root->TryGetNumberField(TEXT("splitterCount"), SummaryNumber) && IsFiniteInt32(SummaryNumber)) Plan->SplitterCount = FMath::Max(0, FMath::RoundToInt(SummaryNumber));
		if (Root->TryGetNumberField(TEXT("mergerCount"), SummaryNumber) && IsFiniteInt32(SummaryNumber)) Plan->MergerCount = FMath::Max(0, FMath::RoundToInt(SummaryNumber));
		if (Root->TryGetNumberField(TEXT("conveyorLineCount"), SummaryNumber) && IsFiniteInt32(SummaryNumber)) Plan->ConveyorLineCount = FMath::Max(0, FMath::RoundToInt(SummaryNumber));
		if (Root->TryGetNumberField(TEXT("pipelineLineCount"), SummaryNumber) && IsFiniteInt32(SummaryNumber)) Plan->PipelineLineCount = FMath::Max(0, FMath::RoundToInt(SummaryNumber));
		Root->TryGetNumberField(TEXT("estimatedConveyorMeters"), Plan->EstimatedConveyorMeters);
		Root->TryGetNumberField(TEXT("estimatedPipelineMeters"), Plan->EstimatedPipelineMeters);
		Root->TryGetBoolField(TEXT("onlyAvailableRecipes"), Plan->bOnlyAvailableRecipes);
		Root->TryGetStringField(TEXT("selectedConveyorClassPath"), Plan->SelectedConveyorClassPath);
		Root->TryGetStringField(TEXT("selectedConveyorDisplayName"), Plan->SelectedConveyorDisplayName);
		Root->TryGetNumberField(TEXT("selectedConveyorCapacityPerMinute"), Plan->SelectedConveyorCapacityPerMinute);
		Root->TryGetStringField(TEXT("selectedConveyorLiftClassPath"), Plan->SelectedConveyorLiftClassPath);
		Root->TryGetStringField(TEXT("selectedConveyorLiftDisplayName"), Plan->SelectedConveyorLiftDisplayName);
		Root->TryGetNumberField(TEXT("selectedConveyorLiftCapacityPerMinute"), Plan->SelectedConveyorLiftCapacityPerMinute);

		const TArray<TSharedPtr<FJsonValue>>* TargetValues = nullptr;
		if (Root->TryGetArrayField(TEXT("targets"), TargetValues) && TargetValues != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& Value : *TargetValues)
			{
				FSFPPlanTarget Target;
				if (Value.IsValid() && Value->Type == EJson::Object && JsonToPlanTarget(Value->AsObject(), Target))
				{
					Plan->Targets.Add(MoveTemp(Target));
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* NodeValues = nullptr;
		if (!Root->TryGetArrayField(TEXT("nodes"), NodeValues) || NodeValues == nullptr)
		{
			OutError = TEXT("Gespeicherter Produktionsplan enthält keine Knoten");
			return nullptr;
		}
		for (const TSharedPtr<FJsonValue>& Value : *NodeValues)
		{
			FSFPPlanNode Node;
			if (Value.IsValid() && Value->Type == EJson::Object && JsonToPersistedNode(Value->AsObject(), Node))
			{
				Plan->Nodes.Add(MoveTemp(Node));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* EdgeValues = nullptr;
		if (!Root->TryGetArrayField(TEXT("edges"), EdgeValues) || EdgeValues == nullptr)
		{
			OutError = TEXT("Gespeicherter Produktionsplan enthält keine Verbindungen");
			return nullptr;
		}
		for (const TSharedPtr<FJsonValue>& Value : *EdgeValues)
		{
			FSFPPlanEdge Edge;
			if (Value.IsValid() && Value->Type == EJson::Object && JsonToPersistedEdge(Value->AsObject(), Edge))
			{
				Plan->Edges.Add(MoveTemp(Edge));
			}
		}

		// Schema 1/2 stored only one summary target. Reconstruct target rows from
		// the graph so old saved plans remain editable in the multi-target UI.
		if (!Plan->bPowerProductionPlan && Plan->Targets.IsEmpty())
		{
			for (const FSFPPlanNode& Node : Plan->Nodes)
			{
				if (Node.Type != ESFPPlanNodeType::Target)
				{
					continue;
				}
				FSFPPlanTarget Target;
				Target.ItemClassPath = Node.ClassPath;
				Target.DisplayName = Node.Title;
				Target.RatePerMinute = Node.RatePerMinute;
				for (const FSFPPlanEdge& Edge : Plan->Edges)
				{
					if (Edge.TargetNodeId == Node.Id
						&& (Node.ClassPath.IsEmpty() || Edge.ItemClassPath == Node.ClassPath))
					{
						if (Target.ItemClassPath.IsEmpty())
						{
							Target.ItemClassPath = Edge.ItemClassPath;
						}
						Target.Form = Edge.Form;
						if (Target.RatePerMinute <= 0.0)
						{
							Target.RatePerMinute = Edge.RatePerMinute;
						}
						break;
					}
				}
				if (!Target.ItemClassPath.IsEmpty() && Target.RatePerMinute > 0.0)
				{
					Plan->Targets.Add(MoveTemp(Target));
				}
			}
		}
		if (!Plan->bPowerProductionPlan
			&& Plan->Targets.IsEmpty()
			&& !Plan->TargetName.IsEmpty()
			&& Plan->TargetRatePerMinute > 0.0)
		{
			FSFPPlanTarget LegacyTarget;
			LegacyTarget.DisplayName = Plan->TargetName;
			LegacyTarget.RatePerMinute = Plan->TargetRatePerMinute;
			Plan->Targets.Add(MoveTemp(LegacyTarget));
		}

		const TArray<TSharedPtr<FJsonValue>>* CostValues = nullptr;
		if (Root->TryGetArrayField(TEXT("constructionCosts"), CostValues) && CostValues != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& Value : *CostValues)
			{
				FSFPConstructionCost Cost;
				if (Value.IsValid() && Value->Type == EJson::Object && JsonToConstructionCost(Value->AsObject(), Cost))
				{
					Plan->ConstructionCosts.Add(MoveTemp(Cost));
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* MachineCostValues = nullptr;
		if (Root->TryGetArrayField(TEXT("machineConstructionCosts"), MachineCostValues) && MachineCostValues != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& Value : *MachineCostValues)
			{
				FSFPConstructionCost Cost;
				if (Value.IsValid() && Value->Type == EJson::Object && JsonToConstructionCost(Value->AsObject(), Cost))
				{
					Plan->MachineConstructionCosts.Add(MoveTemp(Cost));
				}
			}
		}
		if (LoadedSchemaVersion == 1 && Plan->MachineConstructionCosts.IsEmpty())
		{
			Plan->MachineConstructionCosts = Plan->ConstructionCosts;
		}

		const TArray<TSharedPtr<FJsonValue>>* InfrastructureCostValues = nullptr;
		if (Root->TryGetArrayField(TEXT("infrastructureConstructionCosts"), InfrastructureCostValues) && InfrastructureCostValues != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& Value : *InfrastructureCostValues)
			{
				FSFPConstructionCost Cost;
				if (Value.IsValid() && Value->Type == EJson::Object && JsonToConstructionCost(Value->AsObject(), Cost))
				{
					Plan->InfrastructureConstructionCosts.Add(MoveTemp(Cost));
				}
			}
		}

		const TSharedPtr<FJsonObject>* RecipeOverrideObject = nullptr;
		if (Root->TryGetObjectField(TEXT("recipeOverrides"), RecipeOverrideObject)
			&& RecipeOverrideObject != nullptr
			&& RecipeOverrideObject->IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*RecipeOverrideObject)->Values)
			{
				if (Pair.Value.IsValid() && Pair.Value->Type == EJson::String)
				{
					Plan->RecipeOverrides.Add(Pair.Key, Pair.Value->AsString());
				}
			}
		}

		const TSharedPtr<FJsonObject>* MachineSettingsObject = nullptr;
		if (Root->TryGetObjectField(TEXT("machineSettings"), MachineSettingsObject)
			&& MachineSettingsObject != nullptr
			&& MachineSettingsObject->IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*MachineSettingsObject)->Values)
			{
				if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object) continue;
				const TSharedPtr<FJsonObject> SettingsJson = Pair.Value->AsObject();
				if (!SettingsJson.IsValid()) continue;
				FSFPMachinePlanSettings Settings;
				SettingsJson->TryGetNumberField(TEXT("clockPercent"), Settings.ClockPercent);
				double SloopNumber = 0.0;
				if (SettingsJson->TryGetNumberField(TEXT("somersloopCount"), SloopNumber) && IsFiniteInt32(SloopNumber))
				{
					Settings.SomersloopCount = FMath::Max(0, FMath::RoundToInt(SloopNumber));
				}
				SettingsJson->TryGetStringField(TEXT("fuelClassPath"), Settings.FuelClassPath);
				if (FMath::IsFinite(Settings.ClockPercent) && Settings.ClockPercent > 0.0)
				{
					Plan->MachineSettings.Add(Pair.Key, MoveTemp(Settings));
				}
			}
		}

		const TSharedPtr<FJsonObject>* ResourceSourceMixesObject = nullptr;
		if (Root->TryGetObjectField(TEXT("resourceSourceMixes"), ResourceSourceMixesObject)
			&& ResourceSourceMixesObject != nullptr
			&& ResourceSourceMixesObject->IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ResourceSourceMixesObject)->Values)
			{
				if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object) continue;
				const TSharedPtr<FJsonObject> MixJson = Pair.Value->AsObject();
				if (!MixJson.IsValid()) continue;
				FSFPResourceSourceMix Mix;
				MixJson->TryGetBoolField(TEXT("enabled"), Mix.bEnabled);
				MixJson->TryGetBoolField(TEXT("useOccupiedSources"), Mix.bUseOccupiedSources);
				// r4-r6 stored every count as a hard cap. Preserve that meaning when
				// loading an older plan that has no explicit limitation flags.
				if (!MixJson->TryGetBoolField(TEXT("impureLimited"), Mix.bImpureLimited))
					Mix.bImpureLimited = true;
				if (!MixJson->TryGetBoolField(TEXT("normalLimited"), Mix.bNormalLimited))
					Mix.bNormalLimited = true;
				if (!MixJson->TryGetBoolField(TEXT("pureLimited"), Mix.bPureLimited))
					Mix.bPureLimited = true;
				double Number = 0.0;
				if (MixJson->TryGetNumberField(TEXT("impureCount"), Number) && IsFiniteInt32(Number))
					Mix.ImpureCount = FMath::Clamp(FMath::RoundToInt(Number), 0, 100000);
				if (MixJson->TryGetNumberField(TEXT("normalCount"), Number) && IsFiniteInt32(Number))
					Mix.NormalCount = FMath::Clamp(FMath::RoundToInt(Number), 0, 100000);
				if (MixJson->TryGetNumberField(TEXT("pureCount"), Number) && IsFiniteInt32(Number))
					Mix.PureCount = FMath::Clamp(FMath::RoundToInt(Number), 0, 100000);
				Plan->ResourceSourceMixes.Add(Pair.Key, Mix);
			}
		}

		const TSharedPtr<FJsonObject>* AvailableInputObject = nullptr;
		if (Root->TryGetObjectField(TEXT("availableInputRates"), AvailableInputObject)
			&& AvailableInputObject != nullptr
			&& AvailableInputObject->IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*AvailableInputObject)->Values)
			{
				double AvailableRate = 0.0;
				if (Pair.Value.IsValid() && Pair.Value->TryGetNumber(AvailableRate) && AvailableRate >= 0.0)
				{
					Plan->AvailableInputRates.Add(Pair.Key, AvailableRate);
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* WarningValues = nullptr;
		if (Root->TryGetArrayField(TEXT("warnings"), WarningValues) && WarningValues != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& Value : *WarningValues)
			{
				if (Value.IsValid() && Value->Type == EJson::String)
				{
					Plan->Warnings.Add(Value->AsString());
				}
			}
		}

		if (Plan->Nodes.IsEmpty() || Plan->Edges.IsEmpty())
		{
			OutError = TEXT("Gespeicherter Produktionsplan ist leer oder beschädigt");
			return nullptr;
		}
		return Plan;
	}

	TSharedPtr<FSFPPlanResult> LoadPlanFromPath(
		const FString& Path,
		FString& OutError,
		FString* OutPlanName = nullptr,
		FSFPSharedPlanMetadata* OutSharedMetadata = nullptr)
	{
		OutError.Reset();
		if (!IFileManager::Get().FileExists(*Path))
		{
			return nullptr;
		}

		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *Path))
		{
			OutError = FString::Printf(TEXT("Gespeicherter Produktionsplan ist nicht lesbar: %s"), *Path);
			return nullptr;
		}
		TSharedPtr<FSFPPlanResult> Plan = DeserializePlanFromJson(
			JsonText,
			OutError,
			OutPlanName,
			OutSharedMetadata);
		return Plan;
	}

	bool BuildNamedPlanFileName(
		const FString& Name,
		FString& OutCleanName,
		FString& OutFileName,
		FString& OutError)
	{
		OutCleanName = Name.TrimStartAndEnd();
		OutFileName.Reset();
		OutError.Reset();
		if (OutCleanName.IsEmpty())
		{
			OutError = TEXT("Bitte einen Namen für den Produktionsplan eingeben");
			return false;
		}
		if (OutCleanName.Len() > SFPMaxPlanNameLength)
		{
			OutError = FString::Printf(TEXT("Der Planname darf höchstens %d Zeichen lang sein"), SFPMaxPlanNameLength);
			return false;
		}

		FString SafeName = FPaths::MakeValidFileName(OutCleanName, TEXT('_'));
		SafeName = FPaths::GetCleanFilename(SafeName);
		if (SafeName.IsEmpty())
		{
			SafeName = TEXT("Plan");
		}
		if (SafeName.EndsWith(TEXT(".json"), ESearchCase::IgnoreCase))
		{
			SafeName.LeftChopInline(5);
		}
		OutFileName = TEXT("Plan_") + SafeName + TEXT(".json");
		return true;
	}

	void FillSavedPlanInfo(
		FSFPSavedPlanInfo& OutInfo,
		const FString& FileName,
		const FString& PlanName,
		const FSFPPlanResult& Plan,
		const FString& Path,
		const FSFPSharedPlanMetadata* SharedMetadata = nullptr)
	{
		OutInfo = FSFPSavedPlanInfo();
		OutInfo.Name = PlanName.IsEmpty() ? FPaths::GetBaseFilename(FileName) : PlanName;
		OutInfo.FileName = FileName;
		OutInfo.TargetName = Plan.TargetName.Left(160);
		OutInfo.TargetRatePerMinute = Plan.TargetRatePerMinute;
		OutInfo.TargetCount = Plan.Targets.Num();
		OutInfo.bPowerProductionPlan = Plan.bPowerProductionPlan;
		OutInfo.ModifiedAt = IFileManager::Get().GetTimeStamp(*Path);
		if (SharedMetadata != nullptr)
		{
			OutInfo.Revision = SharedMetadata->Revision;
			OutInfo.OwnerId = SharedMetadata->OwnerId;
			OutInfo.OwnerName = SharedMetadata->OwnerName;
			OutInfo.UpdatedBy = SharedMetadata->UpdatedBy;
			if (SharedMetadata->ModifiedAt.GetTicks() > 0)
			{
				OutInfo.ModifiedAt = SharedMetadata->ModifiedAt;
			}
		}
	}

	bool IsSafeNamedPlanFileName(const FString& FileName)
	{
		return !FileName.IsEmpty()
			&& FileName == FPaths::GetCleanFilename(FileName)
			&& FileName.EndsWith(TEXT(".json"), ESearchCase::IgnoreCase);
	}
}

FString FSFPPlannerPersistence::GetLastPlanPath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SFPFactoryPlanner"), TEXT("Plans"), TEXT("LastPlan.json"));
}

FString FSFPPlannerPersistence::GetNamedPlansDirectory()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SFPFactoryPlanner"), TEXT("Plans"), TEXT("Saved"));
}

FString FSFPPlannerPersistence::GetSharedPlansDirectory()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SFPFactoryPlanner"), TEXT("ServerPlans"));
}

bool FSFPPlannerPersistence::SaveLastPlan(const FSFPPlanResult& Plan, FString& OutPath, FString& OutError)
{
	OutPath = GetLastPlanPath();
	return SavePlanToPath(OutPath, FString(), Plan, OutError);
}

TSharedPtr<FSFPPlanResult> FSFPPlannerPersistence::LoadLastPlan(FString& OutPath, FString& OutError)
{
	OutPath = GetLastPlanPath();
	return LoadPlanFromPath(OutPath, OutError);
}

bool FSFPPlannerPersistence::DeleteLastPlan(FString& OutPath, FString& OutError)
{
	OutPath = GetLastPlanPath();
	OutError.Reset();
	if (!IFileManager::Get().FileExists(*OutPath))
	{
		return true;
	}
	if (!IFileManager::Get().Delete(*OutPath, false, true, true))
	{
		OutError = FString::Printf(TEXT("Gespeicherter Produktionsplan konnte nicht gelöscht werden: %s"), *OutPath);
		return false;
	}
	return true;
}

bool FSFPPlannerPersistence::SaveNamedPlan(
	const FString& Name,
	const FSFPPlanResult& Plan,
	FString& OutFileName,
	FString& OutPath,
	FString& OutError)
{
	OutFileName.Reset();
	OutPath.Reset();
	FString CleanName;
	if (!BuildNamedPlanFileName(Name, CleanName, OutFileName, OutError))
	{
		return false;
	}
	OutPath = FPaths::Combine(GetNamedPlansDirectory(), OutFileName);
	return SavePlanToPath(OutPath, CleanName, Plan, OutError);
}

TSharedPtr<FSFPPlanResult> FSFPPlannerPersistence::LoadNamedPlan(
	const FString& FileName,
	FString& OutPath,
	FString& OutError)
{
	if (!IsSafeNamedPlanFileName(FileName) || FileName.Len() > SFPMaxPlanNameLength + 16)
	{
		OutError = TEXT("Ungültiger Dateiname für den gespeicherten Plan");
		return nullptr;
	}
	OutPath = FPaths::Combine(GetNamedPlansDirectory(), FileName);
	return LoadPlanFromPath(OutPath, OutError);
}

bool FSFPPlannerPersistence::DeleteNamedPlan(
	const FString& FileName,
	FString& OutPath,
	FString& OutError)
{
	OutError.Reset();
	if (!IsSafeNamedPlanFileName(FileName) || FileName.Len() > SFPMaxPlanNameLength + 16)
	{
		OutError = TEXT("Ungültiger Dateiname für den gespeicherten Plan");
		return false;
	}
	OutPath = FPaths::Combine(GetNamedPlansDirectory(), FileName);
	if (!IFileManager::Get().FileExists(*OutPath))
	{
		return true;
	}
	if (!IFileManager::Get().Delete(*OutPath, false, true, true))
	{
		OutError = FString::Printf(TEXT("Benannter Produktionsplan konnte nicht gelöscht werden: %s"), *OutPath);
		return false;
	}
	return true;
}

bool FSFPPlannerPersistence::ListNamedPlans(TArray<FSFPSavedPlanInfo>& OutPlans, FString& OutError)
{
	OutPlans.Reset();
	OutError.Reset();
	const FString Directory = GetNamedPlansDirectory();
	if (!IFileManager::Get().DirectoryExists(*Directory))
	{
		return true;
	}

	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Directory, TEXT("*.json")), true, false);
	int32 InvalidFiles = 0;
	for (const FString& FileName : Files)
	{
		FString LoadError;
		FString PlanName;
		const FString Path = FPaths::Combine(Directory, FileName);
		TSharedPtr<FSFPPlanResult> Plan = LoadPlanFromPath(Path, LoadError, &PlanName);
		if (!Plan.IsValid())
		{
			++InvalidFiles;
			continue;
		}

		FSFPSavedPlanInfo Info;
		FillSavedPlanInfo(Info, FileName, PlanName, *Plan, Path);
		OutPlans.Add(MoveTemp(Info));
	}

	OutPlans.Sort([](const FSFPSavedPlanInfo& Left, const FSFPSavedPlanInfo& Right)
	{
		return Left.Name.Compare(Right.Name, ESearchCase::IgnoreCase) < 0;
	});
	if (InvalidFiles > 0)
	{
		OutError = FString::Printf(TEXT("%d beschädigte Plan-Datei(en) wurden übersprungen"), InvalidFiles);
	}
	return true;
}

bool FSFPPlannerPersistence::SerializePlan(
	const FString& Name,
	const FSFPPlanResult& Plan,
	FString& OutJson,
	FString& OutError)
{
	FString CleanName;
	FString IgnoredFileName;
	if (!BuildNamedPlanFileName(Name, CleanName, IgnoredFileName, OutError))
	{
		OutJson.Reset();
		return false;
	}
	return SerializePlanToJson(CleanName, Plan, nullptr, OutJson, OutError);
}

TSharedPtr<FSFPPlanResult> FSFPPlannerPersistence::DeserializePlan(
	const FString& Json,
	FString& OutPlanName,
	FString& OutError)
{
	return DeserializePlanFromJson(Json, OutError, &OutPlanName);
}

bool FSFPPlannerPersistence::SaveSharedPlan(
	const FString& Name,
	const FSFPPlanResult& Plan,
	const FString& EditorId,
	const FString& EditorName,
	const int64 ExpectedRevision,
	FSFPSavedPlanInfo& OutInfo,
	FString& OutError)
{
	OutInfo = FSFPSavedPlanInfo();
	OutError.Reset();
	if (EditorId.IsEmpty())
	{
		OutError = TEXT("Die Spieleridentität für den Multiplayer-Plan fehlt");
		return false;
	}
	if (!ValidateSharedPlanPayload(Plan, OutError))
	{
		return false;
	}

	FString CleanName;
	FString FileName;
	if (!BuildNamedPlanFileName(Name, CleanName, FileName, OutError))
	{
		return false;
	}
	const FString Directory = GetSharedPlansDirectory();
	const FString Path = FPaths::Combine(Directory, FileName);
	const bool bExists = IFileManager::Get().FileExists(*Path);
	FSFPSharedPlanMetadata Metadata;
	if (bExists)
	{
		FString ExistingName;
		FString LoadError;
		const TSharedPtr<FSFPPlanResult> ExistingPlan = LoadPlanFromPath(
			Path,
			LoadError,
			&ExistingName,
			&Metadata);
		if (!ExistingPlan.IsValid())
		{
			OutError = FString::Printf(TEXT("Vorhandener Multiplayer-Plan ist nicht lesbar: %s"), *LoadError);
			return false;
		}
		if (Metadata.Revision <= 0)
		{
			Metadata.Revision = IFileManager::Get().GetTimeStamp(*Path).GetTicks();
		}
		if (ExpectedRevision <= 0 || ExpectedRevision != Metadata.Revision)
		{
			OutError = TEXT("Der Multiplayer-Plan wurde inzwischen geändert; bitte neu laden und erneut versuchen");
			return false;
		}
	}
	else
	{
		if (ExpectedRevision != 0)
		{
			OutError = TEXT("Der Multiplayer-Plan existiert nicht mehr; bitte die Serverliste aktualisieren");
			return false;
		}
		TArray<FSFPSavedPlanInfo> ExistingPlans;
		FString ListError;
		ListSharedPlans(ExistingPlans, ListError);
		if (ExistingPlans.Num() >= SFPMaxSharedPlans)
		{
			OutError = FString::Printf(TEXT("Der Server darf höchstens %d gemeinsame Pläne speichern"), SFPMaxSharedPlans);
			return false;
		}
		Metadata.OwnerId = EditorId;
		Metadata.OwnerName = EditorName;
	}

	const int64 NowRevision = FDateTime::UtcNow().GetTicks();
	Metadata.Revision = FMath::Max(NowRevision, Metadata.Revision + 1);
	if (Metadata.OwnerId.IsEmpty())
	{
		Metadata.OwnerId = EditorId;
		Metadata.OwnerName = EditorName;
	}
	Metadata.UpdatedBy = EditorName;
	Metadata.ModifiedAt = FDateTime::UtcNow();
	if (!SavePlanToPath(Path, CleanName, Plan, OutError, &Metadata))
	{
		return false;
	}
	FillSavedPlanInfo(OutInfo, FileName, CleanName, Plan, Path, &Metadata);
	return true;
}

TSharedPtr<FSFPPlanResult> FSFPPlannerPersistence::LoadSharedPlan(
	const FString& FileName,
	FSFPSavedPlanInfo& OutInfo,
	FString& OutJson,
	FString& OutError)
{
	OutInfo = FSFPSavedPlanInfo();
	OutJson.Reset();
	OutError.Reset();
	if (!IsSafeNamedPlanFileName(FileName) || FileName.Len() > SFPMaxPlanNameLength + 16)
	{
		OutError = TEXT("Ungültiger Dateiname für den Multiplayer-Plan");
		return nullptr;
	}
	const FString Path = FPaths::Combine(GetSharedPlansDirectory(), FileName);
	FString PlanName;
	FSFPSharedPlanMetadata Metadata;
	TSharedPtr<FSFPPlanResult> Plan = LoadPlanFromPath(
		Path,
		OutError,
		&PlanName,
		&Metadata);
	if (!Plan.IsValid())
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("Der Multiplayer-Plan wurde nicht gefunden");
		}
		return nullptr;
	}
	// Never send the persisted owner account identifier or other authority-only
	// metadata back to a client. The summary carries only display-safe fields.
	if (!SerializePlanToJson(PlanName, *Plan, nullptr, OutJson, OutError))
	{
		return nullptr;
	}
	if (Metadata.Revision <= 0)
	{
		Metadata.Revision = IFileManager::Get().GetTimeStamp(*Path).GetTicks();
	}
	FillSavedPlanInfo(OutInfo, FileName, PlanName, *Plan, Path, &Metadata);
	return Plan;
}

bool FSFPPlannerPersistence::DeleteSharedPlan(
	const FString& FileName,
	const FString& RequesterId,
	const int64 ExpectedRevision,
	FString& OutError)
{
	OutError.Reset();
	if (!IsSafeNamedPlanFileName(FileName) || FileName.Len() > SFPMaxPlanNameLength + 16)
	{
		OutError = TEXT("Ungültiger Dateiname für den Multiplayer-Plan");
		return false;
	}
	FSFPSavedPlanInfo Info;
	FString Json;
	TSharedPtr<FSFPPlanResult> Plan = LoadSharedPlan(FileName, Info, Json, OutError);
	if (!Plan.IsValid())
	{
		return false;
	}
	if (ExpectedRevision <= 0 || ExpectedRevision != Info.Revision)
	{
		OutError = TEXT("Der Multiplayer-Plan wurde inzwischen geändert; bitte die Liste aktualisieren");
		return false;
	}
	if (!Info.OwnerId.IsEmpty() && Info.OwnerId != RequesterId)
	{
		OutError = FString::Printf(
			TEXT("Nur der Ersteller %s darf diesen Multiplayer-Plan löschen"),
			Info.OwnerName.IsEmpty() ? TEXT("dieses Plans") : *Info.OwnerName);
		return false;
	}
	const FString Path = FPaths::Combine(GetSharedPlansDirectory(), FileName);
	if (!IFileManager::Get().Delete(*Path, false, true, true))
	{
		OutError = TEXT("Der Multiplayer-Plan konnte auf dem Server nicht gelöscht werden");
		return false;
	}
	return true;
}

bool FSFPPlannerPersistence::ListSharedPlans(TArray<FSFPSavedPlanInfo>& OutPlans, FString& OutError)
{
	OutPlans.Reset();
	OutError.Reset();
	const FString Directory = GetSharedPlansDirectory();
	if (!IFileManager::Get().DirectoryExists(*Directory))
	{
		return true;
	}

	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Directory, TEXT("*.json")), true, false);
	int32 InvalidFiles = 0;
	for (const FString& FileName : Files)
	{
		const FString Path = FPaths::Combine(Directory, FileName);
		FString LoadError;
		FString PlanName;
		FSFPSharedPlanMetadata Metadata;
		TSharedPtr<FSFPPlanResult> Plan = LoadPlanFromPath(Path, LoadError, &PlanName, &Metadata);
		if (!Plan.IsValid())
		{
			++InvalidFiles;
			continue;
		}
		if (Metadata.Revision <= 0)
		{
			Metadata.Revision = IFileManager::Get().GetTimeStamp(*Path).GetTicks();
		}
		FSFPSavedPlanInfo Info;
		FillSavedPlanInfo(Info, FileName, PlanName, *Plan, Path, &Metadata);
		OutPlans.Add(MoveTemp(Info));
	}
	OutPlans.Sort([](const FSFPSavedPlanInfo& Left, const FSFPSavedPlanInfo& Right)
	{
		return Left.Name.Compare(Right.Name, ESearchCase::IgnoreCase) < 0;
	});
	if (InvalidFiles > 0)
	{
		OutError = FString::Printf(TEXT("%d beschädigte Multiplayer-Plan-Datei(en) wurden übersprungen"), InvalidFiles);
	}
	return true;
}

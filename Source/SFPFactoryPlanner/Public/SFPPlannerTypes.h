#pragma once

#include "CoreMinimal.h"

class UFGItemDescriptor;
class UFGRecipe;

enum class ESFPPlanNodeType : uint8
{
	Source,
	Machine,
	Target,
	Byproduct,
	Cycle,
	Splitter,
	Merger,
	Generator
};

struct FSFPPlannerItemRate
{
	TSubclassOf<UFGItemDescriptor> ItemClass;
	FString ClassPath;
	FString DisplayName;
	FString Form;
	double RatePerMinute = 0.0;
};

struct FSFPPlannerRecipe
{
	TSubclassOf<UFGRecipe> RecipeClass;
	FString ClassPath;
	FString DisplayName;
	FString SourceMount;
	bool bAvailable = false;
	bool bAlternateRecipe = false;
	double DurationSeconds = 0.0;
	TArray<FSFPPlannerItemRate> Ingredients;
	TArray<FSFPPlannerItemRate> Products;
	UClass* MachineClass = nullptr;
	FString MachineName;
	double BasePowerMW = 0.0;
	double PowerExponent = 1.0;
	bool bVariablePower = false;
	/** Optional buildables that are part of one logical machine, e.g. modular-miner attachments. */
	TArray<FString> AdditionalBuildableClassPaths;
	/** Extra runtime-derived configuration shown below the recipe name. */
	FString ConfigurationDetail;
	/** Inputs are resource nodes attached to this extractor, not transported recipe ingredients. */
	bool bDirectResourceExtraction = false;
	FString ResourceNodeLabel;
	FString SourceName;
	FString ModulesLabel;
	FString FluidLabel;
	double FluidRatePerMinute = 0;
};

struct FSFPProductOption
{
	TSubclassOf<UFGItemDescriptor> ItemClass;
	FString ClassPath;
	FString DisplayName;
	FString Form;
	FString SourceMount;
	int32 RecipeCount = 0;
	bool bHasAvailableRecipe = false;
};

struct FSFPRecipeOption
{
	FString Category;
	FString SourceName;
	FString Purity;
	FString ModulesLabel;
	FString FluidLabel;
	FString ConfigurationDetail;
    FString SourceItemClassPath;
	// UI metadata: extraction produces a different item than the raw node input.
	bool bProcessesResource = false;

	FString ItemClassPath;
	FString RecipeClassPath;
	FString DisplayName;
	FString MachineName;
	FString SourceMount;
	bool bAvailable = false;
};

/** One independently configured end product in a combined production site. */
struct FSFPPlanTarget
{
	TSubclassOf<UFGItemDescriptor> ItemClass;
	FString ItemClassPath;
	FString DisplayName;
	FString Form;
	double RatePerMinute = 0.0;
};

struct FSFPTransportTier
{
	FString ClassPath;
	FString DisplayName;
	FString Kind;
	double CapacityPerMinute = 0.0;
	double CostUnitLengthMeters = 1.0;
	bool bAvailable = false;
};

/** One fuel supported by a runtime-discovered power generator. */
struct FSFPPowerFuelOption
{
	TSubclassOf<UFGItemDescriptor> ItemClass;
	FString ClassPath;
	FString DisplayName;
	FString Form;
	FString SourceMount;
	bool bAvailable = false;
	double EnergyValueMJ = 0.0;
	TSubclassOf<UFGItemDescriptor> WasteItemClass;
	FString WasteItemClassPath;
	FString WasteDisplayName;
	FString WasteForm;
	double WasteAmountPerFuel = 0.0;
	/** True for wind, water, solar and other generators without a consumed item. */
	bool bFuelFree = false;
	/** Runtime-derived consumption of this fuel for one fully loaded generator setup. */
	double ConsumptionRatePerGenerator = 0.0;
	/** Refined Power modular-chain configuration selected for this fuel. */
	UClass* HeaterClass = nullptr;
	FString HeaterClassPath;
	FString HeaterDisplayName;
	UClass* BoilerClass = nullptr;
	FString BoilerClassPath;
	FString BoilerDisplayName;
	UClass* BoilerPlatformClass = nullptr;
	FString BoilerPlatformClassPath;
	FString BoilerPlatformDisplayName;
	UClass* SteamCoolerClass = nullptr;
	FString SteamCoolerClassPath;
	FString SteamCoolerDisplayName;
	UClass* ExhaustCoolerClass = nullptr;
	FString ExhaustCoolerClassPath;
	FString ExhaustCoolerDisplayName;
	UClass* CoolingPlatformClass = nullptr;
	FString CoolingPlatformClassPath;
	FString CoolingPlatformDisplayName;
	double HeaterCountPerGenerator = 0.0;
	double BoilerCountPerGenerator = 0.0;
	double WaterRatePerGenerator = 0.0;
	double HighSteamRatePerGenerator = 0.0;
	double LowSteamRatePerGenerator = 0.0;
	double ExhaustRatePerGenerator = 0.0;
	TSubclassOf<UFGItemDescriptor> WaterItemClass;
	FString WaterItemClassPath;
	FString WaterDisplayName;
	FString WaterForm;
	TSubclassOf<UFGItemDescriptor> HighSteamItemClass;
	FString HighSteamItemClassPath;
	FString HighSteamDisplayName;
	FString HighSteamForm;
	TSubclassOf<UFGItemDescriptor> LowSteamItemClass;
	FString LowSteamItemClassPath;
	FString LowSteamDisplayName;
	FString LowSteamForm;
	TSubclassOf<UFGItemDescriptor> ExhaustItemClass;
	FString ExhaustItemClassPath;
	FString ExhaustDisplayName;
	FString ExhaustForm;
};

/** A complete power setup discovered from the active build-recipe catalog. */
struct FSFPPowerGeneratorOption
{
	UClass* GeneratorClass = nullptr;
	FString ClassPath;
	FString DisplayName;
	FString SourceMount;
	bool bAvailable = false;
	double PowerProductionMW = 0.0;
	TSubclassOf<UFGItemDescriptor> SupplementalItemClass;
	FString SupplementalItemClassPath;
	FString SupplementalDisplayName;
	FString SupplementalForm;
	/** Maximum supplemental-resource rate for one fully clocked generator. */
	double SupplementalRatePerMinute = 0.0;
	/** A loaded modded generator whose output changes with time, weather or placement. */
	bool bVariableOutput = false;
	/** A complete Refined Power heater -> boiler -> turbine -> generator setup. */
	bool bModularPower = false;
	FString ConfigurationDetail;
	UClass* TurbineClass = nullptr;
	FString TurbineClassPath;
	FString TurbineDisplayName;
	double TurbineSteamRatePerMinute = 0.0;
	double TurbineMaximumRPM = 0.0;
	UClass* ConverterPlatformClass = nullptr;
	FString ConverterPlatformClassPath;
	FString ConverterPlatformDisplayName;
	TArray<FSFPPowerFuelOption> Fuels;
};

/** User selections for a net-power production plan. Empty class paths mean automatic selection. */
struct FSFPPowerPlanRequest
{
	double TargetNetPowerMW = 1000.0;
	double ReservePercent = 10.0;
	bool bOnlyAvailable = true;
	FString GeneratorClassPath;
	FString FuelClassPath;
	TMap<FString, FString> RecipeOverrides;
	double EstimatedConnectionLengthMeters = 10.0;
	FString SelectedConveyorClassPath;
	FString SelectedConveyorLiftClassPath;
};

struct FSFPConstructionCost
{
	FString ItemClassPath;
	FString DisplayName;
	FString Form;
	double Amount = 0.0;
};

struct FSFPPlanNode
{
	int32 Id = INDEX_NONE;
	ESFPPlanNodeType Type = ESFPPlanNodeType::Source;
	/** User-maintained construction progress; it never changes solver output. */
	bool bCompleted = false;
	int32 Depth = 0;
	FString Title;
	FString Detail;
	FString ClassPath;
	FString ProducedItemClassPath;
	FString RecipeClassPath;
	double RatePerMinute = 0.0;
	double MachineCount = 0.0;
	double PowerMW = 0.0;
	int32 InfrastructureCount = 0;
	/** Buildable attachments whose construction costs scale with the whole machine count. */
	TArray<FString> AdditionalBuildableClassPaths;
};

struct FSFPPlanEdge
{
	int32 SourceNodeId = INDEX_NONE;
	int32 TargetNodeId = INDEX_NONE;
	FString ItemName;
	FString ItemClassPath;
	FString Form;
	double RatePerMinute = 0.0;
	FString TransportLabel;
	FString TransportClassPath;
	FString TransportKind;
	int32 RequiredLines = 0;
	double EstimatedLengthMeters = 0.0;
	bool bLocalRoutingLink = false;
};

struct FSFPPlanSupply
{
    FString ItemClassPath, DisplayName, Form;
    int32 Lines = 1;
    double RatePerLine = 150.0;
};

struct FSFPPlanResult
{
    bool bInputPlanning = false;
    bool bInputFeasible = true;
    bool bEnforceSupplyLimits = true;
    TArray<FSFPPlanSupply> Supplies;
    TArray<FSFPPlanTarget> InputPlanningTargets;
    TMap<FString, double> FixedOutputRates;
    TMap<FString, double> SuppliedInputRates;

	bool bSuccess = false;
	FString ErrorMessage;
	TArray<FSFPPlanTarget> Targets;
	/** Backwards-compatible summary fields used by schema 1/2 plan files. */
	FString TargetName;
	double TargetRatePerMinute = 0.0;
	double TotalEquivalentMachines = 0.0;
	double TotalBasePowerMW = 0.0;
	/** True for a generated electricity plan rather than a material-output plan. */
	bool bPowerProductionPlan = false;
	double RequestedNetPowerMW = 0.0;
	double PowerReservePercent = 0.0;
	FString RequestedGeneratorClassPath;
	FString RequestedFuelClassPath;
	FString SelectedGeneratorClassPath;
	FString SelectedGeneratorDisplayName;
	FString SelectedFuelClassPath;
	FString SelectedFuelDisplayName;
	FString SelectedFuelForm;
	double GeneratorPowerMW = 0.0;
	double EquivalentGeneratorCount = 0.0;
	int32 BuiltGeneratorCount = 0;
	double GrossPowerMW = 0.0;
	double SelfConsumptionPowerMW = 0.0;
	double NetPowerMW = 0.0;
	double ReservePowerMW = 0.0;
	double FuelRatePerMinute = 0.0;
	FString SupplementalItemClassPath;
	FString SupplementalDisplayName;
	FString SupplementalForm;
	double SupplementalRatePerMinute = 0.0;
	FString WasteItemClassPath;
	FString WasteDisplayName;
	FString WasteForm;
	double WasteRatePerMinute = 0.0;
	int32 MaxDepth = 0;
	TArray<FSFPPlanNode> Nodes;
	TArray<FSFPPlanEdge> Edges;
	TArray<FSFPConstructionCost> MachineConstructionCosts;
	TArray<FSFPConstructionCost> InfrastructureConstructionCosts;
	TArray<FSFPConstructionCost> ConstructionCosts;
	TMap<FString, FString> RecipeOverrides;
	TMap<FString, double> AvailableInputRates;
	bool bOnlyAvailableRecipes = true;
	FString SelectedConveyorClassPath;
	FString SelectedConveyorDisplayName;
	double SelectedConveyorCapacityPerMinute = 0.0;
	FString SelectedConveyorLiftClassPath;
	FString SelectedConveyorLiftDisplayName;
	double SelectedConveyorLiftCapacityPerMinute = 0.0;
	double EstimatedConnectionLengthMeters = 10.0;
	int32 SplitterCount = 0;
	int32 MergerCount = 0;
	int32 ConveyorLineCount = 0;
	int32 PipelineLineCount = 0;
	double EstimatedConveyorMeters = 0.0;
	double EstimatedPipelineMeters = 0.0;
	TArray<FString> Warnings;
};

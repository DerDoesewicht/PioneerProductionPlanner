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

struct FSFPPlannerFuelOption
{
	TSubclassOf<UFGItemDescriptor> ItemClass;
	FString ClassPath;
	FString DisplayName;
	FString Form;
	FString SourceMount;
	double EnergyValueMJ = 0.0;
};

/** Runtime capabilities read from the concrete production-machine class. */
struct FSFPMachineRuntimeConfig
{
	bool bCanChangePotential = false;
	double MinPotential = 1.0;
	double MaxPotential = 1.0;
	bool bRuntimeMaxPotentialKnown = false;
	bool bCanChangeProductionBoost = false;
	double BaseProductionBoost = 1.0;
	double MaxProductionBoost = 1.0;
	double ProductionBoostPerSloop = 0.0;
	double ProductionBoostPowerExponent = 1.0;
	int32 MaxSomersloops = 0;
	bool bRuntimeMaxProductionBoostKnown = false;
};

/** User-selected operating point for one recipe/machine variant. */
struct FSFPMachinePlanSettings
{
	/** Maximum clock used by the solver for this branch. 100 means vanilla baseline speed. */
	double ClockPercent = 100.0;
	/** Somersloops installed per physical machine. */
	int32 SomersloopCount = 0;
	/** Optional burner fuel selection. Empty means deterministic automatic selection. */
	FString FuelClassPath;
};

/** Available deposits for one raw resource when more than one purity is used. */
struct FSFPResourceSourceMix
{
	/** Automatic purity allocation is the default for every complete purity family. */
	bool bEnabled = true;
	/** Include nodes that already have an extractor when deriving live-world limits. */
	bool bUseOccupiedSources = false;
	/** False means unlimited/automatic availability; true makes the corresponding count a hard cap. */
	bool bImpureLimited = false;
	bool bNormalLimited = false;
	bool bPureLimited = false;
	int32 ImpureCount = 0;
	int32 NormalCount = 0;
	int32 PureCount = 0;
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
	FSFPMachineRuntimeConfig MachineConfig;
	/** True when the production machine burns fuel instead of drawing grid power. */
	bool bFuelPowered = false;
	/** Runtime-discovered fuel descriptors for optional burner manufacturers. */
	TArray<FSFPPlannerFuelOption> FuelOptions;
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
	FString MachineClassPath;
	FSFPMachineRuntimeConfig MachineConfig;
	bool bFuelPowered = false;
	TArray<FSFPPlannerFuelOption> FuelOptions;
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
	/** Generator potential/clock support. Generators never use Somersloops. */
	bool bCanChangePotential = false;
	double MinPotential = 0.01;
	double MaxPotential = 1.0;
	bool bRuntimeMaxPotentialKnown = false;
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

/** Runtime-discovered Alien Power Augmenter parameters. */
struct FSFPAlienPowerAugmenterOption
{
	UClass* BuildableClass = nullptr;
	FString ClassPath;
	FString DisplayName;
	bool bAvailable = false;
	/** Independent base generation contributed by each augmenter before grid multiplication. */
	double BasePowerPerAugmenterMW = 500.0;
	/** Grid-production multiplier contribution of one unfueled augmenter (0.10 = +10%). */
	double PassiveBoostPerAugmenter = 0.10;
	/** Grid-production multiplier contribution of one Matrix-fed augmenter (0.30 = +30%). */
	double FueledBoostPerAugmenter = 0.30;
	TSubclassOf<UFGItemDescriptor> MatrixItemClass;
	FString MatrixItemClassPath;
	FString MatrixDisplayName;
	FString MatrixForm;
	/** Alien Power Matrix consumption for one fueled augmenter. */
	double MatrixRatePerMinute = 5.0;
};

/** User selections for a net-power production plan. Empty class paths mean automatic selection. */
struct FSFPPowerPlanRequest
{
	double TargetNetPowerMW = 1000.0;
	double ReservePercent = 10.0;
	bool bOnlyAvailable = true;
	FString GeneratorClassPath;
	FString FuelClassPath;
	/** Maximum generator clock used for the selected power setup. */
	double GeneratorClockPercent = 100.0;
	/** Alien Power Augmenters connected without Alien Power Matrix supply. */
	int32 PassiveAlienPowerAugmenters = 0;
	/** Alien Power Augmenters continuously supplied with Alien Power Matrix. */
	int32 FueledAlienPowerAugmenters = 0;
	TMap<FString, FString> RecipeOverrides;
	TMap<FString, FSFPMachinePlanSettings> MachineSettings;
	/** Mixed impure/normal/pure deposits, keyed by raw-resource class path. */
	TMap<FString, FSFPResourceSourceMix> ResourceSourceMixes;
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
	/** 100%-clock cycle-equivalent machine count after production amplification. */
	double MachineCount = 0.0;
	/** Physical machines that must actually be built for the configured clock limit. */
	int32 BuiltMachineCount = 0;
	/** Number of machines running at the configured clock limit. */
	int32 FullClockMachineCount = 0;
	/** Configured maximum clock for this machine group. */
	double ConfiguredClockPercent = 100.0;
	/** Clock of the final partially-loaded machine, or 0 when none is needed. */
	double PartialClockPercent = 0.0;
	/** Optional 100%-clock-equivalent upper bound imposed by a mixed source selection. */
	double MaximumMachineCount = 0.0;
	/** Relative material-balance cost; mixed-source fallback supply is deliberately expensive. */
	double SourceCostMultiplier = 1.0;
	int32 SomersloopCount = 0;
	double ProductionBoost = 1.0;
	double PowerMW = 0.0;
	/** This node uses PowerMW as burner heat demand and must not count toward grid power. */
	bool bFuelPowered = false;
	FString FuelClassPath;
	FString FuelDisplayName;
	FString FuelForm;
	double FuelEnergyValueMJ = 0.0;
	double FuelRatePerMinute = 0.0;
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
	/** Base output of one generator at 100% clock. */
	double GeneratorBasePowerMW = 0.0;
	/** Output of one fully configured generator at ConfiguredGeneratorClockPercent. */
	double GeneratorPowerMW = 0.0;
	double EquivalentGeneratorCount = 0.0;
	int32 BuiltGeneratorCount = 0;
	int32 FullClockGeneratorCount = 0;
	double ConfiguredGeneratorClockPercent = 100.0;
	double PartialGeneratorClockPercent = 0.0;
	/** Generator output before Alien Power Augmenter base production / multiplication. */
	double BaseGeneratorGrossPowerMW = 0.0;
	int32 PassiveAlienPowerAugmenters = 0;
	int32 FueledAlienPowerAugmenters = 0;
	double AlienPowerAugmenterBaseMW = 0.0;
	double AlienPowerMultiplier = 1.0;
	double AlienPowerContributionMW = 0.0;
	FString AlienPowerMatrixItemClassPath;
	FString AlienPowerMatrixDisplayName;
	double AlienPowerMatrixRatePerMinute = 0.0;
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
	/** Recipe/machine operating settings, keyed by the selected runtime recipe-variant path. */
	TMap<FString, FSFPMachinePlanSettings> MachineSettings;
	/** Mixed impure/normal/pure deposits, keyed by raw-resource class path. */
	TMap<FString, FSFPResourceSourceMix> ResourceSourceMixes;
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

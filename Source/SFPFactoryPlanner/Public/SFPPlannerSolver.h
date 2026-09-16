#pragma once

#include "CoreMinimal.h"
#include "SFPPlannerTypes.h"

class UWorld;
class AFGRecipeManager;

/** Builds a deterministic production plan directly from the active runtime recipe manager. */
class SFPFACTORYPLANNER_API FSFPPlannerSolver
{
public:
	const TArray<FSFPPlannerRecipe>& GetRuntimeRecipes() const { return Recipes; }
	void EnableMinerDiagnostics() { bMinerDiagnostics = true; }
	const TArray<FString>& GetMinerDiagnostics() const { return MinerDiagnostics; }
	bool Initialize(UWorld* World, FString& OutError);
	/** Rebuilds the cached catalog only when recipes or their live unlock state changed. */
	bool RefreshIfRecipeAvailabilityChanged(UWorld* World, FString& OutError);

	const TArray<TSharedPtr<FSFPProductOption>>& GetProducts() const
	{
		return Products;
	}

	const TArray<FSFPTransportTier>& GetTransportTiers() const
	{
		return TransportTiers;
	}

	const TArray<TSharedPtr<FSFPPowerGeneratorOption>>& GetPowerGenerators() const
	{
		return PowerGenerators;
	}

	const FSFPAlienPowerAugmenterOption& GetAlienPowerAugmenter() const
	{
		return AlienPowerAugmenter;
	}

	void GetRecipeOptionsForItemPath(
		const FString& ItemClassPath,
		bool bOnlyAvailableRecipes,
		TArray<TSharedPtr<FSFPRecipeOption>>& OutOptions) const;

	FSFPPlanResult Solve(
		TSubclassOf<UFGItemDescriptor> TargetItem,
		double TargetRatePerMinute,
		bool bOnlyAvailableRecipes,
		const TMap<FString, FString>& RecipeOverrides,
		double EstimatedConnectionLengthMeters,
		const FString& SelectedConveyorClassPath = FString(),
		const FString& SelectedConveyorLiftClassPath = FString(),
		const TMap<FString, FSFPMachinePlanSettings>& MachineSettings = TMap<FString, FSFPMachinePlanSettings>(),
		const TMap<FString, FSFPResourceSourceMix>& ResourceSourceMixes = TMap<FString, FSFPResourceSourceMix>()) const;

	/** Solves all end products in one shared graph so common intermediate production is merged. */
	FSFPPlanResult Solve(
		const TArray<FSFPPlanTarget>& Targets,
		bool bOnlyAvailableRecipes,
		const TMap<FString, FString>& RecipeOverrides,
		double EstimatedConnectionLengthMeters,
		const FString& SelectedConveyorClassPath = FString(),
		const FString& SelectedConveyorLiftClassPath = FString(),
		const TMap<FString, double>& SuppliedInputs = TMap<FString, double>(),
		bool bEnforceSupplyLimits = true,
		const TMap<FString, FSFPMachinePlanSettings>& MachineSettings = TMap<FString, FSFPMachinePlanSettings>(),
		const TMap<FString, FSFPResourceSourceMix>& ResourceSourceMixes = TMap<FString, FSFPResourceSourceMix>()) const;

	/** Builds generator, fuel, supplemental-resource and complete upstream production chains. */
	FSFPPlanResult SolvePower(const FSFPPowerPlanRequest& Request) const;

private:
	bool bMinerDiagnostics = false;
	TArray<FString> MinerDiagnostics;
	struct FSolveContext;

	int32 FindRecipeFor(
		UClass* ItemClass,
		bool bOnlyAvailableRecipes,
		const TSet<UClass*>* ForbiddenIngredients,
		const FString* PreferredRecipePath,
		int32& OutCandidateCount,
		const TMap<FString, double>* SuppliedInputs = nullptr,
        const TSet<FString>* InputReachablePaths = nullptr,
        const TMap<FString, int32>* InputDistanceByPath = nullptr) const;

	int32 BuildDemand(
		UClass* ItemClass,
		const FString& ItemName,
		const FString& Form,
		double RequiredRate,
		int32 ConsumerNodeId,
		int32 Depth,
		FSolveContext& Context) const;

	void AddTransportAdvice(FSFPPlanEdge& Edge, FSFPPlanResult& Result) const;
	void BuildTransportCatalog(
		const TArray<TSubclassOf<UFGRecipe>>& AllRecipes,
		AFGRecipeManager* RecipeManager);
	void BuildOptionalModularMinerCatalog(
		const TArray<TSubclassOf<UFGRecipe>>& AllRecipes,
		AFGRecipeManager* RecipeManager,
		TMap<UClass*, TSharedPtr<FSFPProductOption>>& ProductsByClass);
	void BuildStandardMinerCatalog(
		const TArray<TSubclassOf<UFGRecipe>>& AllRecipes,
		AFGRecipeManager* RecipeManager,
		TMap<UClass*, TSharedPtr<FSFPProductOption>>& ProductsByClass);
	void BuildFluidExtractorCatalog(
		const TArray<TSubclassOf<UFGRecipe>>& AllRecipes,
		AFGRecipeManager* RecipeManager,
		TMap<UClass*, TSharedPtr<FSFPProductOption>>& ProductsByClass);
	void BuildConstructionCostCatalog(const TArray<TSubclassOf<UFGRecipe>>& AllRecipes);
	void BuildPowerGeneratorCatalog(
		const TArray<TSubclassOf<UFGRecipe>>& AllRecipes,
		AFGRecipeManager* RecipeManager);
	void BuildAlienPowerAugmenterCatalog(
		const TArray<TSubclassOf<UFGRecipe>>& AllRecipes,
		AFGRecipeManager* RecipeManager);
	void BuildOptionalPowerGeneratorCatalog(
		const TArray<TSubclassOf<UFGRecipe>>& AllRecipes,
		AFGRecipeManager* RecipeManager);
	void BuildRoutingInfrastructure(FSFPPlanResult& Result) const;
	bool BalanceMaterials(FSFPPlanResult& Result) const;
	void CompactMaterialGraph(FSFPPlanResult& Result) const;
	void AddConstructionCosts(FSFPPlanResult& Result) const;

	TArray<FSFPPlannerRecipe> Recipes;
	TMap<UClass*, TArray<int32>> RecipesByProduct;
	TArray<TSharedPtr<FSFPProductOption>> Products;
	TSet<FString> KnownRecipeClassPaths;
	TSet<FString> AvailableRecipeClassPaths;
	TArray<FSFPTransportTier> TransportTiers;
	TArray<TSharedPtr<FSFPPowerGeneratorOption>> PowerGenerators;
	FSFPAlienPowerAugmenterOption AlienPowerAugmenter;
	TMap<FString, TArray<FSFPConstructionCost>> ConstructionCostsByMachinePath;
	FString SplitterClassPath;
	FString SplitterDisplayName;
	FString MergerClassPath;
	FString MergerDisplayName;
	FString PipeJunctionClassPath;
	FString PipeJunctionDisplayName;
};

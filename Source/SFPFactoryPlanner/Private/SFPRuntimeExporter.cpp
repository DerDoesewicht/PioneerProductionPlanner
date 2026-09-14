#include "SFPRuntimeExporter.h"

#include "SFPFactoryPlanner.h"
#include "SFPPlannerSolver.h"

#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildableFactory.h"
#include "Buildables/FGBuildablePipeline.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "FGCategory.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FGRecipe.h"
#include "FGRecipeManager.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "ItemAmount.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ModLoading/ModLoadingLibrary.h"
#include "Resources/FGBuildingDescriptor.h"
#include "Resources/FGItemDescriptor.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"
#include "Curves/CurveFloat.h"
#include "Buildables/FGBuildableManufacturerVariablePower.h"

namespace
{

	// Read-only diagnostic fields. Never invoke Blueprint functions or traverse SCS templates.
	TSharedPtr<FJsonObject> IndustrialEvolutionFields(const UObject* Object)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		if (!IsValid(Object)) return Result;
		int32 Count = 0;
		for (TFieldIterator<FProperty> It(Object->GetClass()); It && Count < 128; ++It)
		{
			const FProperty* Property = *It;
			const FString Name = Property->GetName();
			if (!(Name.Contains(TEXT("Speed")) || Name.Contains(TEXT("Duration"))
				|| Name.Contains(TEXT("Cycle")) || Name.Contains(TEXT("Potential"))
				|| Name.Contains(TEXT("Power")) || Name.Contains(TEXT("Recipe"))
				|| Name.Contains(TEXT("Productivity")) || Name.Contains(TEXT("Multiplier"))
				|| Name.Contains(TEXT("Manufactur")) || Name.Contains(TEXT("Tier")))) continue;
			// Exclude containers and object graphs; record scalar values and class references only.
			if (!(CastField<FNumericProperty>(Property) || CastField<FBoolProperty>(Property)
				|| CastField<FEnumProperty>(Property) || CastField<FClassProperty>(Property))) continue;
			FString Value;
			Property->ExportTextItem_Direct(Value, Property->ContainerPtrToValuePtr<void>(Object),
				nullptr, const_cast<UObject*>(Object), PPF_None);
			if (Value.Len() > 1024) continue;
			Result->SetStringField(Name, Value);
			++Count;
		}
		return Result;
	}

	constexpr TCHAR ExportSchemaVersion[] = TEXT("1.2.0");
	constexpr TCHAR ExporterModReference[] = TEXT("SFPFactoryPlanner");
	constexpr TCHAR TargetModReference[] = TEXT("SatisfactoryPlus");

	FString ClassPathOrEmpty(const UClass* Class)
	{
		return IsValid(Class) ? Class->GetPathName() : FString();
	}

	FString SourceMountFromPath(const FString& ObjectPath)
	{
		TArray<FString> Parts;
		ObjectPath.ParseIntoArray(Parts, TEXT("/"), true);
		return Parts.IsEmpty() ? FString() : Parts[0];
	}

	bool IsSatisfactoryPlusPath(const FString& ObjectPath)
	{
		return ObjectPath.StartsWith(TEXT("/SatisfactoryPlus/"), ESearchCase::IgnoreCase)
			|| ObjectPath.Contains(TEXT("SatisfactoryPlus"), ESearchCase::IgnoreCase);
	}

	FString ResourceFormToString(const EResourceForm Form)
	{
		switch (Form)
		{
		case EResourceForm::RF_SOLID:
			return TEXT("solid");
		case EResourceForm::RF_LIQUID:
			return TEXT("liquid");
		case EResourceForm::RF_GAS:
			return TEXT("gas");
		default:
			return TEXT("invalid");
		}
	}

	void AddWarning(
		TArray<TSharedPtr<FJsonValue>>& Warnings,
		const FString& Code,
		const FString& Subject,
		const FString& Detail)
	{
		TSharedRef<FJsonObject> Warning = MakeShared<FJsonObject>();
		Warning->SetStringField(TEXT("code"), Code);
		Warning->SetStringField(TEXT("subject"), Subject);
		Warning->SetStringField(TEXT("detail"), Detail);
		Warnings.Add(MakeShared<FJsonValueObject>(Warning));

		UE_LOG(
			LogSFPFactoryPlanner,
			Warning,
			TEXT("Runtime export warning [%s] %s: %s"),
			*Code,
			*Subject,
			*Detail);
	}

	TSharedRef<FJsonObject> BuildItemAmountJson(
		const FItemAmount& ItemAmount,
		const FString& RecipePath,
		const FString& Direction,
		const int32 Index,
		const float DurationMinutes,
		TSet<UClass*>& ReferencedItemClasses,
		TArray<TSharedPtr<FJsonValue>>& Warnings,
		bool& bSatisfactoryPlusDetected)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("amountRaw"), ItemAmount.Amount);

		UClass* ItemClass = ItemAmount.ItemClass.Get();
		if (!IsValid(ItemClass) || !ItemClass->IsChildOf(UFGItemDescriptor::StaticClass()))
		{
			Result->SetBoolField(TEXT("valid"), false);
			Result->SetStringField(TEXT("classPath"), FString());
			AddWarning(
				Warnings,
				TEXT("INVALID_ITEM_REFERENCE"),
				RecipePath,
				FString::Printf(TEXT("%s[%d] contains a null or invalid item class"), *Direction, Index));
			return Result;
		}

		ReferencedItemClasses.Add(ItemClass);
		const FString ItemPath = ItemClass->GetPathName();
		const TSubclassOf<UFGItemDescriptor> TypedItemClass(ItemClass);
		const EResourceForm Form = UFGItemDescriptor::GetForm(TypedItemClass);
		const bool bFluidUnit = Form == EResourceForm::RF_LIQUID || Form == EResourceForm::RF_GAS;
		const double NormalizedAmount = bFluidUnit
			? static_cast<double>(ItemAmount.Amount) / 1000.0
			: static_cast<double>(ItemAmount.Amount);

		FString DisplayName = UFGItemDescriptor::GetItemName(TypedItemClass).ToString();
		if (DisplayName.IsEmpty())
		{
			DisplayName = ItemClass->GetName();
		}

		Result->SetBoolField(TEXT("valid"), true);
		Result->SetStringField(TEXT("classPath"), ItemPath);
		Result->SetStringField(TEXT("className"), ItemClass->GetName());
		Result->SetStringField(TEXT("displayName"), DisplayName);
		Result->SetStringField(TEXT("sourceMount"), SourceMountFromPath(ItemPath));
		Result->SetStringField(TEXT("form"), ResourceFormToString(Form));
		Result->SetStringField(TEXT("unit"), bFluidUnit ? TEXT("m3") : TEXT("item"));
		Result->SetNumberField(TEXT("amount"), NormalizedAmount);
		if (DurationMinutes > 0.0f)
		{
			Result->SetNumberField(TEXT("ratePerMinute"), NormalizedAmount / DurationMinutes);
		}

		bSatisfactoryPlusDetected |= IsSatisfactoryPlusPath(ItemPath);
		return Result;
	}

	TSharedRef<FJsonObject> BuildItemJson(
		UClass* ItemClass,
		bool& bSatisfactoryPlusDetected)
	{
		const TSubclassOf<UFGItemDescriptor> TypedItemClass(ItemClass);
		const UFGItemDescriptor* ItemCDO = Cast<UFGItemDescriptor>(ItemClass->GetDefaultObject());
		const FString ItemPath = ItemClass->GetPathName();
		const EResourceForm Form = UFGItemDescriptor::GetForm(TypedItemClass);

		FString DisplayName = UFGItemDescriptor::GetItemName(TypedItemClass).ToString();
		if (DisplayName.IsEmpty())
		{
			DisplayName = ItemClass->GetName();
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("classPath"), ItemPath);
		Result->SetStringField(TEXT("className"), ItemClass->GetName());
		Result->SetStringField(TEXT("displayName"), DisplayName);
		Result->SetStringField(TEXT("description"), UFGItemDescriptor::GetItemDescription(TypedItemClass).ToString());
		Result->SetStringField(TEXT("sourceMount"), SourceMountFromPath(ItemPath));
		Result->SetStringField(TEXT("form"), ResourceFormToString(Form));
		Result->SetNumberField(TEXT("stackSize"), UFGItemDescriptor::GetStackSize(TypedItemClass));
		Result->SetNumberField(TEXT("energyValueMJ"), UFGItemDescriptor::GetEnergyValue(TypedItemClass));
		Result->SetNumberField(TEXT("radioactiveDecay"), UFGItemDescriptor::GetRadioactiveDecay(TypedItemClass));
		Result->SetBoolField(TEXT("hasValidDefaultObject"), IsValid(ItemCDO));

		const TSubclassOf<UFGCategory> Category = UFGItemDescriptor::GetCategory(TypedItemClass);
		Result->SetStringField(TEXT("categoryClassPath"), ClassPathOrEmpty(Category.Get()));

		bSatisfactoryPlusDetected |= IsSatisfactoryPlusPath(ItemPath);
		return Result;
	}

	FString GetPluginVersion(const FString& PluginName)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
		return Plugin.IsValid() ? Plugin->GetDescriptor().VersionName : FString();
	}

	FString BuildableDisplayName(const AFGBuildable* Buildable, UClass* BuildableClass)
	{
		if (IsValid(Buildable) && !Buildable->mDisplayName.IsEmpty())
		{
			return Buildable->mDisplayName.ToString();
		}
		return IsValid(BuildableClass) ? BuildableClass->GetName() : FString();
	}

	TArray<TSharedPtr<FJsonValue>> SortedObjectMapValues(
		const TMap<FString, TSharedPtr<FJsonObject>>& ObjectsByPath)
	{
		TArray<FString> Paths;
		ObjectsByPath.GetKeys(Paths);
		Paths.Sort();

		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Path : Paths)
		{
			if (const TSharedPtr<FJsonObject>* Object = ObjectsByPath.Find(Path); Object != nullptr && Object->IsValid())
			{
				Values.Add(MakeShared<FJsonValueObject>(*Object));
			}
		}
		return Values;
	}
}

FSFPExportResult USFPRuntimeExporter::ExportRuntimeData(const UObject* WorldContextObject)
{
	FSFPExportResult ExportResult;
	UWorld* World = GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!IsValid(World))
	{
		ExportResult.ErrorMessage = TEXT("No valid game world is available");
		return ExportResult;
	}

	AFGRecipeManager* RecipeManager = AFGRecipeManager::Get(World);
	if (!IsValid(RecipeManager))
	{
		ExportResult.ErrorMessage = TEXT("FGRecipeManager is not ready; load a save and try again");
		return ExportResult;
	}

	TMap<UClass*, AFGBuildableFactory*> InstanceByClass;
	for (TActorIterator<AFGBuildableFactory> It(World); It; ++It)
	{
		if (IsValid(*It) && !InstanceByClass.Contains(It->GetClass()))
			InstanceByClass.Add(It->GetClass(), *It);
	}
	TArray<TSharedPtr<FJsonValue>> Warnings;
	TArray<TSharedPtr<FJsonValue>> RecipeValues;
	TSet<UClass*> ReferencedItemClasses;
	TMap<FString, TSharedPtr<FJsonObject>> MachinesByPath;
	TMap<FString, TSharedPtr<FJsonObject>> TransportByPath;
	TArray<TSharedPtr<FJsonValue>> IndustrialEvolutionMachines;
	bool bSatisfactoryPlusDetected = false;

	TArray<TSubclassOf<UFGRecipe>> Recipes = RecipeManager->GetAllRecipes();
	Recipes.Sort([](const TSubclassOf<UFGRecipe>& Left, const TSubclassOf<UFGRecipe>& Right)
	{
		return ClassPathOrEmpty(Left.Get()) < ClassPathOrEmpty(Right.Get());
	});

	for (int32 RecipeIndex = 0; RecipeIndex < Recipes.Num(); ++RecipeIndex)
	{
		const TSubclassOf<UFGRecipe>& Recipe = Recipes[RecipeIndex];
		UClass* RecipeClass = Recipe.Get();
		if (!IsValid(RecipeClass) || !RecipeClass->IsChildOf(UFGRecipe::StaticClass()))
		{
			AddWarning(
				Warnings,
				TEXT("INVALID_RECIPE_REFERENCE"),
				FString::Printf(TEXT("recipeManager[%d]"), RecipeIndex),
				TEXT("Skipped a null or invalid recipe class before dereferencing it"));
			continue;
		}

		const UFGRecipe* RecipeCDO = Cast<UFGRecipe>(RecipeClass->GetDefaultObject());
		const FString RecipePath = RecipeClass->GetPathName();
		if (!IsValid(RecipeCDO))
		{
			AddWarning(
				Warnings,
				TEXT("INVALID_RECIPE_CDO"),
				RecipePath,
				TEXT("Skipped recipe because its class default object is invalid"));
			continue;
		}

		const float DurationSeconds = RecipeCDO->GetManufacturingDuration();
		const float DurationMinutes = DurationSeconds / 60.0f;
		FString DisplayName = RecipeCDO->GetDisplayName().ToString();
		if (DisplayName.IsEmpty())
		{
			DisplayName = UFGRecipe::GetRecipeName(Recipe).ToString();
		}
		if (DisplayName.IsEmpty())
		{
			DisplayName = RecipeClass->GetName();
		}

		TSharedRef<FJsonObject> RecipeJson = MakeShared<FJsonObject>();
		RecipeJson->SetStringField(TEXT("classPath"), RecipePath);
		RecipeJson->SetStringField(TEXT("className"), RecipeClass->GetName());
		RecipeJson->SetStringField(TEXT("displayName"), DisplayName);
		RecipeJson->SetStringField(TEXT("sourceMount"), SourceMountFromPath(RecipePath));
		RecipeJson->SetBoolField(TEXT("available"), RecipeManager->IsRecipeAvailable(Recipe));
		RecipeJson->SetNumberField(TEXT("durationSeconds"), DurationSeconds);
		RecipeJson->SetNumberField(TEXT("manualDurationSeconds"), RecipeCDO->GetManualManufacturingDuration());
		RecipeJson->SetNumberField(TEXT("variablePowerConstant"), RecipeCDO->GetPowerConsumptionConstant());
		RecipeJson->SetNumberField(TEXT("variablePowerFactor"), RecipeCDO->GetPowerConsumptionFactor());

		if (DurationSeconds <= 0.0f)
		{
			AddWarning(
				Warnings,
				TEXT("NON_POSITIVE_RECIPE_DURATION"),
				RecipePath,
				FString::Printf(TEXT("Duration is %.6f seconds; rates were omitted"), DurationSeconds));
		}

		TArray<TSharedPtr<FJsonValue>> IngredientsJson;
		const TArray<FItemAmount>& Ingredients = RecipeCDO->GetIngredients();
		for (int32 IngredientIndex = 0; IngredientIndex < Ingredients.Num(); ++IngredientIndex)
		{
			IngredientsJson.Add(MakeShared<FJsonValueObject>(BuildItemAmountJson(
				Ingredients[IngredientIndex],
				RecipePath,
				TEXT("ingredients"),
				IngredientIndex,
				DurationMinutes,
				ReferencedItemClasses,
				Warnings,
				bSatisfactoryPlusDetected)));
		}
		RecipeJson->SetArrayField(TEXT("ingredients"), IngredientsJson);

		TArray<TSharedPtr<FJsonValue>> ProductsJson;
		const TArray<FItemAmount>& Products = RecipeCDO->GetProducts();
		for (int32 ProductIndex = 0; ProductIndex < Products.Num(); ++ProductIndex)
		{
			ProductsJson.Add(MakeShared<FJsonValueObject>(BuildItemAmountJson(
				Products[ProductIndex],
				RecipePath,
				TEXT("products"),
				ProductIndex,
				DurationMinutes,
				ReferencedItemClasses,
				Warnings,
				bSatisfactoryPlusDetected)));
		}
		RecipeJson->SetArrayField(TEXT("products"), ProductsJson);

		if (Products.IsEmpty())
		{
			AddWarning(
				Warnings,
				TEXT("RECIPE_WITHOUT_PRODUCTS"),
				RecipePath,
				TEXT("Recipe was retained but has no products"));
		}

		TArray<TSharedPtr<FJsonValue>> ProducersJson;
		const TArray<TSubclassOf<UObject>> Producers = UFGRecipe::GetProducedIn(Recipe);
		for (int32 ProducerIndex = 0; ProducerIndex < Producers.Num(); ++ProducerIndex)
		{
			UClass* ProducerClass = Producers[ProducerIndex].Get();
			if (!IsValid(ProducerClass))
			{
				AddWarning(
					Warnings,
					TEXT("INVALID_PRODUCER_REFERENCE"),
					RecipePath,
					FString::Printf(TEXT("producers[%d] is null or invalid"), ProducerIndex));
				continue;
			}

			TSharedRef<FJsonObject> ProducerJson = MakeShared<FJsonObject>();
			const FString ProducerPath = ProducerClass->GetPathName();
			ProducerJson->SetStringField(TEXT("classPath"), ProducerPath);
			ProducerJson->SetStringField(TEXT("className"), ProducerClass->GetName());
			ProducerJson->SetStringField(TEXT("sourceMount"), SourceMountFromPath(ProducerPath));

			const AFGBuildableFactory* FactoryCDO = ProducerClass->IsChildOf(AFGBuildableFactory::StaticClass())
				? Cast<AFGBuildableFactory>(ProducerClass->GetDefaultObject())
				: nullptr;
			ProducerJson->SetBoolField(TEXT("isFactory"), IsValid(FactoryCDO));
			if (IsValid(FactoryCDO))
			{
				ProducerJson->SetBoolField(TEXT("runsOnPower"), FactoryCDO->RunsOnPower());
				ProducerJson->SetNumberField(TEXT("basePowerConsumptionMW"), FactoryCDO->GetDefaultProducingPowerConsumption());
				ProducerJson->SetNumberField(TEXT("idlePowerConsumptionMW"), FactoryCDO->GetIdlePowerConsumption());

				if (!MachinesByPath.Contains(ProducerPath))
				{
					AFGBuildableFactory* const* Instance = InstanceByClass.Find(ProducerClass);
					const bool bRuntimeSample = Instance != nullptr && IsValid(*Instance);
					const AFGBuildableFactory* ConnectionSample = bRuntimeSample ? *Instance : FactoryCDO;
					int32 InputConnections = 0;
					int32 OutputConnections = 0;
					for (const UFGFactoryConnectionComponent* Connection : ConnectionSample->GetConnectionComponents())
					{
						if (!IsValid(Connection))
						{
							continue;
						}
						if (Connection->GetDirection() == EFactoryConnectionDirection::FCD_INPUT)
						{
							++InputConnections;
						}
						else if (Connection->GetDirection() == EFactoryConnectionDirection::FCD_OUTPUT)
						{
							++OutputConnections;
						}
					}

					TInlineComponentArray<UFGPipeConnectionComponent*> PipeConnections;
					ConnectionSample->GetComponents(PipeConnections);

					TSharedPtr<FJsonObject> MachineJson = MakeShared<FJsonObject>();
					MachineJson->SetStringField(TEXT("classPath"), ProducerPath);
					MachineJson->SetStringField(TEXT("className"), ProducerClass->GetName());
					MachineJson->SetStringField(TEXT("displayName"), BuildableDisplayName(FactoryCDO, ProducerClass));
					MachineJson->SetStringField(TEXT("sourceMount"), SourceMountFromPath(ProducerPath));
					MachineJson->SetBoolField(TEXT("runsOnPower"), FactoryCDO->RunsOnPower());
					MachineJson->SetNumberField(TEXT("basePowerConsumptionMW"), FactoryCDO->GetDefaultProducingPowerConsumption());
					MachineJson->SetNumberField(TEXT("idlePowerConsumptionMW"), FactoryCDO->GetIdlePowerConsumption());
					MachineJson->SetBoolField(TEXT("connectionCountsKnown"), bRuntimeSample || InputConnections + OutputConnections + PipeConnections.Num() > 0);
					MachineJson->SetStringField(TEXT("connectionCountSource"), bRuntimeSample ? TEXT("live_actor") : TEXT("class_default_object; zero may mean unavailable"));
					MachineJson->SetNumberField(TEXT("solidInputConnections"), InputConnections);
					MachineJson->SetNumberField(TEXT("solidOutputConnections"), OutputConnections);
					MachineJson->SetNumberField(TEXT("pipeConnections"), PipeConnections.Num());
					MachinesByPath.Add(ProducerPath, MoveTemp(MachineJson));
				}
			}

			ProducersJson.Add(MakeShared<FJsonValueObject>(ProducerJson));
			bSatisfactoryPlusDetected |= IsSatisfactoryPlusPath(ProducerPath);
		}
		RecipeJson->SetArrayField(TEXT("producedIn"), ProducersJson);

		RecipeValues.Add(MakeShared<FJsonValueObject>(RecipeJson));
		bSatisfactoryPlusDetected |= IsSatisfactoryPlusPath(RecipePath);
	}

	for (const TSubclassOf<UFGRecipe>& Recipe : Recipes)
	{
		UClass* RawRecipeClass = Recipe.Get();
		const UFGRecipe* RecipeCDO = IsValid(RawRecipeClass)
			? Cast<UFGRecipe>(RawRecipeClass->GetDefaultObject())
			: nullptr;
		if (!IsValid(RecipeCDO) || RecipeCDO->GetProducts().IsEmpty())
		{
			continue;
		}
		UClass* ProductClass = RecipeCDO->GetProducts()[0].ItemClass.Get();
		if (!IsValid(ProductClass) || !ProductClass->IsChildOf(UFGBuildingDescriptor::StaticClass()))
		{
			continue;
		}

		const TSubclassOf<AFGBuildable> BuildableClass = AFGBuildable::GetBuildableClassFromRecipe(Recipe);
		UClass* RawBuildableClass = BuildableClass.Get();
		if (!IsValid(RawBuildableClass))
		{
			continue;
		}


		if (RawBuildableClass->GetPathName().StartsWith(TEXT("/MkPlus/"))
			|| RawBuildableClass->GetPathName().StartsWith(TEXT("/MkPlusLibs/")))
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("constructionRecipe"), RawRecipeClass->GetPathName());
			Entry->SetBoolField(TEXT("available"), RecipeManager->IsRecipeAvailable(Recipe));
			Entry->SetStringField(TEXT("classPath"), RawBuildableClass->GetPathName());
			const AFGBuildable* Default = Cast<AFGBuildable>(RawBuildableClass->GetDefaultObject());
			Entry->SetStringField(TEXT("displayName"), BuildableDisplayName(Default, RawBuildableClass));
			Entry->SetObjectField(TEXT("defaultFields"), IndustrialEvolutionFields(Default));
			TArray<TSharedPtr<FJsonValue>> Hierarchy;
			for (UClass* Parent = RawBuildableClass; Parent; Parent = Parent->GetSuperClass())
				Hierarchy.Add(MakeShared<FJsonValueString>(Parent->GetPathName()));
			Entry->SetArrayField(TEXT("classHierarchy"), Hierarchy);
			const AFGBuildableFactory* Factory = Cast<AFGBuildableFactory>(Default);
			Entry->SetBoolField(TEXT("isFactory"), IsValid(Factory));
			if (IsValid(Factory))
				Entry->SetNumberField(TEXT("basePowerConsumptionMW"), Factory->GetDefaultProducingPowerConsumption());
			AFGBuildableFactory* const* Sample = InstanceByClass.Find(RawBuildableClass);
			Entry->SetBoolField(TEXT("liveSamplePresent"), Sample && IsValid(*Sample));
			if (Sample && IsValid(*Sample))
				Entry->SetObjectField(TEXT("liveFields"), IndustrialEvolutionFields(*Sample));
			TArray<TSharedPtr<FJsonValue>> Functions;
			for (TFieldIterator<UFunction> Function(RawBuildableClass); Function; ++Function)
			{
				const FString Name = Function->GetName();
				if (Name.Contains(TEXT("Recipe")) || Name.Contains(TEXT("Manufactur"))
					|| Name.Contains(TEXT("Speed")) || Name.Contains(TEXT("Potential")))
					Functions.Add(MakeShared<FJsonValueString>(Name));
				if (Functions.Num() >= 128) break;
			}
			Entry->SetArrayField(TEXT("relevantFunctionNames"), Functions);

			if (const AFGBuildableManufacturerVariablePower* VariableDefault =
				Cast<AFGBuildableManufacturerVariablePower>(Default))
			{
				TArray<TSharedPtr<FJsonValue>> CurveSamples;
				const FObjectPropertyBase* CurveProperty = CastField<FObjectPropertyBase>(
					RawBuildableClass->FindPropertyByName(TEXT("mPowerConsumptionCurve")));
				const UCurveFloat* Curve = CurveProperty
					? Cast<UCurveFloat>(CurveProperty->GetObjectPropertyValue_InContainer(VariableDefault)) : nullptr;
				Entry->SetBoolField(TEXT("powerCurvePresent"), IsValid(Curve));
				if (IsValid(Curve))
				{
					Entry->SetStringField(TEXT("powerCurvePath"), Curve->GetPathName());
					for (int32 I = 0; I <= 128; ++I)
						CurveSamples.Add(MakeShared<FJsonValueNumber>(Curve->GetFloatValue(I / 128.0f)));
				}
				Entry->SetArrayField(TEXT("powerCurveSamplesOverCycle"), CurveSamples);
				if (Sample && IsValid(*Sample))
				{
					if (const AFGBuildableManufacturerVariablePower* Live =
						Cast<AFGBuildableManufacturerVariablePower>(*Sample))
					{
						Entry->SetNumberField(TEXT("liveRecipeMinPowerMW"), Live->GetMinPowerConsumption());
						Entry->SetNumberField(TEXT("liveRecipeMaxPowerMW"), Live->GetMaxPowerConsumption());
						Entry->SetNumberField(TEXT("liveProducingPowerBaseMW"), Live->GetProducingPowerConsumptionBase());
					}
				}
			}

			IndustrialEvolutionMachines.Add(MakeShared<FJsonValueObject>(Entry));
		}

		const FString BuildablePath = RawBuildableClass->GetPathName();
		if (TransportByPath.Contains(BuildablePath))
		{
			continue;
		}

		const AFGBuildable* BuildableCDO = Cast<AFGBuildable>(RawBuildableClass->GetDefaultObject());
		if (!IsValid(BuildableCDO))
		{
			continue;
		}

		TSharedPtr<FJsonObject> TransportJson = MakeShared<FJsonObject>();
		TransportJson->SetStringField(TEXT("classPath"), BuildablePath);
		TransportJson->SetStringField(TEXT("className"), RawBuildableClass->GetName());
		TransportJson->SetStringField(TEXT("displayName"), BuildableDisplayName(BuildableCDO, RawBuildableClass));
		TransportJson->SetStringField(TEXT("sourceMount"), SourceMountFromPath(BuildablePath));

		if (const AFGBuildableConveyorBase* Belt = Cast<AFGBuildableConveyorBase>(BuildableCDO))
		{
			TransportJson->SetStringField(TEXT("kind"), TEXT("belt"));
			TransportJson->SetNumberField(TEXT("speedCmPerSecond"), Belt->GetSpeed());
			TransportJson->SetNumberField(
				TEXT("capacityPerMinute"),
				Belt->GetSpeed() * 60.0 / AFGBuildableConveyorBase::ITEM_SPACING);
		}
		else if (const AFGBuildablePipeline* Pipe = Cast<AFGBuildablePipeline>(BuildableCDO))
		{
			TransportJson->SetStringField(TEXT("kind"), TEXT("pipe"));
			TransportJson->SetNumberField(TEXT("flowLimitM3PerSecond"), Pipe->GetFlowLimit());
			TransportJson->SetNumberField(TEXT("capacityPerMinute"), Pipe->GetFlowLimit() * 60.0);
		}
		else
		{
			continue;
		}

		TransportByPath.Add(BuildablePath, MoveTemp(TransportJson));
	}

	const TArray<TSubclassOf<UFGItemDescriptor>>& ManagerItems = RecipeManager->GetAllItemDescriptors();
	for (int32 ItemIndex = 0; ItemIndex < ManagerItems.Num(); ++ItemIndex)
	{
		UClass* ItemClass = ManagerItems[ItemIndex].Get();
		if (!IsValid(ItemClass) || !ItemClass->IsChildOf(UFGItemDescriptor::StaticClass()))
		{
			AddWarning(
				Warnings,
				TEXT("INVALID_ITEM_DESCRIPTOR"),
				FString::Printf(TEXT("recipeManager.itemDescriptors[%d]"), ItemIndex),
				TEXT("Skipped a null or invalid item descriptor class"));
			continue;
		}
		ReferencedItemClasses.Add(ItemClass);
	}

	TArray<UClass*> SortedItemClasses = ReferencedItemClasses.Array();
	SortedItemClasses.Sort([](const UClass& Left, const UClass& Right)
	{
		return Left.GetPathName() < Right.GetPathName();
	});

	TArray<TSharedPtr<FJsonValue>> ItemValues;
	for (UClass* ItemClass : SortedItemClasses)
	{
		if (!IsValid(ItemClass))
		{
			continue;
		}
		ItemValues.Add(MakeShared<FJsonValueObject>(BuildItemJson(ItemClass, bSatisfactoryPlusDetected)));
	}

	TArray<TSharedPtr<FJsonValue>> ModValues;
	FString GameVersion;
	FString SatisfactoryPlusVersion;
	FString ExporterVersion = GetPluginVersion(ExporterModReference);
	if (UGameInstance* GameInstance = World->GetGameInstance())
	{
		if (UModLoadingLibrary* ModLoadingLibrary = GameInstance->GetSubsystem<UModLoadingLibrary>())
		{
			TArray<FModInfo> LoadedMods = ModLoadingLibrary->GetLoadedMods();
			LoadedMods.Sort([](const FModInfo& Left, const FModInfo& Right)
			{
				return Left.Name < Right.Name;
			});

			for (const FModInfo& Mod : LoadedMods)
			{
				TSharedRef<FJsonObject> ModJson = MakeShared<FJsonObject>();
				ModJson->SetStringField(TEXT("name"), Mod.Name);
				ModJson->SetStringField(TEXT("friendlyName"), Mod.FriendlyName);
				ModJson->SetStringField(TEXT("version"), Mod.Version.ToString());
				ModJson->SetStringField(TEXT("createdBy"), Mod.CreatedBy);
				ModJson->SetBoolField(TEXT("requiredOnRemote"), Mod.bRequiredOnRemote);
				ModValues.Add(MakeShared<FJsonValueObject>(ModJson));

				if (Mod.Name.Equals(TEXT("FactoryGame"), ESearchCase::IgnoreCase))
				{
					GameVersion = Mod.Version.ToString();
				}
				else if (Mod.Name.Equals(TargetModReference, ESearchCase::IgnoreCase))
				{
					bSatisfactoryPlusDetected = true;
					SatisfactoryPlusVersion = Mod.Version.ToString();
				}
				else if (Mod.Name.Equals(ExporterModReference, ESearchCase::IgnoreCase))
				{
					ExporterVersion = Mod.Version.ToString();
				}
			}
		}
	}

	if (SatisfactoryPlusVersion.IsEmpty())
	{
		SatisfactoryPlusVersion = GetPluginVersion(TargetModReference);
	}
	if (!SatisfactoryPlusVersion.IsEmpty())
	{
		bSatisfactoryPlusDetected = true;
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schemaVersion"), ExportSchemaVersion);

	TSharedRef<FJsonObject> ExporterJson = MakeShared<FJsonObject>();
	ExporterJson->SetStringField(TEXT("modReference"), ExporterModReference);
	ExporterJson->SetStringField(TEXT("version"), ExporterVersion);
	ExporterJson->SetStringField(TEXT("generatedAtUtc"), FDateTime::UtcNow().ToIso8601());
	ExporterJson->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
	ExporterJson->SetStringField(TEXT("buildVersion"), FApp::GetBuildVersion());
	Root->SetObjectField(TEXT("exporter"), ExporterJson);

	TSharedRef<FJsonObject> RuntimeJson = MakeShared<FJsonObject>();
	RuntimeJson->SetStringField(TEXT("world"), World->GetMapName());
	RuntimeJson->SetStringField(TEXT("gameVersion"), GameVersion);
	RuntimeJson->SetBoolField(TEXT("satisfactoryPlusDetected"), bSatisfactoryPlusDetected);
	RuntimeJson->SetStringField(TEXT("satisfactoryPlusVersion"), SatisfactoryPlusVersion);
	Root->SetObjectField(TEXT("runtime"), RuntimeJson);
	Root->SetArrayField(TEXT("mods"), ModValues);
	Root->SetArrayField(TEXT("recipes"), RecipeValues);
	Root->SetArrayField(TEXT("items"), ItemValues);
	Root->SetArrayField(TEXT("machines"), SortedObjectMapValues(MachinesByPath));
	Root->SetArrayField(TEXT("industrialEvolutionMachines"), IndustrialEvolutionMachines);
	Root->SetArrayField(TEXT("transport"), SortedObjectMapValues(TransportByPath));
	Root->SetArrayField(TEXT("warnings"), Warnings);

	// Fresh diagnostic catalog: do not alter the active planner or its saved plans.
	FSFPPlannerSolver DiagnosticSolver;
	DiagnosticSolver.EnableMinerDiagnostics();
	FString DiagnosticError;
	const bool bDiagnosticSuccess = DiagnosticSolver.Initialize(World, DiagnosticError);
	TSharedRef<FJsonObject> MinerDiagnosticJson = MakeShared<FJsonObject>();
	MinerDiagnosticJson->SetStringField(TEXT("build"), TEXT("1.4.1-Release"));
	MinerDiagnosticJson->SetBoolField(TEXT("catalogInitialized"), bDiagnosticSuccess);
	MinerDiagnosticJson->SetStringField(TEXT("error"), DiagnosticError);
	TArray<TSharedPtr<FJsonValue>> MinerTrace;
	for (const FString& Entry : DiagnosticSolver.GetMinerDiagnostics())
	{
		MinerTrace.Add(MakeShared<FJsonValueString>(Entry));
	}
	MinerDiagnosticJson->SetArrayField(TEXT("trace"), MinerTrace);
	Root->SetObjectField(TEXT("minerDiagnostics"), MinerDiagnosticJson);
	TArray<TSharedPtr<FJsonValue>> SourceRoutes;
	for (const FSFPPlannerRecipe& Route : DiagnosticSolver.GetRuntimeRecipes())
	{
		if (!Route.bDirectResourceExtraction) continue;
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("routeId"), Route.ClassPath);
		Entry->SetStringField(TEXT("name"), Route.DisplayName);
		Entry->SetStringField(TEXT("machine"), Route.MachineClass ? Route.MachineClass->GetPathName() : FString());
		Entry->SetBoolField(TEXT("available"), Route.bAvailable);
		Entry->SetStringField(TEXT("purity"), Route.ResourceNodeLabel);
		Entry->SetStringField(TEXT("configuration"), Route.ConfigurationDetail);
		Entry->SetNumberField(TEXT("powerMW"), Route.BasePowerMW);
		Entry->SetNumberField(TEXT("operatingFluidPerPhysicalMinerM3Min"), Route.FluidRatePerMinute);
		TArray<TSharedPtr<FJsonValue>> ModuleValues;
		for (const FString& Module : Route.AdditionalBuildableClassPaths) ModuleValues.Add(MakeShared<FJsonValueString>(Module));
		Entry->SetArrayField(TEXT("modules"), ModuleValues);
		auto Rates = [](const TArray<FSFPPlannerItemRate>& Items)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FSFPPlannerItemRate& Item : Items)
			{
				TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
				Value->SetStringField(TEXT("classPath"), Item.ClassPath);
				Value->SetStringField(TEXT("name"), Item.DisplayName);
				Value->SetStringField(TEXT("form"), Item.Form);
				Value->SetNumberField(TEXT("ratePerMinute"), Item.RatePerMinute);
				Values.Add(MakeShared<FJsonValueObject>(Value));
			}
			return Values;
		};
		Entry->SetArrayField(TEXT("inputs"), Rates(Route.Ingredients));
		Entry->SetArrayField(TEXT("outputs"), Rates(Route.Products));
		SourceRoutes.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Root->SetArrayField(TEXT("sourceRoutes"), SourceRoutes);

	TSharedRef<FJsonObject> SummaryJson = MakeShared<FJsonObject>();
	SummaryJson->SetNumberField(TEXT("recipes"), RecipeValues.Num());
	SummaryJson->SetNumberField(TEXT("items"), ItemValues.Num());
	SummaryJson->SetNumberField(TEXT("machines"), MachinesByPath.Num());
	SummaryJson->SetNumberField(TEXT("transport"), TransportByPath.Num());
	SummaryJson->SetNumberField(TEXT("warnings"), Warnings.Num());
	SummaryJson->SetNumberField(TEXT("skippedRecipes"), Recipes.Num() - RecipeValues.Num());
	Root->SetObjectField(TEXT("summary"), SummaryJson);

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		ExportResult.ErrorMessage = TEXT("JSON serialization failed");
		return ExportResult;
	}

	const FString OutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SFPFactoryPlanner"), TEXT("Exports"));
	if (!IFileManager::Get().MakeDirectory(*OutputDirectory, true)
		&& !IFileManager::Get().DirectoryExists(*OutputDirectory))
	{
		ExportResult.ErrorMessage = FString::Printf(TEXT("Could not create output directory: %s"), *OutputDirectory);
		return ExportResult;
	}

	const FString SafeWorldName = FPaths::MakeValidFileName(World->GetMapName());
	const FString Timestamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));
	const FString OutputFile = FPaths::Combine(
		OutputDirectory,
		FString::Printf(TEXT("SFP-Runtime-%s-%s.json"), *SafeWorldName, *Timestamp));

	if (!FFileHelper::SaveStringToFile(
		JsonText,
		*OutputFile,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		ExportResult.ErrorMessage = FString::Printf(TEXT("Could not write export file: %s"), *OutputFile);
		return ExportResult;
	}

	ExportResult.bSuccess = true;
	ExportResult.FilePath = FPaths::ConvertRelativePathToFull(OutputFile);
	ExportResult.RecipeCount = RecipeValues.Num();
	ExportResult.ItemCount = ItemValues.Num();
	ExportResult.MachineCount = MachinesByPath.Num();
	ExportResult.TransportCount = TransportByPath.Num();
	ExportResult.WarningCount = Warnings.Num();
	ExportResult.bSatisfactoryPlusDetected = bSatisfactoryPlusDetected;
	ExportResult.SatisfactoryPlusVersion = SatisfactoryPlusVersion;

	UE_LOG(
		LogSFPFactoryPlanner,
		Display,
		TEXT("Runtime export written to %s (%d recipes, %d items, %d warnings)"),
		*ExportResult.FilePath,
		ExportResult.RecipeCount,
		ExportResult.ItemCount,
		ExportResult.WarningCount);
	return ExportResult;
}

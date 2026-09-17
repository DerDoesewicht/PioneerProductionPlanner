#include "SFPPlannerSolver.h"
#include "SFPVariablePower.h"
#include "Curves/CurveFloat.h"
#include "Buildables/FGBuildableManufacturerVariablePower.h"
#include "SFPModularMinerRoutePolicy.h"
#include "SFPMinerRates.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildableFactory.h"
#include "Buildables/FGBuildableGenerator.h"
#include "Buildables/FGBuildableGeneratorFuel.h"
#include "Buildables/FGBuildablePipeline.h"
#include "Buildables/FGBuildableResourceExtractor.h"
#include "Resources/FGResourceDescriptor.h"
#include "Resources/FGResourceNode.h"
#include "SFPStandardMinerRates.h"
#include "FGRecipe.h"
#include "FGRecipeManager.h"
#include "FGSchematic.h"
#include "FGSchematicManager.h"
#include "Internationalization/Regex.h"
#include "Modules/ModuleManager.h"
#include "Resources/FGBuildingDescriptor.h"
#include "Resources/FGItemDescriptor.h"
#include "Resources/FGItemDescriptorNuclearFuel.h"
#include "SFPFactoryPlanner.h"
#include "SFPNumberFormatting.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	constexpr int32 ModularMinerBeltOutputs = 2;
	constexpr int32 MaxPlannerDepth = 40;
	constexpr int32 MaxPlannerNodes = 1200;
	constexpr int32 MaxPlannerExpansions = 25000;

	struct FOptionalBuildableInfo
	{
		UClass* BuildableClass = nullptr;
		FString ClassPath;
		FString DisplayName;
		bool bAvailable = false;
		double PowerMW = 0.0;
	};

	struct FOptionalModularMinerModule : FOptionalBuildableInfo
	{
		UClass* AttachmentClass = nullptr;
		UClass* WasteClass = nullptr;
		int32 Tier = 1;
		double Bonus = 0.0;
		double Malus = 0.0;
	};

	struct FOptionalPowerBuildableInfo : FOptionalBuildableInfo
	{
		UClass* DescriptorClass = nullptr;
		FString DescriptorPath;
		FString Description;
		FString SearchText;
	};

	struct FBoilerOperatingPoint
	{
		double TemperatureC = 0.0;
		double WaterPerMinute = 0.0;
		double SteamPerMinute = 0.0;
	};

	FString SolverSourceMountFromPath(const FString& ObjectPath)
	{
		TArray<FString> Parts;
		ObjectPath.ParseIntoArray(Parts, TEXT("/"), true);
		return Parts.IsEmpty() ? FString() : Parts[0];
	}

	FString SolverResourceFormToString(const EResourceForm Form)
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

	bool IsAutomaticUnpackagingRoute(const FSFPPlannerRecipe& Recipe, UClass* ProductClass)
	{
		if (!IsValid(ProductClass) || !ProductClass->IsChildOf(UFGItemDescriptor::StaticClass()))
		{
			return false;
		}

		const EResourceForm ProductForm = UFGItemDescriptor::GetForm(
			TSubclassOf<UFGItemDescriptor>(ProductClass));
		if (ProductForm != EResourceForm::RF_LIQUID && ProductForm != EResourceForm::RF_GAS)
		{
			return false;
		}

		// Packager recipes are transport/form conversions, not primary fluid sources.
		// Automatically choosing them for a liquid/gas demand creates paths such as
		// Crude Oil -> Packaged Oil -> Unpackage Oil -> Crude Oil. Keep them usable
		// through an explicit recipe override, but never select them as the default
		// producer of a fluid/gas.
		return Recipe.ClassPath.Contains(TEXT("Unpackage"), ESearchCase::IgnoreCase)
			|| Recipe.ClassPath.Contains(TEXT("UnPackaged"), ESearchCase::IgnoreCase)
			|| Recipe.DisplayName.Contains(TEXT("entleeren"), ESearchCase::IgnoreCase)
			|| Recipe.DisplayName.Contains(TEXT("entpack"), ESearchCase::IgnoreCase)
			|| Recipe.DisplayName.Contains(TEXT("unpackage"), ESearchCase::IgnoreCase);
	}

	FString ItemDisplayName(UClass* ItemClass)
	{
		if (!IsValid(ItemClass) || !ItemClass->IsChildOf(UFGItemDescriptor::StaticClass()))
		{
			return TEXT("Unbekanntes Material");
		}

		const TSubclassOf<UFGItemDescriptor> TypedClass(ItemClass);
		FString Name = UFGItemDescriptor::GetItemName(TypedClass).ToString();
		return Name.IsEmpty() ? ItemClass->GetName() : Name;
	}

	FString SolverBuildableDisplayName(const AFGBuildable* Buildable, UClass* BuildableClass)
	{
		if (IsValid(Buildable) && !Buildable->mDisplayName.IsEmpty())
		{
			return Buildable->mDisplayName.ToString();
		}
		return IsValid(BuildableClass) ? BuildableClass->GetName() : TEXT("Unbekannte Maschine");
	}

	UClass* SafeBuildableClassFromRecipe(const TSubclassOf<UFGRecipe>& RecipeClass)
	{
		UClass* RawRecipeClass = RecipeClass.Get();
		const UFGRecipe* RecipeCDO = IsValid(RawRecipeClass)
			&& RawRecipeClass->IsChildOf(UFGRecipe::StaticClass())
			? Cast<UFGRecipe>(RawRecipeClass->GetDefaultObject())
			: nullptr;
		if (!IsValid(RecipeCDO))
		{
			return nullptr;
		}

		// AFGBuildable::GetBuildableClassFromRecipe indexes Products[0] without
		// checking the array. Runtime recipe catalogs from content mods can contain
		// recipes with no products (or a null/non-building first product), which
		// asserted as soon as F8 initialized the planner. Inspect every product
		// defensively and call the descriptor API only for a valid building class.
		for (const FItemAmount& Product : RecipeCDO->GetProducts())
		{
			UClass* ProductClass = Product.ItemClass.Get();
			if (!IsValid(ProductClass)
				|| !ProductClass->IsChildOf(UFGBuildingDescriptor::StaticClass()))
			{
				continue;
			}

			const TSubclassOf<UFGBuildingDescriptor> DescriptorClass(ProductClass);
			UClass* BuildableClass = UFGBuildingDescriptor::GetBuildableClass(DescriptorClass).Get();
			if (IsValid(BuildableClass) && BuildableClass->IsChildOf(AFGBuildable::StaticClass()))
			{
				return BuildableClass;
			}
		}
		return nullptr;
	}

	/**
	 * Resolve a base-game miner from the building descriptor contained in its
	 * live construction recipe.  This deliberately derives the buildable path
	 * from the descriptor path instead of guessing folder capitalization: on
	 * Linux the shipped MinerMK1, MinerMk2 and MinerMk3 package names are
	 * case-sensitive and not consistent with each other.
	 */
	UClass* RuntimeStandardMinerBuildableFromRecipe(
		const TSubclassOf<UFGRecipe>& RecipeClass,
		FString& OutDescriptorPath,
		FString& OutDerivedBuildablePath)
	{
		OutDescriptorPath.Reset();
		OutDerivedBuildablePath.Reset();
		UClass* RawRecipeClass = RecipeClass.Get();
		const UFGRecipe* RecipeCDO = IsValid(RawRecipeClass)
			&& RawRecipeClass->IsChildOf(UFGRecipe::StaticClass())
			? Cast<UFGRecipe>(RawRecipeClass->GetDefaultObject())
			: nullptr;
		if (!IsValid(RecipeCDO))
		{
			return nullptr;
		}

		for (const FItemAmount& Product : RecipeCDO->GetProducts())
		{
			UClass* DescriptorClass = Product.ItemClass.Get();
			if (!IsValid(DescriptorClass))
			{
				continue;
			}

			const FString DescriptorPath = DescriptorClass->GetPathName();
			FString DescriptorName = DescriptorClass->GetName();
			if (!DescriptorPath.StartsWith(
					TEXT("/Game/FactoryGame/Buildable/Factory/Miner"),
					ESearchCase::IgnoreCase)
				|| !DescriptorName.StartsWith(TEXT("Desc_Miner"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			OutDescriptorPath = DescriptorPath;

			// Prefer the descriptor's authoritative class reference when it is
			// already resolved by the runtime recipe catalog.
			if (DescriptorClass->IsChildOf(UFGBuildingDescriptor::StaticClass()))
			{
				UClass* MappedClass = UFGBuildingDescriptor::GetBuildableClass(
					TSubclassOf<UFGBuildingDescriptor>(DescriptorClass)).Get();
				if (IsValid(MappedClass)
					&& MappedClass->IsChildOf(AFGBuildableResourceExtractor::StaticClass()))
				{
					OutDerivedBuildablePath = MappedClass->GetPathName();
					return MappedClass;
				}
			}

			// The descriptor is present in the live recipe even when its buildable
			// soft reference has not been loaded yet. Preserve the exact package
			// directory and derive only the conventional asset/object name.
			DescriptorName.RemoveFromEnd(TEXT("_C"), ESearchCase::IgnoreCase);
			if (!DescriptorName.StartsWith(TEXT("Desc_"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			const FString MinerName = DescriptorName.Mid(5);
			const FString DescriptorPackage = DescriptorClass->GetOutermost()->GetName();
			int32 LastSlash = INDEX_NONE;
			if (!DescriptorPackage.FindLastChar(TEXT('/'), LastSlash) || LastSlash <= 0)
			{
				continue;
			}
			const FString BuildableObject = TEXT("Build_") + MinerName;
			OutDerivedBuildablePath = DescriptorPackage.Left(LastSlash + 1)
				+ BuildableObject + TEXT(".") + BuildableObject + TEXT("_C");
			if (UClass* LoadedClass = LoadClass<AFGBuildableResourceExtractor>(
				nullptr, *OutDerivedBuildablePath))
			{
				return LoadedClass;
			}
		}
		return nullptr;
	}

	int32 VanillaStandardMinerTier(const FString& ClassPath)
	{
		if (ClassPath.Equals(
			TEXT("/Game/FactoryGame/Buildable/Factory/MinerMK1/Build_MinerMk1.Build_MinerMk1_C"),
			ESearchCase::CaseSensitive))
		{
			return 1;
		}
		if (ClassPath.Equals(
			TEXT("/Game/FactoryGame/Buildable/Factory/MinerMk2/Build_MinerMk2.Build_MinerMk2_C"),
			ESearchCase::CaseSensitive))
		{
			return 2;
		}
		if (ClassPath.Equals(
			TEXT("/Game/FactoryGame/Buildable/Factory/MinerMk3/Build_MinerMk3.Build_MinerMk3_C"),
			ESearchCase::CaseSensitive))
		{
			return 3;
		}
		return 0;
	}

	UClass* SafeBuildingDescriptorClassFromRecipe(const TSubclassOf<UFGRecipe>& RecipeClass)
	{
		UClass* RawRecipeClass = RecipeClass.Get();
		const UFGRecipe* RecipeCDO = IsValid(RawRecipeClass)
			&& RawRecipeClass->IsChildOf(UFGRecipe::StaticClass())
			? Cast<UFGRecipe>(RawRecipeClass->GetDefaultObject())
			: nullptr;
		if (!IsValid(RecipeCDO))
		{
			return nullptr;
		}

		for (const FItemAmount& Product : RecipeCDO->GetProducts())
		{
			UClass* ProductClass = Product.ItemClass.Get();
			if (!IsValid(ProductClass)
				|| !ProductClass->IsChildOf(UFGBuildingDescriptor::StaticClass()))
			{
				continue;
			}
			const TSubclassOf<UFGBuildingDescriptor> DescriptorClass(ProductClass);
			UClass* BuildableClass = UFGBuildingDescriptor::GetBuildableClass(DescriptorClass).Get();
			if (IsValid(BuildableClass) && BuildableClass->IsChildOf(AFGBuildable::StaticClass()))
			{
				return ProductClass;
			}
		}
		return nullptr;
	}

	double ParseDecimalNumber(FString Number)
	{
		Number.TrimStartAndEndInline();
		Number.ReplaceInline(TEXT(" "), TEXT(""));
		Number.ReplaceInline(TEXT(","), TEXT("."));
		return FCString::Atod(*Number);
	}

	double DescriptionMaximumPowerMW(const FString& Description)
	{
		double MaximumMW = 0.0;
		const FRegexPattern Pattern(TEXT("([0-9]+(?:[\\.,][0-9]+)?)\\s*(GW|MW)"));
		FRegexMatcher Matcher(Pattern, Description);
		while (Matcher.FindNext())
		{
			double Value = ParseDecimalNumber(Matcher.GetCaptureGroup(1));
			if (Matcher.GetCaptureGroup(2).Equals(TEXT("GW"), ESearchCase::IgnoreCase))
			{
				Value *= 1000.0;
			}
			MaximumMW = FMath::Max(MaximumMW, Value);
		}
		return MaximumMW;
	}

	double DescriptionMaximumRPM(const FString& Description)
	{
		double MaximumRPM = 0.0;
		const FRegexPattern Pattern(TEXT("([0-9][0-9\\., ]*)\\s*(?:RPM|UPM)"));
		FRegexMatcher Matcher(Pattern, Description);
		while (Matcher.FindNext())
		{
			FString Number = Matcher.GetCaptureGroup(1);
			Number.ReplaceInline(TEXT("."), TEXT(""));
			Number.ReplaceInline(TEXT(","), TEXT(""));
			Number.ReplaceInline(TEXT(" "), TEXT(""));
			MaximumRPM = FMath::Max(MaximumRPM, FCString::Atod(*Number));
		}
		return MaximumRPM;
	}

	void ParseBoilerOperatingPoints(
		const FString& Description,
		TArray<FBoilerOperatingPoint>& OutPoints)
	{
		OutPoints.Reset();
		const FRegexPattern Pattern(TEXT(
			"([0-9]+(?:[\\.,][0-9]+)?)/min[^\\r\\n]*?"
			"([0-9]+(?:[\\.,][0-9]+)?)/min[^\\r\\n]*?"
			"\\(([0-9]+(?:[\\.,][0-9]+)?)"));
		FRegexMatcher Matcher(Pattern, Description);
		while (Matcher.FindNext())
		{
			FBoilerOperatingPoint Point;
			Point.WaterPerMinute = ParseDecimalNumber(Matcher.GetCaptureGroup(1));
			Point.SteamPerMinute = ParseDecimalNumber(Matcher.GetCaptureGroup(2));
			Point.TemperatureC = ParseDecimalNumber(Matcher.GetCaptureGroup(3));
			if (Point.WaterPerMinute > 0.0 && Point.SteamPerMinute > 0.0
				&& Point.TemperatureC > 0.0)
			{
				OutPoints.Add(Point);
			}
		}
		OutPoints.Sort([](const FBoilerOperatingPoint& Left, const FBoilerOperatingPoint& Right)
		{
			return Left.TemperatureC < Right.TemperatureC;
		});
	}

	double NormalizeAmount(const FItemAmount& Amount, FString& OutForm)
	{
		UClass* ItemClass = Amount.ItemClass.Get();
		if (!IsValid(ItemClass) || !ItemClass->IsChildOf(UFGItemDescriptor::StaticClass()))
		{
			OutForm = TEXT("invalid");
			return 0.0;
		}

		const EResourceForm Form = UFGItemDescriptor::GetForm(TSubclassOf<UFGItemDescriptor>(ItemClass));
		OutForm = SolverResourceFormToString(Form);
		const bool bFluid = Form == EResourceForm::RF_LIQUID || Form == EResourceForm::RF_GAS;
		return bFluid ? static_cast<double>(Amount.Amount) / 1000.0 : static_cast<double>(Amount.Amount);
	}

	bool IsClassOrParentNamed(UClass* Class, const FName ExpectedName)
	{
		for (UClass* Current = Class; IsValid(Current); Current = Current->GetSuperClass())
		{
			if (Current->GetFName() == ExpectedName)
			{
				return true;
			}
		}
		return false;
	}

	UClass* ReflectedClassValue(const void* Container, UStruct* OwnerType, const FName PropertyName)
	{
		if (Container == nullptr || !IsValid(OwnerType))
		{
			return nullptr;
		}
		const FObjectPropertyBase* Property = CastField<FObjectPropertyBase>(OwnerType->FindPropertyByName(PropertyName));
		if (Property == nullptr)
		{
			return nullptr;
		}
		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);
		return Cast<UClass>(Property->GetObjectPropertyValue(ValuePtr));
	}

	double ReflectedNumberValue(
		const void* Container,
		UStruct* OwnerType,
		const FName PropertyName,
		const double DefaultValue)
	{
		if (Container == nullptr || !IsValid(OwnerType))
		{
			return DefaultValue;
		}
		const FNumericProperty* Property = CastField<FNumericProperty>(OwnerType->FindPropertyByName(PropertyName));
		if (Property == nullptr)
		{
			return DefaultValue;
		}
		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);
		return Property->IsFloatingPoint()
			? Property->GetFloatingPointPropertyValue(ValuePtr)
			: static_cast<double>(Property->GetSignedIntPropertyValue(ValuePtr));
	}

	double FirstPositiveReflectedNumber(
		const void* Container,
		UStruct* OwnerType,
		const TArray<FName>& PropertyNames,
		const double FallbackValue)
	{
		for (const FName PropertyName : PropertyNames)
		{
			const double Value = ReflectedNumberValue(Container, OwnerType, PropertyName, -1.0);
			if (FMath::IsFinite(Value) && Value > KINDA_SMALL_NUMBER)
			{
				return Value;
			}
		}
		return FallbackValue;
	}

	double NormalizeFraction(const double Value, const double FallbackValue)
	{
		if (!FMath::IsFinite(Value) || Value <= KINDA_SMALL_NUMBER)
		{
			return FallbackValue;
		}
		// Content may expose a boost as a fraction (0.10), an absolute multiplier
		// (1.10), or a percentage (10). Convert all three to a +fraction.
		if (Value < 1.0 - KINDA_SMALL_NUMBER)
		{
			return Value;
		}
		if (Value <= 2.0 + KINDA_SMALL_NUMBER)
		{
			return FMath::Max(0.0, Value - 1.0);
		}
		return Value / 100.0;
	}


	bool ApplyVariableRecipePower(FSFPPlannerRecipe& Recipe)
	{
		const AFGBuildableManufacturerVariablePower* Machine = IsValid(Recipe.MachineClass)
			? Cast<AFGBuildableManufacturerVariablePower>(Recipe.MachineClass->GetDefaultObject()) : nullptr;
		if (!Machine) return true;
		const UFGRecipe* RecipeCDO = Recipe.RecipeClass.Get()
			? Cast<UFGRecipe>(Recipe.RecipeClass->GetDefaultObject()) : nullptr;
		const FObjectPropertyBase* Property = CastField<FObjectPropertyBase>(
			Recipe.MachineClass->FindPropertyByName(TEXT("mPowerConsumptionCurve")));
		const UCurveFloat* Curve = Property
			? Cast<UCurveFloat>(Property->GetObjectPropertyValue_InContainer(Machine)) : nullptr;
		if (!RecipeCDO || !IsValid(Curve)) return false;
		// Integrate over normalized cycle time, not wall-clock manufacturing duration.
		// Include fine samples as well as curve extrema; the mean is a numerical estimate.
		float CurveMin = 0, CurveMax = 0;
		Curve->FloatCurve.GetValueRange(CurveMin, CurveMax);
		constexpr int32 Steps = 4096;
		double Sum = 0;
		for (int32 I = 0; I <= Steps; ++I)
		{
			const float Value = Curve->GetFloatValue(static_cast<float>(I) / Steps);
			if (!FMath::IsFinite(Value)) return false;
			CurveMin = FMath::Min(CurveMin, Value);
			CurveMax = FMath::Max(CurveMax, Value);
			Sum += Value * ((I == 0 || I == Steps) ? 0.5 : 1.0);
		}
		SFPVariablePower::Range Power;
		if (!SFPVariablePower::RecipeRange(RecipeCDO->GetPowerConsumptionConstant(),
			RecipeCDO->GetPowerConsumptionFactor(), CurveMin, CurveMax, Sum / Steps, Power)) return false;
		Recipe.bVariablePower = true;
		Recipe.BasePowerMW = FMath::Max(0.0, Power.Mean);
		Recipe.PowerExponent = ReflectedNumberValue(Machine, Recipe.MachineClass, TEXT("mPowerConsumptionExponent"), 1.0);
		if (!FMath::IsFinite(Recipe.PowerExponent) || Recipe.PowerExponent <= 0) return false;
		Recipe.ConfigurationDetail = FString::Printf(
			TEXT("Rezeptleistung bei 100%% je Maschine: %s–%s MW | Zyklusmittel ca. %s MW"),
			*FSFPNumberFormatting::Decimal(FMath::Max(0.0, Power.Minimum), 2),
			*FSFPNumberFormatting::Decimal(Power.Maximum, 2),
			*FSFPNumberFormatting::Decimal(Recipe.BasePowerMW, 2));
		return true;
	}

	bool ReflectedBoolValue(
		const void* Container,
		UStruct* OwnerType,
		const FName PropertyName,
		const bool bDefaultValue)
	{
		if (Container == nullptr || !IsValid(OwnerType))
		{
			return bDefaultValue;
		}
		const FBoolProperty* Property = CastField<FBoolProperty>(OwnerType->FindPropertyByName(PropertyName));
		return Property != nullptr ? Property->GetPropertyValue_InContainer(Container) : bDefaultValue;
	}

	bool IsOptionalBurnerManufacturerClass(const UClass* MachineClass)
	{
		for (const UClass* Current = MachineClass; IsValid(Current); Current = Current->GetSuperClass())
		{
			if (Current->GetName().Equals(TEXT("KhaosBuildableManufacturerBurner"), ESearchCase::CaseSensitive)
				|| Current->GetPathName().Equals(TEXT("/Script/BurnerManufacturer.KhaosBuildableManufacturerBurner"), ESearchCase::CaseSensitive))
			{
				return true;
			}
		}
		return false;
	}

	bool ReadOptionalBurnerFuelClasses(UObject* MachineCDO, TArray<TSoftClassPtr<UFGItemDescriptor>>& OutFuelClasses)
	{
		OutFuelClasses.Reset();
		if (!IsValid(MachineCDO))
		{
			return false;
		}

		UFunction* Getter = MachineCDO->FindFunction(TEXT("GetDefaultFuelClasses"));
		if (!IsValid(Getter))
		{
			return false;
		}

		struct FGetDefaultFuelClassesParams
		{
			TArray<TSoftClassPtr<UFGItemDescriptor>> ReturnValue;
		};

		FGetDefaultFuelClassesParams Params;
		MachineCDO->ProcessEvent(Getter, &Params);
		OutFuelClasses = MoveTemp(Params.ReturnValue);
		return !OutFuelClasses.IsEmpty();
	}

	int32 KnownVanillaSomersloopSlots(const UClass* MachineClass)
	{
		if (!IsValid(MachineClass))
		{
			return INDEX_NONE;
		}

		// Resource extractors (miners, water/oil extractors, resource-well extractors)
		// are intentionally not production-amplifiable in vanilla.
		if (MachineClass->IsChildOf(AFGBuildableResourceExtractor::StaticClass()))
		{
			return 0;
		}

		const FString ClassName = MachineClass->GetName();
		// Fallbacks are only used when the loaded class does not expose a usable
		// runtime max-production-boost. Runtime values always win so modded/S+
		// machine classes can configure their own limits.
		if (ClassName.Equals(TEXT("Build_SmelterMk1_C"), ESearchCase::CaseSensitive)
			|| ClassName.Equals(TEXT("Build_ConstructorMk1_C"), ESearchCase::CaseSensitive))
		{
			return 1;
		}
		if (ClassName.Equals(TEXT("Build_AssemblerMk1_C"), ESearchCase::CaseSensitive)
			|| ClassName.Equals(TEXT("Build_FoundryMk1_C"), ESearchCase::CaseSensitive)
			|| ClassName.Equals(TEXT("Build_OilRefinery_C"), ESearchCase::CaseSensitive)
			|| ClassName.Equals(TEXT("Build_Converter_C"), ESearchCase::CaseSensitive))
		{
			return 2;
		}
		if (ClassName.Equals(TEXT("Build_ManufacturerMk1_C"), ESearchCase::CaseSensitive)
			|| ClassName.Equals(TEXT("Build_Blender_C"), ESearchCase::CaseSensitive)
			|| ClassName.Equals(TEXT("Build_HadronCollider_C"), ESearchCase::CaseSensitive)
			|| ClassName.Equals(TEXT("Build_QuantumEncoder_C"), ESearchCase::CaseSensitive))
		{
			return 4;
		}
		if (ClassName.Equals(TEXT("Build_Packager_C"), ESearchCase::CaseSensitive))
		{
			return 0;
		}
		return INDEX_NONE;
	}

	FSFPMachineRuntimeConfig ReadMachineRuntimeConfig(const UObject* MachineCDO, UClass* MachineClass)
	{
		FSFPMachineRuntimeConfig Config;
		if (!IsValid(MachineCDO) || !IsValid(MachineClass))
		{
			return Config;
		}

		Config.bCanChangePotential = ReflectedBoolValue(MachineCDO, MachineClass, TEXT("mCanChangePotential"), false);
		Config.MinPotential = FMath::Max(0.001, ReflectedNumberValue(MachineCDO, MachineClass, TEXT("mMinPotential"), 1.0));
		const double ReflectedMaxPotential = FMath::Max(
			ReflectedNumberValue(MachineCDO, MachineClass, TEXT("mMaxPotential"), 1.0),
			ReflectedNumberValue(MachineCDO, MachineClass, TEXT("mMaxDefaultPotential"), 1.0));
		Config.bRuntimeMaxPotentialKnown = FMath::IsFinite(ReflectedMaxPotential) && ReflectedMaxPotential > 1.0 + KINDA_SMALL_NUMBER;
		Config.MaxPotential = Config.bRuntimeMaxPotentialKnown ? ReflectedMaxPotential : 1.0;

		Config.BaseProductionBoost = FMath::Max(0.001, ReflectedNumberValue(
			MachineCDO, MachineClass, TEXT("mBaseProductionBoost"), 1.0));
		Config.ProductionBoostPerSloop = FMath::Max(0.0, ReflectedNumberValue(
			MachineCDO, MachineClass, TEXT("mProductionShardBoostMultiplier"), 0.0));
		Config.ProductionBoostPowerExponent = FMath::Max(0.001, ReflectedNumberValue(
			MachineCDO, MachineClass, TEXT("mProductionBoostPowerConsumptionExponent"), 1.0));
		Config.bCanChangeProductionBoost = ReflectedBoolValue(
			MachineCDO, MachineClass, TEXT("mCanChangeProductionBoost"), false)
			|| Config.ProductionBoostPerSloop > KINDA_SMALL_NUMBER;

		const double ReflectedMaxBoost = FMath::Max(
			ReflectedNumberValue(MachineCDO, MachineClass, TEXT("mMaxProductionBoost"), Config.BaseProductionBoost),
			ReflectedNumberValue(MachineCDO, MachineClass, TEXT("mMaxDefaultProductionBoost"), Config.BaseProductionBoost));
		Config.bRuntimeMaxProductionBoostKnown = FMath::IsFinite(ReflectedMaxBoost)
			&& ReflectedMaxBoost > Config.BaseProductionBoost + KINDA_SMALL_NUMBER;
		Config.MaxProductionBoost = Config.bRuntimeMaxProductionBoostKnown
			? ReflectedMaxBoost
			: Config.BaseProductionBoost;
		if (Config.bRuntimeMaxProductionBoostKnown && Config.ProductionBoostPerSloop > KINDA_SMALL_NUMBER)
		{
			Config.MaxSomersloops = FMath::Max(0, FMath::RoundToInt(
				(Config.MaxProductionBoost - Config.BaseProductionBoost) / Config.ProductionBoostPerSloop));
		}

		const int32 VanillaSlotFallback = KnownVanillaSomersloopSlots(MachineClass);
		if (VanillaSlotFallback == 0)
		{
			// Explicitly non-amplifiable vanilla building (e.g. miners / Packager).
			Config.bCanChangeProductionBoost = false;
			Config.bRuntimeMaxProductionBoostKnown = true;
			Config.MaxSomersloops = 0;
			Config.MaxProductionBoost = Config.BaseProductionBoost;
		}
		else if (VanillaSlotFallback > 0 && !Config.bRuntimeMaxProductionBoostKnown)
		{
			// Some CDOs expose the production-amplification capability but not a
			// meaningful max boost. Use the documented vanilla slot count only as
			// a fallback; modded/S+ runtime values remain authoritative.
			if (Config.ProductionBoostPerSloop <= KINDA_SMALL_NUMBER)
			{
				Config.ProductionBoostPerSloop = 1.0 / static_cast<double>(VanillaSlotFallback);
			}
			Config.bCanChangeProductionBoost = true;
			Config.bRuntimeMaxProductionBoostKnown = true;
			Config.MaxSomersloops = VanillaSlotFallback;
			Config.MaxProductionBoost = Config.BaseProductionBoost
				+ static_cast<double>(VanillaSlotFallback) * Config.ProductionBoostPerSloop;
		}
		return Config;
	}

	void ApplyGeneratorClockRuntimeConfig(
		FSFPPowerGeneratorOption& Generator,
		const UObject* GeneratorCDO,
		UClass* GeneratorClass)
	{
		if (!IsValid(GeneratorCDO) || !IsValid(GeneratorClass))
		{
			return;
		}

		Generator.bCanChangePotential = ReflectedBoolValue(
			GeneratorCDO, GeneratorClass, TEXT("mCanChangePotential"), false);
		Generator.MinPotential = FMath::Max(0.01, ReflectedNumberValue(
			GeneratorCDO, GeneratorClass, TEXT("mMinPotential"), 0.01));
		const double ReflectedMaxPotential = FMath::Max(
			ReflectedNumberValue(GeneratorCDO, GeneratorClass, TEXT("mMaxPotential"), 1.0),
			ReflectedNumberValue(GeneratorCDO, GeneratorClass, TEXT("mMaxDefaultPotential"), 1.0));
		Generator.bRuntimeMaxPotentialKnown = FMath::IsFinite(ReflectedMaxPotential)
			&& ReflectedMaxPotential > 1.0 + KINDA_SMALL_NUMBER;
		Generator.MaxPotential = Generator.bRuntimeMaxPotentialKnown
			? ReflectedMaxPotential
			: (Generator.bCanChangePotential ? 2.5 : 1.0);
	}

	double GeneratorMinClockPercent(const FSFPPowerGeneratorOption& Generator)
	{
		return Generator.bCanChangePotential
			? FMath::Max(1.0, Generator.MinPotential * 100.0)
			: 100.0;
	}

	double GeneratorMaxClockPercent(const FSFPPowerGeneratorOption& Generator)
	{
		if (!Generator.bCanChangePotential)
		{
			return 100.0;
		}
		return FMath::Max(100.0, Generator.MaxPotential * 100.0);
	}

	double ModularHeaterByproductRatePerMinute(
		const void* HeaterCDO,
		UStruct* HeaterClass,
		const FString& FuelClassPath,
		const FString& FuelDisplayName)
	{
		// Newer Refined Power builds can expose a direct by-product rate. Prefer
		// that runtime value when present; fluid inventories store large raw
		// values in litres, while the planner consistently displays m3/min.
		static const FName RateProperties[] = {
			TEXT("mCo2ProductionRate"),
			TEXT("mByproductRate"),
			TEXT("mOutputRatePerMinute"),
			TEXT("mOutputProductionRate"),
		};
		for (const FName PropertyName : RateProperties)
		{
			double Rate = ReflectedNumberValue(
				HeaterCDO, HeaterClass, PropertyName, -1.0);
			if (Rate > KINDA_SMALL_NUMBER)
			{
				if (Rate >= 1000.0)
				{
					Rate /= 1000.0;
				}
				return Rate;
			}
		}

		// Legacy heater classes only expose an amount generated per factory tick,
		// which cannot be converted safely without assuming a frame/tick rate.
		// Use the published per-minute operating points as guarded fallbacks.
		const FString FuelKey = (FuelClassPath + TEXT(" ") + FuelDisplayName).ToLower();
		if (FuelKey.Contains(TEXT("ionized")) || FuelKey.Contains(TEXT("ionised")))
		{
			return 75.0;
		}
		if (FuelKey.Contains(TEXT("molten")) && FuelKey.Contains(TEXT("salt")))
		{
			return 60.0;
		}
		if (FuelKey.Contains(TEXT("turbofuel"))
			|| FuelKey.Contains(TEXT("wood"))
			|| FuelKey.Contains(TEXT("mycelia"))
			|| FuelKey.Contains(TEXT("packagedbiofuel"))
			|| FuelKey.Contains(TEXT("packaged_biofuel")))
		{
			return 45.0;
		}
		return 30.0;
	}

	void ReflectedClassArray(
		const void* Container,
		UStruct* OwnerType,
		const FName PropertyName,
		TArray<UClass*>& OutClasses)
	{
		OutClasses.Reset();
		if (Container == nullptr || !IsValid(OwnerType))
		{
			return;
		}
		const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(OwnerType->FindPropertyByName(PropertyName));
		const FObjectPropertyBase* InnerProperty = ArrayProperty != nullptr
			? CastField<FObjectPropertyBase>(ArrayProperty->Inner)
			: nullptr;
		if (ArrayProperty == nullptr || InnerProperty == nullptr)
		{
			return;
		}
		const void* ArrayValue = ArrayProperty->ContainerPtrToValuePtr<void>(Container);
		FScriptArrayHelper Helper(ArrayProperty, ArrayValue);
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			if (UClass* Value = Cast<UClass>(InnerProperty->GetObjectPropertyValue(Helper.GetRawPtr(Index))))
			{
				OutClasses.Add(Value);
			}
		}
	}

	double OptionalBuildablePower(UClass* BuildableClass)
	{
		const AFGBuildableFactory* FactoryCDO = IsValid(BuildableClass)
			? Cast<AFGBuildableFactory>(BuildableClass->GetDefaultObject())
			: nullptr;
		return IsValid(FactoryCDO)
			? FMath::Max(0.0, static_cast<double>(FactoryCDO->GetDefaultProducingPowerConsumption()))
			: 0.0;
	}

	double ModularMinerTierMultiplier(const int32 Tier)
	{
		if (Tier == 2)
		{
			return 2.0;
		}
		if (Tier == 3)
		{
			return 4.0;
		}
		return Tier > 3 ? 8.0 : 1.0;
	}

}

struct FSFPPlannerSolver::FSolveContext
{
	FSFPPlanResult Result;
	TSet<UClass*> RecursionStack;
	TSet<FString> AlternativeWarnings;
    TSet<FString> InputReachablePaths;
    // Shortest production distance from any user-supplied input. 0 means the supplied item itself.
    TMap<FString, int32> InputDistanceByPath;
	TMap<UClass*, int32> ProviderNodeByItem;
	TMap<UClass*, int32> RecipeIndexByItem;
	TMap<UClass*, int32> CycleNodeByItem;
	TMap<FString, int32> ByproductNodeByKey;
	TMap<FString, int32> DirectResourceNodeByKey;
	TMap<FString, FString> RecipeOverrides;
	TMap<FString, FSFPMachinePlanSettings> MachineSettings;
	TMap<FString, FSFPResourceSourceMix> ResourceSourceMixes;
	TMap<FString, double> MixedSourceUsedRates;
	TMap<FString, int32> MixedProviderNodeByRecipe;
	TMap<FString, int32> MixedExternalNodeByItem;
	bool bDispatchingMixedSource = false;
	int32 ExpansionCount = 0;
	bool bOnlyAvailableRecipes = false;
};

bool FSFPPlannerSolver::RefreshIfRecipeAvailabilityChanged(UWorld* World, FString& OutError)
{
	OutError.Reset();
	if (!IsValid(World))
	{
		OutError = TEXT("Kein gültiger Spielstand geladen");
		return false;
	}

	AFGRecipeManager* RecipeManager = AFGRecipeManager::Get(World);
	if (!IsValid(RecipeManager))
	{
		OutError = TEXT("Der Rezeptmanager ist noch nicht bereit");
		return false;
	}

	TSet<FString> CurrentKnownRecipeClassPaths;
	TSet<FString> CurrentAvailableRecipeClassPaths;
	for (const TSubclassOf<UFGRecipe>& RecipeClass : RecipeManager->GetAllRecipes())
	{
		UClass* RawRecipeClass = RecipeClass.Get();
		if (!IsValid(RawRecipeClass) || !RawRecipeClass->IsChildOf(UFGRecipe::StaticClass()))
		{
			continue;
		}

		const FString RecipeClassPath = RawRecipeClass->GetPathName();
		CurrentKnownRecipeClassPaths.Add(RecipeClassPath);
		if (RecipeManager->IsRecipeAvailable(RecipeClass))
		{
			CurrentAvailableRecipeClassPaths.Add(RecipeClassPath);
		}
	}

	auto SetsMatch = [](const TSet<FString>& Left, const TSet<FString>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (const FString& Value : Left)
		{
			if (!Right.Contains(Value))
			{
				return false;
			}
		}
		return true;
	};

	if (SetsMatch(KnownRecipeClassPaths, CurrentKnownRecipeClassPaths)
		&& SetsMatch(AvailableRecipeClassPaths, CurrentAvailableRecipeClassPaths))
	{
		return true;
	}

	UE_LOG(
		LogSFPFactoryPlanner,
		Display,
		TEXT("Runtime recipes or unlocks changed (%d/%d available -> %d/%d); rebuilding planner catalog"),
		AvailableRecipeClassPaths.Num(),
		KnownRecipeClassPaths.Num(),
		CurrentAvailableRecipeClassPaths.Num(),
		CurrentKnownRecipeClassPaths.Num());
	return Initialize(World, OutError);
}

bool FSFPPlannerSolver::Initialize(UWorld* World, FString& OutError)
{
	Recipes.Reset();
	RecipesByProduct.Reset();
	Products.Reset();
	KnownRecipeClassPaths.Reset();
	AvailableRecipeClassPaths.Reset();
	TransportTiers.Reset();
	PowerGenerators.Reset();
	ConstructionCostsByMachinePath.Reset();
	SplitterClassPath.Reset();
	SplitterDisplayName.Reset();
	MergerClassPath.Reset();
	MergerDisplayName.Reset();
	PipeJunctionClassPath.Reset();
	PipeJunctionDisplayName.Reset();
	MinerDiagnostics.Reset();

	if (!IsValid(World))
	{
		OutError = TEXT("Kein gültiger Spielstand geladen");
		return false;
	}

	AFGRecipeManager* RecipeManager = AFGRecipeManager::Get(World);
	if (!IsValid(RecipeManager))
	{
		OutError = TEXT("Der Rezeptmanager ist noch nicht bereit");
		return false;
	}

	TArray<TSubclassOf<UFGRecipe>> AllRecipes = RecipeManager->GetAllRecipes();
	TSet<FString> AlternateRecipePaths;
	if (AFGSchematicManager* SchematicManager = AFGSchematicManager::Get(World); IsValid(SchematicManager))
	{
		TArray<TSubclassOf<UFGSchematic>> Schematics;
		SchematicManager->GetAllSchematics(Schematics);
		for (const auto& Schematic : Schematics)
		{
			if (!IsValid(Schematic.Get())) continue;
			const UObject* CDO = Schematic->GetDefaultObject();
			const FProperty* Type = Schematic->FindPropertyByName(TEXT("mType"));
			if (!Type) continue;
			FString TypeName;
			Type->ExportTextItem_Direct(TypeName, Type->ContainerPtrToValuePtr<void>(CDO), nullptr, const_cast<UObject*>(CDO), PPF_None);
			if (!TypeName.Contains(TEXT("Alternate"))) continue;
			const FArrayProperty* Unlocks = CastField<FArrayProperty>(Schematic->FindPropertyByName(TEXT("mUnlocks")));
			const FObjectPropertyBase* UnlockItem = Unlocks ? CastField<FObjectPropertyBase>(Unlocks->Inner) : nullptr;
			if (!UnlockItem) continue;
			FScriptArrayHelper Array(Unlocks, Unlocks->ContainerPtrToValuePtr<void>(CDO));
			for (int32 I = 0; I < Array.Num(); ++I)
			{
				UObject* Unlock = UnlockItem->GetObjectPropertyValue(Array.GetRawPtr(I));
				if (!IsValid(Unlock)) continue;
				TArray<UClass*> UnlockedRecipes;
				ReflectedClassArray(Unlock, Unlock->GetClass(), TEXT("mRecipes"), UnlockedRecipes);
				for (UClass* RecipeClass : UnlockedRecipes)
					if (IsValid(RecipeClass)) AlternateRecipePaths.Add(RecipeClass->GetPathName());
			}
		}
	}

	AllRecipes.Sort([](const TSubclassOf<UFGRecipe>& Left, const TSubclassOf<UFGRecipe>& Right)
	{
		const UClass* LeftClass = Left.Get();
		const UClass* RightClass = Right.Get();
		return (LeftClass ? LeftClass->GetPathName() : FString())
			< (RightClass ? RightClass->GetPathName() : FString());
	});

	TMap<UClass*, TSharedPtr<FSFPProductOption>> ProductsByClass;
	TMap<UClass*, bool> BuildableAvailability;
	for (const TSubclassOf<UFGRecipe>& ConstructionRecipe : AllRecipes)
	{
		UClass* BuildableClass = SafeBuildableClassFromRecipe(ConstructionRecipe);
		if (!IsValid(BuildableClass))
		{
			continue;
		}
		bool& bAvailable = BuildableAvailability.FindOrAdd(BuildableClass);
		bAvailable |= RecipeManager->IsRecipeAvailable(ConstructionRecipe);
	}

	for (const TSubclassOf<UFGRecipe>& RecipeClass : AllRecipes)
	{
		UClass* RawRecipeClass = RecipeClass.Get();
		if (!IsValid(RawRecipeClass) || !RawRecipeClass->IsChildOf(UFGRecipe::StaticClass()))
		{
			continue;
		}
		const FString RecipeClassPath = RawRecipeClass->GetPathName();
		const bool bRecipeAvailable = RecipeManager->IsRecipeAvailable(RecipeClass);
		KnownRecipeClassPaths.Add(RecipeClassPath);
		if (bRecipeAvailable)
		{
			AvailableRecipeClassPaths.Add(RecipeClassPath);
		}

		const UFGRecipe* RecipeCDO = Cast<UFGRecipe>(RawRecipeClass->GetDefaultObject());
		if (!IsValid(RecipeCDO) || RecipeCDO->GetManufacturingDuration() <= 0.0f || RecipeCDO->GetProducts().IsEmpty())
		{
			continue;
		}

		UClass* MachineClass = nullptr;
		const AFGBuildableFactory* MachineCDO = nullptr;
		int32 BestMachineAvailabilityRank = MIN_int32;
		for (const TSubclassOf<UObject>& Producer : UFGRecipe::GetProducedIn(RecipeClass))
		{
			UClass* CandidateClass = Producer.Get();
			if (IsValid(CandidateClass) && CandidateClass->IsChildOf(AFGBuildableFactory::StaticClass()))
			{
				const AFGBuildableFactory* CandidateCDO = Cast<AFGBuildableFactory>(CandidateClass->GetDefaultObject());
				if (IsValid(CandidateCDO))
				{
					const bool* bKnownAvailable = BuildableAvailability.Find(CandidateClass);
					const int32 AvailabilityRank = bKnownAvailable == nullptr
						? 1
						: (*bKnownAvailable ? 2 : 0);
					if (AvailabilityRank > BestMachineAvailabilityRank)
					{
						MachineClass = CandidateClass;
						MachineCDO = CandidateCDO;
						BestMachineAvailabilityRank = AvailabilityRank;
					}
				}
			}
		}

		if (!IsValid(MachineCDO))
		{
			continue;
		}

		FSFPPlannerRecipe Recipe;
		Recipe.RecipeClass = RecipeClass;
		Recipe.ClassPath = RecipeClassPath;
		Recipe.bAlternateRecipe = AlternateRecipePaths.Contains(RecipeClassPath)
			|| RecipeClassPath.Contains(TEXT("Alternate")) || RecipeClassPath.Contains(TEXT("Alternative"));
		Recipe.DisplayName = RecipeCDO->GetDisplayName().ToString();
		if (Recipe.DisplayName.IsEmpty())
		{
			Recipe.DisplayName = UFGRecipe::GetRecipeName(RecipeClass).ToString();
		}
		if (Recipe.DisplayName.IsEmpty())
		{
			Recipe.DisplayName = RawRecipeClass->GetName();
		}
		Recipe.SourceMount = SolverSourceMountFromPath(Recipe.ClassPath);
		const bool* bMachineBuildable = BuildableAvailability.Find(MachineClass);
		Recipe.bAvailable = bRecipeAvailable
			&& (bMachineBuildable == nullptr || *bMachineBuildable);
		Recipe.DurationSeconds = RecipeCDO->GetManufacturingDuration();
		Recipe.MachineClass = MachineClass;
		Recipe.MachineName = SolverBuildableDisplayName(MachineCDO, MachineClass);
		Recipe.BasePowerMW = MachineCDO->GetDefaultProducingPowerConsumption();
		Recipe.PowerExponent = FMath::Max(0.001, ReflectedNumberValue(
			MachineCDO, MachineClass, TEXT("mPowerConsumptionExponent"), 1.0));
		Recipe.MachineConfig = ReadMachineRuntimeConfig(MachineCDO, MachineClass);
		Recipe.bFuelPowered = IsOptionalBurnerManufacturerClass(MachineClass);
		if (Recipe.bFuelPowered)
		{
			TArray<TSoftClassPtr<UFGItemDescriptor>> BurnerFuelClasses;
			if (ReadOptionalBurnerFuelClasses(const_cast<AFGBuildableFactory*>(MachineCDO), BurnerFuelClasses))
			{
				for (const TSoftClassPtr<UFGItemDescriptor>& SoftFuelClass : BurnerFuelClasses)
				{
					UClass* FuelClass = SoftFuelClass.LoadSynchronous();
					if (!IsValid(FuelClass) || !FuelClass->IsChildOf(UFGItemDescriptor::StaticClass())) continue;
					FSFPPlannerFuelOption Fuel;
					Fuel.ItemClass = TSubclassOf<UFGItemDescriptor>(FuelClass);
					Fuel.ClassPath = FuelClass->GetPathName();
					Fuel.DisplayName = ItemDisplayName(FuelClass);
					Fuel.Form = SolverResourceFormToString(UFGItemDescriptor::GetForm(Fuel.ItemClass));
					Fuel.SourceMount = SolverSourceMountFromPath(Fuel.ClassPath);
					Fuel.EnergyValueMJ = FMath::Max(0.0, static_cast<double>(UFGItemDescriptor::GetEnergyValue(Fuel.ItemClass)));
					if (Fuel.EnergyValueMJ > KINDA_SMALL_NUMBER) Recipe.FuelOptions.Add(MoveTemp(Fuel));
				}
			}
			Recipe.FuelOptions.Sort([](const FSFPPlannerFuelOption& A, const FSFPPlannerFuelOption& B)
			{
				return A.ClassPath < B.ClassPath;
			});
		}
		if (!ApplyVariableRecipePower(Recipe)) continue;

		const double DurationMinutes = Recipe.DurationSeconds / 60.0;
		for (const FItemAmount& Ingredient : RecipeCDO->GetIngredients())
		{
			UClass* ItemClass = Ingredient.ItemClass.Get();
			if (!IsValid(ItemClass) || !ItemClass->IsChildOf(UFGItemDescriptor::StaticClass()))
			{
				continue;
			}

			FSFPPlannerItemRate Item;
			Item.ItemClass = TSubclassOf<UFGItemDescriptor>(ItemClass);
			Item.ClassPath = ItemClass->GetPathName();
			Item.DisplayName = ItemDisplayName(ItemClass);
			const double Amount = NormalizeAmount(Ingredient, Item.Form);
			Item.RatePerMinute = Amount / DurationMinutes;
			FSFPPlannerItemRate* ExistingIngredient = Recipe.Ingredients.FindByPredicate(
				[ItemClass](const FSFPPlannerItemRate& Existing)
				{
					return Existing.ItemClass.Get() == ItemClass;
				});
			if (ExistingIngredient != nullptr)
			{
				ExistingIngredient->RatePerMinute += Item.RatePerMinute;
			}
			else
			{
				Recipe.Ingredients.Add(MoveTemp(Item));
			}
		}

		for (const FItemAmount& Product : RecipeCDO->GetProducts())
		{
			UClass* ItemClass = Product.ItemClass.Get();
			if (!IsValid(ItemClass) || !ItemClass->IsChildOf(UFGItemDescriptor::StaticClass()))
			{
				continue;
			}

			FSFPPlannerItemRate Item;
			Item.ItemClass = TSubclassOf<UFGItemDescriptor>(ItemClass);
			Item.ClassPath = ItemClass->GetPathName();
			Item.DisplayName = ItemDisplayName(ItemClass);
			const double Amount = NormalizeAmount(Product, Item.Form);
			Item.RatePerMinute = Amount / DurationMinutes;
			FSFPPlannerItemRate* ExistingProduct = Recipe.Products.FindByPredicate(
				[ItemClass](const FSFPPlannerItemRate& Existing)
				{
					return Existing.ItemClass.Get() == ItemClass;
				});
			if (ExistingProduct != nullptr)
			{
				ExistingProduct->RatePerMinute += Item.RatePerMinute;
			}
			else
			{
				Recipe.Products.Add(MoveTemp(Item));
			}
		}

		for (const FSFPPlannerItemRate& Product : Recipe.Products)
		{
			UClass* ItemClass = Product.ItemClass.Get();
			TSharedPtr<FSFPProductOption>& Option = ProductsByClass.FindOrAdd(ItemClass);
			if (!Option.IsValid())
			{
				Option = MakeShared<FSFPProductOption>();
				Option->ItemClass = Product.ItemClass;
				Option->ClassPath = Product.ClassPath;
				Option->DisplayName = Product.DisplayName;
				Option->Form = Product.Form;
				Option->SourceMount = SolverSourceMountFromPath(Product.ClassPath);
			}
			++Option->RecipeCount;
			Option->bHasAvailableRecipe |= Recipe.bAvailable;
		}

		if (Recipe.Products.IsEmpty())
		{
			continue;
		}

		const int32 RecipeIndex = Recipes.Add(MoveTemp(Recipe));
		for (const FSFPPlannerItemRate& Product : Recipes[RecipeIndex].Products)
		{
			RecipesByProduct.FindOrAdd(Product.ItemClass.Get()).Add(RecipeIndex);
		}
	}


	// MkPlus manufacturers inherit the vanilla producer class but are not listed in
	// Recipe::ProducedIn. Preserve each original recipe and add machine-specific variants.
	// No hard dependency on MkPlus and no inferred Mk-number multipliers.
	const int32 OriginalRecipeCount = Recipes.Num();
	TSet<FString> AddedMachineVariants;
	for (const TSubclassOf<UFGRecipe>& ConstructionRecipe : AllRecipes)
	{
		UClass* MachineClass = SafeBuildableClassFromRecipe(ConstructionRecipe);
		if (!IsValid(MachineClass) || !MachineClass->GetPathName().StartsWith(TEXT("/MkPlus/"))
			|| !MachineClass->IsChildOf(AFGBuildableFactory::StaticClass())) continue;
		const AFGBuildableFactory* MachineCDO = Cast<AFGBuildableFactory>(MachineClass->GetDefaultObject());
		if (!IsValid(MachineCDO)) continue;
		const double Speed = ReflectedNumberValue(MachineCDO, MachineClass, TEXT("mManufacturingSpeed"), 0.0);
		if (!FMath::IsFinite(Speed) || Speed <= 0.0) continue;
		double Power = MachineCDO->GetDefaultProducingPowerConsumption();
		const bool bVariableMachine = Cast<AFGBuildableManufacturerVariablePower>(MachineCDO) != nullptr;
		if (Power <= 0.0 && MachineCDO->RunsOnPower() && !bVariableMachine) continue;
		if (!FMath::IsFinite(Power) || Power < 0.0) continue;
		for (int32 BaseIndex = 0; BaseIndex < OriginalRecipeCount; ++BaseIndex)
		{
			const FSFPPlannerRecipe BaseRecipe = Recipes[BaseIndex];
			if (!BaseRecipe.RecipeClass.Get() || BaseRecipe.bDirectResourceExtraction) continue;
			bool bCompatible = false;
			for (const TSubclassOf<UObject>& Producer : UFGRecipe::GetProducedIn(BaseRecipe.RecipeClass))
			{
				UClass* ProducerClass = Producer.Get();
				// Match a concrete recipe producer, never just the generic factory base.
				if (IsValid(ProducerClass) && ProducerClass->GetPathName().StartsWith(TEXT("/Game/"))
					&& ProducerClass->IsChildOf(AFGBuildableFactory::StaticClass())
					&& MachineClass->IsChildOf(ProducerClass)) { bCompatible = true; break; }
			}
			if (!bCompatible || MachineClass == BaseRecipe.MachineClass) continue;
			const FString VariantPath = BaseRecipe.ClassPath + TEXT("|Machine=") + MachineClass->GetPathName();
			if (AddedMachineVariants.Contains(VariantPath)) continue;
			AddedMachineVariants.Add(VariantPath);
			FSFPPlannerRecipe Variant = BaseRecipe;
			Variant.ClassPath = VariantPath;
			Variant.MachineClass = MachineClass;
			Variant.MachineName = SolverBuildableDisplayName(MachineCDO, MachineClass);
			Variant.BasePowerMW = Power;
			Variant.PowerExponent = FMath::Max(0.001, ReflectedNumberValue(
				MachineCDO, MachineClass, TEXT("mPowerConsumptionExponent"), 1.0));
			Variant.MachineConfig = ReadMachineRuntimeConfig(MachineCDO, MachineClass);
			Variant.bFuelPowered = IsOptionalBurnerManufacturerClass(MachineClass);
			Variant.FuelOptions.Reset();
			if (Variant.bFuelPowered)
			{
				TArray<TSoftClassPtr<UFGItemDescriptor>> BurnerFuelClasses;
				if (ReadOptionalBurnerFuelClasses(const_cast<AFGBuildableFactory*>(MachineCDO), BurnerFuelClasses))
				{
					for (const TSoftClassPtr<UFGItemDescriptor>& SoftFuelClass : BurnerFuelClasses)
					{
						UClass* FuelClass = SoftFuelClass.LoadSynchronous();
						if (!IsValid(FuelClass) || !FuelClass->IsChildOf(UFGItemDescriptor::StaticClass())) continue;
						FSFPPlannerFuelOption Fuel;
						Fuel.ItemClass = TSubclassOf<UFGItemDescriptor>(FuelClass);
						Fuel.ClassPath = FuelClass->GetPathName();
						Fuel.DisplayName = ItemDisplayName(FuelClass);
						Fuel.Form = SolverResourceFormToString(UFGItemDescriptor::GetForm(Fuel.ItemClass));
						Fuel.SourceMount = SolverSourceMountFromPath(Fuel.ClassPath);
						Fuel.EnergyValueMJ = FMath::Max(0.0, static_cast<double>(UFGItemDescriptor::GetEnergyValue(Fuel.ItemClass)));
						if (Fuel.EnergyValueMJ > KINDA_SMALL_NUMBER) Variant.FuelOptions.Add(MoveTemp(Fuel));
					}
				}
				Variant.FuelOptions.Sort([](const FSFPPlannerFuelOption& A, const FSFPPlannerFuelOption& B)
				{
					return A.ClassPath < B.ClassPath;
				});
			}
			Variant.bAvailable = RecipeManager->IsRecipeAvailable(BaseRecipe.RecipeClass)
				&& BuildableAvailability.FindRef(MachineClass);
			Variant.DurationSeconds = BaseRecipe.DurationSeconds / Speed;
			for (FSFPPlannerItemRate& Item : Variant.Ingredients) Item.RatePerMinute *= Speed;
			for (FSFPPlannerItemRate& Item : Variant.Products) Item.RatePerMinute *= Speed;
			Variant.ConfigurationDetail = FString::Printf(
				TEXT("Produktionsfaktor: %s | Leistung bei 100%%: %s MW"),
				*FSFPNumberFormatting::Decimal(Speed), *FSFPNumberFormatting::Decimal(Power));
			if (!ApplyVariableRecipePower(Variant)) continue;
			const int32 Index = Recipes.Add(MoveTemp(Variant));
			for (const FSFPPlannerItemRate& Product : Recipes[Index].Products)
			{
				RecipesByProduct.FindOrAdd(Product.ItemClass.Get()).Add(Index);
				if (TSharedPtr<FSFPProductOption>* Option = ProductsByClass.Find(Product.ItemClass.Get()))
				{
					++(*Option)->RecipeCount;
					(*Option)->bHasAvailableRecipe |= Recipes[Index].bAvailable;
				}
			}
		}
	}

	// Build modular raw routes first. Standard-miner registration can then avoid
	// mixing Miner Mk.1-3 into a resource selector owned by the modular system.
	BuildOptionalModularMinerCatalog(AllRecipes, RecipeManager, ProductsByClass);
	BuildStandardMinerCatalog(AllRecipes, RecipeManager, ProductsByClass);
	BuildFluidExtractorCatalog(AllRecipes, RecipeManager, ProductsByClass);

	ProductsByClass.GenerateValueArray(Products);
	Products.Sort([](const TSharedPtr<FSFPProductOption>& Left, const TSharedPtr<FSFPProductOption>& Right)
	{
		return Left.IsValid() && Right.IsValid()
			? Left->DisplayName.Compare(Right->DisplayName, ESearchCase::IgnoreCase) < 0
			: Left.IsValid();
	});

	BuildTransportCatalog(AllRecipes, RecipeManager);
	BuildConstructionCostCatalog(AllRecipes);
	BuildPowerGeneratorCatalog(AllRecipes, RecipeManager);
	BuildAlienPowerAugmenterCatalog(AllRecipes, RecipeManager);
	if (Products.IsEmpty())
	{
		OutError = TEXT("Keine maschinenproduzierten Gegenstände gefunden");
		return false;
	}

	return true;
}

void FSFPPlannerSolver::BuildPowerGeneratorCatalog(
	const TArray<TSubclassOf<UFGRecipe>>& AllRecipes,
	AFGRecipeManager* RecipeManager)
{
	TMap<UClass*, TSharedPtr<FSFPPowerGeneratorOption>> GeneratorsByClass;
	for (const TSubclassOf<UFGRecipe>& RecipeClass : AllRecipes)
	{
		UClass* GeneratorClass = SafeBuildableClassFromRecipe(RecipeClass);
		if (!IsValid(GeneratorClass)
			|| !GeneratorClass->IsChildOf(AFGBuildableGeneratorFuel::StaticClass())
			|| GeneratorClass->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}

		const AFGBuildableGeneratorFuel* GeneratorCDO =
			Cast<AFGBuildableGeneratorFuel>(GeneratorClass->GetDefaultObject());
		if (!IsValid(GeneratorCDO))
		{
			continue;
		}

		TSharedPtr<FSFPPowerGeneratorOption>& Generator = GeneratorsByClass.FindOrAdd(GeneratorClass);
		if (!Generator.IsValid())
		{
			Generator = MakeShared<FSFPPowerGeneratorOption>();
			Generator->GeneratorClass = GeneratorClass;
			Generator->ClassPath = GeneratorClass->GetPathName();
			Generator->DisplayName = SolverBuildableDisplayName(GeneratorCDO, GeneratorClass);
			Generator->SourceMount = SolverSourceMountFromPath(Generator->ClassPath);
			Generator->PowerProductionMW = FMath::Max(
				0.0,
				static_cast<double>(GeneratorCDO->GetDefaultPowerProductionCapacity()));
			ApplyGeneratorClockRuntimeConfig(*Generator, GeneratorCDO, GeneratorClass);

			if (GeneratorCDO->GetRequiresSupplementalResource())
			{
				UClass* SupplementalClass = GeneratorCDO->GetSupplementalResourceClass().Get();
				if (IsValid(SupplementalClass)
					&& SupplementalClass->IsChildOf(UFGItemDescriptor::StaticClass()))
				{
					Generator->SupplementalItemClass = TSubclassOf<UFGItemDescriptor>(SupplementalClass);
					Generator->SupplementalItemClassPath = SupplementalClass->GetPathName();
					Generator->SupplementalDisplayName = ItemDisplayName(SupplementalClass);
					Generator->SupplementalForm = SolverResourceFormToString(
						UFGItemDescriptor::GetForm(Generator->SupplementalItemClass));
					Generator->SupplementalRatePerMinute = FMath::Max(
						0.0,
						static_cast<double>(GeneratorCDO->GetSupplementalConsumptionRateMaximum()) * 60.0);
					if (Generator->SupplementalRatePerMinute <= KINDA_SMALL_NUMBER)
					{
						const double SupplementalToPowerRatio = ReflectedNumberValue(
							GeneratorCDO,
							GeneratorClass,
							TEXT("mSupplementalToPowerRatio"),
							0.0);
						Generator->SupplementalRatePerMinute = FMath::Max(
							0.0,
							SupplementalToPowerRatio * Generator->PowerProductionMW * 60.0);
					}
				}
			}

			for (const TSoftClassPtr<UFGItemDescriptor>& SoftFuelClass : GeneratorCDO->GetDefaultFuelClasses())
			{
				UClass* FuelClass = SoftFuelClass.LoadSynchronous();
				if (!IsValid(FuelClass)
					|| !FuelClass->IsChildOf(UFGItemDescriptor::StaticClass()))
				{
					continue;
				}

				FSFPPowerFuelOption Fuel;
				Fuel.ItemClass = TSubclassOf<UFGItemDescriptor>(FuelClass);
				Fuel.ClassPath = FuelClass->GetPathName();
				Fuel.DisplayName = ItemDisplayName(FuelClass);
				Fuel.Form = SolverResourceFormToString(UFGItemDescriptor::GetForm(Fuel.ItemClass));
				Fuel.SourceMount = SolverSourceMountFromPath(Fuel.ClassPath);
				Fuel.EnergyValueMJ = FMath::Max(
					0.0,
					static_cast<double>(UFGItemDescriptor::GetEnergyValue(Fuel.ItemClass)));
				if (Fuel.EnergyValueMJ > KINDA_SMALL_NUMBER)
				{
					Fuel.ConsumptionRatePerGenerator = Generator->PowerProductionMW * 60.0
						/ Fuel.EnergyValueMJ;
					if (Fuel.Form == TEXT("liquid") || Fuel.Form == TEXT("gas"))
					{
						Fuel.ConsumptionRatePerGenerator /= 1000.0;
					}
				}

				const TArray<int32>* FuelRecipeIndices = RecipesByProduct.Find(FuelClass);
				Fuel.bAvailable = FuelRecipeIndices == nullptr || FuelRecipeIndices->IsEmpty();
				if (FuelRecipeIndices != nullptr)
				{
					for (const int32 RecipeIndex : *FuelRecipeIndices)
					{
						if (Recipes.IsValidIndex(RecipeIndex) && Recipes[RecipeIndex].bAvailable)
						{
							Fuel.bAvailable = true;
							break;
						}
					}
				}

				if (FuelClass->IsChildOf(UFGItemDescriptorNuclearFuel::StaticClass()))
				{
					const TSubclassOf<UFGItemDescriptorNuclearFuel> NuclearFuelClass(FuelClass);
					UClass* WasteClass = UFGItemDescriptorNuclearFuel::GetSpentFuelClass(
						NuclearFuelClass).Get();
					if (IsValid(WasteClass)
						&& WasteClass->IsChildOf(UFGItemDescriptor::StaticClass()))
					{
						Fuel.WasteItemClass = TSubclassOf<UFGItemDescriptor>(WasteClass);
						Fuel.WasteItemClassPath = WasteClass->GetPathName();
						Fuel.WasteDisplayName = ItemDisplayName(WasteClass);
						Fuel.WasteForm = SolverResourceFormToString(
							UFGItemDescriptor::GetForm(Fuel.WasteItemClass));
						Fuel.WasteAmountPerFuel = FMath::Max(
							0,
							UFGItemDescriptorNuclearFuel::GetAmountWasteCreated(NuclearFuelClass));
					}
				}
				if (Fuel.EnergyValueMJ > KINDA_SMALL_NUMBER)
				{
					Generator->Fuels.Add(MoveTemp(Fuel));
				}
			}

			Generator->Fuels.Sort([](const FSFPPowerFuelOption& Left, const FSFPPowerFuelOption& Right)
			{
				if (Left.bAvailable != Right.bAvailable)
				{
					return Left.bAvailable;
				}
				return Left.DisplayName.Compare(Right.DisplayName, ESearchCase::IgnoreCase) < 0;
			});
		}

		Generator->bAvailable |= IsValid(RecipeManager)
			&& RecipeManager->IsRecipeAvailable(RecipeClass);
	}

	GeneratorsByClass.GenerateValueArray(PowerGenerators);
	PowerGenerators.RemoveAll([](const TSharedPtr<FSFPPowerGeneratorOption>& Generator)
	{
		return !Generator.IsValid()
			|| Generator->PowerProductionMW <= KINDA_SMALL_NUMBER
			|| Generator->Fuels.IsEmpty();
	});
	BuildOptionalPowerGeneratorCatalog(AllRecipes, RecipeManager);
	PowerGenerators.Sort([](
		const TSharedPtr<FSFPPowerGeneratorOption>& Left,
		const TSharedPtr<FSFPPowerGeneratorOption>& Right)
	{
		if (!Left.IsValid() || !Right.IsValid())
		{
			return Left.IsValid();
		}
		if (Left->bAvailable != Right->bAvailable)
		{
			return Left->bAvailable;
		}
		const int32 NameOrder = Left->DisplayName.Compare(Right->DisplayName, ESearchCase::IgnoreCase);
		return NameOrder == 0 ? Left->ClassPath < Right->ClassPath : NameOrder < 0;
	});
}

void FSFPPlannerSolver::BuildAlienPowerAugmenterCatalog(
	const TArray<TSubclassOf<UFGRecipe>>& AllRecipes,
	AFGRecipeManager* RecipeManager)
{
	AlienPowerAugmenter = FSFPAlienPowerAugmenterOption();
	UClass* MatrixClass = nullptr;

	for (const TSubclassOf<UFGRecipe>& RecipeClass : AllRecipes)
	{
		UClass* RawRecipeClass = RecipeClass.Get();
		const UFGRecipe* RecipeCDO = IsValid(RawRecipeClass)
			? Cast<UFGRecipe>(RawRecipeClass->GetDefaultObject()) : nullptr;
		if (!IsValid(RecipeCDO))
		{
			continue;
		}

		for (const FItemAmount& Product : RecipeCDO->GetProducts())
		{
			UClass* ProductClass = Product.ItemClass.Get();
			if (IsValid(ProductClass)
				&& ProductClass->IsChildOf(UFGItemDescriptor::StaticClass())
				&& (ProductClass->GetName().Equals(TEXT("Desc_AlienPowerFuel_C"), ESearchCase::CaseSensitive)
					|| ProductClass->GetPathName().Contains(TEXT("/AlienPowerFuel/Desc_AlienPowerFuel"), ESearchCase::IgnoreCase)))
			{
				MatrixClass = ProductClass;
			}
		}

		UClass* DescriptorClass = SafeBuildingDescriptorClassFromRecipe(RecipeClass);
		UClass* BuildableClass = SafeBuildableClassFromRecipe(RecipeClass);
		if (!IsValid(DescriptorClass) || !IsValid(BuildableClass))
		{
			continue;
		}
		const FString DescriptorPath = DescriptorClass->GetPathName();
		const FString BuildablePath = BuildableClass->GetPathName();
		const bool bAlienAugmenter = DescriptorPath.Contains(TEXT("AlienPowerBuilding"), ESearchCase::IgnoreCase)
			|| BuildablePath.Contains(TEXT("AlienPower"), ESearchCase::IgnoreCase);
		if (!bAlienAugmenter)
		{
			continue;
		}

		const AFGBuildable* BuildableCDO = Cast<AFGBuildable>(BuildableClass->GetDefaultObject());
		AlienPowerAugmenter.BuildableClass = BuildableClass;
		AlienPowerAugmenter.ClassPath = BuildablePath;
		AlienPowerAugmenter.DisplayName = SolverBuildableDisplayName(BuildableCDO, BuildableClass);
		AlienPowerAugmenter.bAvailable |= IsValid(RecipeManager)
			&& RecipeManager->IsRecipeAvailable(RecipeClass);

		const UObject* CDO = BuildableClass->GetDefaultObject();
		// Prefer runtime/CDO values when the game or another mod exposes them.
		// Exact vanilla values are only fallbacks for the exact Alien Power Augmenter.
		AlienPowerAugmenter.BasePowerPerAugmenterMW = FirstPositiveReflectedNumber(
			CDO, BuildableClass,
			{TEXT("mBasePowerProduction"), TEXT("mPowerProduction"), TEXT("mPowerProductionCapacity"), TEXT("mBasePowerProductionCapacity")},
			500.0);
		AlienPowerAugmenter.PassiveBoostPerAugmenter = NormalizeFraction(
			FirstPositiveReflectedNumber(CDO, BuildableClass,
				{TEXT("mPowerMultiplier"), TEXT("mPowerBoost"), TEXT("mPassivePowerBoost"), TEXT("mGridBoost")}, 0.10),
			0.10);
		AlienPowerAugmenter.FueledBoostPerAugmenter = NormalizeFraction(
			FirstPositiveReflectedNumber(CDO, BuildableClass,
				{TEXT("mFuelPowerMultiplier"), TEXT("mFueledPowerBoost"), TEXT("mBoostedPowerMultiplier"), TEXT("mGridBoostWithFuel")}, 0.30),
			0.30);
		AlienPowerAugmenter.MatrixRatePerMinute = FirstPositiveReflectedNumber(
			CDO, BuildableClass,
			{TEXT("mFuelConsumptionRate"), TEXT("mFuelConsumptionPerMinute"), TEXT("mAlienPowerFuelConsumptionRate"), TEXT("mMatrixConsumptionRate")},
			5.0);
		if (AlienPowerAugmenter.MatrixRatePerMinute < 1.0)
		{
			// Runtime factory rates are often stored per second.
			AlienPowerAugmenter.MatrixRatePerMinute *= 60.0;
		}
	}

	if (IsValid(MatrixClass))
	{
		AlienPowerAugmenter.MatrixItemClass = TSubclassOf<UFGItemDescriptor>(MatrixClass);
		AlienPowerAugmenter.MatrixItemClassPath = MatrixClass->GetPathName();
		AlienPowerAugmenter.MatrixDisplayName = ItemDisplayName(MatrixClass);
		AlienPowerAugmenter.MatrixForm = SolverResourceFormToString(
			UFGItemDescriptor::GetForm(AlienPowerAugmenter.MatrixItemClass));
	}
}

void FSFPPlannerSolver::BuildOptionalPowerGeneratorCatalog(
	const TArray<TSubclassOf<UFGRecipe>>& AllRecipes,
	AFGRecipeManager* RecipeManager)
{
	TMap<UClass*, FOptionalPowerBuildableInfo> BuildablesByClass;
	for (const TSubclassOf<UFGRecipe>& RecipeClass : AllRecipes)
	{
		UClass* BuildableClass = SafeBuildableClassFromRecipe(RecipeClass);
		UClass* DescriptorClass = SafeBuildingDescriptorClassFromRecipe(RecipeClass);
		if (!IsValid(BuildableClass) || !IsValid(DescriptorClass)
			|| BuildableClass->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}

		FOptionalPowerBuildableInfo& Info = BuildablesByClass.FindOrAdd(BuildableClass);
		if (!IsValid(Info.BuildableClass))
		{
			const AFGBuildable* BuildableCDO = Cast<AFGBuildable>(BuildableClass->GetDefaultObject());
			Info.BuildableClass = BuildableClass;
			Info.ClassPath = BuildableClass->GetPathName();
			Info.DisplayName = SolverBuildableDisplayName(BuildableCDO, BuildableClass);
			Info.DescriptorClass = DescriptorClass;
			Info.DescriptorPath = DescriptorClass->GetPathName();
			Info.Description = UFGItemDescriptor::GetItemDescription(
				TSubclassOf<UFGItemDescriptor>(DescriptorClass)).ToString();
			Info.SearchText = FString::Printf(
				TEXT("%s|%s|%s"),
				*Info.ClassPath,
				*Info.DescriptorPath,
				*Info.DisplayName).ToLower();
		}
		Info.bAvailable |= IsValid(RecipeManager)
			&& RecipeManager->IsRecipeAvailable(RecipeClass);
	}

	auto IsItemAvailable = [this](UClass* ItemClass)
	{
		if (!IsValid(ItemClass))
		{
			return false;
		}
		const TArray<int32>* RecipeIndices = RecipesByProduct.Find(ItemClass);
		if (RecipeIndices == nullptr || RecipeIndices->IsEmpty())
		{
			return true;
		}
		for (const int32 RecipeIndex : *RecipeIndices)
		{
			if (Recipes.IsValidIndex(RecipeIndex) && Recipes[RecipeIndex].bAvailable)
			{
				return true;
			}
		}
		return false;
	};

	auto PopulateItem = [](UClass* ItemClass,
		TSubclassOf<UFGItemDescriptor>& OutClass,
		FString& OutPath,
		FString& OutName,
		FString& OutForm)
	{
		if (!IsValid(ItemClass) || !ItemClass->IsChildOf(UFGItemDescriptor::StaticClass()))
		{
			return;
		}
		OutClass = TSubclassOf<UFGItemDescriptor>(ItemClass);
		OutPath = ItemClass->GetPathName();
		OutName = ItemDisplayName(ItemClass);
		OutForm = SolverResourceFormToString(UFGItemDescriptor::GetForm(OutClass));
	};

	auto NormalizedFluidRate = [](const double ReflectedValue, const double FallbackValue)
	{
		if (!FMath::IsFinite(ReflectedValue) || ReflectedValue <= KINDA_SMALL_NUMBER)
		{
			return FallbackValue;
		}
		return ReflectedValue > 1000.0 ? ReflectedValue / 1000.0 : ReflectedValue;
	};

	auto IsAlreadyCataloged = [this](UClass* BuildableClass)
	{
		return PowerGenerators.ContainsByPredicate(
			[BuildableClass](const TSharedPtr<FSFPPowerGeneratorOption>& Existing)
			{
				return Existing.IsValid() && Existing->GeneratorClass == BuildableClass;
			});
	};

	TArray<const FOptionalPowerBuildableInfo*> ModularGenerators;
	TArray<const FOptionalPowerBuildableInfo*> Turbines;
	TArray<const FOptionalPowerBuildableInfo*> Heaters;
	TArray<const FOptionalPowerBuildableInfo*> Boilers;
	TArray<const FOptionalPowerBuildableInfo*> Platforms;
	TArray<const FOptionalPowerBuildableInfo*> Coolers;

	for (const TPair<UClass*, FOptionalPowerBuildableInfo>& Pair : BuildablesByClass)
	{
		const FOptionalPowerBuildableInfo& Info = Pair.Value;
		const bool bModularPower = Info.SearchText.Contains(TEXT("modularpower"))
			|| Info.SearchText.Contains(TEXT("mpbuildings"))
			|| Info.SearchText.Contains(TEXT("mpplatform"));
		if (bModularPower && Info.SearchText.Contains(TEXT("generator")))
		{
			ModularGenerators.Add(&Info);
			continue;
		}
		if (bModularPower && Info.SearchText.Contains(TEXT("turbine")))
		{
			Turbines.Add(&Info);
			continue;
		}
		if (bModularPower && Info.SearchText.Contains(TEXT("heater")))
		{
			Heaters.Add(&Info);
			continue;
		}
		if (bModularPower && Info.SearchText.Contains(TEXT("boiler"))
			&& !Info.SearchText.Contains(TEXT("platform")))
		{
			Boilers.Add(&Info);
			continue;
		}
		if (bModularPower && Info.SearchText.Contains(TEXT("platform")))
		{
			Platforms.Add(&Info);
			continue;
		}
		if (bModularPower && (Info.SearchText.Contains(TEXT("cooler"))
			|| Info.SearchText.Contains(TEXT("chimney"))))
		{
			Coolers.Add(&Info);
			continue;
		}

		const bool bFuelGenerator = Info.BuildableClass->IsChildOf(
			AFGBuildableGeneratorFuel::StaticClass());
		const bool bGeneratorBase = Info.BuildableClass->IsChildOf(
			AFGBuildableGenerator::StaticClass());
		const bool bRenewablePath = Info.SearchText.Contains(TEXT("solarpanel"))
			|| Info.SearchText.Contains(TEXT("solar_panel"))
			|| Info.SearchText.Contains(TEXT("windturbine"))
			|| Info.SearchText.Contains(TEXT("water_turbine"))
			|| Info.SearchText.Contains(TEXT("waterturbine"))
			|| Info.SearchText.Contains(TEXT("geothermal"));
		if (bFuelGenerator || (!bGeneratorBase && !bRenewablePath)
			|| IsAlreadyCataloged(Info.BuildableClass))
		{
			continue;
		}

		const UObject* BuildableCDO = Info.BuildableClass->GetDefaultObject();
		double PowerMW = 0.0;
		if (const AFGBuildableGenerator* GeneratorCDO = Cast<AFGBuildableGenerator>(BuildableCDO))
		{
			PowerMW = FMath::Max(
				0.0,
				static_cast<double>(GeneratorCDO->GetDefaultPowerProductionCapacity()));
		}
		PowerMW = FMath::Max(PowerMW, ReflectedNumberValue(
			BuildableCDO, Info.BuildableClass, TEXT("mMaxPowerOutput"), 0.0));
		PowerMW = FMath::Max(PowerMW, ReflectedNumberValue(
			BuildableCDO, Info.BuildableClass, TEXT("mMaxSolarPanelProduction"), 0.0));
		PowerMW = FMath::Max(PowerMW, ReflectedNumberValue(
			BuildableCDO, Info.BuildableClass, TEXT("mMaxTurbinePowerProduction"), 0.0));
		PowerMW = FMath::Max(PowerMW, DescriptionMaximumPowerMW(Info.Description));
		if (PowerMW <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		TSharedPtr<FSFPPowerGeneratorOption> Generator = MakeShared<FSFPPowerGeneratorOption>();
		Generator->GeneratorClass = Info.BuildableClass;
		Generator->ClassPath = Info.ClassPath;
		Generator->DisplayName = Info.DisplayName;
		Generator->SourceMount = SolverSourceMountFromPath(Info.ClassPath);
		Generator->bAvailable = Info.bAvailable;
		Generator->PowerProductionMW = PowerMW;
		ApplyGeneratorClockRuntimeConfig(*Generator, BuildableCDO, Info.BuildableClass);
		Generator->bVariableOutput = Info.SearchText.Contains(TEXT("solar"))
			|| Info.SearchText.Contains(TEXT("wind"))
			|| Info.SearchText.Contains(TEXT("water"))
			|| Info.SearchText.Contains(TEXT("geothermal"));
		Generator->ConfigurationDetail = Generator->bVariableOutput
			? TEXT("Wetter-/standortabhängige Maximalleistung")
			: TEXT("Brennstofflose Stromerzeugung");

		FSFPPowerFuelOption FuelFree;
		FuelFree.ClassPath = TEXT("SFP.FuelFree");
		FuelFree.DisplayName = TEXT("Kein Brennstoff");
		FuelFree.Form = TEXT("none");
		FuelFree.SourceMount = Generator->SourceMount;
		FuelFree.bAvailable = Info.bAvailable;
		FuelFree.bFuelFree = true;
		Generator->Fuels.Add(MoveTemp(FuelFree));
		PowerGenerators.Add(MoveTemp(Generator));
	}

	auto SortInfos = [](TArray<const FOptionalPowerBuildableInfo*>& Infos)
	{
		Infos.Sort([](
			const FOptionalPowerBuildableInfo& Left,
			const FOptionalPowerBuildableInfo& Right)
		{
			if (Left.bAvailable != Right.bAvailable)
			{
				return Left.bAvailable;
			}
			return Left.ClassPath < Right.ClassPath;
		});
	};
	SortInfos(ModularGenerators);
	SortInfos(Turbines);
	SortInfos(Heaters);
	SortInfos(Boilers);
	SortInfos(Platforms);
	SortInfos(Coolers);

	auto FindNamed = [](const TArray<const FOptionalPowerBuildableInfo*>& Infos,
		const FString& Token) -> const FOptionalPowerBuildableInfo*
	{
		const FOptionalPowerBuildableInfo* LockedMatch = nullptr;
		for (const FOptionalPowerBuildableInfo* Info : Infos)
		{
			if (Info != nullptr && Info->SearchText.Contains(Token))
			{
				if (Info->bAvailable)
				{
					return Info;
				}
				LockedMatch = LockedMatch == nullptr ? Info : LockedMatch;
			}
		}
		return LockedMatch;
	};

	const FOptionalPowerBuildableInfo* BoilerPlatform = FindNamed(Platforms, TEXT("boilerplatform"));
	const FOptionalPowerBuildableInfo* ConverterPlatform = FindNamed(Platforms, TEXT("converterplatform"));
	const FOptionalPowerBuildableInfo* CoolingPlatform = FindNamed(Platforms, TEXT("coolingplatform"));
	const FOptionalPowerBuildableInfo* SuperPlatform = FindNamed(Platforms, TEXT("superplatform"));
	const FOptionalPowerBuildableInfo* SteamCooler = FindNamed(Coolers, TEXT("steamcooler"));
	const FOptionalPowerBuildableInfo* ExhaustCooler = FindNamed(Coolers, TEXT("co2cooler"));
	if (ExhaustCooler == nullptr)
	{
		ExhaustCooler = FindNamed(Coolers, TEXT("chimney"));
	}

	TSet<UClass*> KnownItemClasses;
	for (const TSubclassOf<UFGRecipe>& RecipeClass : AllRecipes)
	{
		UClass* RawRecipeClass = RecipeClass.Get();
		const UFGRecipe* RecipeCDO = IsValid(RawRecipeClass)
			? Cast<UFGRecipe>(RawRecipeClass->GetDefaultObject()) : nullptr;
		if (!IsValid(RecipeCDO))
		{
			continue;
		}
		for (const FItemAmount& Ingredient : RecipeCDO->GetIngredients())
		{
			UClass* ItemClass = Ingredient.ItemClass.Get();
			if (IsValid(ItemClass) && ItemClass->IsChildOf(UFGItemDescriptor::StaticClass())
				&& !ItemClass->IsChildOf(UFGBuildingDescriptor::StaticClass()))
			{
				KnownItemClasses.Add(ItemClass);
			}
		}
		for (const FItemAmount& Product : RecipeCDO->GetProducts())
		{
			UClass* ItemClass = Product.ItemClass.Get();
			if (IsValid(ItemClass) && ItemClass->IsChildOf(UFGItemDescriptor::StaticClass())
				&& !ItemClass->IsChildOf(UFGBuildingDescriptor::StaticClass()))
			{
				KnownItemClasses.Add(ItemClass);
			}
		}
	}
	for (const FSFPPlannerRecipe& Recipe : Recipes)
	{
		for (const FSFPPlannerItemRate& Ingredient : Recipe.Ingredients)
		{
			if (IsValid(Ingredient.ItemClass.Get()))
			{
				KnownItemClasses.Add(Ingredient.ItemClass.Get());
			}
		}
		for (const FSFPPlannerItemRate& Product : Recipe.Products)
		{
			if (IsValid(Product.ItemClass.Get()))
			{
				KnownItemClasses.Add(Product.ItemClass.Get());
			}
		}
	}
	for (const TSharedPtr<FSFPPowerGeneratorOption>& Existing : PowerGenerators)
	{
		if (!Existing.IsValid())
		{
			continue;
		}
		for (const FSFPPowerFuelOption& ExistingFuel : Existing->Fuels)
		{
			if (IsValid(ExistingFuel.ItemClass.Get()))
			{
				KnownItemClasses.Add(ExistingFuel.ItemClass.Get());
			}
		}
	}
	auto FindKnownItem = [&KnownItemClasses](
		const TArray<FString>& RequiredTokens,
		const TArray<FString>& RejectedTokens) -> UClass*
	{
		UClass* BestMatch = nullptr;
		for (UClass* ItemClass : KnownItemClasses)
		{
			if (!IsValid(ItemClass))
			{
				continue;
			}
			const FString Search = (ItemClass->GetPathName() + TEXT("|")
				+ ItemDisplayName(ItemClass)).ToLower();
			bool bMatches = true;
			for (const FString& Token : RequiredTokens)
			{
				bMatches &= Search.Contains(Token);
			}
			for (const FString& Token : RejectedTokens)
			{
				bMatches &= !Search.Contains(Token);
			}
			if (bMatches && (BestMatch == nullptr
				|| ItemClass->GetPathName() < BestMatch->GetPathName()))
			{
				BestMatch = ItemClass;
			}
		}
		return BestMatch;
	};

	auto FallbackFuelMatchesHeater = [](const FString& HeaterSearch, UClass* ItemClass)
	{
		if (!IsValid(ItemClass) || !ItemClass->IsChildOf(UFGItemDescriptor::StaticClass()))
		{
			return false;
		}
		const TSubclassOf<UFGItemDescriptor> TypedItem(ItemClass);
		if (UFGItemDescriptor::GetEnergyValue(TypedItem) <= KINDA_SMALL_NUMBER)
		{
			return false;
		}
		const FString ItemSearch = (ItemClass->GetPathName() + TEXT("|")
			+ ItemDisplayName(ItemClass)).ToLower();
		if (HeaterSearch.Contains(TEXT("biomass")))
		{
			return ItemSearch.Contains(TEXT("biomass"))
				|| ItemSearch.Contains(TEXT("biofuel"))
				|| ItemSearch.Contains(TEXT("leaves"))
				|| ItemSearch.Contains(TEXT("wood"))
				|| ItemSearch.Contains(TEXT("mycelia"));
		}
		if (HeaterSearch.Contains(TEXT("coalheater")))
		{
			return ItemSearch.Contains(TEXT("coal"))
				|| ItemSearch.Contains(TEXT("coke"))
				|| ItemSearch.Contains(TEXT("sulfur"));
		}
		if (HeaterSearch.Contains(TEXT("solutionheater")))
		{
			const EResourceForm Form = UFGItemDescriptor::GetForm(TypedItem);
			return (Form == EResourceForm::RF_LIQUID || Form == EResourceForm::RF_GAS)
				&& (ItemSearch.Contains(TEXT("fuel")) || ItemSearch.Contains(TEXT("oil")));
		}
		if (HeaterSearch.Contains(TEXT("nuclearheater")))
		{
			return ItemClass->IsChildOf(UFGItemDescriptorNuclearFuel::StaticClass())
				|| ItemSearch.Contains(TEXT("fuelrod"))
				|| ItemSearch.Contains(TEXT("uranium"))
				|| ItemSearch.Contains(TEXT("plutonium"));
		}
		return false;
	};

	auto HeaterTemperature = [](const FOptionalPowerBuildableInfo& Heater)
	{
		const UObject* CDO = Heater.BuildableClass->GetDefaultObject();
		double Temperature = ReflectedNumberValue(
			CDO, Heater.BuildableClass, TEXT("mMaxHeatValue"), 0.0);
		if (Temperature > KINDA_SMALL_NUMBER)
		{
			return Temperature;
		}
		if (Heater.SearchText.Contains(TEXT("biomass"))
			|| Heater.SearchText.Contains(TEXT("coalheater")))
		{
			return 500.0;
		}
		return 2000.0;
	};

	auto BoilerPointForTemperature = [](const FOptionalPowerBuildableInfo& Boiler,
		const double Temperature)
	{
		TArray<FBoilerOperatingPoint> Points;
		ParseBoilerOperatingPoints(Boiler.Description, Points);
		FBoilerOperatingPoint Selected;
		for (const FBoilerOperatingPoint& Point : Points)
		{
			if (Point.TemperatureC <= Temperature + KINDA_SMALL_NUMBER)
			{
				Selected = Point;
			}
		}
		if (Selected.SteamPerMinute > KINDA_SMALL_NUMBER)
		{
			return Selected;
		}

		const bool bMk2 = Boiler.SearchText.Contains(TEXT("boilermk2"))
			|| Boiler.SearchText.Contains(TEXT("mk.2"));
		const double TierTemperature = Temperature >= 2000.0 ? 2000.0
			: Temperature >= 1500.0 ? 1500.0
			: Temperature >= 1250.0 ? 1250.0
			: Temperature >= 1000.0 ? 1000.0
			: Temperature >= 750.0 ? 750.0
			: 500.0;
		Selected.TemperatureC = TierTemperature;
		if (bMk2)
		{
			Selected.WaterPerMinute = TierTemperature >= 2000.0 ? 150.0
				: TierTemperature >= 1500.0 ? 90.0
				: TierTemperature >= 1250.0 ? 75.0
				: TierTemperature >= 1000.0 ? 60.0
				: TierTemperature >= 750.0 ? 45.0 : 30.0;
		}
		else
		{
			Selected.WaterPerMinute = TierTemperature >= 2000.0 ? 100.0
				: TierTemperature >= 1500.0 ? 60.0
				: TierTemperature >= 1250.0 ? 50.0
				: TierTemperature >= 1000.0 ? 40.0
				: TierTemperature >= 750.0 ? 30.0 : 20.0;
		}
		Selected.SteamPerMinute = Selected.WaterPerMinute * 2.0;
		return Selected;
	};

	for (const FOptionalPowerBuildableInfo* GeneratorInfo : ModularGenerators)
	{
		if (GeneratorInfo == nullptr || IsAlreadyCataloged(GeneratorInfo->BuildableClass))
		{
			continue;
		}
		const UObject* GeneratorCDO = GeneratorInfo->BuildableClass->GetDefaultObject();
		double PowerMW = ReflectedNumberValue(
			GeneratorCDO, GeneratorInfo->BuildableClass, TEXT("mMaxPowerOutput"), 0.0);
		PowerMW = FMath::Max(PowerMW, DescriptionMaximumPowerMW(GeneratorInfo->Description));
		double RequiredRPM = ReflectedNumberValue(
			GeneratorCDO, GeneratorInfo->BuildableClass, TEXT("mMaxRPM"), 0.0);
		RequiredRPM = FMath::Max(RequiredRPM, DescriptionMaximumRPM(GeneratorInfo->Description));
		const bool bSuperGenerator = GeneratorInfo->SearchText.Contains(TEXT("supergenerator"));
		const bool bHighVoltage = GeneratorInfo->SearchText.Contains(TEXT("hvgenerator"))
			|| GeneratorInfo->DisplayName.Contains(TEXT("HV"), ESearchCase::IgnoreCase)
			|| GeneratorInfo->DisplayName.Contains(TEXT("Hochspannung"), ESearchCase::IgnoreCase);
		if (PowerMW <= KINDA_SMALL_NUMBER)
		{
			PowerMW = bSuperGenerator ? 10000.0 : (bHighVoltage ? 400.0 : 150.0);
		}
		if (RequiredRPM <= KINDA_SMALL_NUMBER)
		{
			RequiredRPM = bSuperGenerator ? 100000.0 : (bHighVoltage ? 30000.0 : 15000.0);
		}

		const FOptionalPowerBuildableInfo* BestTurbine = nullptr;
		double BestTurbineRPM = 0.0;
		double BestSteamRate = 0.0;
		for (const FOptionalPowerBuildableInfo* Turbine : Turbines)
		{
			if (Turbine == nullptr)
			{
				continue;
			}
			const UObject* TurbineCDO = Turbine->BuildableClass->GetDefaultObject();
			double MaximumRPM = ReflectedNumberValue(
				TurbineCDO, Turbine->BuildableClass, TEXT("mRedMaxTurbineRPM"), 0.0);
			MaximumRPM = FMath::Max(MaximumRPM, ReflectedNumberValue(
				TurbineCDO, Turbine->BuildableClass, TEXT("mMaxRPM"), 0.0));
			MaximumRPM = FMath::Max(MaximumRPM, DescriptionMaximumRPM(Turbine->Description));
			const bool bSuperTurbine = Turbine->SearchText.Contains(TEXT("superturbine"));
			const bool bMk2Turbine = Turbine->SearchText.Contains(TEXT("turbinemk2"))
				|| Turbine->SearchText.Contains(TEXT("mk.2"));
			if (MaximumRPM <= KINDA_SMALL_NUMBER)
			{
				MaximumRPM = bSuperTurbine ? 100000.0 : (bMk2Turbine ? 30000.0 : 15000.0);
			}
			if (MaximumRPM + KINDA_SMALL_NUMBER < RequiredRPM
				|| (bSuperGenerator && !bSuperTurbine))
			{
				continue;
			}
			const bool bBetter = BestTurbine == nullptr
				|| (Turbine->bAvailable && !BestTurbine->bAvailable)
				|| (Turbine->bAvailable == BestTurbine->bAvailable
					&& MaximumRPM < BestTurbineRPM);
			if (!bBetter)
			{
				continue;
			}
			BestTurbine = Turbine;
			BestTurbineRPM = MaximumRPM;
			const double FallbackSteamRate = bSuperTurbine ? 300.0 : (bMk2Turbine ? 120.0 : 60.0);
			BestSteamRate = NormalizedFluidRate(ReflectedNumberValue(
				TurbineCDO, Turbine->BuildableClass, TEXT("mSteamPullAmount"), 0.0),
				FallbackSteamRate);
		}
		if (BestTurbine == nullptr || BestSteamRate <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FOptionalPowerBuildableInfo* RequiredPlatform = bSuperGenerator
			? SuperPlatform : ConverterPlatform;
		if (RequiredPlatform == nullptr)
		{
			// A turbine and generator without their physical mounting platform are
			// not a buildable modular plant, even when locked entries are displayed.
			continue;
		}
		TSharedPtr<FSFPPowerGeneratorOption> Generator = MakeShared<FSFPPowerGeneratorOption>();
		Generator->GeneratorClass = GeneratorInfo->BuildableClass;
		Generator->ClassPath = GeneratorInfo->ClassPath;
		Generator->DisplayName = FString::Printf(
			TEXT("%s + %s"), *GeneratorInfo->DisplayName, *BestTurbine->DisplayName);
		Generator->SourceMount = SolverSourceMountFromPath(GeneratorInfo->ClassPath);
		Generator->PowerProductionMW = PowerMW;
		ApplyGeneratorClockRuntimeConfig(*Generator, GeneratorCDO, GeneratorInfo->BuildableClass);
		Generator->bModularPower = true;
		Generator->TurbineClass = BestTurbine->BuildableClass;
		Generator->TurbineClassPath = BestTurbine->ClassPath;
		Generator->TurbineDisplayName = BestTurbine->DisplayName;
		Generator->TurbineSteamRatePerMinute = BestSteamRate;
		Generator->TurbineMaximumRPM = BestTurbineRPM;
		Generator->ConverterPlatformClass = RequiredPlatform != nullptr
			? RequiredPlatform->BuildableClass : nullptr;
		Generator->ConverterPlatformClassPath = RequiredPlatform != nullptr
			? RequiredPlatform->ClassPath : FString();
		Generator->ConverterPlatformDisplayName = RequiredPlatform != nullptr
			? RequiredPlatform->DisplayName : TEXT("Konverter-Plattform");
		Generator->bAvailable = GeneratorInfo->bAvailable
			&& BestTurbine->bAvailable
			&& RequiredPlatform != nullptr
			&& RequiredPlatform->bAvailable;
		Generator->ConfigurationDetail = FString::Printf(
			TEXT("%s UPM | %s Dampf/min"),
			*FSFPNumberFormatting::Decimal(RequiredRPM, 0),
			*FSFPNumberFormatting::Decimal(BestSteamRate));

		TMap<FString, FSFPPowerFuelOption> FuelsByPath;
		for (const FOptionalPowerBuildableInfo* Heater : Heaters)
		{
			if (Heater == nullptr)
			{
				continue;
			}
			const UObject* HeaterCDO = Heater->BuildableClass->GetDefaultObject();
			TArray<UClass*> AllowedFuelClasses;
			ReflectedClassArray(
				HeaterCDO, Heater->BuildableClass, TEXT("mAllowedFuelItems"), AllowedFuelClasses);
			if (AllowedFuelClasses.IsEmpty())
			{
				for (UClass* ItemClass : KnownItemClasses)
				{
					if (FallbackFuelMatchesHeater(Heater->SearchText, ItemClass))
					{
						AllowedFuelClasses.Add(ItemClass);
					}
				}
			}
			AllowedFuelClasses.Sort([](const UClass& Left, const UClass& Right)
			{
				return Left.GetPathName() < Right.GetPathName();
			});

			const double Temperature = HeaterTemperature(*Heater);
			const FOptionalPowerBuildableInfo* BestBoiler = nullptr;
			FBoilerOperatingPoint BestBoilerPoint;
			double BestBoilerCount = 0.0;
			for (const FOptionalPowerBuildableInfo* Boiler : Boilers)
			{
				if (Boiler == nullptr)
				{
					continue;
				}
				const FBoilerOperatingPoint Point = BoilerPointForTemperature(*Boiler, Temperature);
				if (Point.SteamPerMinute <= KINDA_SMALL_NUMBER)
				{
					continue;
				}
				const double BoilerCount = BestSteamRate / Point.SteamPerMinute;
				const bool bBetter = BestBoiler == nullptr
					|| (Boiler->bAvailable && !BestBoiler->bAvailable)
					|| (Boiler->bAvailable == BestBoiler->bAvailable
						&& BoilerCount < BestBoilerCount - KINDA_SMALL_NUMBER);
				if (bBetter)
				{
					BestBoiler = Boiler;
					BestBoilerPoint = Point;
					BestBoilerCount = BoilerCount;
				}
			}
			if (BestBoiler == nullptr || BestBoilerCount <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			if (BoilerPlatform == nullptr)
			{
				continue;
			}

			const double EnergyMultiplier = FMath::Max(
				KINDA_SMALL_NUMBER,
				ReflectedNumberValue(
					HeaterCDO, Heater->BuildableClass, TEXT("mEnergyValueMultiplier"), 1.0));
			UClass* WaterClass = ReflectedClassValue(
				BestBoiler->BuildableClass->GetDefaultObject(),
				BestBoiler->BuildableClass,
				TEXT("mWaterItemClass"));
			UClass* HighSteamClass = ReflectedClassValue(
				BestTurbine->BuildableClass->GetDefaultObject(),
				BestTurbine->BuildableClass,
				TEXT("mHighSteamItemClass"));
			if (!IsValid(HighSteamClass))
			{
				HighSteamClass = ReflectedClassValue(
					BestBoiler->BuildableClass->GetDefaultObject(),
					BestBoiler->BuildableClass,
					TEXT("mSteamItemClass"));
			}
			UClass* LowSteamClass = ReflectedClassValue(
				BestTurbine->BuildableClass->GetDefaultObject(),
				BestTurbine->BuildableClass,
				TEXT("mLowSteamItemClass"));
			UClass* ExhaustClass = ReflectedBoolValue(
				HeaterCDO, Heater->BuildableClass, TEXT("mProducesCo2"), true)
				? ReflectedClassValue(
					HeaterCDO, Heater->BuildableClass, TEXT("mCo2ItemClass"))
				: nullptr;
			UClass* HeaterWasteClass = ReflectedBoolValue(
				HeaterCDO, Heater->BuildableClass, TEXT("mProducesNukeWaste"), false)
				? ReflectedClassValue(
					HeaterCDO, Heater->BuildableClass, TEXT("mNukeWasteItemClass"))
				: nullptr;
			const double OutputAmount = FMath::Max(0.0, ReflectedNumberValue(
				HeaterCDO, Heater->BuildableClass, TEXT("mOutputGenerationAmount"), 1.0));
			if (!IsValid(WaterClass))
			{
				WaterClass = FindKnownItem(
					{TEXT("rawresources/water/desc_water")},
					{TEXT("packaged")});
			}
			if (!IsValid(HighSteamClass))
			{
				HighSteamClass = FindKnownItem(
					{TEXT("desc_gas_steam_hot")},
					{TEXT("packaged")});
			}
			if (!IsValid(LowSteamClass))
			{
				LowSteamClass = FindKnownItem(
					{TEXT("desc_gas_steam")},
					{TEXT("hot"), TEXT("packaged")});
			}
			if (!IsValid(ExhaustClass)
				&& ReflectedBoolValue(
					HeaterCDO, Heater->BuildableClass, TEXT("mProducesCo2"), true))
			{
				ExhaustClass = FindKnownItem(
					{TEXT("desc_fluegas")},
					{TEXT("packaged")});
			}
			const bool bNeedsSteamCooling = IsValid(LowSteamClass);
			const bool bNeedsExhaustCooling = IsValid(ExhaustClass);
			if ((bNeedsSteamCooling && SteamCooler == nullptr)
				|| (bNeedsExhaustCooling && ExhaustCooler == nullptr)
				|| ((bNeedsSteamCooling || bNeedsExhaustCooling)
					&& CoolingPlatform == nullptr))
			{
				continue;
			}

			for (UClass* FuelClass : AllowedFuelClasses)
			{
				if (!IsValid(FuelClass)
					|| !FuelClass->IsChildOf(UFGItemDescriptor::StaticClass()))
				{
					continue;
				}
				FSFPPowerFuelOption Fuel;
				PopulateItem(
					FuelClass, Fuel.ItemClass, Fuel.ClassPath, Fuel.DisplayName, Fuel.Form);
				Fuel.SourceMount = SolverSourceMountFromPath(Fuel.ClassPath);
				Fuel.EnergyValueMJ = FMath::Max(
					0.0,
					static_cast<double>(UFGItemDescriptor::GetEnergyValue(Fuel.ItemClass)));
				if (Fuel.EnergyValueMJ <= KINDA_SMALL_NUMBER)
				{
					continue;
				}
				double ConsumptionPerHeater = 3600.0
					/ (Fuel.EnergyValueMJ * EnergyMultiplier);
				if (Fuel.Form == TEXT("liquid") || Fuel.Form == TEXT("gas"))
				{
					ConsumptionPerHeater /= 1000.0;
				}
				Fuel.ConsumptionRatePerGenerator = ConsumptionPerHeater * BestBoilerCount;
				Fuel.HeaterClass = Heater->BuildableClass;
				Fuel.HeaterClassPath = Heater->ClassPath;
				Fuel.HeaterDisplayName = Heater->DisplayName;
				Fuel.BoilerClass = BestBoiler->BuildableClass;
				Fuel.BoilerClassPath = BestBoiler->ClassPath;
				Fuel.BoilerDisplayName = BestBoiler->DisplayName;
				Fuel.BoilerPlatformClass = BoilerPlatform != nullptr
					? BoilerPlatform->BuildableClass : nullptr;
				Fuel.BoilerPlatformClassPath = BoilerPlatform != nullptr
					? BoilerPlatform->ClassPath : FString();
				Fuel.BoilerPlatformDisplayName = BoilerPlatform != nullptr
					? BoilerPlatform->DisplayName : TEXT("Heizkessel-Plattform");
				Fuel.SteamCoolerClass = SteamCooler != nullptr
					? SteamCooler->BuildableClass : nullptr;
				Fuel.SteamCoolerClassPath = SteamCooler != nullptr
					? SteamCooler->ClassPath : FString();
				Fuel.SteamCoolerDisplayName = SteamCooler != nullptr
					? SteamCooler->DisplayName : TEXT("Dampf-Kühlturm");
				Fuel.ExhaustCoolerClass = ExhaustCooler != nullptr
					? ExhaustCooler->BuildableClass : nullptr;
				Fuel.ExhaustCoolerClassPath = ExhaustCooler != nullptr
					? ExhaustCooler->ClassPath : FString();
				Fuel.ExhaustCoolerDisplayName = ExhaustCooler != nullptr
					? ExhaustCooler->DisplayName : TEXT("Rauchgas-Schornstein");
				Fuel.CoolingPlatformClass = CoolingPlatform != nullptr
					? CoolingPlatform->BuildableClass : nullptr;
				Fuel.CoolingPlatformClassPath = CoolingPlatform != nullptr
					? CoolingPlatform->ClassPath : FString();
				Fuel.CoolingPlatformDisplayName = CoolingPlatform != nullptr
					? CoolingPlatform->DisplayName : TEXT("Abgasplattform");
				Fuel.HeaterCountPerGenerator = BestBoilerCount;
				Fuel.BoilerCountPerGenerator = BestBoilerCount;
				Fuel.WaterRatePerGenerator = BestBoilerPoint.WaterPerMinute * BestBoilerCount;
				Fuel.HighSteamRatePerGenerator = BestSteamRate;
				Fuel.LowSteamRatePerGenerator = IsValid(LowSteamClass) ? BestSteamRate : 0.0;
				Fuel.ExhaustRatePerGenerator = IsValid(ExhaustClass)
					? ModularHeaterByproductRatePerMinute(
						HeaterCDO,
						Heater->BuildableClass,
						Fuel.ClassPath,
						Fuel.DisplayName) * BestBoilerCount
					: 0.0;
				PopulateItem(
					WaterClass,
					Fuel.WaterItemClass,
					Fuel.WaterItemClassPath,
					Fuel.WaterDisplayName,
					Fuel.WaterForm);
				PopulateItem(
					HighSteamClass,
					Fuel.HighSteamItemClass,
					Fuel.HighSteamItemClassPath,
					Fuel.HighSteamDisplayName,
					Fuel.HighSteamForm);
				PopulateItem(
					LowSteamClass,
					Fuel.LowSteamItemClass,
					Fuel.LowSteamItemClassPath,
					Fuel.LowSteamDisplayName,
					Fuel.LowSteamForm);
				PopulateItem(
					ExhaustClass,
					Fuel.ExhaustItemClass,
					Fuel.ExhaustItemClassPath,
					Fuel.ExhaustDisplayName,
					Fuel.ExhaustForm);

				UClass* FuelWasteClass = HeaterWasteClass;
				if (FuelClass->IsChildOf(UFGItemDescriptorNuclearFuel::StaticClass()))
				{
					const TSubclassOf<UFGItemDescriptorNuclearFuel> NuclearFuelClass(FuelClass);
					UClass* DescriptorWaste = UFGItemDescriptorNuclearFuel::GetSpentFuelClass(
						NuclearFuelClass).Get();
					FuelWasteClass = IsValid(DescriptorWaste)
						? DescriptorWaste : FuelWasteClass;
					Fuel.WasteAmountPerFuel = FMath::Max(
						0,
						UFGItemDescriptorNuclearFuel::GetAmountWasteCreated(NuclearFuelClass));
				}
				if (IsValid(FuelWasteClass))
				{
					PopulateItem(
						FuelWasteClass,
						Fuel.WasteItemClass,
						Fuel.WasteItemClassPath,
						Fuel.WasteDisplayName,
						Fuel.WasteForm);
					if (Fuel.WasteAmountPerFuel <= KINDA_SMALL_NUMBER)
					{
						Fuel.WasteAmountPerFuel = OutputAmount;
					}
				}

				Fuel.bAvailable = Generator->bAvailable
					&& Heater->bAvailable
					&& BestBoiler->bAvailable
					&& BoilerPlatform != nullptr
					&& BoilerPlatform->bAvailable
					&& IsValid(WaterClass)
					&& IsValid(HighSteamClass)
					&& IsItemAvailable(FuelClass)
					&& IsItemAvailable(WaterClass)
					&& (!bNeedsSteamCooling || SteamCooler->bAvailable)
					&& (!bNeedsExhaustCooling || ExhaustCooler->bAvailable)
					&& (!(bNeedsSteamCooling || bNeedsExhaustCooling)
						|| CoolingPlatform->bAvailable);

				FSFPPowerFuelOption* ExistingFuel = FuelsByPath.Find(Fuel.ClassPath);
				const bool bBetterFuel = ExistingFuel == nullptr
					|| (Fuel.bAvailable && !ExistingFuel->bAvailable)
					|| (Fuel.bAvailable == ExistingFuel->bAvailable
						&& Fuel.ConsumptionRatePerGenerator
							< ExistingFuel->ConsumptionRatePerGenerator - KINDA_SMALL_NUMBER);
				if (bBetterFuel)
				{
					FuelsByPath.Add(Fuel.ClassPath, MoveTemp(Fuel));
				}
			}
		}

		FuelsByPath.GenerateValueArray(Generator->Fuels);
		Generator->Fuels.Sort([](const FSFPPowerFuelOption& Left, const FSFPPowerFuelOption& Right)
		{
			if (Left.bAvailable != Right.bAvailable)
			{
				return Left.bAvailable;
			}
			return Left.DisplayName.Compare(Right.DisplayName, ESearchCase::IgnoreCase) < 0;
		});
		Generator->bAvailable = Generator->bAvailable
			&& Generator->Fuels.ContainsByPredicate([](const FSFPPowerFuelOption& Fuel)
			{
				return Fuel.bAvailable;
			});
		if (!Generator->Fuels.IsEmpty())
		{
			PowerGenerators.Add(MoveTemp(Generator));
		}
	}

	UE_LOG(
		LogSFPFactoryPlanner,
		Display,
		TEXT("Power catalog: %d complete generator configurations (%d modular buildables discovered)"),
		PowerGenerators.Num(),
		ModularGenerators.Num() + Turbines.Num() + Heaters.Num() + Boilers.Num()
			+ Platforms.Num() + Coolers.Num());
}

void FSFPPlannerSolver::BuildStandardMinerCatalog(
	const TArray<TSubclassOf<UFGRecipe>>& AllRecipes,
	AFGRecipeManager* RecipeManager,
	TMap<UClass*, TSharedPtr<FSFPProductOption>>& ProductsByClass)
{
	// Ordinary miners have construction recipes, but no UFGRecipe for extraction.
	// Use a bounded list of real base-game node resources, never all solid items:
	// farming soil, seeds, packaged fluids and processing byproducts are not nodes.
	static const TCHAR* NodeResources[] = {
		TEXT("/Game/FactoryGame/Resource/RawResources/Coal/Desc_Coal.Desc_Coal_C"),
		TEXT("/Game/FactoryGame/Resource/RawResources/OreIron/Desc_OreIron.Desc_OreIron_C"),
		TEXT("/Game/FactoryGame/Resource/RawResources/OreCopper/Desc_OreCopper.Desc_OreCopper_C"),
		TEXT("/Game/FactoryGame/Resource/RawResources/Stone/Desc_Stone.Desc_Stone_C"),
		TEXT("/Game/FactoryGame/Resource/RawResources/OreGold/Desc_OreGold.Desc_OreGold_C"),
		TEXT("/Game/FactoryGame/Resource/RawResources/RawQuartz/Desc_RawQuartz.Desc_RawQuartz_C"),
		TEXT("/Game/FactoryGame/Resource/RawResources/Sulfur/Desc_Sulfur.Desc_Sulfur_C"),
		TEXT("/Game/FactoryGame/Resource/RawResources/OreBauxite/Desc_OreBauxite.Desc_OreBauxite_C"),
		TEXT("/Game/FactoryGame/Resource/RawResources/OreUranium/Desc_OreUranium.Desc_OreUranium_C"),
		TEXT("/Game/FactoryGame/Resource/RawResources/SAM/Desc_SAM.Desc_SAM_C"),
	};
	TMap<UClass*, FOptionalBuildableInfo> Miners;
	auto RegisterStandardMiner = [&Miners](UClass* Class, const bool bAvailable)
	{
		if (!IsValid(Class)
			|| !Class->IsChildOf(AFGBuildableResourceExtractor::StaticClass())
			|| IsClassOrParentNamed(Class, TEXT("KLMMBuildableMiner")))
		{
			return;
		}
		const auto* CDO = Cast<AFGBuildableResourceExtractor>(Class->GetDefaultObject());
		if (!IsValid(CDO))
		{
			return;
		}
		FOptionalBuildableInfo& Info = Miners.FindOrAdd(Class);
		Info.BuildableClass = Class;
		Info.ClassPath = Class->GetPathName();
		Info.DisplayName = SolverBuildableDisplayName(CDO, Class);
		Info.PowerMW = OptionalBuildablePower(Class);
		Info.bAvailable |= bAvailable;
	};

	// Prefer the actual construction catalog so unlock state remains authoritative.
	// Standard miners are resolved a second way from the descriptors in the live
	// recipes. This is required on Linux where the shipped MinerMK1/MinerMk2/MinerMk3
	// package capitalization differs and a guessed canonical path can silently miss.
	for (const TSubclassOf<UFGRecipe>& ConstructionRecipe : AllRecipes)
	{
		const bool bAvailable = IsValid(RecipeManager)
			&& RecipeManager->IsRecipeAvailable(ConstructionRecipe);
		UClass* BuildableClass = SafeBuildableClassFromRecipe(ConstructionRecipe);
		FString DescriptorPath;
		FString DerivedBuildablePath;
		if (!IsValid(BuildableClass)
			|| !BuildableClass->IsChildOf(AFGBuildableResourceExtractor::StaticClass()))
		{
			BuildableClass = RuntimeStandardMinerBuildableFromRecipe(
				ConstructionRecipe, DescriptorPath, DerivedBuildablePath);
		}
		RegisterStandardMiner(BuildableClass, bAvailable);
		if (bMinerDiagnostics && !DescriptorPath.IsEmpty())
		{
			const FString RecipePath = IsValid(ConstructionRecipe.Get())
				? ConstructionRecipe->GetPathName() : TEXT("<invalid>");
			const FString ResolvedBuildablePath = IsValid(BuildableClass)
				? BuildableClass->GetPathName() : TEXT("<missing>");
			MinerDiagnostics.Add(FString::Printf(
				TEXT("STANDARD_RUNTIME recipe=%s descriptor=%s derived=%s resolved=%s available=%d"),
				*RecipePath,
				*DescriptorPath,
				*DerivedBuildablePath,
				*ResolvedBuildablePath,
				bAvailable ? 1 : 0));
		}
	}

	// Some runtime recipe catalogs omit the original vanilla build recipe entirely.
	// Resolve the three canonical miner descriptors as a discovery fallback. This
	// does not mark a locked miner available: availability still comes from any
	// construction recipe above that resolves to the same buildable class.
	struct FVanillaMinerFallback
	{
		const TCHAR* BuildablePath;
		const TCHAR* DescriptorPath;
		const TCHAR* BuildRecipePath;
		bool bBasicTierAlwaysAvailable;
	};
	static const FVanillaMinerFallback VanillaMinerFallbacks[] = {
		{
			TEXT("/Game/FactoryGame/Buildable/Factory/MinerMK1/Build_MinerMk1.Build_MinerMk1_C"),
			TEXT("/Game/FactoryGame/Buildable/Factory/MinerMK1/Desc_MinerMk1.Desc_MinerMk1_C"),
			TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_MinerMk1.Recipe_MinerMk1_C"),
			true
		},
		{
			TEXT("/Game/FactoryGame/Buildable/Factory/MinerMk2/Build_MinerMk2.Build_MinerMk2_C"),
			TEXT("/Game/FactoryGame/Buildable/Factory/MinerMk2/Desc_MinerMk2.Desc_MinerMk2_C"),
			TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_MinerMk2.Recipe_MinerMk2_C"),
			false
		},
		{
			TEXT("/Game/FactoryGame/Buildable/Factory/MinerMk3/Build_MinerMk3.Build_MinerMk3_C"),
			TEXT("/Game/FactoryGame/Buildable/Factory/MinerMk3/Desc_MinerMk3.Desc_MinerMk3_C"),
			TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_MinerMk3.Recipe_MinerMk3_C"),
			false
		},
	};
	for (const FVanillaMinerFallback& Fallback : VanillaMinerFallbacks)
	{
		// Load the actual buildable first. The runtime export proves these exact
		// vanilla assets exist even in catalogs where the planner's generic machine
		// discovery reports zero miners. Descriptor resolution remains a fallback.
		UClass* BuildableClass = LoadClass<AFGBuildableResourceExtractor>(
			nullptr, Fallback.BuildablePath);
		if (!IsValid(BuildableClass) || !BuildableClass->IsChildOf(AFGBuildableResourceExtractor::StaticClass()))
		{
			UClass* DescriptorClass = LoadObject<UClass>(nullptr, Fallback.DescriptorPath);
			if (IsValid(DescriptorClass) && DescriptorClass->IsChildOf(UFGBuildingDescriptor::StaticClass()))
			{
				BuildableClass = UFGBuildingDescriptor::GetBuildableClass(
					TSubclassOf<UFGBuildingDescriptor>(DescriptorClass)).Get();
			}
		}
		if (!IsValid(BuildableClass) || !BuildableClass->IsChildOf(AFGBuildableResourceExtractor::StaticClass()))
		{
			UE_LOG(LogSFPFactoryPlanner, Warning,
				TEXT("Standard miner fallback could not resolve buildable: %s"),
				Fallback.BuildablePath);
			continue;
		}

		bool bBuildRecipeResolved = false;
		bool bBuildRecipeAvailable = false;
		if (UClass* BuildRecipeClass = LoadObject<UClass>(nullptr, Fallback.BuildRecipePath);
			IsValid(BuildRecipeClass) && BuildRecipeClass->IsChildOf(UFGRecipe::StaticClass()))
		{
			bBuildRecipeResolved = true;
			if (IsValid(RecipeManager))
			{
				bBuildRecipeAvailable = RecipeManager->IsRecipeAvailable(
					TSubclassOf<UFGRecipe>(BuildRecipeClass));
			}
		}

		// Mk.1 is the base-game extraction fallback and must remain usable even if
		// a modded recipe manager omits construction-recipe unlock state. Higher
		// tiers still honor their real Build Gun recipe availability.
		const bool bFallbackAvailable = Fallback.bBasicTierAlwaysAvailable
			|| (bBuildRecipeResolved && bBuildRecipeAvailable);
		RegisterStandardMiner(BuildableClass, bFallbackAvailable);

		UE_LOG(
			LogSFPFactoryPlanner,
			Display,
			TEXT("Standard miner fallback: buildable=%s recipe=%s resolved=%d available=%d"),
			Fallback.BuildablePath,
			Fallback.BuildRecipePath,
			bBuildRecipeResolved ? 1 : 0,
			bFallbackAvailable ? 1 : 0);
	}
	if (bMinerDiagnostics)
	{
		MinerDiagnostics.Add(FString::Printf(
			TEXT("STANDARD_CANDIDATES miners=%d"), Miners.Num()));
	}
	TArray<FOptionalBuildableInfo> SortedMiners;
	Miners.GenerateValueArray(SortedMiners);
	SortedMiners.Sort([](const FOptionalBuildableInfo& A, const FOptionalBuildableInfo& B)
	{
		return A.ClassPath < B.ClassPath;
	});
	const TCHAR* PurityNames[] = { TEXT("Unrein"), TEXT("Normal"), TEXT("Rein") };
	int32 TotalStandardRoutes = 0;
	for (const FOptionalBuildableInfo& Info : SortedMiners)
	{
		const auto* CDO = Cast<AFGBuildableResourceExtractor>(Info.BuildableClass->GetDefaultObject());
		const int32 VanillaMinerTier = VanillaStandardMinerTier(Info.ClassPath);
		const bool bVanillaStandardMiner = VanillaMinerTier > 0;
		double Cycle = IsValid(CDO) ? CDO->GetDefaultExtractCycleTime() : 0.0;
		double Amount = IsValid(CDO) ? CDO->GetNumExtractedItemsPerCycle() : 0.0;
		// Defensive vanilla fallback. Some modded CDO patching can leave the generic
		// extractor accessors at zero during early catalog initialization even though
		// the vanilla miner itself is valid. Preserve authoritative CDO values when
		// present; only use the known vanilla base rates for invalid zero values.
		if ((!FMath::IsFinite(Cycle) || Cycle <= KINDA_SMALL_NUMBER
			|| !FMath::IsFinite(Amount) || Amount <= KINDA_SMALL_NUMBER)
			&& bVanillaStandardMiner)
		{
			Amount = 1.0;
			Cycle = VanillaMinerTier == 3 ? 0.25
				: VanillaMinerTier == 2 ? 0.5 : 1.0;
		}
		// Respect extraction restrictions instead of assuming any miner can mine anything.
		const FArrayProperty* Forms = CastField<FArrayProperty>(Info.BuildableClass->FindPropertyByName(TEXT("mAllowedResourceForms")));
		// Vanilla miners can leave this optional list empty. Empty/absent means the
		// extractor's native node restriction decides; an explicit non-empty list is
		// authoritative and must contain RF_SOLID.
		bool bReflectedSolidAllowed = Forms == nullptr;
		int32 AllowedFormCount = 0;
		if (Forms != nullptr)
		{
			FScriptArrayHelper Values(Forms, Forms->ContainerPtrToValuePtr<void>(CDO));
			AllowedFormCount = Values.Num();
			bReflectedSolidAllowed = Values.Num() == 0;
			const FEnumProperty* Enum = CastField<FEnumProperty>(Forms->Inner);
			const FNumericProperty* Numeric = Enum != nullptr ? Enum->GetUnderlyingProperty() : CastField<FNumericProperty>(Forms->Inner);
			for (int32 I = 0; Numeric != nullptr && I < Values.Num(); ++I)
			{
				bReflectedSolidAllowed |= Numeric->GetSignedIntPropertyValue(Values.GetRawPtr(I)) == static_cast<int64>(EResourceForm::RF_SOLID);
			}
		}
		const bool bSolidAllowed = bVanillaStandardMiner || bReflectedSolidAllowed;
		UClass* NodeRestriction = ReflectedClassValue(CDO, Info.BuildableClass, TEXT("mRestrictToNodeType"));
		TArray<UClass*> Allowed;
		ReflectedClassArray(CDO, Info.BuildableClass, TEXT("mAllowedResources"), Allowed);
		// Missing restriction metadata must not turn the vanilla empty allow-list into
		// "allow nothing". Only an explicitly true property activates the list.
		const bool bReflectedRestricted = ReflectedBoolValue(
			CDO, Info.BuildableClass, TEXT("mOnlyAllowCertainResources"), false);
		const bool bRestricted = !bVanillaStandardMiner && bReflectedRestricted;
		const bool bReflectedNodeTypeAllowed = !IsValid(NodeRestriction)
			|| AFGResourceNode::StaticClass()->IsChildOf(NodeRestriction);
		const bool bNodeTypeAllowed = bVanillaStandardMiner
			|| bReflectedNodeTypeAllowed;
		if (bMinerDiagnostics)
		{
			MinerDiagnostics.Add(FString::Printf(
				TEXT("STANDARD_FILTER class=%s vanillaTier=%d cycle=%g amount=%g forms=%d reflectedSolid=%d appliedSolid=%d node=%s reflectedNodeAllowed=%d appliedNodeAllowed=%d reflectedRestricted=%d appliedRestricted=%d allowedResources=%d available=%d"),
				*Info.ClassPath,
				VanillaMinerTier,
				Cycle,
				Amount,
				AllowedFormCount,
				bReflectedSolidAllowed ? 1 : 0,
				bSolidAllowed ? 1 : 0,
				IsValid(NodeRestriction) ? *NodeRestriction->GetPathName() : TEXT("<none>"),
				bReflectedNodeTypeAllowed ? 1 : 0,
				bNodeTypeAllowed ? 1 : 0,
				bReflectedRestricted ? 1 : 0,
				bRestricted ? 1 : 0,
				Allowed.Num(),
				Info.bAvailable ? 1 : 0));
		}
		if (!bSolidAllowed || !bNodeTypeAllowed) { continue; }
		int32 RoutesForMiner = 0;
		for (const TCHAR* ResourcePath : NodeResources)
		{
			UClass* ResourceClass = LoadClass<UFGResourceDescriptor>(nullptr, ResourcePath);
			if (!IsValid(ResourceClass) || !ResourceClass->IsChildOf(UFGResourceDescriptor::StaticClass())) { continue; }
			const TArray<int32>* ExistingResourceRoutes = RecipesByProduct.Find(ResourceClass);
			const bool bHasModularRawRoute = ExistingResourceRoutes != nullptr
				&& ExistingResourceRoutes->ContainsByPredicate([this](const int32 RecipeIndex)
				{
					return Recipes.IsValidIndex(RecipeIndex)
						&& Recipes[RecipeIndex].ClassPath.StartsWith(TEXT("KAPI.ModularMinerRaw|"));
				});
			if (!SFPShouldAddStandardMinerRoute(bVanillaStandardMiner, bHasModularRawRoute))
			{
				if (bMinerDiagnostics)
				{
					MinerDiagnostics.Add(FString::Printf(
						TEXT("STANDARD_RESOURCE_SKIPPED class=%s resource=%s reason=modular_raw_route"),
						*Info.ClassPath,
						*ResourceClass->GetPathName()));
				}
				continue;
			}
			const TSubclassOf<UFGItemDescriptor> Descriptor(ResourceClass);
			if (!SFPStandardMinerRates::Supports(
				UFGItemDescriptor::GetForm(Descriptor) == EResourceForm::RF_SOLID,
				bRestricted, Allowed.Contains(ResourceClass))) { continue; }
			for (int32 Purity = 0; Purity < 3; ++Purity)
			{
				const double Rate = SFPStandardMinerRates::Rate(Amount, Cycle, Purity);
				if (Rate <= KINDA_SMALL_NUMBER) { continue; }
				FSFPPlannerRecipe Recipe;
				Recipe.ClassPath = FString::Printf(TEXT("SFP.StandardMiner|%s|%s|%d"), *Info.ClassPath, ResourcePath, Purity);
				Recipe.SourceMount = SolverSourceMountFromPath(Info.ClassPath);
				Recipe.bAvailable = Info.bAvailable;
				Recipe.DurationSeconds = Amount * 60.0 / Rate;
				Recipe.MachineClass = Info.BuildableClass;
				Recipe.MachineName = Info.DisplayName;
				Recipe.BasePowerMW = Info.PowerMW;
				Recipe.PowerExponent = FMath::Max(0.001, ReflectedNumberValue(
					CDO, Info.BuildableClass, TEXT("mPowerConsumptionExponent"), 1.0));
				Recipe.MachineConfig = ReadMachineRuntimeConfig(CDO, Info.BuildableClass);
				Recipe.bDirectResourceExtraction = true;
				Recipe.SourceName = ItemDisplayName(ResourceClass);
				Recipe.ResourceNodeLabel = PurityNames[Purity];
				Recipe.DisplayName = FString::Printf(TEXT("Abbau: %s (%s, %s)"), *Recipe.SourceName, PurityNames[Purity], *Info.DisplayName);
				Recipe.ConfigurationDetail = FString::Printf(TEXT("%s | %s | %s | %s/min"), *Recipe.SourceName, *Info.DisplayName, PurityNames[Purity], *FSFPNumberFormatting::Decimal(Rate));
				FSFPPlannerItemRate Resource;
				Resource.ItemClass = Descriptor;
				Resource.ClassPath = ResourceClass->GetPathName();
				Resource.DisplayName = Recipe.SourceName;
				Resource.Form = TEXT("solid");
				Resource.RatePerMinute = Rate;
				// Distinct raw-node input pool is handled by the existing material balance.
				Recipe.Ingredients.Add(Resource);
				Recipe.Products.Add(Resource);
				RecipesByProduct.FindOrAdd(ResourceClass).Add(Recipes.Add(MoveTemp(Recipe)));
				TSharedPtr<FSFPProductOption>& Option = ProductsByClass.FindOrAdd(ResourceClass);
				if (!Option.IsValid())
				{
					Option = MakeShared<FSFPProductOption>();
					Option->ItemClass = Descriptor;
					Option->ClassPath = Resource.ClassPath;
					Option->DisplayName = Resource.DisplayName;
					Option->Form = Resource.Form;
					Option->SourceMount = SolverSourceMountFromPath(Resource.ClassPath);
				}
				++Option->RecipeCount;
				Option->bHasAvailableRecipe |= Info.bAvailable;
				++RoutesForMiner;
				++TotalStandardRoutes;
			}
		}
		if (bMinerDiagnostics)
		{
			MinerDiagnostics.Add(FString::Printf(
				TEXT("STANDARD_RESULT class=%s routes=%d"),
				*Info.ClassPath,
				RoutesForMiner));
		}
	}
	if (bMinerDiagnostics)
	{
		MinerDiagnostics.Add(FString::Printf(
			TEXT("STANDARD_SUMMARY miners=%d routes=%d"),
			Miners.Num(),
			TotalStandardRoutes));
	}
}

void FSFPPlannerSolver::BuildFluidExtractorCatalog(
	const TArray<TSubclassOf<UFGRecipe>>& AllRecipes,
	AFGRecipeManager* RecipeManager,
	TMap<UClass*, TSharedPtr<FSFPProductOption>>& ProductsByClass)
{
	// Water pumps, oil extractors and the KLib bio-water extractor do not expose extraction as
	// UFGRecipe production. Their build recipes only create the building, while
	// rate, cycle size and supported liquid live on the extractor CDO. Register
	// one synthetic route per unlocked uniform-rate extractor so the liquid is
	// planned as an actual source instead of a misleading locked packaging or
	// byproduct recipe. Class-name checks keep optional mods link-free, while the
	// authoritative values still come entirely from the loaded building data.
	TMap<UClass*, FOptionalBuildableInfo> Extractors;
	for (const TSubclassOf<UFGRecipe>& ConstructionRecipe : AllRecipes)
	{
		UClass* BuildableClass = SafeBuildableClassFromRecipe(ConstructionRecipe);
		const AFGBuildableResourceExtractor* ExtractorCDO = IsValid(BuildableClass)
			? Cast<AFGBuildableResourceExtractor>(BuildableClass->GetDefaultObject())
			: nullptr;
		const FString BuildablePath = IsValid(BuildableClass)
			? BuildableClass->GetPathName()
			: FString();
		const bool bBioWaterExtractor = BuildablePath.Contains(
			TEXT("BioWaterExtractor"),
			ESearchCase::IgnoreCase);
		const bool bWaterExtractor = !bBioWaterExtractor
			&& (IsClassOrParentNamed(BuildableClass, TEXT("FGBuildableWaterPump"))
				|| BuildablePath.Contains(TEXT("/MiniExtractor/"), ESearchCase::IgnoreCase));
		const bool bOilExtractor = BuildablePath.Contains(TEXT("/OilPump/"), ESearchCase::IgnoreCase)
			|| BuildablePath.Contains(TEXT("OilExtractor"), ESearchCase::IgnoreCase);
		if (!IsValid(ExtractorCDO)
			|| (!bWaterExtractor && !bBioWaterExtractor && !bOilExtractor))
		{
			continue;
		}

		FOptionalBuildableInfo& Info = Extractors.FindOrAdd(BuildableClass);
		if (!IsValid(Info.BuildableClass))
		{
			Info.BuildableClass = BuildableClass;
			Info.ClassPath = BuildableClass->GetPathName();
			Info.DisplayName = SolverBuildableDisplayName(
				Cast<AFGBuildable>(BuildableClass->GetDefaultObject()),
				BuildableClass);
			Info.PowerMW = OptionalBuildablePower(BuildableClass);
		}
		Info.bAvailable |= IsValid(RecipeManager)
			&& RecipeManager->IsRecipeAvailable(ConstructionRecipe);
	}

	TArray<FOptionalBuildableInfo> SortedExtractors;
	Extractors.GenerateValueArray(SortedExtractors);
	SortedExtractors.Sort([](const FOptionalBuildableInfo& Left, const FOptionalBuildableInfo& Right)
	{
		return Left.ClassPath < Right.ClassPath;
	});

	for (const FOptionalBuildableInfo& Info : SortedExtractors)
	{
		const AFGBuildableResourceExtractor* ExtractorCDO = IsValid(Info.BuildableClass)
			? Cast<AFGBuildableResourceExtractor>(Info.BuildableClass->GetDefaultObject())
			: nullptr;
		if (!IsValid(ExtractorCDO))
		{
			continue;
		}

		const double CycleSeconds = ExtractorCDO->GetDefaultExtractCycleTime();
		const int32 RawAmountPerCycle = ExtractorCDO->GetNumExtractedItemsPerCycle();
		if (CycleSeconds <= KINDA_SMALL_NUMBER || RawAmountPerCycle <= 0)
		{
			continue;
		}

		TArray<UClass*> ResourceClasses;
		ReflectedClassArray(
			ExtractorCDO,
			Info.BuildableClass,
			TEXT("mAllowedResources"),
			ResourceClasses);

		const bool bBioWaterExtractor = Info.ClassPath.Contains(
			TEXT("BioWaterExtractor"),
			ESearchCase::IgnoreCase);
		const bool bWaterExtractor = !bBioWaterExtractor
			&& (IsClassOrParentNamed(Info.BuildableClass, TEXT("FGBuildableWaterPump"))
				|| Info.ClassPath.Contains(TEXT("/MiniExtractor/"), ESearchCase::IgnoreCase));
		const bool bOilExtractor = Info.ClassPath.Contains(TEXT("/OilPump/"), ESearchCase::IgnoreCase)
			|| Info.ClassPath.Contains(TEXT("OilExtractor"), ESearchCase::IgnoreCase);
		// Base-game and compatible modded water pumps may rely on their placement
		// resource and leave the optional allow-list empty. The canonical water
		// descriptor is loaded only for that specific class hierarchy. Other
		// extractors require an explicit liquid descriptor and are skipped safely.
		if (ResourceClasses.IsEmpty() && bWaterExtractor)
		{
			UClass* WaterClass = LoadObject<UClass>(
				nullptr,
				TEXT("/Game/FactoryGame/Resource/RawResources/Water/Desc_Water.Desc_Water_C"));
			if (IsValid(WaterClass))
			{
				ResourceClasses.Add(WaterClass);
			}
		}
		if (ResourceClasses.IsEmpty() && bOilExtractor)
		{
			UClass* CrudeOilClass = LoadObject<UClass>(
				nullptr,
				TEXT("/Game/FactoryGame/Resource/RawResources/CrudeOil/Desc_LiquidOil.Desc_LiquidOil_C"));
			if (IsValid(CrudeOilClass))
			{
				ResourceClasses.Add(CrudeOilClass);
			}
		}

		TArray<UClass*> UniqueResourceClasses;
		for (UClass* ResourceClass : ResourceClasses)
		{
			if (IsValid(ResourceClass))
			{
				UniqueResourceClasses.AddUnique(ResourceClass);
			}
		}
		UniqueResourceClasses.Sort([](const UClass& Left, const UClass& Right)
		{
			return Left.GetPathName() < Right.GetPathName();
		});

		for (UClass* ResourceClass : UniqueResourceClasses)
		{
			if (!IsValid(ResourceClass)
				|| !ResourceClass->IsChildOf(UFGItemDescriptor::StaticClass()))
			{
				continue;
			}

			const TSubclassOf<UFGItemDescriptor> ResourceDescriptor(ResourceClass);
			const EResourceForm Form = UFGItemDescriptor::GetForm(ResourceDescriptor);
			if (Form != EResourceForm::RF_LIQUID && Form != EResourceForm::RF_GAS)
			{
				continue;
			}

			const double AmountPerCycle = static_cast<double>(RawAmountPerCycle) / 1000.0;
			const double BaseRatePerMinute = AmountPerCycle * 60.0 / CycleSeconds;
			if (BaseRatePerMinute <= KINDA_SMALL_NUMBER)
			{
				continue;
			}

			static const TCHAR* PurityNames[] = {TEXT("Unrein"), TEXT("Normal"), TEXT("Rein")};
			static const double PurityMultipliers[] = {0.5, 1.0, 2.0};
			const int32 RouteCount = bOilExtractor ? 3 : 1;
			for (int32 RouteIndex = 0; RouteIndex < RouteCount; ++RouteIndex)
			{
				const double RatePerMinute = BaseRatePerMinute
					* (bOilExtractor ? PurityMultipliers[RouteIndex] : 1.0);
				FString SourceLabel = TEXT("Flüssigkeitsvorkommen");
				if (bOilExtractor)
				{
					SourceLabel = PurityNames[RouteIndex];
				}
				else if (bWaterExtractor)
				{
					SourceLabel = TEXT("Wasserfläche");
				}

				FSFPPlannerRecipe Recipe;
				Recipe.ClassPath = bOilExtractor
					? FString::Printf(
						TEXT("SFP.DirectFluidExtractor|%s|%s|%d"),
						*Info.ClassPath,
						*ResourceClass->GetPathName(),
						RouteIndex)
					: FString::Printf(
						TEXT("SFP.DirectFluidExtractor|%s|%s"),
						*Info.ClassPath,
						*ResourceClass->GetPathName());
				Recipe.SourceMount = SolverSourceMountFromPath(Info.ClassPath);
				Recipe.bAvailable = Info.bAvailable;
				Recipe.DurationSeconds = CycleSeconds;
				Recipe.MachineClass = Info.BuildableClass;
				Recipe.MachineName = Info.DisplayName;
				Recipe.BasePowerMW = Info.PowerMW;
				Recipe.PowerExponent = FMath::Max(0.001, ReflectedNumberValue(
					ExtractorCDO, Info.BuildableClass, TEXT("mPowerConsumptionExponent"), 1.0));
				Recipe.MachineConfig = ReadMachineRuntimeConfig(ExtractorCDO, Info.BuildableClass);
				Recipe.bDirectResourceExtraction = true;
				Recipe.SourceName = ItemDisplayName(ResourceClass);
				Recipe.ResourceNodeLabel = SourceLabel;
				Recipe.DisplayName = FString::Printf(
					TEXT("Förderung: %s (%s, %s)"),
					*Recipe.SourceName,
					*SourceLabel,
					*Info.DisplayName);
				Recipe.ConfigurationDetail = FString::Printf(
					TEXT("%s | %s | %s m³/min pro Extraktor"),
					*Recipe.SourceName,
					*SourceLabel,
					*FSFPNumberFormatting::Decimal(RatePerMinute));

				FSFPPlannerItemRate Resource;
				Resource.ItemClass = ResourceDescriptor;
				Resource.ClassPath = ResourceClass->GetPathName();
				Resource.DisplayName = Recipe.SourceName;
				Resource.Form = SolverResourceFormToString(Form);
				Resource.RatePerMinute = RatePerMinute;
				Recipe.Ingredients.Add(Resource);
				Recipe.Products.Add(Resource);

				const int32 RecipeIndex = Recipes.Add(MoveTemp(Recipe));
				RecipesByProduct.FindOrAdd(ResourceClass).Add(RecipeIndex);

				TSharedPtr<FSFPProductOption>& Option = ProductsByClass.FindOrAdd(ResourceClass);
				if (!Option.IsValid())
				{
					Option = MakeShared<FSFPProductOption>();
					Option->ItemClass = ResourceDescriptor;
					Option->ClassPath = ResourceClass->GetPathName();
					Option->DisplayName = Resource.DisplayName;
					Option->Form = Resource.Form;
					Option->SourceMount = SolverSourceMountFromPath(Option->ClassPath);
				}
				++Option->RecipeCount;
				Option->bHasAvailableRecipe |= Info.bAvailable;
			}
		}
	}
}

void FSFPPlannerSolver::BuildOptionalModularMinerCatalog(
	const TArray<TSubclassOf<UFGRecipe>>& AllRecipes,
	AFGRecipeManager* RecipeManager,
	TMap<UClass*, TSharedPtr<FSFPProductOption>>& ProductsByClass)
{
	auto TraceMinerObject = [this](const UObject* Object)
	{
		if (!bMinerDiagnostics || !IsValid(Object)) { return; }
		MinerDiagnostics.Add(FString::Printf(TEXT("OBJECT %s CLASS %s"),
			*Object->GetPathName(), *Object->GetClass()->GetPathName()));
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property->GetName().StartsWith(TEXT("m"))) { continue; }
			FString Value;
			Property->ExportTextItem_Direct(Value, Property->ContainerPtrToValuePtr<void>(Object),
				nullptr, const_cast<UObject*>(Object), PPF_None);
			MinerDiagnostics.Add(Property->GetName() + TEXT("=") + Value);
		}
	};

	// KAPI and KLib deliberately remain optional. All access below uses reflected
	// property names, so a Vanilla/SML game never has to load or link either module.
	TMap<UClass*, FOptionalBuildableInfo> Buildables;
	for (const TSubclassOf<UFGRecipe>& RecipeClass : AllRecipes)
	{
		UClass* BuildableClass = SafeBuildableClassFromRecipe(RecipeClass);
		if (!IsValid(BuildableClass))
		{
			continue;
		}

		FOptionalBuildableInfo& Info = Buildables.FindOrAdd(BuildableClass);
		if (!IsValid(Info.BuildableClass))
		{
			Info.BuildableClass = BuildableClass;
			Info.ClassPath = BuildableClass->GetPathName();
			Info.DisplayName = SolverBuildableDisplayName(
				Cast<AFGBuildable>(BuildableClass->GetDefaultObject()),
				BuildableClass);
			Info.PowerMW = OptionalBuildablePower(BuildableClass);
		}
		Info.bAvailable |= IsValid(RecipeManager) && RecipeManager->IsRecipeAvailable(RecipeClass);
	}

	TArray<FOptionalBuildableInfo> MinerBuildables;
	TArray<FOptionalModularMinerModule> Modules;
	for (const TPair<UClass*, FOptionalBuildableInfo>& Pair : Buildables)
	{
		UClass* BuildableClass = Pair.Key;
		if (IsClassOrParentNamed(BuildableClass, TEXT("KLMMBuildableMiner")))
		{
			MinerBuildables.Add(Pair.Value);
		}
		if (!IsClassOrParentNamed(BuildableClass, TEXT("KLMMBuildableModule")))
		{
			continue;
		}

		const UObject* ModuleCDO = BuildableClass->GetDefaultObject();
		FOptionalModularMinerModule Module;
		Module.BuildableClass = Pair.Value.BuildableClass;
		Module.ClassPath = Pair.Value.ClassPath;
		Module.DisplayName = Pair.Value.DisplayName;
		Module.bAvailable = Pair.Value.bAvailable;
		Module.PowerMW = Pair.Value.PowerMW;
		Module.AttachmentClass = ReflectedClassValue(
			ModuleCDO, BuildableClass, TEXT("mAttachmentClass"));
		Module.WasteClass = ReflectedClassValue(
			ModuleCDO, BuildableClass, TEXT("mWasteProductionClass"));
		Module.Tier = FMath::Max(1, FMath::RoundToInt(ReflectedNumberValue(
			ModuleCDO, BuildableClass, TEXT("mTier"), 1.0)));
		Module.Bonus = ReflectedNumberValue(ModuleCDO, BuildableClass, TEXT("mBonus"), 0.0);
		Module.Malus = ReflectedNumberValue(ModuleCDO, BuildableClass, TEXT("mMalus"), 0.0);
		if (IsValid(Module.AttachmentClass))
		{
			Modules.Add(MoveTemp(Module));
		}
	}

	if (bMinerDiagnostics)
	{
		MinerDiagnostics.Add(FString::Printf(TEXT("BUILDABLES miners=%d modules=%d"), MinerBuildables.Num(), Modules.Num()));
		for (const FOptionalBuildableInfo& Miner : MinerBuildables) { TraceMinerObject(Miner.BuildableClass->GetDefaultObject()); }
		for (const FOptionalModularMinerModule& Module : Modules) { TraceMinerObject(Module.BuildableClass->GetDefaultObject()); }
	}
	if (MinerBuildables.IsEmpty() || Modules.IsEmpty())
	{
		return;
	}
	MinerBuildables.Sort([](const FOptionalBuildableInfo& Left, const FOptionalBuildableInfo& Right)
	{
		return Left.ClassPath < Right.ClassPath;
	});
	Modules.Sort([](const FOptionalModularMinerModule& Left, const FOptionalModularMinerModule& Right)
	{
		return Left.ClassPath < Right.ClassPath;
	});

	FARFilter Filter;
	Filter.ClassPaths.Add(FTopLevelAssetPath(
		FName(TEXT("/Script/KAPI")),
		FName(TEXT("KAPIModularMinerDescription"))));
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> DescriptionAssets;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().GetAssets(Filter, DescriptionAssets);
	DescriptionAssets.Sort([](const FAssetData& Left, const FAssetData& Right)
	{
		return Left.PackageName.LexicalLess(Right.PackageName);
	});

	struct FDescriptionCandidate
	{
		UObject* Asset = nullptr;
		int32 Priority = MIN_int32;
	};
	TMap<UClass*, FDescriptionCandidate> DescriptionByResource;
	for (const FAssetData& AssetData : DescriptionAssets)
	{
		UObject* Description = AssetData.GetAsset();
		TraceMinerObject(Description);
		if (!IsValid(Description)
			|| ReflectedBoolValue(Description, Description->GetClass(), TEXT("mIsDisabled"), false))
		{
			if (bMinerDiagnostics) { MinerDiagnostics.Add(TEXT("DESCRIPTION_REJECTED invalid_or_disabled")); }
			continue;
		}
		UClass* ResourceClass = ReflectedClassValue(
			Description, Description->GetClass(), TEXT("mResourceClass"));
		if (!IsValid(ResourceClass) || !ResourceClass->IsChildOf(UFGItemDescriptor::StaticClass()))
		{
			continue;
		}
		const bool bFallbackDescription = ReflectedBoolValue(
			Description, Description->GetClass(), TEXT("mIsFallbackDescriptor"), false);
		const int32 Priority = (bFallbackDescription ? 0 : 100000)
			+ FMath::RoundToInt(ReflectedNumberValue(
				Description, Description->GetClass(), TEXT("mPriority"), 0.0));
		FDescriptionCandidate& Candidate = DescriptionByResource.FindOrAdd(ResourceClass);
		if (!IsValid(Candidate.Asset) || Priority > Candidate.Priority)
		{
			Candidate.Asset = Description;
			Candidate.Priority = Priority;
		}
	}

	auto ClassesMatch = [](UClass* Left, UClass* Right)
	{
		return IsValid(Left) && IsValid(Right)
			&& Left == Right;
	};
	auto ModuleMatchesSpecifier = [&ClassesMatch](
		const FOptionalModularMinerModule& Module,
		UClass* Specifier)
	{
		return ClassesMatch(Module.BuildableClass, Specifier)
			|| ClassesMatch(Module.AttachmentClass, Specifier)
			|| ClassesMatch(Module.WasteClass, Specifier);
	};

	int32 AddedRoutes = 0;
	struct FPurityVariant
	{
		const TCHAR* Label;
		double Multiplier;
	};
	const FPurityVariant Purities[] = {
		{TEXT("Unrein"), 0.5},
		{TEXT("Normal"), 1.0},
		{TEXT("Rein"), 2.0}
	};
	TArray<UClass*> ResourceClasses;
	DescriptionByResource.GetKeys(ResourceClasses);
	ResourceClasses.Sort([](const UClass& Left, const UClass& Right)
	{
		return Left.GetPathName() < Right.GetPathName();
	});
	for (UClass* ResourceClass : ResourceClasses)
	{
		const FDescriptionCandidate* DescriptionCandidate = DescriptionByResource.Find(ResourceClass);
		UObject* Description = DescriptionCandidate != nullptr ? DescriptionCandidate->Asset : nullptr;
		UClass* DescriptionClass = IsValid(Description) ? Description->GetClass() : nullptr;
		if (!IsValid(DescriptionClass))
		{
			continue;
		}

		if (bMinerDiagnostics)
		{
			MinerDiagnostics.Add(FString::Printf(TEXT("SELECTED resource=%s description=%s"),
				*ResourceClass->GetPathName(), *Description->GetPathName()));
		}
		TArray<UClass*> NeededModules;
		TArray<UClass*> PreventedModules;
		ReflectedClassArray(Description, DescriptionClass, TEXT("mNeededModules"), NeededModules);
		ReflectedClassArray(Description, DescriptionClass, TEXT("mPreventModules"), PreventedModules);
		const int32 NeededDrillTier = FMath::Max(1, FMath::RoundToInt(ReflectedNumberValue(
			Description, DescriptionClass, TEXT("mDrillTier"), 1.0)));

		// A route is a complete installed loadout, not one module pretending to
		// satisfy every required attachment. Resolve compatible slots first.
		struct FFluidChoice { UClass* Item = nullptr; double RawPerSecond = 0; double Bonus = 0; };
		TArray<FFluidChoice> Fluids;
		const FMapProperty* FluidMap = CastField<FMapProperty>(DescriptionClass->FindPropertyByName(TEXT("mFluidInfo")));
		const FObjectPropertyBase* FluidKey = FluidMap ? CastField<FObjectPropertyBase>(FluidMap->KeyProp) : nullptr;
		const FStructProperty* FluidValue = FluidMap ? CastField<FStructProperty>(FluidMap->ValueProp) : nullptr;
		if (FluidKey && FluidValue)
		{
			FScriptMapHelper Map(FluidMap, FluidMap->ContainerPtrToValuePtr<void>(Description));
			for (int32 I = 0; I < Map.GetMaxIndex(); ++I)
			{
				if (!Map.IsValidIndex(I)) continue;
				FFluidChoice Choice;
				Choice.Item = Cast<UClass>(FluidKey->GetObjectPropertyValue(Map.GetKeyPtr(I)));
				Choice.RawPerSecond = ReflectedNumberValue(Map.GetValuePtr(I), FluidValue->Struct, TEXT("mNormalFluidCountPerSecond"), 0);
				Choice.Bonus = ReflectedNumberValue(Map.GetValuePtr(I), FluidValue->Struct, TEXT("ProductionTimeMulti"), 0);
				if (IsValid(Choice.Item) && Choice.Item->IsChildOf(UFGItemDescriptor::StaticClass())
					&& Choice.RawPerSecond > 0 && FMath::IsFinite(Choice.RawPerSecond) && FMath::IsFinite(Choice.Bonus))
					Fluids.Add(Choice);
			}
		}
		Fluids.Sort([](const FFluidChoice& A, const FFluidChoice& B) { return A.Item->GetPathName() < B.Item->GetPathName(); });

		struct FOutputChoice { UClass* Item = nullptr; UClass* Trash = nullptr; UClass* Waste = nullptr; };
		TArray<FOutputChoice> Outputs;
		FOutputChoice RawOutput;
		RawOutput.Item = ResourceClass;
		Outputs.Add(RawOutput);
		const FMapProperty* OutputMap = CastField<FMapProperty>(DescriptionClass->FindPropertyByName(TEXT("mModuleInformation")));
		const FObjectPropertyBase* OutputKey = OutputMap ? CastField<FObjectPropertyBase>(OutputMap->KeyProp) : nullptr;
		const FStructProperty* OutputValue = OutputMap ? CastField<FStructProperty>(OutputMap->ValueProp) : nullptr;
		if (OutputKey && OutputValue)
		{
			FScriptMapHelper Map(OutputMap, OutputMap->ContainerPtrToValuePtr<void>(Description));
			for (int32 I = 0; I < Map.GetMaxIndex(); ++I)
			{
				if (!Map.IsValidIndex(I)) continue;
				FOutputChoice Choice;
				Choice.Waste = Cast<UClass>(OutputKey->GetObjectPropertyValue(Map.GetKeyPtr(I)));
				Choice.Item = ReflectedClassValue(Map.GetValuePtr(I), OutputValue->Struct, TEXT("mProductionItem"));
				Choice.Trash = ReflectedClassValue(Map.GetValuePtr(I), OutputValue->Struct, TEXT("mTrashItem"));
				if (IsValid(Choice.Waste) && IsValid(Choice.Item) && Choice.Item->IsChildOf(UFGItemDescriptor::StaticClass())
					&& !SFPIsUnsupportedMinerConversion(ResourceClass->GetPathName(), Choice.Item->GetPathName()))
					Outputs.Add(Choice);
			}
		}
		Outputs.Sort([](const FOutputChoice& A, const FOutputChoice& B)
		{
			if ((A.Waste == nullptr) != (B.Waste == nullptr)) return A.Waste == nullptr;
			return A.Item->GetPathName() < B.Item->GetPathName();
		});
		for (const FOptionalBuildableInfo& Miner : MinerBuildables)
		{
			const UObject* MinerCDO = Miner.BuildableClass->GetDefaultObject();
			UClass* DrillAttachment = ReflectedClassValue(MinerCDO, Miner.BuildableClass, TEXT("mDrillAttachmentClass"));
			UClass* WasteAttachment = ReflectedClassValue(MinerCDO, Miner.BuildableClass, TEXT("mWasteProducerAttachmentClass"));
			UClass* FluidAttachment = ReflectedClassValue(MinerCDO, Miner.BuildableClass, TEXT("mFluidAttachmentClass"));
			if (!IsValid(DrillAttachment)) continue;
			// Fluid and booster equipment is optional for both raw extraction and
			// processing routes. Processing-specific required modules are added per
			// output below; otherwise mNeededModules (for example the Smelter Module)
			// incorrectly removes every Mining-Head-only raw route.
			TArray<UClass*> OptionalExtraSlots;
			if (IsValid(FluidAttachment)) OptionalExtraSlots.AddUnique(FluidAttachment);
			UClass* PowerAttachment = ReflectedClassValue(MinerCDO, Miner.BuildableClass, TEXT("mPowerShardAttachmentClass"));
			if (IsValid(PowerAttachment)) OptionalExtraSlots.AddUnique(PowerAttachment);
			for (const FOutputChoice& Output : Outputs)
			{
				const bool bRawMiningOutput = !IsValid(Output.Waste)
					&& Output.Item == ResourceClass;
				TArray<UClass*> ExtraSlots = OptionalExtraSlots;
				if (!bRawMiningOutput)
				{
					for (UClass* Needed : NeededModules)
					{
						if (ClassesMatch(Needed, DrillAttachment)
							|| ClassesMatch(Needed, WasteAttachment)) continue;
						for (const FOptionalModularMinerModule& Module : Modules)
						{
							if (ModuleMatchesSpecifier(Module, Needed))
							{
								ExtraSlots.AddUnique(Module.AttachmentClass);
							}
						}
					}
				}
				ExtraSlots.Sort([](const UClass& A, const UClass& B)
				{
					return A.GetPathName() < B.GetPathName();
				});
				TArray<TArray<const FOptionalModularMinerModule*>> Extras;
				Extras.Add(TArray<const FOptionalModularMinerModule*>());
				for (UClass* Slot : ExtraSlots)
				{
					TArray<TArray<const FOptionalModularMinerModule*>> Next = Extras;
					for (const auto& Existing : Extras)
					{
						for (const FOptionalModularMinerModule& Module : Modules)
						{
							if (ClassesMatch(Module.AttachmentClass, Slot)
								&& !IsValid(Module.WasteClass))
							{
								auto Combination = Existing;
								Combination.Add(&Module);
								Next.Add(MoveTemp(Combination));
							}
						}
					}
					Extras = MoveTemp(Next);
				}
				for (const FOptionalModularMinerModule& Drill : Modules)
				{
					if (!ClassesMatch(Drill.AttachmentClass, DrillAttachment) || IsValid(Drill.WasteClass) || Drill.Tier < NeededDrillTier) continue;
					TArray<const FOptionalModularMinerModule*> WasteOptions;
					if (!Output.Waste) WasteOptions.Add(nullptr);
					else for (const FOptionalModularMinerModule& Module : Modules)
						if (ClassesMatch(Module.AttachmentClass, WasteAttachment) && ClassesMatch(Module.WasteClass, Output.Waste)) WasteOptions.Add(&Module);
					for (const FOptionalModularMinerModule* Waste : WasteOptions)
					for (const auto& Extra : Extras)
					{
						TArray<const FOptionalModularMinerModule*> Loadout = Extra;
						Loadout.Add(&Drill);
						if (Waste) Loadout.Add(Waste);
						bool bValid = true;
						for (int32 A = 0; A < Loadout.Num(); ++A)
						for (int32 B = A + 1; B < Loadout.Num(); ++B)
							if (ClassesMatch(Loadout[A]->AttachmentClass, Loadout[B]->AttachmentClass)) bValid = false;
						if (SFPRequiredModulesApplyToMinerOutput(bRawMiningOutput)
							&& !SFPMinerRates::RequiredPresent(Loadout, NeededModules,
							[&](const FOptionalModularMinerModule* M, UClass* Needed) { return ModuleMatchesSpecifier(*M, Needed); })) bValid = false;
						for (UClass* Forbidden : PreventedModules)
							if (Loadout.ContainsByPredicate([&](const FOptionalModularMinerModule* M) { return ModuleMatchesSpecifier(*M, Forbidden); })) bValid = false;
						if (!bValid) continue;
						const bool bFluid = Loadout.ContainsByPredicate([&](const FOptionalModularMinerModule* M) { return ClassesMatch(M->AttachmentClass, FluidAttachment); });
						TArray<FFluidChoice> Choices = bFluid ? Fluids : TArray<FFluidChoice>();
						if (!bFluid) Choices.Add(FFluidChoice());
						for (const FFluidChoice& Fluid : Choices)
						for (const FPurityVariant& Purity : Purities)
						{
							double Bonus = Fluid.Bonus, MalusMultiplier = 1.0, Power = Miner.PowerMW;
							bool bAvailable = Miner.bAvailable;
							TArray<FString> ModulePaths, ModuleNames;
							for (const FOptionalModularMinerModule* Module : Loadout)
							{
								Bonus += Module->Bonus;
								if (IsValid(Module->WasteClass)) { if (Module->Malus > 0) MalusMultiplier *= Module->Malus; }
								else Bonus -= Module->Malus;
								Power += Module->PowerMW;
								bAvailable &= Module->bAvailable;
								ModulePaths.Add(Module->ClassPath);
								ModuleNames.Add(Module->DisplayName);
							}
						const double Cycle = SFPMinerRates::Cycle(Purity.Multiplier, Bonus, MalusMultiplier);
						const double Amount = static_cast<double>(FMath::Max(1, FMath::FloorToInt(ReflectedNumberValue(MinerCDO, Miner.BuildableClass, TEXT("mItemsPerCycle"), 1) * ModularMinerTierMultiplier(Drill.Tier))));
						const double Rate = SFPMinerRates::Output(Amount, Cycle, ModularMinerBeltOutputs);
						FSFPPlannerRecipe Recipe;
						Recipe.ClassPath = Output.Waste
							? FString::Printf(TEXT("KAPI.ModularMiner|%s|%s|%s|%s|%s"), *Description->GetPathName(), *Miner.ClassPath, *Drill.ClassPath, *Waste->ClassPath, Purity.Label)
							: FString::Printf(TEXT("KAPI.ModularMinerRaw|%s|%s|%s|%s"), *Description->GetPathName(), *Miner.ClassPath, *Drill.ClassPath, Purity.Label);
						for (const auto* Module : Extra) Recipe.ClassPath += TEXT("|") + Module->ClassPath;
						if (Fluid.Item) Recipe.ClassPath += TEXT("|") + Fluid.Item->GetPathName();
						Recipe.SourceMount = TEXT("KLib");
						Recipe.bAvailable = bAvailable;
						Recipe.DurationSeconds = Cycle;
						Recipe.MachineClass = Miner.BuildableClass;
						Recipe.MachineName = Miner.DisplayName;
						Recipe.BasePowerMW = Power;
						Recipe.PowerExponent = FMath::Max(0.001, ReflectedNumberValue(
							MinerCDO, Miner.BuildableClass, TEXT("mPowerConsumptionExponent"), 1.0));
						Recipe.MachineConfig = ReadMachineRuntimeConfig(MinerCDO, Miner.BuildableClass);
						Recipe.AdditionalBuildableClassPaths = ModulePaths;
						Recipe.bDirectResourceExtraction = true;
						Recipe.ResourceNodeLabel = Purity.Label;
						Recipe.SourceName = ItemDisplayName(ResourceClass);
						Recipe.ModulesLabel = FString::Join(ModuleNames, TEXT(" + "));
						Recipe.FluidLabel = Fluid.Item ? ItemDisplayName(Fluid.Item) : TEXT("Keine");
						Recipe.DisplayName = FString::Printf(TEXT("Abbau: %s (%s, %s, %s)"), *ItemDisplayName(Output.Item), Purity.Label, *Recipe.ModulesLabel, *Recipe.FluidLabel);
						auto ItemRate = [](UClass* Class, double Value)
						{
							FSFPPlannerItemRate R;
							R.ItemClass = TSubclassOf<UFGItemDescriptor>(Class); R.ClassPath = Class->GetPathName();
							R.DisplayName = ItemDisplayName(Class);
							R.Form = SolverResourceFormToString(UFGItemDescriptor::GetForm(TSubclassOf<UFGItemDescriptor>(Class)));
							R.RatePerMinute = Value; return R;
						};
						Recipe.Ingredients.Add(ItemRate(ResourceClass, Rate));
						if (Fluid.Item)
						{
							// KLib's fluid task runs once per second at its own potential,
							// independent of the miner cycle. Raw fluid inventory units are litres.
							const double FluidRate = SFPMinerRates::OperatingFluid(Fluid.RawPerSecond, Purity.Multiplier);
							Recipe.Ingredients.Add(ItemRate(Fluid.Item, FluidRate));
							Recipe.FluidRatePerMinute = FluidRate;
						}
						Recipe.Products.Add(ItemRate(Output.Item, Rate));
						if (IsValid(Output.Trash) && Output.Trash->IsChildOf(UFGItemDescriptor::StaticClass()))
							Recipe.Products.Add(ItemRate(Output.Trash, SFPMinerRates::Waste(Amount, Cycle)));
						Recipe.ConfigurationDetail = FString::Printf(TEXT("%s | %s | %s | %s | %s: %s m³/min"),
							*Recipe.SourceName, Purity.Label, *Recipe.ModulesLabel, *Miner.DisplayName, *Recipe.FluidLabel,
							*FSFPNumberFormatting::Decimal(Recipe.FluidRatePerMinute));
						if (bMinerDiagnostics) MinerDiagnostics.Add(FString::Printf(TEXT("ROUTE_ADDED %s available=%d rate=%f fluid=%f"), *Recipe.ClassPath, bAvailable ? 1 : 0, Rate, Recipe.FluidRatePerMinute));
						const int32 Index = Recipes.Add(MoveTemp(Recipe));
						// Index coproducts too; the existing primary-product policy still applies.
						for (const FSFPPlannerItemRate& Product : Recipes[Index].Products)
						{
							RecipesByProduct.FindOrAdd(Product.ItemClass.Get()).Add(Index);
							TSharedPtr<FSFPProductOption>& Option = ProductsByClass.FindOrAdd(Product.ItemClass.Get());
							if (!Option.IsValid())
							{
								Option = MakeShared<FSFPProductOption>();
								Option->ItemClass = Product.ItemClass; Option->ClassPath = Product.ClassPath;
								Option->DisplayName = Product.DisplayName; Option->Form = Product.Form;
								Option->SourceMount = SolverSourceMountFromPath(Product.ClassPath);
							}
							++Option->RecipeCount;
							Option->bHasAvailableRecipe |= bAvailable;
						}
						++AddedRoutes;
						}
					}
				}
			}
		}
	}

	if (AddedRoutes > 0)
	{
		UE_LOG(LogSFPFactoryPlanner, Display,
			TEXT("Added %d optional KAPI modular-miner extraction/direct-production routes"),
			AddedRoutes);
	}
}

void FSFPPlannerSolver::GetRecipeOptionsForItemPath(
	const FString& ItemClassPath,
	const bool bOnlyAvailableRecipes,
	TArray<TSharedPtr<FSFPRecipeOption>>& OutOptions) const
{
	OutOptions.Reset();
	UClass* ItemClass = nullptr;
	for (const TSharedPtr<FSFPProductOption>& Product : Products)
	{
		if (Product.IsValid() && Product->ClassPath == ItemClassPath)
		{
			ItemClass = Product->ItemClass.Get();
			break;
		}
	}
	const TArray<int32>* CandidateIndices = RecipesByProduct.Find(ItemClass);
	if (CandidateIndices == nullptr)
	{
		return;
	}
	// The first runtime output is the recipe's primary product. Do not offer
	// unrelated factories solely for a secondary output when a direct source exists.
	const bool bHasPrimarySource = CandidateIndices->ContainsByPredicate(
		[this, ItemClass, bOnlyAvailableRecipes](const int32 Index)
		{
			return Recipes.IsValidIndex(Index)
				&& (!bOnlyAvailableRecipes || Recipes[Index].bAvailable)
				&& !Recipes[Index].Products.IsEmpty()
				&& Recipes[Index].Products[0].ItemClass.Get() == ItemClass;
		});
	for (const int32 RecipeIndex : *CandidateIndices)
	{
		if (!Recipes.IsValidIndex(RecipeIndex))
		{
			continue;
		}
		const FSFPPlannerRecipe& Recipe = Recipes[RecipeIndex];
		if (bHasPrimarySource && (Recipe.Products.IsEmpty()
			|| Recipe.Products[0].ItemClass.Get() != ItemClass))
		{
			continue;
		}
		if (bOnlyAvailableRecipes && !Recipe.bAvailable)
		{
			continue;
		}
		TSharedPtr<FSFPRecipeOption> Option = MakeShared<FSFPRecipeOption>();
		Option->ItemClassPath = ItemClassPath;
		Option->RecipeClassPath = Recipe.ClassPath;
		Option->DisplayName = Recipe.DisplayName;
		Option->MachineName = Recipe.MachineName;
		Option->MachineClassPath = IsValid(Recipe.MachineClass) ? Recipe.MachineClass->GetPathName() : FString();
		Option->MachineConfig = Recipe.MachineConfig;
		Option->bFuelPowered = Recipe.bFuelPowered;
		Option->FuelOptions = Recipe.FuelOptions;
		Option->SourceMount = Recipe.SourceMount;
		Option->bAvailable = Recipe.bAvailable;
		Option->Category = Recipe.bDirectResourceExtraction ? TEXT("Direktabbau / Förderung")
			: (Recipe.ClassPath.Contains(TEXT("/Converter/")) || (Recipe.MachineClass && Recipe.MachineClass->GetPathName().Contains(TEXT("/Converter/"))))
			? TEXT("Umwandlung")
			: Recipe.bAlternateRecipe
			? TEXT("Alternative Rezepte") : TEXT("Standardrezepte");
		Option->SourceName = !Recipe.SourceName.IsEmpty() ? Recipe.SourceName
			: Recipe.bDirectResourceExtraction && !Recipe.Ingredients.IsEmpty() ? Recipe.Ingredients[0].DisplayName : FString();
		Option->Purity = Recipe.ResourceNodeLabel;
		Option->ModulesLabel = Recipe.ModulesLabel;
		Option->FluidLabel = Recipe.FluidLabel;
		Option->ConfigurationDetail = Recipe.ConfigurationDetail;
        Option->SourceItemClassPath = Recipe.bDirectResourceExtraction && !Recipe.Ingredients.IsEmpty() ? Recipe.Ingredients[0].ClassPath : FString();
		Option->bProcessesResource = Recipe.bDirectResourceExtraction
			&& !Recipe.Ingredients.IsEmpty()
			&& Recipe.Ingredients[0].ClassPath != ItemClassPath;
		OutOptions.Add(MoveTemp(Option));
	}
	OutOptions.Sort([](const TSharedPtr<FSFPRecipeOption>& Left, const TSharedPtr<FSFPRecipeOption>& Right)
	{
		if (!Left.IsValid() || !Right.IsValid())
		{
			return Left.IsValid();
		}
		if (Left->bAvailable != Right->bAvailable)
		{
			return Left->bAvailable;
		}
		auto CategoryRank = [](const FString& Category)
		{
			if (Category == TEXT("Direktabbau / Förderung")) return 0;
			if (Category == TEXT("Standardrezepte")) return 1;
			if (Category == TEXT("Alternative Rezepte")) return 2;
			if (Category == TEXT("Umwandlung")) return 3;
			return 4;
		};
		const int32 LeftRank = CategoryRank(Left->Category);
		const int32 RightRank = CategoryRank(Right->Category);
		if (LeftRank != RightRank)
		{
			return LeftRank < RightRank;
		}
		return Left->DisplayName.Compare(Right->DisplayName, ESearchCase::IgnoreCase) < 0;
	});
}

void FSFPPlannerSolver::BuildConstructionCostCatalog(const TArray<TSubclassOf<UFGRecipe>>& AllRecipes)
{
	for (const TSubclassOf<UFGRecipe>& RecipeClass : AllRecipes)
	{
		UClass* RawRecipeClass = RecipeClass.Get();
		const UFGRecipe* RecipeCDO = IsValid(RawRecipeClass)
			? Cast<UFGRecipe>(RawRecipeClass->GetDefaultObject())
			: nullptr;
		if (!IsValid(RecipeCDO) || RecipeCDO->GetIngredients().IsEmpty())
		{
			continue;
		}

		const bool bBuildRecipe = RecipeCDO->GetProducts().ContainsByPredicate([](const FItemAmount& Product)
		{
			UClass* ProductClass = Product.ItemClass.Get();
			return IsValid(ProductClass) && ProductClass->IsChildOf(UFGBuildingDescriptor::StaticClass());
		});
		if (!bBuildRecipe)
		{
			continue;
		}

		UClass* BuildableClass = SafeBuildableClassFromRecipe(RecipeClass);
		if (!IsValid(BuildableClass))
		{
			continue;
		}
		const FString MachinePath = BuildableClass->GetPathName();
		if (ConstructionCostsByMachinePath.Contains(MachinePath))
		{
			continue;
		}

		TArray<FSFPConstructionCost> Costs;
		for (const FItemAmount& Ingredient : RecipeCDO->GetIngredients())
		{
			UClass* ItemClass = Ingredient.ItemClass.Get();
			if (!IsValid(ItemClass) || !ItemClass->IsChildOf(UFGItemDescriptor::StaticClass()))
			{
				continue;
			}

			FSFPConstructionCost Cost;
			Cost.ItemClassPath = ItemClass->GetPathName();
			Cost.DisplayName = ItemDisplayName(ItemClass);
			Cost.Amount = NormalizeAmount(Ingredient, Cost.Form);
			if (Cost.Amount > 0.0)
			{
				Costs.Add(MoveTemp(Cost));
			}
		}
		if (!Costs.IsEmpty())
		{
			ConstructionCostsByMachinePath.Add(MachinePath, MoveTemp(Costs));
		}

		const FString BuildableName = SolverBuildableDisplayName(
			Cast<AFGBuildable>(BuildableClass->GetDefaultObject()),
			BuildableClass);
		const FString SearchText = (MachinePath + TEXT("|") + BuildableName).ToLower();
		const bool bSpecialVariant = SearchText.Contains(TEXT("smart"))
			|| SearchText.Contains(TEXT("programm"));
		if (SearchText.Contains(TEXT("splitter")) && (SplitterClassPath.IsEmpty() || !bSpecialVariant))
		{
			SplitterClassPath = MachinePath;
			SplitterDisplayName = BuildableName;
		}
		if (SearchText.Contains(TEXT("merger")) && (MergerClassPath.IsEmpty() || !bSpecialVariant))
		{
			MergerClassPath = MachinePath;
			MergerDisplayName = BuildableName;
		}
		if ((SearchText.Contains(TEXT("pipelinejunction")) || SearchText.Contains(TEXT("pipejunction")))
			&& PipeJunctionClassPath.IsEmpty())
		{
			PipeJunctionClassPath = MachinePath;
			PipeJunctionDisplayName = BuildableName;
		}
	}
}

void FSFPPlannerSolver::AddConstructionCosts(FSFPPlanResult& Result) const
{
	TMap<FString, FSFPConstructionCost> MachineTotals;
	TMap<FString, FSFPConstructionCost> InfrastructureTotals;
	auto AddScaledCosts = [this](
		TMap<FString, FSFPConstructionCost>& Totals,
		const FString& BuildableClassPath,
		const double Multiplier,
		const FString& MissingLabel,
		TArray<FString>& Warnings)
	{
		if (BuildableClassPath.IsEmpty() || Multiplier <= 0.0)
		{
			return;
		}
		const TArray<FSFPConstructionCost>* UnitCosts = ConstructionCostsByMachinePath.Find(BuildableClassPath);
		if (UnitCosts == nullptr)
		{
			Warnings.AddUnique(FString::Printf(TEXT("Baukosten für %s konnten nicht ermittelt werden"), *MissingLabel));
			return;
		}
		for (const FSFPConstructionCost& UnitCost : *UnitCosts)
		{
			FSFPConstructionCost& Total = Totals.FindOrAdd(UnitCost.ItemClassPath);
			if (Total.ItemClassPath.IsEmpty())
			{
				Total = UnitCost;
				Total.Amount = 0.0;
			}
			Total.Amount += UnitCost.Amount * Multiplier;
		}
	};

	for (const FSFPPlanNode& Node : Result.Nodes)
	{
		if ((Node.Type == ESFPPlanNodeType::Machine || Node.Type == ESFPPlanNodeType::Generator)
			&& Node.MachineCount > 0.0)
		{
			const double BuiltMachineCount = static_cast<double>(
				Node.BuiltMachineCount > 0
					? Node.BuiltMachineCount
					: FMath::Max(1, FMath::CeilToInt(Node.MachineCount)));
			AddScaledCosts(
				MachineTotals,
				Node.ClassPath,
				BuiltMachineCount,
				Node.Title,
				Result.Warnings);
			for (const FString& AttachmentClassPath : Node.AdditionalBuildableClassPaths)
			{
				AddScaledCosts(
					MachineTotals,
					AttachmentClassPath,
					BuiltMachineCount,
					FString::Printf(TEXT("%s-Modul"), *Node.Title),
					Result.Warnings);
			}
		}
		else if ((Node.Type == ESFPPlanNodeType::Splitter || Node.Type == ESFPPlanNodeType::Merger)
			&& Node.InfrastructureCount > 0)
		{
			AddScaledCosts(
				InfrastructureTotals,
				Node.ClassPath,
				static_cast<double>(Node.InfrastructureCount),
				Node.Title,
				Result.Warnings);
		}
 	}

	for (const FSFPPlanEdge& Edge : Result.Edges)
	{
		if (Edge.RequiredLines <= 0 || Edge.EstimatedLengthMeters <= 0.0 || Edge.TransportClassPath.IsEmpty())
		{
			continue;
		}
		double UnitLength = 1.0;
		for (const FSFPTransportTier& Tier : TransportTiers)
		{
			if (Tier.ClassPath == Edge.TransportClassPath)
			{
				UnitLength = FMath::Max(0.1, Tier.CostUnitLengthMeters);
				break;
			}
		}
		const int32 CostUnitsPerLine = FMath::Max(1, FMath::CeilToInt(Edge.EstimatedLengthMeters / UnitLength));
		AddScaledCosts(
			InfrastructureTotals,
			Edge.TransportClassPath,
			static_cast<double>(CostUnitsPerLine * Edge.RequiredLines),
			Edge.TransportLabel,
			Result.Warnings);
	}

	auto SortCosts = [](TArray<FSFPConstructionCost>& Costs)
	{
		Costs.Sort([](const FSFPConstructionCost& Left, const FSFPConstructionCost& Right)
		{
			return Left.DisplayName.Compare(Right.DisplayName, ESearchCase::IgnoreCase) < 0;
		});
	};
	MachineTotals.GenerateValueArray(Result.MachineConstructionCosts);
	InfrastructureTotals.GenerateValueArray(Result.InfrastructureConstructionCosts);
	SortCosts(Result.MachineConstructionCosts);
	SortCosts(Result.InfrastructureConstructionCosts);

	TMap<FString, FSFPConstructionCost> GrandTotals = MachineTotals;
	for (const TPair<FString, FSFPConstructionCost>& Pair : InfrastructureTotals)
	{
		FSFPConstructionCost& Total = GrandTotals.FindOrAdd(Pair.Key);
		if (Total.ItemClassPath.IsEmpty())
		{
			Total = Pair.Value;
			Total.Amount = 0.0;
		}
		Total.Amount += Pair.Value.Amount;
	}
	GrandTotals.GenerateValueArray(Result.ConstructionCosts);
	SortCosts(Result.ConstructionCosts);
}

void FSFPPlannerSolver::BuildTransportCatalog(
	const TArray<TSubclassOf<UFGRecipe>>& AllRecipes,
	AFGRecipeManager* RecipeManager)
{
	TMap<UClass*, int32> TransportIndexByBuildable;
	for (const TSubclassOf<UFGRecipe>& RecipeClass : AllRecipes)
	{
		UClass* RawRecipeClass = RecipeClass.Get();
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

		UClass* RawBuildableClass = SafeBuildableClassFromRecipe(RecipeClass);
		if (!IsValid(RawBuildableClass))
		{
			continue;
		}
		if (const int32* ExistingIndex = TransportIndexByBuildable.Find(RawBuildableClass))
		{
			if (TransportTiers.IsValidIndex(*ExistingIndex) && IsValid(RecipeManager))
			{
				TransportTiers[*ExistingIndex].bAvailable |= RecipeManager->IsRecipeAvailable(RecipeClass);
			}
			continue;
		}

		const AFGBuildable* BuildableCDO = Cast<AFGBuildable>(RawBuildableClass->GetDefaultObject());
		if (!IsValid(BuildableCDO))
		{
			continue;
		}

		FSFPTransportTier Tier;
		Tier.ClassPath = RawBuildableClass->GetPathName();
		Tier.DisplayName = SolverBuildableDisplayName(BuildableCDO, RawBuildableClass);
		Tier.bAvailable = IsValid(RecipeManager) && RecipeManager->IsRecipeAvailable(RecipeClass);

		if (const AFGBuildableConveyorLift* Lift = Cast<AFGBuildableConveyorLift>(BuildableCDO))
		{
			Tier.Kind = TEXT("lift");
			Tier.CostUnitLengthMeters = 2.0;
			Tier.CapacityPerMinute = static_cast<double>(Lift->GetSpeed())
				* 60.0 / static_cast<double>(AFGBuildableConveyorBase::ITEM_SPACING);
		}
		else if (const AFGBuildableConveyorBase* Belt = Cast<AFGBuildableConveyorBase>(BuildableCDO))
		{
			Tier.Kind = TEXT("belt");
			Tier.CostUnitLengthMeters = 2.0;
			// Belt speed is centimetres per second; use the game's authoritative item spacing.
			Tier.CapacityPerMinute = static_cast<double>(Belt->GetSpeed())
				* 60.0 / static_cast<double>(AFGBuildableConveyorBase::ITEM_SPACING);
		}
		else if (const AFGBuildablePipeline* Pipe = Cast<AFGBuildablePipeline>(BuildableCDO))
		{
			Tier.Kind = TEXT("pipe");
			Tier.CostUnitLengthMeters = 1.0;
			// Pipeline flow limit is cubic metres per second.
			Tier.CapacityPerMinute = static_cast<double>(Pipe->GetFlowLimit()) * 60.0;
		}
		else
		{
			continue;
		}

		if (Tier.CapacityPerMinute > 0.0)
		{
			TransportIndexByBuildable.Add(RawBuildableClass, TransportTiers.Add(MoveTemp(Tier)));
		}
	}

	TransportTiers.Sort([](const FSFPTransportTier& Left, const FSFPTransportTier& Right)
	{
		if (Left.Kind != Right.Kind)
		{
			return Left.Kind < Right.Kind;
		}
		if (!FMath::IsNearlyEqual(Left.CapacityPerMinute, Right.CapacityPerMinute))
		{
			return Left.CapacityPerMinute < Right.CapacityPerMinute;
		}
		return Left.DisplayName < Right.DisplayName;
	});
}

int32 FSFPPlannerSolver::FindRecipeFor(
	UClass* ItemClass,
	const bool bOnlyAvailableRecipes,
	const TSet<UClass*>* ForbiddenIngredients,
	const FString* PreferredRecipePath,
	int32& OutCandidateCount, const TMap<FString, double>* SuppliedInputs, const TSet<FString>* InputReachablePaths, const TMap<FString, int32>* InputDistanceByPath) const
{
	OutCandidateCount = 0;
	const TArray<int32>* CandidateIndices = RecipesByProduct.Find(ItemClass);
	if (CandidateIndices == nullptr)
	{
		return INDEX_NONE;
	}

	int32 BestIndex = INDEX_NONE;
	int32 PreferredIndex = INDEX_NONE;
	int32 BestScore = MIN_int32;
	// Apply the same primary-output policy as the selector, but only consider
	// sources that are viable in the current recursion branch.
	const bool bHasPrimarySource = CandidateIndices->ContainsByPredicate(
		[this, ItemClass, bOnlyAvailableRecipes, ForbiddenIngredients, PreferredRecipePath, SuppliedInputs](const int32 Index)
		{
			if (!Recipes.IsValidIndex(Index)) return false;
			const FSFPPlannerRecipe& Recipe = Recipes[Index];
			const bool bExplicitlyPreferred = PreferredRecipePath != nullptr && Recipe.ClassPath == *PreferredRecipePath;
			if (!bExplicitlyPreferred && IsAutomaticUnpackagingRoute(Recipe, ItemClass)) return false;
            if (SuppliedInputs && Recipe.bDirectResourceExtraction && !Recipe.Ingredients.IsEmpty()
                && SuppliedInputs->Contains(Recipe.Ingredients[0].ClassPath)) return false;
			if ((bOnlyAvailableRecipes && !Recipe.bAvailable)
				|| Recipe.Products.IsEmpty()
				|| Recipe.Products[0].ItemClass.Get() != ItemClass) return false;
			const bool bSelfExtraction = Recipe.bDirectResourceExtraction
				&& Recipe.Ingredients.Num() == 1
				&& Recipe.Ingredients[0].ItemClass.Get() == ItemClass;
			return bSelfExtraction || ForbiddenIngredients == nullptr
				|| !Recipe.Ingredients.ContainsByPredicate(
					[ForbiddenIngredients, &Recipe](const FSFPPlannerItemRate& Ingredient)
					{
						return !(Recipe.bDirectResourceExtraction && &Ingredient == &Recipe.Ingredients[0])
						&& ForbiddenIngredients->Contains(Ingredient.ItemClass.Get());
					});
		});
	for (const int32 Index : *CandidateIndices)
	{
		if (!Recipes.IsValidIndex(Index))
		{
			continue;
		}

		const FSFPPlannerRecipe& Recipe = Recipes[Index];
		const bool bExplicitlyPreferred = PreferredRecipePath != nullptr && Recipe.ClassPath == *PreferredRecipePath;
		if (!bExplicitlyPreferred && IsAutomaticUnpackagingRoute(Recipe, ItemClass))
		{
			continue;
		}
            if (SuppliedInputs && Recipe.bDirectResourceExtraction && !Recipe.Ingredients.IsEmpty()
                && SuppliedInputs->Contains(Recipe.Ingredients[0].ClassPath)) continue;
		if (bHasPrimarySource && (Recipe.Products.IsEmpty()
			|| Recipe.Products[0].ItemClass.Get() != ItemClass))
		{
			continue;
		}
		if (bOnlyAvailableRecipes && !Recipe.bAvailable)
		{
			continue;
		}
		const bool bContainsForbiddenIngredient = ForbiddenIngredients != nullptr
			&& Recipe.Ingredients.ContainsByPredicate(
				[ForbiddenIngredients, &Recipe](const FSFPPlannerItemRate& Ingredient)
				{
					return !(Recipe.bDirectResourceExtraction && &Ingredient == &Recipe.Ingredients[0])
						&& ForbiddenIngredients->Contains(Ingredient.ItemClass.Get());
				});
		const bool bDirectSelfExtraction = Recipe.bDirectResourceExtraction
			&& Recipe.Ingredients.Num() == 1
			&& Recipe.Ingredients[0].ItemClass.Get() == ItemClass;
		if (bContainsForbiddenIngredient && !bDirectSelfExtraction)
		{
			continue;
		}

		++OutCandidateCount;
		if (PreferredRecipePath != nullptr && Recipe.ClassPath == *PreferredRecipePath)
		{
			PreferredIndex = Index;
		}
		int32 Score = 0;
        int32 BestInputDistance = MAX_int32;
        if (InputDistanceByPath)
        {
            for (const FSFPPlannerItemRate& Ingredient : Recipe.Ingredients)
            {
                if (const int32* Distance = InputDistanceByPath->Find(Ingredient.ClassPath))
                {
                    BestInputDistance = FMath::Min(BestInputDistance, *Distance);
                }
            }
        }
        if (BestInputDistance != MAX_int32)
        {
            const int32* ProductDistance = InputDistanceByPath
                ? InputDistanceByPath->Find(ItemClass->GetPathName())
                : nullptr;
            // A recipe on the shortest path back to a supplied input must beat
            // generic SatisfactoryPlus/alternate scoring. This prevents the
            // automatic selector from wandering into an external-source branch
            // even though the user supplied the raw material that should cap it.
            if (ProductDistance && *ProductDistance > 0 && BestInputDistance + 1 == *ProductDistance)
            {
                Score += 100000 - FMath::Min(BestInputDistance, 100) * 100;
            }
            else
            {
                Score += 5000;
            }
        }
        else if (InputReachablePaths && Recipe.Ingredients.ContainsByPredicate([&](const FSFPPlannerItemRate& I) { return InputReachablePaths->Contains(I.ClassPath); }))
        {
            Score += 5000;
        }
		Score += Recipe.SourceMount.Equals(TEXT("SatisfactoryPlus"), ESearchCase::IgnoreCase) ? 1000 : 0;
		Score += Recipe.bAvailable ? 200 : 0;
		Score += Recipe.ClassPath.Contains(TEXT("Alternate"), ESearchCase::IgnoreCase) ? 0 : 50;
		Score += Recipe.bDirectResourceExtraction
			&& Recipe.ResourceNodeLabel.Equals(TEXT("Normal"), ESearchCase::IgnoreCase)
			? 20
			: 0;
		// Prefer extraction only when it really produces the resource itself.
		// A modular miner that turns an ore into a processed item stays available
		// as an explicit choice, but must not outrank that item's normal recipe.
		Score += bDirectSelfExtraction ? 2000 : 0;
		Score -= Recipe.AdditionalBuildableClassPaths.Num() * 10;
		Score -= Recipe.Ingredients.Num() * 3;
		Score -= FMath::Max(0, Recipe.Products.Num() - 1);

		if (Score > BestScore)
		{
			BestScore = Score;
			BestIndex = Index;
		}
	}
	return PreferredIndex != INDEX_NONE ? PreferredIndex : BestIndex;
}

int32 FSFPPlannerSolver::BuildDemand(
	UClass* ItemClass,
	const FString& ItemName,
	const FString& Form,
	const double RequiredRate,
	const int32 ConsumerNodeId,
	const int32 Depth,
	FSolveContext& Context) const
{
	if (!IsValid(ItemClass) || RequiredRate <= KINDA_SMALL_NUMBER)
	{
		return INDEX_NONE;
	}
	if (++Context.ExpansionCount > MaxPlannerExpansions)
	{
		Context.Result.Warnings.AddUnique(FString::Printf(
			TEXT("Plan bei %d Rechenschritten begrenzt"),
			MaxPlannerExpansions));
		return INDEX_NONE;
	}
	if (Context.Result.Nodes.Num() >= MaxPlannerNodes)
	{
		Context.Result.Warnings.AddUnique(FString::Printf(
			TEXT("Plan bei %d Knoten begrenzt; Zielmenge oder Rezeptwahl verkleinern"),
			MaxPlannerNodes));
		return INDEX_NONE;
	}

	Context.Result.MaxDepth = FMath::Max(Context.Result.MaxDepth, Depth);
	auto AddOrMergeEdge = [&](const int32 SourceNodeId, const int32 TargetNodeId, const FString& EdgeItemName,
		const FString& EdgeItemPath, const FString& EdgeForm, const double EdgeRate, const bool bDirectResourceLink)
	{
		if (SourceNodeId == INDEX_NONE || TargetNodeId == INDEX_NONE)
		{
			return;
		}
		for (FSFPPlanEdge& Existing : Context.Result.Edges)
		{
			if (Existing.SourceNodeId == SourceNodeId
				&& Existing.TargetNodeId == TargetNodeId
				&& Existing.ItemClassPath == EdgeItemPath)
			{
				Existing.RatePerMinute += EdgeRate;
				if (!bDirectResourceLink)
				{
					AddTransportAdvice(Existing, Context.Result);
				}
				return;
			}
		}
		FSFPPlanEdge Edge;
		Edge.SourceNodeId = SourceNodeId;
		Edge.TargetNodeId = TargetNodeId;
		Edge.ItemName = EdgeItemName;
		Edge.ItemClassPath = EdgeItemPath;
		Edge.Form = EdgeForm;
		Edge.RatePerMinute = EdgeRate;
		if (bDirectResourceLink)
		{
			Edge.bLocalRoutingLink = true;
			Edge.TransportKind = TEXT("resource_node");
			Edge.TransportLabel = TEXT("Direkt am Rohstoffknoten");
		}
		else
		{
			AddTransportAdvice(Edge, Context.Result);
		}
		Context.Result.Edges.Add(MoveTemp(Edge));
	};
	auto AddEdgeToConsumer = [&](const int32 SourceNodeId)
	{
		AddOrMergeEdge(
			SourceNodeId,
			ConsumerNodeId,
			ItemName,
			ItemClass->GetPathName(),
			Form,
			RequiredRate,
			false);
	};

	if (Context.Result.SuppliedInputRates.Contains(ItemClass->GetPathName()))
	{
		int32 Id;
		if (const int32* Existing = Context.ProviderNodeByItem.Find(ItemClass))
		{
			Id = *Existing;
			Context.Result.Nodes[Id].RatePerMinute += RequiredRate;
		}
		else
		{
			FSFPPlanNode Node;
			Node.Id = Context.Result.Nodes.Num(); Node.Type = ESFPPlanNodeType::Source;
			Node.ClassPath = ItemClass->GetPathName(); Node.Title = ItemName;
			Node.Depth = Depth; Node.RatePerMinute = RequiredRate;
			Id = Context.Result.Nodes.Add(MoveTemp(Node));
			Context.ProviderNodeByItem.Add(ItemClass, Id);
		}
		AddEdgeToConsumer(Id);
		return Id;
	}

	if (Depth > MaxPlannerDepth || Context.RecursionStack.Contains(ItemClass))
	{
		int32 NodeId = INDEX_NONE;
		if (const int32* ExistingNodeId = Context.CycleNodeByItem.Find(ItemClass))
		{
			NodeId = *ExistingNodeId;
			FSFPPlanNode& ExistingNode = Context.Result.Nodes[NodeId];
			ExistingNode.RatePerMinute += RequiredRate;
			ExistingNode.Depth = FMath::Max(ExistingNode.Depth, Depth);
			ExistingNode.Detail = FString::Printf(
				TEXT("Manuelle Rückführung/Zufuhr: %s/min"),
				*FSFPNumberFormatting::Decimal(ExistingNode.RatePerMinute));
		}
		else
		{
			FSFPPlanNode CycleNode;
			CycleNode.Id = Context.Result.Nodes.Num();
			CycleNode.Type = ESFPPlanNodeType::Cycle;
			CycleNode.Depth = Depth;
			CycleNode.Title = FString::Printf(TEXT("Kreislauf: %s"), *ItemName);
			CycleNode.Detail = FString::Printf(
				TEXT("Manuelle Rückführung/Zufuhr: %s/min"),
				*FSFPNumberFormatting::Decimal(RequiredRate));
			CycleNode.ClassPath = ItemClass->GetPathName();
			CycleNode.RatePerMinute = RequiredRate;
			NodeId = Context.Result.Nodes.Add(MoveTemp(CycleNode));
			Context.CycleNodeByItem.Add(ItemClass, NodeId);
		}
		AddEdgeToConsumer(NodeId);
		Context.Result.Warnings.AddUnique(FString::Printf(TEXT("Kreislauf bei %s wurde begrenzt"), *ItemName));
		return NodeId;
	}

	// A configured source mix is a capacity constraint, not another recipe.
	// Split extraction over matching purity variants while keeping the
	// selected miner/extractor and module loadout unchanged.
	const FString ItemPath = ItemClass->GetPathName();
	int32 MixedBaseRecipeIndex = INDEX_NONE;
	// Automatic purity allocation must also run before the player has changed
	// or saved any source limits. The selected purity is only a representative
	// recipe for the chosen miner/module configuration; it must not restrict the
	// automatic source family to that one purity.
	if (!Context.bDispatchingMixedSource)
	{
		if (const FString* PreferredPath = Context.RecipeOverrides.Find(ItemPath))
		{
			MixedBaseRecipeIndex = Recipes.IndexOfByPredicate([PreferredPath](const FSFPPlannerRecipe& Candidate)
			{
				return Candidate.ClassPath == *PreferredPath;
			});
		}
		if (MixedBaseRecipeIndex == INDEX_NONE)
		{
			int32 IgnoredCandidateCount = 0;
			MixedBaseRecipeIndex = FindRecipeFor(
				ItemClass,
				Context.bOnlyAvailableRecipes,
				nullptr,
				nullptr,
				IgnoredCandidateCount,
				&Context.Result.SuppliedInputRates,
				&Context.InputReachablePaths,
				&Context.InputDistanceByPath);
		}
	}

	const bool bBaseIsExtraction = Recipes.IsValidIndex(MixedBaseRecipeIndex)
		&& Recipes[MixedBaseRecipeIndex].bDirectResourceExtraction
		&& !Recipes[MixedBaseRecipeIndex].Ingredients.IsEmpty()
		&& Recipes[MixedBaseRecipeIndex].Products.ContainsByPredicate([ItemClass](const FSFPPlannerItemRate& Product)
		{
			return Product.ItemClass.Get() == ItemClass;
		});
	if (bBaseIsExtraction)
	{
		const FSFPPlannerRecipe& BaseRecipe = Recipes[MixedBaseRecipeIndex];
		const FString MixedSourcePath = BaseRecipe.Ingredients[0].ClassPath;
		const FSFPResourceSourceMix AutomaticSourceMix;
		const FSFPResourceSourceMix* ConfiguredSourceMix = Context.ResourceSourceMixes.Find(MixedSourcePath);
		const FSFPResourceSourceMix* SourceMix = ConfiguredSourceMix != nullptr
			? ConfiguredSourceMix : &AutomaticSourceMix;
		if (SourceMix->bEnabled)
		{
			struct FMixedVariant
			{
				const TCHAR* Label = nullptr;
				int32 Count = 0;
				bool bLimited = false;
				int32 RecipeIndex = INDEX_NONE;
				int32 EffectiveCount = 0;
			};
			TArray<FMixedVariant> Variants = {
				{TEXT("Rein"), FMath::Max(0, SourceMix->PureCount), SourceMix->bPureLimited, INDEX_NONE, 0},
				{TEXT("Normal"), FMath::Max(0, SourceMix->NormalCount), SourceMix->bNormalLimited, INDEX_NONE, 0},
				{TEXT("Unrein"), FMath::Max(0, SourceMix->ImpureCount), SourceMix->bImpureLimited, INDEX_NONE, 0}
			};
			for (FMixedVariant& Variant : Variants)
			{
				Variant.RecipeIndex = Recipes.IndexOfByPredicate([&](const FSFPPlannerRecipe& Candidate)
				{
					return Candidate.bDirectResourceExtraction
						&& Candidate.ResourceNodeLabel.Equals(Variant.Label, ESearchCase::IgnoreCase)
						&& Candidate.MachineClass == BaseRecipe.MachineClass
						&& Candidate.SourceName == BaseRecipe.SourceName
						&& Candidate.ModulesLabel == BaseRecipe.ModulesLabel
						&& Candidate.FluidLabel == BaseRecipe.FluidLabel
						&& !Candidate.Ingredients.IsEmpty()
						&& Candidate.Ingredients[0].ClassPath == MixedSourcePath
						&& Candidate.Products.ContainsByPredicate([ItemClass](const FSFPPlannerItemRate& Product)
						{
							return Product.ItemClass.Get() == ItemClass;
						})
						&& (!Context.bOnlyAvailableRecipes || Candidate.bAvailable);
				});
			}

			// Runtime mods may expose only the purities that actually exist for a
			// resource. One or two valid variants still form a capacity-limited
			// source family; requiring all three would make those resources infinite.
			const bool bHasPurityFamily = Variants.ContainsByPredicate([](const FMixedVariant& Variant)
			{
				return Variant.RecipeIndex != INDEX_NONE;
			});
			if (bHasPurityFamily)
			{
				const FString PreviousOverride = Context.RecipeOverrides.FindRef(ItemPath);
				const bool bHadOverride = Context.RecipeOverrides.Contains(ItemPath);
				double RemainingRate = RequiredRate;
				double TotalConfiguredCapacity = 0.0;
				int32 FirstProviderNodeId = INDEX_NONE;
				TArray<FString> MissingSourceAlternatives;
				TMap<FString, double> CapacityPerSourceByLabel;
				for (FMixedVariant& Variant : Variants)
				{
					if (!Recipes.IsValidIndex(Variant.RecipeIndex))
					{
						continue;
					}
					const FSFPPlannerRecipe& VariantRecipe = Recipes[Variant.RecipeIndex];
					const FSFPPlannerItemRate* Product = VariantRecipe.Products.FindByPredicate([ItemClass](const FSFPPlannerItemRate& Candidate)
					{
						return Candidate.ItemClass.Get() == ItemClass;
					});
					if (Product == nullptr || Product->RatePerMinute <= KINDA_SMALL_NUMBER)
					{
						continue;
					}

					const FSFPMachinePlanSettings* SavedSettings = Context.MachineSettings.Find(VariantRecipe.ClassPath);
					if (SavedSettings == nullptr)
					{
						SavedSettings = Context.MachineSettings.Find(BaseRecipe.ClassPath);
					}
					const double RequestedClock = SavedSettings != nullptr
						? FMath::Max(0.001, SavedSettings->ClockPercent / 100.0)
						: 1.0;
					double ConfiguredClock = VariantRecipe.MachineConfig.bCanChangePotential
						? FMath::Max(VariantRecipe.MachineConfig.MinPotential, RequestedClock)
						: 1.0;
					ConfiguredClock = VariantRecipe.MachineConfig.bRuntimeMaxPotentialKnown
						? FMath::Min(VariantRecipe.MachineConfig.MaxPotential, ConfiguredClock)
						: FMath::Min(10.0, ConfiguredClock);

					int32 SomersloopCount = SavedSettings != nullptr ? FMath::Max(0, SavedSettings->SomersloopCount) : 0;
					if (!VariantRecipe.MachineConfig.bCanChangeProductionBoost
						|| VariantRecipe.MachineConfig.ProductionBoostPerSloop <= KINDA_SMALL_NUMBER)
					{
						SomersloopCount = 0;
					}
					else
					{
						SomersloopCount = VariantRecipe.MachineConfig.bRuntimeMaxProductionBoostKnown
							? FMath::Min(SomersloopCount, VariantRecipe.MachineConfig.MaxSomersloops)
							: FMath::Min(SomersloopCount, 64);
					}
					double ProductionBoost = VariantRecipe.MachineConfig.BaseProductionBoost
						+ static_cast<double>(SomersloopCount) * VariantRecipe.MachineConfig.ProductionBoostPerSloop;
					ProductionBoost = FMath::Max(0.001, ProductionBoost);
					if (VariantRecipe.MachineConfig.bRuntimeMaxProductionBoostKnown)
					{
						ProductionBoost = FMath::Min(VariantRecipe.MachineConfig.MaxProductionBoost, ProductionBoost);
					}

					const double CapacityPerSource = Product->RatePerMinute * ConfiguredClock * ProductionBoost;
					CapacityPerSourceByLabel.Add(Variant.Label, CapacityPerSource);
					const FString UsageKey = MixedSourcePath + TEXT("|") + VariantRecipe.ClassPath;
					const double AlreadyUsedRate = Context.MixedSourceUsedRates.FindRef(UsageKey);
					Variant.EffectiveCount = Variant.bLimited
						? Variant.Count
						: FMath::Max(0, FMath::CeilToInt((AlreadyUsedRate + RemainingRate) / CapacityPerSource));
					const double Capacity = static_cast<double>(Variant.EffectiveCount) * CapacityPerSource;
					TotalConfiguredCapacity += Capacity;
					if (Variant.EffectiveCount <= 0)
					{
						continue;
					}
					const double AvailableCapacity = FMath::Max(0.0, Capacity - AlreadyUsedRate);
					const double AssignedRate = FMath::Min(RemainingRate, AvailableCapacity);
					if (AssignedRate <= KINDA_SMALL_NUMBER)
					{
						continue;
					}

					Context.ProviderNodeByItem.Remove(ItemClass);
					Context.RecipeIndexByItem.Remove(ItemClass);
					if (const int32* ExistingProvider = Context.MixedProviderNodeByRecipe.Find(UsageKey))
					{
						Context.ProviderNodeByItem.Add(ItemClass, *ExistingProvider);
						Context.RecipeIndexByItem.Add(ItemClass, Variant.RecipeIndex);
					}
					Context.RecipeOverrides.Add(ItemPath, VariantRecipe.ClassPath);
					Context.bDispatchingMixedSource = true;
					const int32 ProviderNodeId = BuildDemand(
						ItemClass, ItemName, Form, AssignedRate, ConsumerNodeId, Depth, Context);
					Context.bDispatchingMixedSource = false;
					if (ProviderNodeId != INDEX_NONE)
					{
						FirstProviderNodeId = FirstProviderNodeId == INDEX_NONE ? ProviderNodeId : FirstProviderNodeId;
						Context.MixedProviderNodeByRecipe.Add(UsageKey, ProviderNodeId);
						Context.MixedSourceUsedRates.FindOrAdd(UsageKey) += AssignedRate;
						FSFPPlanNode& ProviderNode = Context.Result.Nodes[ProviderNodeId];
						ProviderNode.MaximumMachineCount = static_cast<double>(Variant.EffectiveCount) * ConfiguredClock;
						const FString SourceKey = VariantRecipe.ClassPath + TEXT("|") + MixedSourcePath;
						if (const int32* ResourceNodeId = Context.DirectResourceNodeByKey.Find(SourceKey))
						{
							FSFPPlanNode& ResourceNode = Context.Result.Nodes[*ResourceNodeId];
							ResourceNode.MaximumMachineCount = static_cast<double>(Variant.EffectiveCount);
							ResourceNode.Detail = Variant.bLimited
								? FString::Printf(
									TEXT("%d von %d × %s-Knoten genutzt | %s/min Abbauäquivalent"),
									ProviderNode.BuiltMachineCount,
									Variant.EffectiveCount,
									Variant.Label,
									*FSFPNumberFormatting::Decimal(ResourceNode.RatePerMinute))
								: FString::Printf(
									TEXT("%d × %s-Knoten benötigt (automatisch) | %s/min Abbauäquivalent"),
									ProviderNode.BuiltMachineCount,
									Variant.Label,
									*FSFPNumberFormatting::Decimal(ResourceNode.RatePerMinute));
						}
					}
					RemainingRate -= AssignedRate;
					if (RemainingRate <= KINDA_SMALL_NUMBER)
					{
						break;
					}
				}

				if (bHadOverride)
				{
					Context.RecipeOverrides.Add(ItemPath, PreviousOverride);
				}
				else
				{
					Context.RecipeOverrides.Remove(ItemPath);
				}
				Context.ProviderNodeByItem.Remove(ItemClass);
				Context.RecipeIndexByItem.Remove(ItemClass);

				if (RemainingRate > KINDA_SMALL_NUMBER)
				{
					Context.Result.bResourceSourceLimitsExceeded = true;
					const double PreviousRelativeShortage = Context.Result.LimitingResourceDisplayName.IsEmpty()
						? -1.0
						: Context.Result.LimitingResourceShortagePerMinute
							/ FMath::Max(1.0, Context.Result.LimitingResourceCapacityPerMinute);
					const double RelativeShortage = RemainingRate
						/ FMath::Max(1.0, TotalConfiguredCapacity);
					if (RelativeShortage > PreviousRelativeShortage)
					{
						Context.Result.LimitingResourceClassPath = MixedSourcePath;
						Context.Result.LimitingResourceDisplayName = ItemName;
						Context.Result.LimitingResourceCapacityPerMinute = TotalConfiguredCapacity;
						Context.Result.LimitingResourceShortagePerMinute = RemainingRate;
					}
					MissingSourceAlternatives.Reset();
					for (const FMixedVariant& Variant : Variants)
					{
						const double* PerSource = CapacityPerSourceByLabel.Find(Variant.Label);
						if (PerSource != nullptr && *PerSource > KINDA_SMALL_NUMBER)
						{
							MissingSourceAlternatives.Add(FString::Printf(
								TEXT("%d %s"), FMath::CeilToInt(RemainingRate / *PerSource), Variant.Label));
						}
					}
					int32 ExternalNodeId = INDEX_NONE;
					if (const int32* ExistingExternalNodeId = Context.MixedExternalNodeByItem.Find(ItemPath))
					{
						ExternalNodeId = *ExistingExternalNodeId;
					}
					else
					{
						FSFPPlanNode ExternalNode;
						ExternalNode.Id = Context.Result.Nodes.Num();
						ExternalNode.Type = ESFPPlanNodeType::Source;
						ExternalNode.Depth = Depth;
						ExternalNode.Title = ItemName;
						ExternalNode.ClassPath = ItemPath;
						ExternalNode.SourceCostMultiplier = 1000000.0;
						ExternalNodeId = Context.Result.Nodes.Add(MoveTemp(ExternalNode));
						Context.MixedExternalNodeByItem.Add(ItemPath, ExternalNodeId);
					}
					FSFPPlanNode& ExternalNode = Context.Result.Nodes[ExternalNodeId];
					ExternalNode.RatePerMinute += RemainingRate;
					ExternalNode.Detail = FString::Printf(
						TEXT("Mischquellen reichen nicht aus: %s/min externe Zufuhr"),
						*FSFPNumberFormatting::Decimal(ExternalNode.RatePerMinute));
					AddOrMergeEdge(ExternalNodeId, ConsumerNodeId, ItemName, ItemPath, Form, RemainingRate, false);
					Context.Result.Warnings.AddUnique(FString::Printf(
						TEXT("%s: Mischquellen liefern höchstens %s/min; %s/min bleiben extern. Zusätzlich benötigt (alternativ): %s"),
						*ItemName,
						*FSFPNumberFormatting::Decimal(TotalConfiguredCapacity),
						*FSFPNumberFormatting::Decimal(RemainingRate),
						*FString::Join(MissingSourceAlternatives, TEXT(" oder "))));
					FirstProviderNodeId = FirstProviderNodeId == INDEX_NONE ? ExternalNodeId : FirstProviderNodeId;
				}
				return FirstProviderNodeId;
			}
		}
	}

	if (const int32* ExistingNodeId = Context.ProviderNodeByItem.Find(ItemClass))
	{
		FSFPPlanNode& ExistingNode = Context.Result.Nodes[*ExistingNodeId];
		ExistingNode.Depth = FMath::Max(ExistingNode.Depth, Depth);
		if (ExistingNode.Type == ESFPPlanNodeType::Source || ExistingNode.Type == ESFPPlanNodeType::Cycle)
		{
			const bool bLockedSource = ExistingNode.Detail.StartsWith(TEXT("Gesperrtes Rezept:"));
			ExistingNode.RatePerMinute += RequiredRate;
			ExistingNode.Detail = ExistingNode.Type == ESFPPlanNodeType::Cycle
				? FString::Printf(TEXT("Zyklische Zufuhr: %s/min"), *FSFPNumberFormatting::Decimal(ExistingNode.RatePerMinute))
				: bLockedSource
					? FString::Printf(TEXT("Gesperrtes Rezept: %s/min extern"), *FSFPNumberFormatting::Decimal(ExistingNode.RatePerMinute))
					: FString::Printf(TEXT("Externe Quelle: %s/min"), *FSFPNumberFormatting::Decimal(ExistingNode.RatePerMinute));
			AddEdgeToConsumer(*ExistingNodeId);
			return *ExistingNodeId;
		}
	}

	int32 EffectiveRecipeIndex = INDEX_NONE;
	int32 FilteredCandidateCount = 0;
	if (const int32* ExistingRecipeIndex = Context.RecipeIndexByItem.Find(ItemClass))
	{
		EffectiveRecipeIndex = *ExistingRecipeIndex;
		int32 IgnoredCandidateCount = 0;
		const FString* PreferredRecipePath = Context.RecipeOverrides.Find(ItemClass->GetPathName());
		FindRecipeFor(ItemClass, Context.bOnlyAvailableRecipes, nullptr, PreferredRecipePath, IgnoredCandidateCount, &Context.Result.SuppliedInputRates, &Context.InputReachablePaths, &Context.InputDistanceByPath);
		FilteredCandidateCount = IgnoredCandidateCount;
	}
	else
	{
		TSet<UClass*> ForbiddenIngredients = Context.RecursionStack;
		ForbiddenIngredients.Add(ItemClass);
		const FString* PreferredRecipePath = Context.RecipeOverrides.Find(ItemClass->GetPathName());
		EffectiveRecipeIndex = FindRecipeFor(
			ItemClass,
			Context.bOnlyAvailableRecipes,
			&ForbiddenIngredients,
			PreferredRecipePath,
			FilteredCandidateCount, &Context.Result.SuppliedInputRates, &Context.InputReachablePaths, &Context.InputDistanceByPath);
	}

	if (EffectiveRecipeIndex == INDEX_NONE)
	{
		int32 AnyCandidateCount = 0;
		const int32 AnyRecipeIndex = FindRecipeFor(ItemClass, false, nullptr, nullptr, AnyCandidateCount, &Context.Result.SuppliedInputRates, &Context.InputReachablePaths, &Context.InputDistanceByPath);
		int32 AvailableCandidateCount = 0;
		const int32 AvailableRecipeIndex = FindRecipeFor(ItemClass, true, nullptr, nullptr, AvailableCandidateCount, &Context.Result.SuppliedInputRates, &Context.InputReachablePaths, &Context.InputDistanceByPath);
		const bool bLocked = Context.bOnlyAvailableRecipes
			&& AnyRecipeIndex != INDEX_NONE
			&& AvailableRecipeIndex == INDEX_NONE;
		const bool bCycleOnly = AnyRecipeIndex != INDEX_NONE && !bLocked;

		FSFPPlanNode SourceNode;
		SourceNode.Id = Context.Result.Nodes.Num();
		SourceNode.Type = bCycleOnly ? ESFPPlanNodeType::Cycle : ESFPPlanNodeType::Source;
		SourceNode.Depth = Depth;
		SourceNode.Title = bCycleOnly ? FString::Printf(TEXT("Kreislauf: %s"), *ItemName) : ItemName;
		SourceNode.Detail = bLocked
			? FString::Printf(TEXT("Gesperrtes Rezept: %s/min extern"), *FSFPNumberFormatting::Decimal(RequiredRate))
			: bCycleOnly
				? FString::Printf(TEXT("Zyklische Zufuhr: %s/min"), *FSFPNumberFormatting::Decimal(RequiredRate))
				: FString::Printf(TEXT("Externe Quelle: %s/min"), *FSFPNumberFormatting::Decimal(RequiredRate));
		SourceNode.ClassPath = ItemClass->GetPathName();
		SourceNode.RatePerMinute = RequiredRate;
		const int32 NodeId = Context.Result.Nodes.Add(MoveTemp(SourceNode));
		Context.ProviderNodeByItem.Add(ItemClass, NodeId);
		AddEdgeToConsumer(NodeId);
		if (bLocked)
		{
			Context.Result.Warnings.AddUnique(FString::Printf(TEXT("Für %s ist noch kein Rezept freigeschaltet"), *ItemName));
		}
		else if (bCycleOnly)
		{
			Context.Result.Warnings.AddUnique(FString::Printf(TEXT("Für %s blieb nur eine zyklische Rezeptkette"), *ItemName));
		}
		return NodeId;
	}

	const FSFPPlannerRecipe& Recipe = Recipes[EffectiveRecipeIndex];
	if (const FString* PreferredRecipePath = Context.RecipeOverrides.Find(ItemClass->GetPathName()))
	{
		if (Recipe.ClassPath != *PreferredRecipePath)
		{
			Context.Result.Warnings.AddUnique(FString::Printf(
				TEXT("Die gewählte Rezeptvariante für %s ist in dieser Kette nicht verwendbar; '%s' wurde genutzt"),
				*ItemName,
				*Recipe.DisplayName));
		}
	}
	const FSFPPlannerItemRate* TargetProduct = Recipe.Products.FindByPredicate([ItemClass](const FSFPPlannerItemRate& Product)
	{
		return Product.ItemClass.Get() == ItemClass;
	});
	if (TargetProduct == nullptr || TargetProduct->RatePerMinute <= KINDA_SMALL_NUMBER)
	{
		return INDEX_NONE;
	}

	if (FilteredCandidateCount > 1)
	{
		const FString WarningKey = ItemClass->GetPathName();
		if (!Context.AlternativeWarnings.Contains(WarningKey))
		{
			Context.AlternativeWarnings.Add(WarningKey);
			Context.Result.Warnings.Add(FString::Printf(
				TEXT("%s: %d Rezepte möglich, automatisch '%s' gewählt"),
				*ItemName,
				FilteredCandidateCount,
				*Recipe.DisplayName));
		}
	}

	FSFPMachinePlanSettings OperatingSettings;
	if (const FSFPMachinePlanSettings* SavedSettings = Context.MachineSettings.Find(Recipe.ClassPath))
	{
		OperatingSettings = *SavedSettings;
	}

	double ConfiguredClock = FMath::Max(0.001, OperatingSettings.ClockPercent / 100.0);
	if (!Recipe.MachineConfig.bCanChangePotential)
	{
		ConfiguredClock = 1.0;
	}
	else
	{
		ConfiguredClock = FMath::Max(Recipe.MachineConfig.MinPotential, ConfiguredClock);
		if (Recipe.MachineConfig.bRuntimeMaxPotentialKnown)
		{
			ConfiguredClock = FMath::Min(Recipe.MachineConfig.MaxPotential, ConfiguredClock);
		}
		else
		{
			// Do not hardcode vanilla's shard limit. Modded machines can expose a
			// different maximum; until runtime reports it, keep only a safety cap.
			ConfiguredClock = FMath::Min(10.0, ConfiguredClock);
		}
	}

	int32 SomersloopCount = FMath::Max(0, OperatingSettings.SomersloopCount);
	if (!Recipe.MachineConfig.bCanChangeProductionBoost
		|| Recipe.MachineConfig.ProductionBoostPerSloop <= KINDA_SMALL_NUMBER)
	{
		SomersloopCount = 0;
	}
	else if (Recipe.MachineConfig.bRuntimeMaxProductionBoostKnown)
	{
		SomersloopCount = FMath::Min(SomersloopCount, Recipe.MachineConfig.MaxSomersloops);
	}
	else
	{
		SomersloopCount = FMath::Min(SomersloopCount, 64);
	}

	double ProductionBoost = Recipe.MachineConfig.BaseProductionBoost
		+ static_cast<double>(SomersloopCount) * Recipe.MachineConfig.ProductionBoostPerSloop;
	ProductionBoost = FMath::Max(0.001, ProductionBoost);
	if (Recipe.MachineConfig.bRuntimeMaxProductionBoostKnown)
	{
		ProductionBoost = FMath::Min(Recipe.MachineConfig.MaxProductionBoost, ProductionBoost);
	}

	// Somersloops amplify products without multiplying recipe inputs. Therefore
	// target output first becomes an unboosted output-equivalent, then a smaller
	// cycle-equivalent used for ingredient demand and machine clock distribution.
	const double AddedOutputEquivalent = RequiredRate / TargetProduct->RatePerMinute;
	const double AddedMachineCount = AddedOutputEquivalent / ProductionBoost;
	int32 MachineNodeId = INDEX_NONE;
	if (const int32* ExistingNodeId = Context.ProviderNodeByItem.Find(ItemClass))
	{
		MachineNodeId = *ExistingNodeId;
	}
	else
	{
		FSFPPlanNode MachineNode;
		MachineNode.Id = Context.Result.Nodes.Num();
		MachineNode.Type = ESFPPlanNodeType::Machine;
		MachineNode.Depth = Depth;
		MachineNode.Title = Recipe.MachineName;
		MachineNode.ClassPath = IsValid(Recipe.MachineClass) ? Recipe.MachineClass->GetPathName() : Recipe.ClassPath;
		MachineNode.ProducedItemClassPath = ItemClass->GetPathName();
		MachineNode.RecipeClassPath = Recipe.ClassPath;
		MachineNode.AdditionalBuildableClassPaths = Recipe.AdditionalBuildableClassPaths;
		MachineNodeId = Context.Result.Nodes.Add(MoveTemp(MachineNode));
		Context.ProviderNodeByItem.Add(ItemClass, MachineNodeId);
		Context.RecipeIndexByItem.Add(ItemClass, EffectiveRecipeIndex);
	}

	FSFPPlanNode& MachineNode = Context.Result.Nodes[MachineNodeId];
	MachineNode.Depth = FMath::Max(MachineNode.Depth, Depth);
	MachineNode.RatePerMinute += RequiredRate;
	MachineNode.MachineCount += AddedMachineCount;
	MachineNode.ConfiguredClockPercent = ConfiguredClock * 100.0;
	MachineNode.SomersloopCount = SomersloopCount;
	MachineNode.ProductionBoost = ProductionBoost;
	MachineNode.bFuelPowered = Recipe.bFuelPowered;
	const double PreviousPower = MachineNode.PowerMW;
	const double PreviousFuelRate = MachineNode.FuelRatePerMinute;
	const SFPVariablePower::ConfiguredClocking ClockingResult = SFPVariablePower::ClockedPowerConfigured(
		Recipe.BasePowerMW,
		MachineNode.MachineCount,
		ConfiguredClock,
		Recipe.PowerExponent,
		ProductionBoost,
		Recipe.MachineConfig.ProductionBoostPowerExponent);
	MachineNode.PowerMW = ClockingResult.Power;
	MachineNode.BuiltMachineCount = FMath::Max(1, ClockingResult.BuiltMachines);
	MachineNode.FullClockMachineCount = FMath::Max(0, ClockingResult.FullClockMachines);
	MachineNode.PartialClockPercent = FMath::Max(0.0, ClockingResult.PartialClock * 100.0);

	const FSFPPlannerFuelOption* SelectedFuel = nullptr;
	if (Recipe.bFuelPowered)
	{
		if (!OperatingSettings.FuelClassPath.IsEmpty())
		{
			SelectedFuel = Recipe.FuelOptions.FindByPredicate([&OperatingSettings](const FSFPPlannerFuelOption& Fuel)
			{
				return Fuel.ClassPath == OperatingSettings.FuelClassPath;
			});
		}
		if (SelectedFuel == nullptr && !Recipe.FuelOptions.IsEmpty())
		{
			// FuelOptions are sorted by class path while the catalog is built.
			// This gives deterministic automatic selection without a hard dependency
			// on BurnerManufacturer or on any particular vanilla/mod fuel.
			SelectedFuel = &Recipe.FuelOptions[0];
		}
		if (SelectedFuel != nullptr && SelectedFuel->EnergyValueMJ > KINDA_SMALL_NUMBER)
		{
			MachineNode.FuelClassPath = SelectedFuel->ClassPath;
			MachineNode.FuelDisplayName = SelectedFuel->DisplayName;
			MachineNode.FuelForm = SelectedFuel->Form;
			MachineNode.FuelEnergyValueMJ = SelectedFuel->EnergyValueMJ;
			MachineNode.FuelRatePerMinute = MachineNode.PowerMW * 60.0 / SelectedFuel->EnergyValueMJ;
			if (SelectedFuel->Form == TEXT("liquid") || SelectedFuel->Form == TEXT("gas"))
			{
				MachineNode.FuelRatePerMinute /= 1000.0;
			}
		}
		else
		{
			Context.Result.Warnings.AddUnique(FString::Printf(
				TEXT("Für Brennermaschine %s konnte kein gültiger Brennstoff mit Energiewert ermittelt werden"),
				*Recipe.MachineName));
		}
	}

	FString Clocking;
	if (MachineNode.PartialClockPercent > 0.05)
	{
		Clocking = MachineNode.FullClockMachineCount > 0
			? FString::Printf(
				TEXT("%d × %s%% + 1 × %s%%"),
				MachineNode.FullClockMachineCount,
				*FSFPNumberFormatting::Decimal(MachineNode.ConfiguredClockPercent, 1),
				*FSFPNumberFormatting::Decimal(MachineNode.PartialClockPercent, 1))
			: FString::Printf(TEXT("1 × %s%%"), *FSFPNumberFormatting::Decimal(MachineNode.PartialClockPercent, 1));
	}
	else
	{
		Clocking = FString::Printf(
			TEXT("%d × %s%%"),
			MachineNode.BuiltMachineCount,
			*FSFPNumberFormatting::Decimal(MachineNode.ConfiguredClockPercent, 1));
	}
	if (Recipe.bVariablePower)
	{
		Context.Result.Warnings.AddUnique(TEXT("Variable Rezeptleistung: Die Leistungssumme verwendet numerische Zyklusmittelwerte. Spitzen können höher liegen; Netzreserve einplanen."));
	}
	const FString ConfigurationLine = Recipe.ConfigurationDetail.IsEmpty()
		? FString()
		: FString::Printf(TEXT("\n%s"), *Recipe.ConfigurationDetail);
	const FString BoostLine = SomersloopCount > 0
		? FString::Printf(
			TEXT("\nSomersloops: %d je Maschine | Produktionsverstärkung: %s%%"),
			SomersloopCount,
			*FSFPNumberFormatting::Decimal(ProductionBoost * 100.0, 1))
		: FString();
	const FString PowerLabel = Recipe.bFuelPowered ? TEXT("Brennleistung") : TEXT("Strombedarf");
	MachineNode.Detail = FString::Printf(
		TEXT("%s%s\n%d Maschinen gebaut | %s%s\n%s bei geplanter Taktung: %s MW"),
		*Recipe.DisplayName,
		*ConfigurationLine,
		MachineNode.BuiltMachineCount,
		*Clocking,
		*BoostLine,
		*PowerLabel,
		*FSFPNumberFormatting::Decimal(MachineNode.PowerMW, 2));
	if (Recipe.bFuelPowered && SelectedFuel != nullptr)
	{
		MachineNode.Detail += FString::Printf(
			TEXT("\nBrennstoff: %s | %s/min | %s MJ"),
			*SelectedFuel->DisplayName,
			*FSFPNumberFormatting::Decimal(MachineNode.FuelRatePerMinute, 3),
			*FSFPNumberFormatting::Decimal(SelectedFuel->EnergyValueMJ, 1));
	}
	if (Recipe.bVariablePower)
	{
		MachineNode.Detail += TEXT("\nZyklusmittelwert bei variablem Verbrauch");
	}
	Context.Result.TotalEquivalentMachines += AddedMachineCount;
	if (!Recipe.bFuelPowered)
	{
		Context.Result.TotalBasePowerMW += MachineNode.PowerMW - PreviousPower;
	}
	AddEdgeToConsumer(MachineNodeId);

	Context.RecursionStack.Add(ItemClass);
	for (const FSFPPlannerItemRate& Ingredient : Recipe.Ingredients)
	{
		const double IngredientRate = AddedMachineCount * Ingredient.RatePerMinute;
		if (!Recipe.bDirectResourceExtraction || &Ingredient != &Recipe.Ingredients[0])
		{
			BuildDemand(
				Ingredient.ItemClass.Get(),
				Ingredient.DisplayName,
				Ingredient.Form,
				IngredientRate,
				MachineNodeId,
				Depth + 1,
				Context);
			continue;
		}

		const FString SourceKey = Recipe.ClassPath + TEXT("|") + Ingredient.ClassPath;
		int32 ResourceNodeId = INDEX_NONE;
		if (const int32* ExistingResourceNodeId = Context.DirectResourceNodeByKey.Find(SourceKey))
		{
			ResourceNodeId = *ExistingResourceNodeId;
		}
		else
		{
			FSFPPlanNode ResourceNode;
			ResourceNode.Id = Context.Result.Nodes.Num();
			ResourceNode.Type = ESFPPlanNodeType::Source;
			ResourceNode.Depth = Depth + 1;
			ResourceNode.Title = FString::Printf(
				TEXT("%s-Rohstoffknoten (%s)"),
				*Ingredient.DisplayName,
				*Recipe.ResourceNodeLabel);
			ResourceNode.ClassPath = Ingredient.ClassPath;
			ResourceNodeId = Context.Result.Nodes.Add(MoveTemp(ResourceNode));
			Context.DirectResourceNodeByKey.Add(SourceKey, ResourceNodeId);
		}
		FSFPPlanNode& ResourceNode = Context.Result.Nodes[ResourceNodeId];
		ResourceNode.Depth = FMath::Max(ResourceNode.Depth, Depth + 1);
		ResourceNode.RatePerMinute += IngredientRate;
		ResourceNode.MachineCount = static_cast<double>(MachineNode.BuiltMachineCount);
		ResourceNode.BuiltMachineCount = MachineNode.BuiltMachineCount;
		ResourceNode.Detail = FString::Printf(
			TEXT("%d × %s-Knoten | %s/min Abbauäquivalent"),
			MachineNode.BuiltMachineCount,
			*Recipe.ResourceNodeLabel,
			*FSFPNumberFormatting::Decimal(ResourceNode.RatePerMinute));
		AddOrMergeEdge(
			ResourceNodeId,
			MachineNodeId,
			Ingredient.DisplayName,
			Ingredient.ClassPath,
			Ingredient.Form,
			IngredientRate,
			true);
	}

	if (Recipe.bFuelPowered && SelectedFuel != nullptr)
	{
		const double AddedFuelRate = FMath::Max(0.0, MachineNode.FuelRatePerMinute - PreviousFuelRate);
		if (AddedFuelRate > KINDA_SMALL_NUMBER)
		{
			BuildDemand(
				SelectedFuel->ItemClass.Get(),
				SelectedFuel->DisplayName,
				SelectedFuel->Form,
				AddedFuelRate,
				MachineNodeId,
				Depth + 1,
				Context);
		}
	}
	Context.RecursionStack.Remove(ItemClass);

	for (const FSFPPlannerItemRate& Product : Recipe.Products)
	{
		if (Product.ItemClass.Get() == ItemClass)
		{
			continue;
		}

		const FString ByproductKey = Recipe.ClassPath + TEXT("|") + Product.ClassPath;
		int32 ByproductNodeId = INDEX_NONE;
		if (const int32* ExistingByproductId = Context.ByproductNodeByKey.Find(ByproductKey))
		{
			ByproductNodeId = *ExistingByproductId;
		}
		else
		{
			FSFPPlanNode ByproductNode;
			ByproductNode.Id = Context.Result.Nodes.Num();
			ByproductNode.Type = ESFPPlanNodeType::Byproduct;
			ByproductNode.Depth = FMath::Max(0, Depth - 1);
			ByproductNode.Title = Product.DisplayName;
			ByproductNode.ClassPath = Product.ClassPath;
			ByproductNodeId = Context.Result.Nodes.Add(MoveTemp(ByproductNode));
			Context.ByproductNodeByKey.Add(ByproductKey, ByproductNodeId);
		}

		const double ByproductRate = AddedMachineCount * Product.RatePerMinute * ProductionBoost;
		FSFPPlanNode& ByproductNode = Context.Result.Nodes[ByproductNodeId];
		ByproductNode.RatePerMinute += ByproductRate;
		ByproductNode.Detail = FString::Printf(
			TEXT("Lagereingang Nebenprodukt: %s/min"),
			*FSFPNumberFormatting::Decimal(ByproductNode.RatePerMinute));
		AddOrMergeEdge(
			MachineNodeId,
			ByproductNodeId,
			Product.DisplayName,
			Product.ClassPath,
			Product.Form,
			ByproductRate,
			false);
	}

	return MachineNodeId;
}

void FSFPPlannerSolver::AddTransportAdvice(FSFPPlanEdge& Edge, FSFPPlanResult& Result) const
{
	const bool bFluid = Edge.Form == TEXT("liquid") || Edge.Form == TEXT("gas");
	if (bFluid)
	{
		const FSFPTransportTier* Chosen = nullptr;
		for (const FSFPTransportTier& Tier : TransportTiers)
		{
			if (Tier.Kind != TEXT("pipe") || Tier.CapacityPerMinute <= 0.0
				|| (Result.bOnlyAvailableRecipes && !Tier.bAvailable))
			{
				continue;
			}
			Chosen = &Tier;
			if (Tier.CapacityPerMinute + KINDA_SMALL_NUMBER >= Edge.RatePerMinute)
			{
				break;
			}
		}
		if (Chosen != nullptr)
		{
			Edge.RequiredLines = FMath::Max(1, FMath::CeilToInt(Edge.RatePerMinute / Chosen->CapacityPerMinute));
			Edge.TransportClassPath = Chosen->ClassPath;
			Edge.TransportKind = Chosen->Kind;
			const double PerLineRate = Edge.RatePerMinute / static_cast<double>(Edge.RequiredLines);
			if (Edge.RequiredLines == 1)
			{
				Edge.TransportLabel = FString::Printf(
					TEXT("1 Leitung | %s/%s pro min\n%s"),
					*FSFPNumberFormatting::Decimal(PerLineRate),
					*FSFPNumberFormatting::Decimal(Chosen->CapacityPerMinute, 0),
					*Chosen->DisplayName);
			}
			else
			{
				Edge.TransportLabel = FString::Printf(
					TEXT("%d parallele Leitungen | Ø %s/%s pro min\n%s"),
					Edge.RequiredLines,
					*FSFPNumberFormatting::Decimal(PerLineRate),
					*FSFPNumberFormatting::Decimal(Chosen->CapacityPerMinute, 0),
					*Chosen->DisplayName);
			}
			return;
		}
		Edge.RequiredLines = 0;
		Edge.TransportClassPath.Reset();
		Edge.TransportKind = TEXT("pipe");
		Edge.TransportLabel = TEXT("Freigeschaltete Rohrkapazität nicht geladen");
		Result.Warnings.AddUnique(TEXT("Keine passende freigeschaltete Rohrleitung gefunden"));
		return;
	}

	const double EffectiveCapacity = FMath::Min(
		Result.SelectedConveyorCapacityPerMinute,
		Result.SelectedConveyorLiftCapacityPerMinute);
	Edge.TransportKind = TEXT("belt");
	Edge.TransportClassPath = Result.SelectedConveyorClassPath;
	if (EffectiveCapacity <= 0.0)
	{
		Edge.RequiredLines = 0;
		Edge.TransportLabel = TEXT("Förderband- oder Liftkapazität nicht geladen");
		Result.Warnings.AddUnique(TEXT("Keine passende Förderband- und Förderlift-Kombination gefunden"));
		return;
	}

	Edge.RequiredLines = FMath::Max(1, FMath::CeilToInt(Edge.RatePerMinute / EffectiveCapacity));
	const double PerLineRate = Edge.RatePerMinute / static_cast<double>(Edge.RequiredLines);
	if (Edge.RequiredLines == 1)
	{
		Edge.TransportLabel = FString::Printf(
			TEXT("1 Linie | %s/%s pro min\n%s + %s"),
			*FSFPNumberFormatting::Decimal(PerLineRate),
			*FSFPNumberFormatting::Decimal(EffectiveCapacity, 0),
			*Result.SelectedConveyorDisplayName,
			*Result.SelectedConveyorLiftDisplayName);
	}
	else
	{
		Edge.TransportLabel = FString::Printf(
			TEXT("%d parallele Linien | Ø %s/%s pro min\n%s + %s"),
			Edge.RequiredLines,
			*FSFPNumberFormatting::Decimal(PerLineRate),
			*FSFPNumberFormatting::Decimal(EffectiveCapacity, 0),
			*Result.SelectedConveyorDisplayName,
			*Result.SelectedConveyorLiftDisplayName);
	}
}

void FSFPPlannerSolver::BuildRoutingInfrastructure(FSFPPlanResult& Result) const
{
	auto IsFluid = [](const FString& Form)
	{
		return Form == TEXT("liquid") || Form == TEXT("gas");
	};
	auto FindNode = [&Result](const int32 NodeId) -> FSFPPlanNode*
	{
		return Result.Nodes.FindByPredicate([NodeId](const FSFPPlanNode& Node)
		{
			return Node.Id == NodeId;
		});
	};
	auto AddRoutingNode = [&](const ESFPPlanNodeType Type, const FSFPPlanEdge& Edge,
		const int32 EndpointCount, const int32 InputLineCount, const int32 OutputLineCount,
		const double TotalRate, const int32 Depth) -> int32
	{
		const bool bFluid = IsFluid(Edge.Form);
		// A three-port splitter/merger changes the number of parallel transport
		// lanes by at most two. Count only the actual lane delta; the selected
		// transport capacity must remain valid on both sides of the junction.
		const int32 LineDelta = Type == ESFPPlanNodeType::Splitter
			? FMath::Max(0, OutputLineCount - InputLineCount)
			: FMath::Max(0, InputLineCount - OutputLineCount);
		const int32 HardwareCount = FMath::Max(1, FMath::CeilToInt(
			static_cast<double>(LineDelta) / 2.0));
		FSFPPlanNode Node;
		Node.Id = Result.Nodes.Num();
		Node.Type = Type;
		Node.Depth = FMath::Max(0, Depth);
		if (Type == ESFPPlanNodeType::Splitter)
		{
			Node.Title = bFluid
				? (PipeJunctionDisplayName.IsEmpty() ? TEXT("Pipeline Junction") : PipeJunctionDisplayName)
				: (SplitterDisplayName.IsEmpty() ? TEXT("Splitter") : SplitterDisplayName);
			Node.Detail = FString::Printf(
				TEXT("%d × Infrastruktur | %d Verbraucher\n%d Linien rein → %d Linien raus | %s: %s/min"),
				HardwareCount, EndpointCount, InputLineCount, OutputLineCount,
				*Edge.ItemName, *FSFPNumberFormatting::Decimal(TotalRate));
			Node.ClassPath = bFluid ? PipeJunctionClassPath : SplitterClassPath;
			Result.SplitterCount += HardwareCount;
		}
		else
		{
			Node.Title = bFluid
				? (PipeJunctionDisplayName.IsEmpty() ? TEXT("Pipeline Junction") : PipeJunctionDisplayName)
				: (MergerDisplayName.IsEmpty() ? TEXT("Fusionator") : MergerDisplayName);
			Node.Detail = FString::Printf(
				TEXT("%d × Infrastruktur | %d Erzeuger\n%d Linien rein → %d Linien raus | %s: %s/min"),
				HardwareCount, EndpointCount, InputLineCount, OutputLineCount,
				*Edge.ItemName, *FSFPNumberFormatting::Decimal(TotalRate));
			Node.ClassPath = bFluid ? PipeJunctionClassPath : MergerClassPath;
			Result.MergerCount += HardwareCount;
		}
		Node.RatePerMinute = TotalRate;
		Node.InfrastructureCount = HardwareCount;
		const int32 NodeId = Result.Nodes.Add(MoveTemp(Node));
		Result.MaxDepth = FMath::Max(Result.MaxDepth, Result.Nodes[NodeId].Depth);
		return NodeId;
	};
	auto AddLocalLink = [&](const FSFPPlanEdge& TemplateEdge, const int32 SourceNodeId,
		const int32 TargetNodeId, const double TotalRate)
	{
		FSFPPlanEdge Link = TemplateEdge;
		Link.SourceNodeId = SourceNodeId;
		Link.TargetNodeId = TargetNodeId;
		Link.RatePerMinute = TotalRate;
		Link.bLocalRoutingLink = true;
		AddTransportAdvice(Link, Result);
		Result.Edges.Add(MoveTemp(Link));
	};
	auto BuildGroups = [&Result](const bool bOutgoing)
	{
		TMap<FString, TArray<int32>> Groups;
		for (int32 EdgeIndex = 0; EdgeIndex < Result.Edges.Num(); ++EdgeIndex)
		{
			const FSFPPlanEdge& Edge = Result.Edges[EdgeIndex];
			if (Edge.bLocalRoutingLink)
			{
				continue;
			}
			const int32 NodeId = bOutgoing ? Edge.SourceNodeId : Edge.TargetNodeId;
			Groups.FindOrAdd(FString::Printf(TEXT("%d|%s"), NodeId, *Edge.ItemClassPath)).Add(EdgeIndex);
		}
		return Groups;
	};
	auto TotalRateFor = [&Result](const TArray<int32>& EdgeIndices)
	{
		double TotalRate = 0.0;
		for (const int32 EdgeIndex : EdgeIndices)
		{
			if (Result.Edges.IsValidIndex(EdgeIndex))
			{
				TotalRate += Result.Edges[EdgeIndex].RatePerMinute;
			}
		}
		return TotalRate;
	};
	auto TransportBranchesFor = [&Result](const TArray<int32>& EdgeIndices)
	{
		int32 Branches = 0;
		for (const int32 EdgeIndex : EdgeIndices)
		{
			if (Result.Edges.IsValidIndex(EdgeIndex))
			{
				Branches += FMath::Max(1, Result.Edges[EdgeIndex].RequiredLines);
			}
		}
		return Branches;
	};
	auto DistinctEndpointCountFor = [&Result](
		const TArray<int32>& EdgeIndices, const bool bOutgoing)
	{
		TSet<int32> EndpointNodeIds;
		for (const int32 EdgeIndex : EdgeIndices)
		{
			if (Result.Edges.IsValidIndex(EdgeIndex))
			{
				const FSFPPlanEdge& Edge = Result.Edges[EdgeIndex];
				EndpointNodeIds.Add(bOutgoing ? Edge.TargetNodeId : Edge.SourceNodeId);
			}
		}
		return EndpointNodeIds.Num();
	};
	auto RequiredLinesForRate = [&](const FSFPPlanEdge& TemplateEdge, const double TotalRate)
	{
		FSFPPlanEdge Probe = TemplateEdge;
		Probe.RatePerMinute = TotalRate;
		AddTransportAdvice(Probe, Result);
		return FMath::Max(0, Probe.RequiredLines);
	};

	// Machine nodes already represent a group of physical machines. Their edge
	// labels carry the complete parallel-lane count, so machine count alone must
	// never fabricate a merger or splitter. Junctions below are created only for
	// a real topology change that also changes the required number of lanes.

	// A distributor is needed only when one real producer feeds multiple
	// consumers. RequiredLines already describes parallel belts/pipes for one
	// logical connection and must not fabricate an extra splitter by itself.
	const TMap<FString, TArray<int32>> OutgoingGroups = BuildGroups(true);
	for (const TPair<FString, TArray<int32>>& Pair : OutgoingGroups)
	{
		if (!Result.Edges.IsValidIndex(Pair.Value[0]))
		{
			continue;
		}
		const int32 ConsumerCount = DistinctEndpointCountFor(Pair.Value, true);
		if (ConsumerCount <= 1)
		{
			continue;
		}
		const FSFPPlanEdge TemplateEdge = Result.Edges[Pair.Value[0]];
		const double TotalRate = TotalRateFor(Pair.Value);
		const int32 InputLineCount = RequiredLinesForRate(TemplateEdge, TotalRate);
		const int32 OutputLineCount = TransportBranchesFor(Pair.Value);
		if (InputLineCount <= 0 || OutputLineCount <= InputLineCount)
		{
			continue;
		}
		const FSFPPlanNode* SourceNode = FindNode(TemplateEdge.SourceNodeId);
		const int32 SplitterId = AddRoutingNode(
			ESFPPlanNodeType::Splitter,
			TemplateEdge,
			ConsumerCount,
			InputLineCount,
			OutputLineCount,
			TotalRate,
			SourceNode != nullptr ? SourceNode->Depth - 1 : 0);
		for (const int32 EdgeIndex : Pair.Value)
		{
			Result.Edges[EdgeIndex].SourceNodeId = SplitterId;
		}
		AddLocalLink(TemplateEdge, TemplateEdge.SourceNodeId, SplitterId, TotalRate);
	}

	// A collector is needed only when multiple real producers converge at one
	// consumer. Three capacity lines belonging to one edge stay three parallel
	// lines; merging them into one belt would create an impossible bottleneck.
	const TMap<FString, TArray<int32>> IncomingGroups = BuildGroups(false);
	for (const TPair<FString, TArray<int32>>& Pair : IncomingGroups)
	{
		if (!Result.Edges.IsValidIndex(Pair.Value[0]))
		{
			continue;
		}
		const int32 ProviderCount = DistinctEndpointCountFor(Pair.Value, false);
		if (ProviderCount <= 1)
		{
			continue;
		}
		const FSFPPlanEdge TemplateEdge = Result.Edges[Pair.Value[0]];
		const double TotalRate = TotalRateFor(Pair.Value);
		const int32 InputLineCount = TransportBranchesFor(Pair.Value);
		const int32 OutputLineCount = RequiredLinesForRate(TemplateEdge, TotalRate);
		if (OutputLineCount <= 0 || InputLineCount <= OutputLineCount)
		{
			continue;
		}
		const FSFPPlanNode* TargetNode = FindNode(TemplateEdge.TargetNodeId);
		const int32 MergerId = AddRoutingNode(
			ESFPPlanNodeType::Merger,
			TemplateEdge,
			ProviderCount,
			InputLineCount,
			OutputLineCount,
			TotalRate,
			TargetNode != nullptr ? TargetNode->Depth + 1 : 0);
		for (const int32 EdgeIndex : Pair.Value)
		{
			Result.Edges[EdgeIndex].TargetNodeId = MergerId;
		}
		AddLocalLink(TemplateEdge, MergerId, TemplateEdge.TargetNodeId, TotalRate);
	}

	for (FSFPPlanEdge& Edge : Result.Edges)
	{
		Edge.EstimatedLengthMeters = Edge.bLocalRoutingLink ? 0.0 : Result.EstimatedConnectionLengthMeters;
		if (Edge.bLocalRoutingLink)
		{
			continue;
		}
		if (Edge.TransportKind == TEXT("pipe"))
		{
			Result.PipelineLineCount += Edge.RequiredLines;
			Result.EstimatedPipelineMeters += Edge.EstimatedLengthMeters * Edge.RequiredLines;
		}
		else if (Edge.TransportKind == TEXT("belt"))
		{
			Result.ConveyorLineCount += Edge.RequiredLines;
			Result.EstimatedConveyorMeters += Edge.EstimatedLengthMeters * Edge.RequiredLines;
		}
	}
	const bool bNeedsPipeJunctionCosts = PipeJunctionClassPath.IsEmpty()
		&& Result.Nodes.ContainsByPredicate([](const FSFPPlanNode& Node)
	{
		return (Node.Type == ESFPPlanNodeType::Splitter || Node.Type == ESFPPlanNodeType::Merger)
			&& Node.Title == TEXT("Pipeline Junction");
	});
	const bool bNeedsSolidSplitterCosts = Result.Nodes.ContainsByPredicate([this](const FSFPPlanNode& Node)
	{
		return Node.Type == ESFPPlanNodeType::Splitter
			&& Node.Title != TEXT("Pipeline Junction")
			&& (PipeJunctionClassPath.IsEmpty() || Node.ClassPath != PipeJunctionClassPath);
	});
	const bool bNeedsSolidMergerCosts = Result.Nodes.ContainsByPredicate([this](const FSFPPlanNode& Node)
	{
		return Node.Type == ESFPPlanNodeType::Merger
			&& Node.Title != TEXT("Pipeline Junction")
			&& (PipeJunctionClassPath.IsEmpty() || Node.ClassPath != PipeJunctionClassPath);
	});
	if ((bNeedsSolidSplitterCosts && SplitterClassPath.IsEmpty())
		|| (bNeedsSolidMergerCosts && MergerClassPath.IsEmpty())
		|| (bNeedsPipeJunctionCosts && PipeJunctionClassPath.IsEmpty()))
	{
		Result.Warnings.AddUnique(TEXT("Für mindestens einen angezeigten Splitter, Fusionator oder eine Pipeline Junction wurde kein Baukosten-Rezept gefunden"));
	}
}

FSFPPlanResult FSFPPlannerSolver::Solve(
	const TSubclassOf<UFGItemDescriptor> TargetItem,
	const double TargetRatePerMinute,
	const bool bOnlyAvailableRecipes,
	const TMap<FString, FString>& RecipeOverrides,
	const double EstimatedConnectionLengthMeters,
	const FString& SelectedConveyorClassPath,
	const FString& SelectedConveyorLiftClassPath,
	const TMap<FString, FSFPMachinePlanSettings>& MachineSettings,
	const TMap<FString, FSFPResourceSourceMix>& ResourceSourceMixes) const
{
	FSFPPlanTarget Target;
	Target.ItemClass = TargetItem;
	if (IsValid(TargetItem.Get()))
	{
		Target.ItemClassPath = TargetItem.Get()->GetPathName();
		Target.DisplayName = ItemDisplayName(TargetItem.Get());
		Target.Form = SolverResourceFormToString(UFGItemDescriptor::GetForm(TargetItem));
	}
	Target.RatePerMinute = TargetRatePerMinute;
	TArray<FSFPPlanTarget> SingleTarget;
	SingleTarget.Add(MoveTemp(Target));
	return Solve(
		SingleTarget,
		bOnlyAvailableRecipes,
		RecipeOverrides,
		EstimatedConnectionLengthMeters,
		SelectedConveyorClassPath,
		SelectedConveyorLiftClassPath,
		TMap<FString, double>(),
		true,
		MachineSettings,
		ResourceSourceMixes);
}

FSFPPlanResult FSFPPlannerSolver::Solve(
	const TArray<FSFPPlanTarget>& Targets,
	const bool bOnlyAvailableRecipes,
	const TMap<FString, FString>& RecipeOverrides,
	const double EstimatedConnectionLengthMeters,
	const FString& SelectedConveyorClassPath,
	const FString& SelectedConveyorLiftClassPath,
	const TMap<FString, double>& SuppliedInputs,
	const bool bEnforceSupplyLimits,
	const TMap<FString, FSFPMachinePlanSettings>& MachineSettings,
	const TMap<FString, FSFPResourceSourceMix>& ResourceSourceMixes) const
{
	FSolveContext Context;
    Context.Result.SuppliedInputRates = SuppliedInputs;
    Context.Result.bEnforceSupplyLimits = bEnforceSupplyLimits;
    // Build a shortest-path map from the user's supplied materials through
    // primary recipe outputs. A boolean reachability set alone becomes almost
    // useless in large modpacks because converter/recycling cycles eventually
    // mark most of the catalog reachable. The distance map lets recipe selection
    // keep walking toward the actual selected input instead of an external source.
    for (const auto& Input : SuppliedInputs)
    {
        Context.InputReachablePaths.Add(Input.Key);
        Context.InputDistanceByPath.Add(Input.Key, 0);
    }
    if (!SuppliedInputs.IsEmpty())
    for (int32 Pass = 0; Pass < MaxPlannerDepth; ++Pass)
    {
        bool Changed = false;
        for (const FSFPPlannerRecipe& Recipe : Recipes)
        {
            if ((bOnlyAvailableRecipes && !Recipe.bAvailable) || Recipe.Products.IsEmpty()) continue;
            UClass* PrimaryProductClass = Recipe.Products[0].ItemClass.Get();
            if (!IsValid(PrimaryProductClass) || IsAutomaticUnpackagingRoute(Recipe, PrimaryProductClass)) continue;
            if (Recipe.bDirectResourceExtraction && !Recipe.Ingredients.IsEmpty()
                && SuppliedInputs.Contains(Recipe.Ingredients[0].ClassPath)) continue;

            int32 BestIngredientDistance = MAX_int32;
            for (const FSFPPlannerItemRate& Ingredient : Recipe.Ingredients)
            {
                if (const int32* Distance = Context.InputDistanceByPath.Find(Ingredient.ClassPath))
                {
                    BestIngredientDistance = FMath::Min(BestIngredientDistance, *Distance);
                }
            }
            if (BestIngredientDistance == MAX_int32) continue;

            const FString& Output = Recipe.Products[0].ClassPath;
            const int32 CandidateDistance = BestIngredientDistance + 1;
            int32* ExistingDistance = Context.InputDistanceByPath.Find(Output);
            if (!ExistingDistance || CandidateDistance < *ExistingDistance)
            {
                Context.InputDistanceByPath.Add(Output, CandidateDistance);
                Context.InputReachablePaths.Add(Output);
                Changed = true;
            }
        }
        if (!Changed) break;
    }
	if (Targets.IsEmpty())
	{
		Context.Result.ErrorMessage = TEXT("Kein Endprodukt zur Produktionsstätte hinzugefügt");
		return Context.Result;
	}
	if (Targets.Num() > 32)
	{
		Context.Result.ErrorMessage = TEXT("Eine Produktionsstätte kann höchstens 32 Endprodukte enthalten");
		return Context.Result;
	}

	Context.bOnlyAvailableRecipes = bOnlyAvailableRecipes;
	Context.RecipeOverrides = RecipeOverrides;
	Context.MachineSettings = MachineSettings;
	Context.ResourceSourceMixes = ResourceSourceMixes;
	Context.Result.RecipeOverrides = RecipeOverrides;
	Context.Result.MachineSettings = MachineSettings;
	Context.Result.ResourceSourceMixes = ResourceSourceMixes;
	Context.Result.bOnlyAvailableRecipes = bOnlyAvailableRecipes;
	Context.Result.EstimatedConnectionLengthMeters = FMath::Clamp(EstimatedConnectionLengthMeters, 0.5, 1000.0);

	auto ResolveTransportTier = [this, bOnlyAvailableRecipes, &Context](
		const FString& Kind,
		const FString& RequestedClassPath) -> const FSFPTransportTier*
	{
		const FSFPTransportTier* FastestEligible = nullptr;
		for (const FSFPTransportTier& Tier : TransportTiers)
		{
			if (Tier.Kind != Kind || Tier.CapacityPerMinute <= 0.0
				|| (bOnlyAvailableRecipes && !Tier.bAvailable))
			{
				continue;
			}
			FastestEligible = &Tier;
			if (!RequestedClassPath.IsEmpty() && Tier.ClassPath == RequestedClassPath)
			{
				return &Tier;
			}
		}
		if (!RequestedClassPath.IsEmpty() && FastestEligible != nullptr)
		{
			Context.Result.Warnings.AddUnique(FString::Printf(
				TEXT("Gewählte Transportstufe ist nicht verfügbar; %s wird verwendet"),
				*FastestEligible->DisplayName));
		}
		return FastestEligible;
	};

	const FSFPTransportTier* Conveyor = ResolveTransportTier(TEXT("belt"), SelectedConveyorClassPath);
	const FSFPTransportTier* ConveyorLift = ResolveTransportTier(TEXT("lift"), SelectedConveyorLiftClassPath);
	if (Conveyor != nullptr)
	{
		Context.Result.SelectedConveyorClassPath = Conveyor->ClassPath;
		Context.Result.SelectedConveyorDisplayName = Conveyor->DisplayName;
		Context.Result.SelectedConveyorCapacityPerMinute = Conveyor->CapacityPerMinute;
	}
	if (ConveyorLift != nullptr)
	{
		Context.Result.SelectedConveyorLiftClassPath = ConveyorLift->ClassPath;
		Context.Result.SelectedConveyorLiftDisplayName = ConveyorLift->DisplayName;
		Context.Result.SelectedConveyorLiftCapacityPerMinute = ConveyorLift->CapacityPerMinute;
	}

	TMap<FString, int32> NormalizedTargetByPath;
	for (const FSFPPlanTarget& RequestedTarget : Targets)
	{
		UClass* TargetClass = RequestedTarget.ItemClass.Get();
		if (!IsValid(TargetClass) || !TargetClass->IsChildOf(UFGItemDescriptor::StaticClass()))
		{
			Context.Result.ErrorMessage = TEXT("Mindestens ein Endprodukt ist ungültig");
			return Context.Result;
		}
		if (!FMath::IsFinite(RequestedTarget.RatePerMinute) || RequestedTarget.RatePerMinute <= 0.0)
		{
			Context.Result.ErrorMessage = TEXT("Jede Endproduktmenge muss größer als 0 sein");
			return Context.Result;
		}

		const FString TargetPath = TargetClass->GetPathName();
		if (const int32* ExistingIndex = NormalizedTargetByPath.Find(TargetPath))
		{
			Context.Result.Targets[*ExistingIndex].RatePerMinute += RequestedTarget.RatePerMinute;
			continue;
		}

		FSFPPlanTarget NormalizedTarget;
		NormalizedTarget.ItemClass = TSubclassOf<UFGItemDescriptor>(TargetClass);
		NormalizedTarget.ItemClassPath = TargetPath;
		NormalizedTarget.DisplayName = ItemDisplayName(TargetClass);
		NormalizedTarget.Form = SolverResourceFormToString(
			UFGItemDescriptor::GetForm(NormalizedTarget.ItemClass));
		NormalizedTarget.RatePerMinute = RequestedTarget.RatePerMinute;
		NormalizedTargetByPath.Add(TargetPath, Context.Result.Targets.Add(MoveTemp(NormalizedTarget)));
	}

	Context.Result.Targets.Sort([](const FSFPPlanTarget& A, const FSFPPlanTarget& B)
	{
		return A.ItemClassPath < B.ItemClassPath;
	});

	for (const FSFPPlanTarget& Target : Context.Result.Targets)
	{
		// Every requested final product ends in its own visible storage endpoint.
		// Its incoming edge is therefore a real machine/routing-to-container line.
		UClass* TargetClass = Target.ItemClass.Get();
		FSFPPlanNode TargetNode;
		TargetNode.Id = Context.Result.Nodes.Num();
		TargetNode.Type = ESFPPlanNodeType::Target;
		TargetNode.Depth = 0;
		TargetNode.Title = Target.DisplayName;
		TargetNode.Detail = FString::Printf(
			TEXT("Lagereingang: %s/min"),
			*FSFPNumberFormatting::Decimal(Target.RatePerMinute));
		TargetNode.ClassPath = Target.ItemClassPath;
		TargetNode.RatePerMinute = Target.RatePerMinute;
		const int32 TargetNodeId = Context.Result.Nodes.Add(MoveTemp(TargetNode));

		BuildDemand(
			TargetClass,
			Target.DisplayName,
			Target.Form,
			Target.RatePerMinute,
			TargetNodeId,
			1,
			Context);
	}

	Context.Result.TargetRatePerMinute = 0.0;
	for (const FSFPPlanTarget& Target : Context.Result.Targets)
	{
		Context.Result.TargetRatePerMinute += Target.RatePerMinute;
	}
	Context.Result.TargetName = Context.Result.Targets.Num() == 1
		? Context.Result.Targets[0].DisplayName
		: FString::Printf(TEXT("%d Endprodukte"), Context.Result.Targets.Num());
	if (!BalanceMaterials(Context.Result)) return Context.Result;
	CompactMaterialGraph(Context.Result);
	BuildRoutingInfrastructure(Context.Result);
	AddConstructionCosts(Context.Result);
	Context.Result.bSuccess = Context.Result.Nodes.Num() > Context.Result.Targets.Num()
		&& Context.Result.Edges.Num() >= Context.Result.Targets.Num();
	if (!Context.Result.bSuccess)
	{
		Context.Result.ErrorMessage = TEXT("Für die Endprodukte konnte kein gemeinsamer Produktionsplan erzeugt werden");
	}
	return Context.Result;
}

FSFPPlanResult FSFPPlannerSolver::SolvePower(const FSFPPowerPlanRequest& Request) const
{
	FSFPPlanResult InvalidResult;
	InvalidResult.bPowerProductionPlan = true;
	if (!FMath::IsFinite(Request.TargetNetPowerMW) || Request.TargetNetPowerMW <= 0.0)
	{
		InvalidResult.ErrorMessage = TEXT("Die gewünschte Nettoleistung muss größer als 0 MW sein");
		return InvalidResult;
	}
	if (!FMath::IsFinite(Request.ReservePercent)
		|| Request.ReservePercent < 0.0
		|| Request.ReservePercent > 500.0)
	{
		InvalidResult.ErrorMessage = TEXT("Die Leistungsreserve muss zwischen 0 und 500 Prozent liegen");
		return InvalidResult;
	}
	if (!FMath::IsFinite(Request.GeneratorClockPercent)
		|| Request.GeneratorClockPercent < 1.0
		|| Request.GeneratorClockPercent > 100000.0)
	{
		InvalidResult.ErrorMessage = TEXT("Der Generator-Takt muss zwischen 1 und 100000 Prozent liegen");
		return InvalidResult;
	}
	if (Request.PassiveAlienPowerAugmenters < 0 || Request.FueledAlienPowerAugmenters < 0
		|| Request.PassiveAlienPowerAugmenters > 10000 || Request.FueledAlienPowerAugmenters > 10000)
	{
		InvalidResult.ErrorMessage = TEXT("Die Anzahl der Alien Power Augmenter ist ungültig");
		return InvalidResult;
	}
	const int32 RequestedAlienPowerAugmenters = Request.PassiveAlienPowerAugmenters
		+ Request.FueledAlienPowerAugmenters;
	if (RequestedAlienPowerAugmenters > 0)
	{
		if (!IsValid(AlienPowerAugmenter.BuildableClass))
		{
			InvalidResult.ErrorMessage = TEXT("Alien Power Augmenter konnte im aktiven Spielstand nicht gefunden werden");
			return InvalidResult;
		}
		if (Request.bOnlyAvailable && !AlienPowerAugmenter.bAvailable)
		{
			InvalidResult.ErrorMessage = TEXT("Alien Power Augmenter ist in diesem Spielstand noch nicht freigeschaltet");
			return InvalidResult;
		}
		if (Request.FueledAlienPowerAugmenters > 0
			&& !IsValid(AlienPowerAugmenter.MatrixItemClass.Get()))
		{
			InvalidResult.ErrorMessage = TEXT("Alien Power Matrix konnte für den gespeisten Power Augmenter nicht gefunden werden");
			return InvalidResult;
		}
	}
	if (PowerGenerators.IsEmpty())
	{
		InvalidResult.ErrorMessage = TEXT("Keine vollständigen Stromerzeuger im aktiven Baukatalog gefunden");
		return InvalidResult;
	}

	struct FPowerCandidate
	{
		const FSFPPowerGeneratorOption* Generator = nullptr;
		const FSFPPowerFuelOption* Fuel = nullptr;
	};
	TArray<FPowerCandidate> Candidates;
	for (const TSharedPtr<FSFPPowerGeneratorOption>& Generator : PowerGenerators)
	{
		if (!Generator.IsValid()
			|| (!Request.GeneratorClassPath.IsEmpty()
				&& Generator->ClassPath != Request.GeneratorClassPath)
			|| (Request.bOnlyAvailable && !Generator->bAvailable))
		{
			continue;
		}
		const double MinGeneratorClock = GeneratorMinClockPercent(*Generator);
		const double MaxGeneratorClock = GeneratorMaxClockPercent(*Generator);
		if (Request.GeneratorClockPercent + 0.001 < MinGeneratorClock
			|| Request.GeneratorClockPercent > MaxGeneratorClock + 0.001)
		{
			continue;
		}
		for (const FSFPPowerFuelOption& Fuel : Generator->Fuels)
		{
			if ((!Request.FuelClassPath.IsEmpty() && Fuel.ClassPath != Request.FuelClassPath)
				|| (Request.bOnlyAvailable && !Fuel.bAvailable)
				|| (!Fuel.bFuelFree && Fuel.EnergyValueMJ <= KINDA_SMALL_NUMBER))
			{
				continue;
			}
			FPowerCandidate Candidate;
			Candidate.Generator = Generator.Get();
			Candidate.Fuel = &Fuel;
			Candidates.Add(Candidate);
		}
	}
	if (Candidates.IsEmpty())
	{
		InvalidResult.ErrorMessage = Request.bOnlyAvailable
			? TEXT("Die gewählte Generator-/Brennstoffkombination ist in diesem Spielstand nicht freigeschaltet")
			: TEXT("Die gewählte Generator-/Brennstoffkombination ist nicht geladen");
		return InvalidResult;
	}

	auto BuildCandidate = [this, &Request](
		const FPowerCandidate& Candidate,
		const double GeneratorGrossPowerMW,
		FSFPPlanResult& OutResult) -> bool
	{
		OutResult = FSFPPlanResult();
		OutResult.bPowerProductionPlan = true;
		OutResult.RequestedNetPowerMW = Request.TargetNetPowerMW;
		OutResult.PowerReservePercent = Request.ReservePercent;
		OutResult.RequestedGeneratorClassPath = Request.GeneratorClassPath;
		OutResult.RequestedFuelClassPath = Request.FuelClassPath;
		OutResult.ConfiguredGeneratorClockPercent = Request.GeneratorClockPercent;
		OutResult.PassiveAlienPowerAugmenters = FMath::Max(0, Request.PassiveAlienPowerAugmenters);
		OutResult.FueledAlienPowerAugmenters = FMath::Max(0, Request.FueledAlienPowerAugmenters);
		OutResult.RecipeOverrides = Request.RecipeOverrides;
		OutResult.MachineSettings = Request.MachineSettings;
		OutResult.ResourceSourceMixes = Request.ResourceSourceMixes;
		OutResult.bOnlyAvailableRecipes = Request.bOnlyAvailable;
		OutResult.EstimatedConnectionLengthMeters = FMath::Clamp(
			Request.EstimatedConnectionLengthMeters,
			0.5,
			1000.0);

		if (Candidate.Generator == nullptr || Candidate.Fuel == nullptr
			|| Candidate.Generator->PowerProductionMW <= KINDA_SMALL_NUMBER
			|| (!Candidate.Fuel->bFuelFree
				&& Candidate.Fuel->EnergyValueMJ <= KINDA_SMALL_NUMBER)
			|| !FMath::IsFinite(GeneratorGrossPowerMW) || GeneratorGrossPowerMW < 0.0)
		{
			return false;
		}

		const SFPVariablePower::AlienAugmentation Augmentation = SFPVariablePower::ApplyAlienAugmentation(
			GeneratorGrossPowerMW,
			OutResult.PassiveAlienPowerAugmenters,
			OutResult.FueledAlienPowerAugmenters,
			AlienPowerAugmenter.BasePowerPerAugmenterMW,
			AlienPowerAugmenter.PassiveBoostPerAugmenter,
			AlienPowerAugmenter.FueledBoostPerAugmenter);
		OutResult.BaseGeneratorGrossPowerMW = GeneratorGrossPowerMW;
		OutResult.AlienPowerAugmenterBaseMW = static_cast<double>(
			OutResult.PassiveAlienPowerAugmenters + OutResult.FueledAlienPowerAugmenters)
			* AlienPowerAugmenter.BasePowerPerAugmenterMW;
		OutResult.AlienPowerMultiplier = Augmentation.Multiplier;
		OutResult.AlienPowerContributionMW = Augmentation.Contribution;
		OutResult.AlienPowerMatrixItemClassPath = AlienPowerAugmenter.MatrixItemClassPath;
		OutResult.AlienPowerMatrixDisplayName = AlienPowerAugmenter.MatrixDisplayName;
		OutResult.AlienPowerMatrixRatePerMinute = static_cast<double>(OutResult.FueledAlienPowerAugmenters)
			* AlienPowerAugmenter.MatrixRatePerMinute;

		auto ResolveTransportTier = [this, &Request, &OutResult](
			const FString& Kind,
			const FString& RequestedClassPath) -> const FSFPTransportTier*
		{
			const FSFPTransportTier* FastestEligible = nullptr;
			for (const FSFPTransportTier& Tier : TransportTiers)
			{
				if (Tier.Kind != Kind || Tier.CapacityPerMinute <= 0.0
					|| (Request.bOnlyAvailable && !Tier.bAvailable))
				{
					continue;
				}
				FastestEligible = &Tier;
				if (!RequestedClassPath.IsEmpty() && Tier.ClassPath == RequestedClassPath)
				{
					return &Tier;
				}
			}
			if (!RequestedClassPath.IsEmpty() && FastestEligible != nullptr)
			{
				OutResult.Warnings.AddUnique(FString::Printf(
					TEXT("Gewählte Transportstufe ist nicht verfügbar; %s wird verwendet"),
					*FastestEligible->DisplayName));
			}
			return FastestEligible;
		};
		const FSFPTransportTier* Conveyor = ResolveTransportTier(
			TEXT("belt"),
			Request.SelectedConveyorClassPath);
		const FSFPTransportTier* ConveyorLift = ResolveTransportTier(
			TEXT("lift"),
			Request.SelectedConveyorLiftClassPath);
		if (Conveyor != nullptr)
		{
			OutResult.SelectedConveyorClassPath = Conveyor->ClassPath;
			OutResult.SelectedConveyorDisplayName = Conveyor->DisplayName;
			OutResult.SelectedConveyorCapacityPerMinute = Conveyor->CapacityPerMinute;
		}
		if (ConveyorLift != nullptr)
		{
			OutResult.SelectedConveyorLiftClassPath = ConveyorLift->ClassPath;
			OutResult.SelectedConveyorLiftDisplayName = ConveyorLift->DisplayName;
			OutResult.SelectedConveyorLiftCapacityPerMinute = ConveyorLift->CapacityPerMinute;
		}

		OutResult.TargetName = TEXT("Stromversorgung");
		OutResult.TargetRatePerMinute = Request.TargetNetPowerMW;
		OutResult.SelectedGeneratorClassPath = Candidate.Generator->ClassPath;
		OutResult.SelectedGeneratorDisplayName = Candidate.Generator->DisplayName;
		OutResult.SelectedFuelClassPath = Candidate.Fuel->ClassPath;
		OutResult.SelectedFuelDisplayName = Candidate.Fuel->DisplayName;
		OutResult.SelectedFuelForm = Candidate.Fuel->Form;
		OutResult.GeneratorBasePowerMW = Candidate.Generator->PowerProductionMW;
		const double GeneratorClockFactor = Request.GeneratorClockPercent / 100.0;
		OutResult.GeneratorPowerMW = Candidate.Generator->PowerProductionMW * GeneratorClockFactor;
		OutResult.GrossPowerMW = Augmentation.GrossPower;
		OutResult.EquivalentGeneratorCount = GeneratorGrossPowerMW / Candidate.Generator->PowerProductionMW;
		OutResult.FullClockGeneratorCount = FMath::Max(
			0,
			FMath::FloorToInt((OutResult.EquivalentGeneratorCount + KINDA_SMALL_NUMBER) / GeneratorClockFactor));
		const double RemainingEquivalentGenerators = FMath::Max(
			0.0,
			OutResult.EquivalentGeneratorCount
				- static_cast<double>(OutResult.FullClockGeneratorCount) * GeneratorClockFactor);
		OutResult.PartialGeneratorClockPercent = RemainingEquivalentGenerators * 100.0;
		OutResult.BuiltGeneratorCount = OutResult.FullClockGeneratorCount
			+ (OutResult.PartialGeneratorClockPercent > 0.05 ? 1 : 0);

		if (Candidate.Fuel->bFuelFree)
		{
			OutResult.FuelRatePerMinute = 0.0;
		}
		else if (Candidate.Fuel->ConsumptionRatePerGenerator > KINDA_SMALL_NUMBER)
		{
			OutResult.FuelRatePerMinute = OutResult.EquivalentGeneratorCount
				* Candidate.Fuel->ConsumptionRatePerGenerator;
		}
		else
		{
			double RawFuelRatePerMinute = GeneratorGrossPowerMW * 60.0
				/ Candidate.Fuel->EnergyValueMJ;
			const bool bFluidFuel = Candidate.Fuel->Form == TEXT("liquid")
				|| Candidate.Fuel->Form == TEXT("gas");
			if (bFluidFuel)
			{
				RawFuelRatePerMinute /= 1000.0;
			}
			OutResult.FuelRatePerMinute = RawFuelRatePerMinute;
		}
		OutResult.SupplementalItemClassPath = Candidate.Generator->SupplementalItemClassPath;
		OutResult.SupplementalDisplayName = Candidate.Generator->SupplementalDisplayName;
		OutResult.SupplementalForm = Candidate.Generator->SupplementalForm;
		OutResult.SupplementalRatePerMinute =
			OutResult.EquivalentGeneratorCount * Candidate.Generator->SupplementalRatePerMinute;
		OutResult.WasteItemClassPath = Candidate.Fuel->WasteItemClassPath;
		OutResult.WasteDisplayName = Candidate.Fuel->WasteDisplayName;
		OutResult.WasteForm = Candidate.Fuel->WasteForm;
		OutResult.WasteRatePerMinute =
			OutResult.FuelRatePerMinute * Candidate.Fuel->WasteAmountPerFuel;

		FSolveContext Context;
		Context.bOnlyAvailableRecipes = Request.bOnlyAvailable;
		Context.RecipeOverrides = Request.RecipeOverrides;
		Context.MachineSettings = Request.MachineSettings;
		Context.ResourceSourceMixes = Request.ResourceSourceMixes;
		Context.Result = OutResult;

		FSFPPlanNode TargetNode;
		TargetNode.Id = 0;
		TargetNode.Type = ESFPPlanNodeType::Target;
		TargetNode.Depth = 0;
		TargetNode.Title = TEXT("Stromnetz");
		TargetNode.Detail = FString::Printf(
			TEXT("Ziel: %s MW netto\nReserve: %s%%"),
			*FSFPNumberFormatting::Decimal(Request.TargetNetPowerMW, 2),
			*FSFPNumberFormatting::Decimal(Request.ReservePercent, 1));
		TargetNode.ClassPath = TEXT("SFP.PowerGrid");
		TargetNode.RatePerMinute = Request.TargetNetPowerMW;
		Context.Result.Nodes.Add(MoveTemp(TargetNode));

		FSFPPlanNode GeneratorNode;
		GeneratorNode.Id = 1;
		GeneratorNode.Type = ESFPPlanNodeType::Generator;
		GeneratorNode.Depth = 1;
		GeneratorNode.Title = Candidate.Generator->DisplayName;
		GeneratorNode.ClassPath = Candidate.Generator->ClassPath;
		GeneratorNode.ProducedItemClassPath = TEXT("SFP.ElectricPower");
		GeneratorNode.RatePerMinute = GeneratorGrossPowerMW;
		GeneratorNode.MachineCount = Context.Result.EquivalentGeneratorCount;
		GeneratorNode.BuiltMachineCount = Context.Result.BuiltGeneratorCount;
		GeneratorNode.FullClockMachineCount = Context.Result.FullClockGeneratorCount;
		GeneratorNode.ConfiguredClockPercent = Context.Result.ConfiguredGeneratorClockPercent;
		GeneratorNode.PartialClockPercent = Context.Result.PartialGeneratorClockPercent;
		GeneratorNode.PowerMW = GeneratorGrossPowerMW;
		Context.Result.Nodes.Add(MoveTemp(GeneratorNode));
		Context.Result.TotalEquivalentMachines += Context.Result.EquivalentGeneratorCount;
		Context.Result.MaxDepth = 1;

		int32 AlienPowerAugmenterNodeId = INDEX_NONE;
		const int32 AlienPowerAugmenterCount = Context.Result.PassiveAlienPowerAugmenters
			+ Context.Result.FueledAlienPowerAugmenters;
		if (AlienPowerAugmenterCount > 0)
		{
			FSFPPlanNode AugmenterNode;
			AugmenterNode.Id = Context.Result.Nodes.Num();
			AugmenterNode.Type = ESFPPlanNodeType::Generator;
			AugmenterNode.Depth = 1;
			AugmenterNode.Title = AlienPowerAugmenter.DisplayName.IsEmpty()
				? TEXT("Alien Power Augmenter") : AlienPowerAugmenter.DisplayName;
			AugmenterNode.ClassPath = AlienPowerAugmenter.ClassPath;
			AugmenterNode.ProducedItemClassPath = TEXT("SFP.AlienAugmentedPower");
			AugmenterNode.RatePerMinute = Context.Result.AlienPowerContributionMW;
			AugmenterNode.MachineCount = static_cast<double>(AlienPowerAugmenterCount);
			AugmenterNode.BuiltMachineCount = AlienPowerAugmenterCount;
			AugmenterNode.PowerMW = Context.Result.AlienPowerContributionMW;
			AugmenterNode.Detail = FString::Printf(
				TEXT("%d passiv + %d mit Matrix\n%s MW Basis je Gebäude | Netzfaktor ×%s\nBeitrag zur Bruttoerzeugung: %s MW"),
				Context.Result.PassiveAlienPowerAugmenters,
				Context.Result.FueledAlienPowerAugmenters,
				*FSFPNumberFormatting::Decimal(AlienPowerAugmenter.BasePowerPerAugmenterMW, 1),
				*FSFPNumberFormatting::Decimal(Context.Result.AlienPowerMultiplier, 3),
				*FSFPNumberFormatting::Decimal(Context.Result.AlienPowerContributionMW, 2));
			AlienPowerAugmenterNodeId = Context.Result.Nodes.Add(MoveTemp(AugmenterNode));
			Context.Result.TotalEquivalentMachines += AlienPowerAugmenterCount;
		}

		int32 WasteSourceNodeId = 1;
		auto AddPowerComponentNode = [this, &Context](
			const FString& Title,
			const FString& Detail,
			UClass* BuildableClass,
			const FString& ClassPath,
			const double Count,
			const int32 Depth,
			const FString& AttachmentClassPath)
		{
			FSFPPlanNode Node;
			Node.Id = Context.Result.Nodes.Num();
			Node.Type = ESFPPlanNodeType::Machine;
			Node.Depth = Depth;
			Node.Title = Title;
			Node.Detail = Detail;
			Node.ClassPath = ClassPath;
			Node.MachineCount = Count;
			const double UnitPowerMW = OptionalBuildablePower(BuildableClass);
			Node.PowerMW = UnitPowerMW * Count;
			if (!AttachmentClassPath.IsEmpty())
			{
				Node.AdditionalBuildableClassPaths.Add(AttachmentClassPath);
			}
			const int32 NodeId = Context.Result.Nodes.Add(MoveTemp(Node));
			Context.Result.TotalEquivalentMachines += Count;
			Context.Result.TotalBasePowerMW += UnitPowerMW * Count;
			Context.Result.MaxDepth = FMath::Max(Context.Result.MaxDepth, Depth);
			return NodeId;
		};
		auto AddPowerComponentEdge = [this, &Context](
			const int32 SourceNodeId,
			const int32 TargetNodeId,
			const FString& ItemName,
			const FString& ItemClassPath,
			const FString& Form,
			const double RatePerMinute,
			const bool bLocal,
			const FString& LocalLabel)
		{
			if (RatePerMinute <= KINDA_SMALL_NUMBER)
			{
				return;
			}
			FSFPPlanEdge Edge;
			Edge.SourceNodeId = SourceNodeId;
			Edge.TargetNodeId = TargetNodeId;
			Edge.ItemName = ItemName;
			Edge.ItemClassPath = ItemClassPath;
			Edge.Form = Form;
			Edge.RatePerMinute = RatePerMinute;
			Edge.bLocalRoutingLink = bLocal;
			if (bLocal)
			{
				Edge.RequiredLines = 1;
				Edge.TransportKind = TEXT("local");
				Edge.TransportLabel = LocalLabel;
			}
			else
			{
				AddTransportAdvice(Edge, Context.Result);
			}
			Context.Result.Edges.Add(MoveTemp(Edge));
		};

		if (Candidate.Generator->bModularPower)
		{
			const double GeneratorSets = Context.Result.EquivalentGeneratorCount;
			const double TurbineSteamRate = GeneratorSets
				* Candidate.Generator->TurbineSteamRatePerMinute;
			const int32 TurbineNodeId = AddPowerComponentNode(
				Candidate.Generator->TurbineDisplayName,
				FString::Printf(
					TEXT("%s Sätze | %s Dampf/min\nMaximal %s UPM"),
					*FSFPNumberFormatting::Decimal(GeneratorSets, 3),
					*FSFPNumberFormatting::Decimal(TurbineSteamRate),
					*FSFPNumberFormatting::Decimal(Candidate.Generator->TurbineMaximumRPM, 0)),
				Candidate.Generator->TurbineClass,
				Candidate.Generator->TurbineClassPath,
				GeneratorSets,
				2,
				FString());
			Context.Result.Nodes[TurbineNodeId].ProducedItemClassPath = TEXT("SFP.RotationalPower");
			const int32 ConverterPlatformNodeId = AddPowerComponentNode(
				Candidate.Generator->ConverterPlatformDisplayName,
				FString::Printf(
					TEXT("%s Plattformen für Turbine und Generator"),
					*FSFPNumberFormatting::Decimal(GeneratorSets, 3)),
				Candidate.Generator->ConverterPlatformClass,
				Candidate.Generator->ConverterPlatformClassPath,
				GeneratorSets,
				3,
				FString());
			Context.Result.Nodes[ConverterPlatformNodeId].ProducedItemClassPath = TEXT("SFP.StructuralMount");
			AddPowerComponentEdge(
				ConverterPlatformNodeId,
				TurbineNodeId,
				TEXT("Montagebasis"),
				TEXT("SFP.StructuralMount"),
				TEXT("local"),
				GeneratorSets,
				true,
				TEXT("Turbine und Generator auf Konverter-Plattform"));
			AddPowerComponentEdge(
				TurbineNodeId,
				1,
				TEXT("Rotationsenergie"),
				TEXT("SFP.RotationalPower"),
				TEXT("rotation"),
				GeneratorSets * Candidate.Generator->TurbineMaximumRPM,
				true,
				FString::Printf(
					TEXT("Direktkupplung | %s UPM je Satz"),
					*FSFPNumberFormatting::Decimal(Candidate.Generator->TurbineMaximumRPM, 0)));

			const double BoilerCount = GeneratorSets * Candidate.Fuel->BoilerCountPerGenerator;
			const double WaterRate = GeneratorSets * Candidate.Fuel->WaterRatePerGenerator;
			const int32 BoilerNodeId = AddPowerComponentNode(
				Candidate.Fuel->BoilerDisplayName,
				FString::Printf(
					TEXT("%s Kesseläquivalente\n%s Wasser/min → %s Dampf/min"),
					*FSFPNumberFormatting::Decimal(BoilerCount, 3),
					*FSFPNumberFormatting::Decimal(WaterRate),
					*FSFPNumberFormatting::Decimal(TurbineSteamRate)),
				Candidate.Fuel->BoilerClass,
				Candidate.Fuel->BoilerClassPath,
				BoilerCount,
				3,
				FString());
			Context.Result.Nodes[BoilerNodeId].ProducedItemClassPath =
				Candidate.Fuel->HighSteamItemClassPath.IsEmpty()
					? TEXT("SFP.HighPressureSteam") : Candidate.Fuel->HighSteamItemClassPath;
			AddPowerComponentEdge(
				BoilerNodeId,
				TurbineNodeId,
				Candidate.Fuel->HighSteamDisplayName.IsEmpty()
					? TEXT("Hochdruckdampf") : Candidate.Fuel->HighSteamDisplayName,
				Candidate.Fuel->HighSteamItemClassPath.IsEmpty()
					? TEXT("SFP.HighPressureSteam") : Candidate.Fuel->HighSteamItemClassPath,
				Candidate.Fuel->HighSteamForm.IsEmpty()
					? TEXT("gas") : Candidate.Fuel->HighSteamForm,
				TurbineSteamRate,
				false,
				FString());

			const double HeaterCount = GeneratorSets * Candidate.Fuel->HeaterCountPerGenerator;
			const int32 HeaterNodeId = AddPowerComponentNode(
				Candidate.Fuel->HeaterDisplayName,
				FString::Printf(
					TEXT("%s Heizkesseläquivalente\n%s: %s/min"),
					*FSFPNumberFormatting::Decimal(HeaterCount, 3),
					*Candidate.Fuel->DisplayName,
					*FSFPNumberFormatting::Decimal(Context.Result.FuelRatePerMinute)),
				Candidate.Fuel->HeaterClass,
				Candidate.Fuel->HeaterClassPath,
				HeaterCount,
				4,
				FString());
			Context.Result.Nodes[HeaterNodeId].ProducedItemClassPath = TEXT("SFP.ProcessHeat");
			WasteSourceNodeId = HeaterNodeId;
			AddPowerComponentEdge(
				HeaterNodeId,
				BoilerNodeId,
				TEXT("Prozesswärme"),
				TEXT("SFP.ProcessHeat"),
				TEXT("heat"),
				FMath::Max(HeaterCount, KINDA_SMALL_NUMBER),
				true,
				TEXT("Direkt auf Heizkessel-Plattform"));
			const int32 BoilerPlatformNodeId = AddPowerComponentNode(
				Candidate.Fuel->BoilerPlatformDisplayName,
				FString::Printf(
					TEXT("%s Plattformen für Heizkessel und Dampfkessel"),
					*FSFPNumberFormatting::Decimal(BoilerCount, 3)),
				Candidate.Fuel->BoilerPlatformClass,
				Candidate.Fuel->BoilerPlatformClassPath,
				BoilerCount,
				5,
				FString());
			Context.Result.Nodes[BoilerPlatformNodeId].ProducedItemClassPath = TEXT("SFP.StructuralMount");
			AddPowerComponentEdge(
				BoilerPlatformNodeId,
				HeaterNodeId,
				TEXT("Montagebasis"),
				TEXT("SFP.StructuralMount"),
				TEXT("local"),
				BoilerCount,
				true,
				TEXT("Heizkessel und Dampfkessel auf Heizkessel-Plattform"));

			if (IsValid(Candidate.Fuel->ItemClass.Get())
				&& Context.Result.FuelRatePerMinute > KINDA_SMALL_NUMBER)
			{
				BuildDemand(
					Candidate.Fuel->ItemClass.Get(),
					Candidate.Fuel->DisplayName,
					Candidate.Fuel->Form,
					Context.Result.FuelRatePerMinute,
					HeaterNodeId,
					5,
					Context);
			}
			if (IsValid(Candidate.Fuel->WaterItemClass.Get())
				&& WaterRate > KINDA_SMALL_NUMBER)
			{
				BuildDemand(
					Candidate.Fuel->WaterItemClass.Get(),
					Candidate.Fuel->WaterDisplayName,
					Candidate.Fuel->WaterForm,
					WaterRate,
					BoilerNodeId,
					4,
					Context);
			}

			const double LowSteamRate = GeneratorSets * Candidate.Fuel->LowSteamRatePerGenerator;
			if (LowSteamRate > KINDA_SMALL_NUMBER)
			{
				const int32 SteamCoolerNodeId = AddPowerComponentNode(
					Candidate.Fuel->SteamCoolerDisplayName,
					FString::Printf(
						TEXT("Turbinenabgang: %s/min"),
						*FSFPNumberFormatting::Decimal(LowSteamRate)),
					Candidate.Fuel->SteamCoolerClass,
					Candidate.Fuel->SteamCoolerClassPath,
					GeneratorSets,
					1,
					FString());
				AddPowerComponentEdge(
					TurbineNodeId,
					SteamCoolerNodeId,
					Candidate.Fuel->LowSteamDisplayName.IsEmpty()
						? TEXT("Niederdruckdampf") : Candidate.Fuel->LowSteamDisplayName,
					Candidate.Fuel->LowSteamItemClassPath.IsEmpty()
						? TEXT("SFP.LowPressureSteam") : Candidate.Fuel->LowSteamItemClassPath,
					Candidate.Fuel->LowSteamForm.IsEmpty()
						? TEXT("gas") : Candidate.Fuel->LowSteamForm,
					LowSteamRate,
					false,
					FString());
				const int32 SteamCoolingPlatformNodeId = AddPowerComponentNode(
					Candidate.Fuel->CoolingPlatformDisplayName,
					FString::Printf(
						TEXT("%s Plattformen für Dampf-Kühlturm"),
						*FSFPNumberFormatting::Decimal(GeneratorSets, 3)),
					Candidate.Fuel->CoolingPlatformClass,
					Candidate.Fuel->CoolingPlatformClassPath,
					GeneratorSets,
					2,
					FString());
				Context.Result.Nodes[SteamCoolingPlatformNodeId].ProducedItemClassPath = TEXT("SFP.StructuralMount");
				AddPowerComponentEdge(
					SteamCoolingPlatformNodeId,
					SteamCoolerNodeId,
					TEXT("Montagebasis"),
					TEXT("SFP.StructuralMount"),
					TEXT("local"),
					GeneratorSets,
					true,
					TEXT("Dampf-Kühlturm auf Abgasplattform"));
			}

			const double ExhaustRate = GeneratorSets * Candidate.Fuel->ExhaustRatePerGenerator;
			if (ExhaustRate > KINDA_SMALL_NUMBER)
			{
				const int32 ExhaustCoolerNodeId = AddPowerComponentNode(
					Candidate.Fuel->ExhaustCoolerDisplayName,
					FString::Printf(
						TEXT("Abgasbehandlung: %s/min"),
						*FSFPNumberFormatting::Decimal(ExhaustRate)),
					Candidate.Fuel->ExhaustCoolerClass,
					Candidate.Fuel->ExhaustCoolerClassPath,
					HeaterCount,
					3,
					FString());
				AddPowerComponentEdge(
					HeaterNodeId,
					ExhaustCoolerNodeId,
					Candidate.Fuel->ExhaustDisplayName.IsEmpty()
						? TEXT("Rauchgas") : Candidate.Fuel->ExhaustDisplayName,
					Candidate.Fuel->ExhaustItemClassPath.IsEmpty()
						? TEXT("SFP.FlueGas") : Candidate.Fuel->ExhaustItemClassPath,
					Candidate.Fuel->ExhaustForm.IsEmpty()
						? TEXT("gas") : Candidate.Fuel->ExhaustForm,
					ExhaustRate,
					false,
					FString());
				const int32 ExhaustCoolingPlatformNodeId = AddPowerComponentNode(
					Candidate.Fuel->CoolingPlatformDisplayName,
					FString::Printf(
						TEXT("%s Plattformen für Rauchgas-Schornstein"),
						*FSFPNumberFormatting::Decimal(HeaterCount, 3)),
					Candidate.Fuel->CoolingPlatformClass,
					Candidate.Fuel->CoolingPlatformClassPath,
					HeaterCount,
					4,
					FString());
				Context.Result.Nodes[ExhaustCoolingPlatformNodeId].ProducedItemClassPath = TEXT("SFP.StructuralMount");
				AddPowerComponentEdge(
					ExhaustCoolingPlatformNodeId,
					ExhaustCoolerNodeId,
					TEXT("Montagebasis"),
					TEXT("SFP.StructuralMount"),
					TEXT("local"),
					HeaterCount,
					true,
					TEXT("Rauchgas-Schornstein auf Abgasplattform"));
			}
		}
		else
		{
			if (!Candidate.Fuel->bFuelFree
				&& IsValid(Candidate.Fuel->ItemClass.Get())
				&& Context.Result.FuelRatePerMinute > KINDA_SMALL_NUMBER)
			{
				BuildDemand(
					Candidate.Fuel->ItemClass.Get(),
					Candidate.Fuel->DisplayName,
					Candidate.Fuel->Form,
					Context.Result.FuelRatePerMinute,
					1,
					2,
					Context);
			}
			if (IsValid(Candidate.Generator->SupplementalItemClass.Get())
				&& Context.Result.SupplementalRatePerMinute > KINDA_SMALL_NUMBER)
			{
				BuildDemand(
					Candidate.Generator->SupplementalItemClass.Get(),
					Candidate.Generator->SupplementalDisplayName,
					Candidate.Generator->SupplementalForm,
					Context.Result.SupplementalRatePerMinute,
					1,
					2,
					Context);
			}
		}

		if (Context.Result.AlienPowerMatrixRatePerMinute > KINDA_SMALL_NUMBER
			&& AlienPowerAugmenterNodeId != INDEX_NONE
			&& IsValid(AlienPowerAugmenter.MatrixItemClass.Get()))
		{
			BuildDemand(
				AlienPowerAugmenter.MatrixItemClass.Get(),
				AlienPowerAugmenter.MatrixDisplayName.IsEmpty() ? TEXT("Alien Power Matrix") : AlienPowerAugmenter.MatrixDisplayName,
				AlienPowerAugmenter.MatrixForm.IsEmpty() ? TEXT("solid") : AlienPowerAugmenter.MatrixForm,
				Context.Result.AlienPowerMatrixRatePerMinute,
				AlienPowerAugmenterNodeId,
				2,
				Context);
		}

		if (IsValid(Candidate.Fuel->WasteItemClass.Get())
			&& Context.Result.WasteRatePerMinute > KINDA_SMALL_NUMBER)
		{
			FSFPPlanNode WasteNode;
			WasteNode.Id = Context.Result.Nodes.Num();
			WasteNode.Type = ESFPPlanNodeType::Byproduct;
			WasteNode.Depth = 0;
			WasteNode.Title = Candidate.Fuel->WasteDisplayName;
			WasteNode.Detail = FString::Printf(
				TEXT("Lagereingang Anlagenabfall: %s/min"),
				*FSFPNumberFormatting::Decimal(Context.Result.WasteRatePerMinute));
			WasteNode.ClassPath = Candidate.Fuel->WasteItemClassPath;
			WasteNode.RatePerMinute = Context.Result.WasteRatePerMinute;
			const int32 WasteNodeId = Context.Result.Nodes.Add(MoveTemp(WasteNode));

			FSFPPlanEdge WasteEdge;
			WasteEdge.SourceNodeId = WasteSourceNodeId;
			WasteEdge.TargetNodeId = WasteNodeId;
			WasteEdge.ItemName = Candidate.Fuel->WasteDisplayName;
			WasteEdge.ItemClassPath = Candidate.Fuel->WasteItemClassPath;
			WasteEdge.Form = Candidate.Fuel->WasteForm;
			WasteEdge.RatePerMinute = Context.Result.WasteRatePerMinute;
			AddTransportAdvice(WasteEdge, Context.Result);
			Context.Result.Edges.Add(MoveTemp(WasteEdge));
		}

		if (!BalanceMaterials(Context.Result))
		{
			OutResult = MoveTemp(Context.Result);
			return false;
		}
		// Material balancing can eliminate an initially-created fallback through
		// coproduct reuse. Judge feasibility from the final balanced withdrawal,
		// not merely from the recursive expansion before balancing.
		Context.Result.bResourceSourceLimitsExceeded = false;
		double LargestBalancedShortage = 0.0;
		for (const FSFPPlanNode& Node : Context.Result.Nodes)
		{
			if (Node.Type != ESFPPlanNodeType::Source
				|| Node.SourceCostMultiplier < 100000.0
				|| Node.RatePerMinute <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			Context.Result.bResourceSourceLimitsExceeded = true;
			if (Node.RatePerMinute > LargestBalancedShortage)
			{
				LargestBalancedShortage = Node.RatePerMinute;
				const bool bSameResource = Context.Result.LimitingResourceClassPath == Node.ClassPath;
				Context.Result.LimitingResourceClassPath = Node.ClassPath;
				Context.Result.LimitingResourceDisplayName = Node.Title;
				Context.Result.LimitingResourceShortagePerMinute = Node.RatePerMinute;
				if (!bSameResource)
				{
					Context.Result.LimitingResourceCapacityPerMinute = 0.0;
				}
			}
		}

		Context.Result.SelfConsumptionPowerMW = Context.Result.TotalBasePowerMW;
		Context.Result.NetPowerMW = FMath::Max(
			0.0,
			Context.Result.GrossPowerMW - Context.Result.SelfConsumptionPowerMW);
		Context.Result.ReservePowerMW = FMath::Max(
			0.0,
			Context.Result.NetPowerMW - Context.Result.RequestedNetPowerMW);

		FString ClockingText;
		if (Context.Result.PartialGeneratorClockPercent > 0.05)
		{
			ClockingText = Context.Result.FullClockGeneratorCount > 0
				? FString::Printf(
					TEXT("%d × %s%% + 1 × %s%%"),
					Context.Result.FullClockGeneratorCount,
					*FSFPNumberFormatting::Decimal(Context.Result.ConfiguredGeneratorClockPercent, 1),
					*FSFPNumberFormatting::Decimal(Context.Result.PartialGeneratorClockPercent, 1))
				: FString::Printf(
					TEXT("1 × %s%%"),
					*FSFPNumberFormatting::Decimal(Context.Result.PartialGeneratorClockPercent, 1));
		}
		else
		{
			ClockingText = FString::Printf(
				TEXT("%d × %s%%"),
				Context.Result.BuiltGeneratorCount,
				*FSFPNumberFormatting::Decimal(Context.Result.ConfiguredGeneratorClockPercent, 1));
		}
		const FString GenerationQualifier = Candidate.Generator->bVariableOutput
			? TEXT(" maximal") : FString();
		const FString ConfigurationSuffix = Candidate.Generator->ConfigurationDetail.IsEmpty()
			? FString()
			: FString::Printf(TEXT(" | %s"), *Candidate.Generator->ConfigurationDetail);
		Context.Result.Nodes[1].Detail = FString::Printf(
			TEXT("%s%s\n%d Generatoren gebaut | %s\n%s MW je Generator bei %s%% | %s MW%s Generatorbasis\n%s MW%s gesamt brutto | %s MW%s netto"),
			*Candidate.Fuel->DisplayName,
			*ConfigurationSuffix,
			Context.Result.BuiltGeneratorCount,
			*ClockingText,
			*FSFPNumberFormatting::Decimal(Context.Result.GeneratorPowerMW, 2),
			*FSFPNumberFormatting::Decimal(Context.Result.ConfiguredGeneratorClockPercent, 1),
			*FSFPNumberFormatting::Decimal(Context.Result.BaseGeneratorGrossPowerMW, 2),
			*GenerationQualifier,
			*FSFPNumberFormatting::Decimal(Context.Result.GrossPowerMW, 2),
			*GenerationQualifier,
			*FSFPNumberFormatting::Decimal(Context.Result.NetPowerMW, 2),
			*GenerationQualifier);
		if (Candidate.Generator->bVariableOutput)
		{
			Context.Result.Warnings.AddUnique(FString::Printf(
				TEXT("%s: Planung verwendet die wetter-/standortabhängige Maximalleistung von %s MW je Generator"),
				*Candidate.Generator->DisplayName,
				*FSFPNumberFormatting::Decimal(Candidate.Generator->PowerProductionMW, 2)));
		}

		if (AlienPowerAugmenterNodeId != INDEX_NONE
			&& Context.Result.AlienPowerContributionMW > KINDA_SMALL_NUMBER)
		{
			FSFPPlanEdge AugmenterEdge;
			AugmenterEdge.SourceNodeId = AlienPowerAugmenterNodeId;
			// Feed the local generator/grid aggregation node. The final edge from node 1
			// to the target already carries the complete augmented net output.
			AugmenterEdge.TargetNodeId = 1;
			AugmenterEdge.ItemName = TEXT("Alien-verstärkte Leistung");
			AugmenterEdge.ItemClassPath = TEXT("SFP.AlienAugmentedPower");
			AugmenterEdge.Form = TEXT("power");
			AugmenterEdge.RatePerMinute = Context.Result.AlienPowerContributionMW;
			AugmenterEdge.TransportKind = TEXT("power");
			AugmenterEdge.TransportLabel = FString::Printf(
				TEXT("Alien Power Augmenter: +%s MW | ×%s"),
				*FSFPNumberFormatting::Decimal(Context.Result.AlienPowerContributionMW, 2),
				*FSFPNumberFormatting::Decimal(Context.Result.AlienPowerMultiplier, 3));
			AugmenterEdge.bLocalRoutingLink = true;
			Context.Result.Edges.Add(MoveTemp(AugmenterEdge));
		}

		FSFPPlanEdge PowerEdge;
		PowerEdge.SourceNodeId = 1;
		PowerEdge.TargetNodeId = 0;
		PowerEdge.ItemName = TEXT("Elektrische Leistung");
		PowerEdge.ItemClassPath = TEXT("SFP.ElectricPower");
		PowerEdge.Form = TEXT("power");
		PowerEdge.RatePerMinute = Context.Result.NetPowerMW;
		PowerEdge.TransportKind = TEXT("power");
		PowerEdge.TransportLabel = FString::Printf(
			TEXT("Stromnetz: %s MW netto"),
			*FSFPNumberFormatting::Decimal(Context.Result.NetPowerMW, 2));
		PowerEdge.bLocalRoutingLink = true;
		Context.Result.Edges.Add(MoveTemp(PowerEdge));


		CompactMaterialGraph(Context.Result);
		BuildRoutingInfrastructure(Context.Result);
		AddConstructionCosts(Context.Result);
		Context.Result.bSuccess = Context.Result.Nodes.Num() >= 2
			&& !Context.Result.Edges.IsEmpty()
			&& Context.Result.NetPowerMW > KINDA_SMALL_NUMBER;
		OutResult = MoveTemp(Context.Result);
		return OutResult.bSuccess;
	};

	const double RequiredNetWithReserve = Request.TargetNetPowerMW
		* (1.0 + Request.ReservePercent / 100.0);
	TSharedPtr<FSFPPlanResult> BestPlan;
	bool bBestPlanUsesVariableOutput = false;
	const bool bPreferStableAutomaticChoice = Request.GeneratorClassPath.IsEmpty();
	FString LastCandidateError;
	auto RememberCandidateError = [&LastCandidateError](
		const FPowerCandidate& Candidate,
		const FSFPPlanResult& FailedPlan,
		const FString& Fallback)
	{
		const FString Reason = FailedPlan.ErrorMessage.IsEmpty()
			? Fallback
			: FailedPlan.ErrorMessage;
		LastCandidateError = FString::Printf(
			TEXT("%s mit %s: %s"),
			Candidate.Generator != nullptr ? *Candidate.Generator->DisplayName : TEXT("Generator"),
			Candidate.Fuel != nullptr ? *Candidate.Fuel->DisplayName : TEXT("Brennstoff"),
			*Reason);
	};
	for (const FPowerCandidate& Candidate : Candidates)
	{
		double GeneratorGrossPowerMW = SFPVariablePower::GeneratorGrossForTarget(
			RequiredNetWithReserve,
			Request.PassiveAlienPowerAugmenters,
			Request.FueledAlienPowerAugmenters,
			AlienPowerAugmenter.BasePowerPerAugmenterMW,
			AlienPowerAugmenter.PassiveBoostPerAugmenter,
			AlienPowerAugmenter.FueledBoostPerAugmenter);

		FSFPPlanResult FinalPlan;
		bool bCandidateBuildFailed = false;
		const double NetToleranceMW = FMath::Max(
			0.01,
			RequiredNetWithReserve * 1.0e-9);

		// Find a guaranteed upper bracket first. A fixed-point iteration can
		// converge arbitrarily slowly when the fuel chain consumes most of the
		// generated power. Doubling the generator contribution reaches every
		// feasible monotonic solution without tying correctness to an iteration
		// count chosen for a particular fuel.
		double LowGeneratorGrossMW = GeneratorGrossPowerMW;
		FSFPPlanResult LowPlan;
		if (!BuildCandidate(Candidate, LowGeneratorGrossMW, LowPlan))
		{
			RememberCandidateError(
				Candidate,
				LowPlan,
				TEXT("Die Brennstoff- und Rohstoffkette konnte nicht vollständig bilanziert werden."));
			continue;
		}

		double HighGeneratorGrossMW = LowGeneratorGrossMW;
		FSFPPlanResult HighPlan;
		bool bHaveUpperBracket = LowPlan.NetPowerMW + NetToleranceMW
			>= RequiredNetWithReserve;
		if (bHaveUpperBracket)
		{
			HighPlan = MoveTemp(LowPlan);
		}
		else
		{
			constexpr int32 MaxPowerBracketExpansions = 32;
			for (int32 Expansion = 0;
				Expansion < MaxPowerBracketExpansions && !bHaveUpperBracket;
				++Expansion)
			{
				const double ShortfallMW = FMath::Max(
					0.0,
					RequiredNetWithReserve - LowPlan.NetPowerMW);
				const double EffectiveGridMultiplier = FMath::Max(
					1.0e-6,
					LowPlan.AlienPowerMultiplier);
				const double ShortfallStepMW = ShortfallMW
					/ EffectiveGridMultiplier * 1.25 + NetToleranceMW;
				HighGeneratorGrossMW = FMath::Max(
					LowGeneratorGrossMW * 2.0,
					LowGeneratorGrossMW + FMath::Max(ShortfallStepMW, 1.0));
				if (!FMath::IsFinite(HighGeneratorGrossMW))
				{
					RememberCandidateError(
						Candidate,
						LowPlan,
						TEXT("Die adaptive Eigenverbrauchsberechnung überschreitet den darstellbaren Leistungsbereich."));
					bCandidateBuildFailed = true;
					break;
				}

				FSFPPlanResult ProbePlan;
				if (!BuildCandidate(Candidate, HighGeneratorGrossMW, ProbePlan))
				{
					RememberCandidateError(
						Candidate,
						ProbePlan,
						TEXT("Die adaptive Eigenverbrauchsberechnung konnte nicht abgeschlossen werden."));
					bCandidateBuildFailed = true;
					break;
				}
				if (ProbePlan.NetPowerMW + NetToleranceMW >= RequiredNetWithReserve)
				{
					HighPlan = MoveTemp(ProbePlan);
					bHaveUpperBracket = true;
					break;
				}

				LowGeneratorGrossMW = HighGeneratorGrossMW;
				LowPlan = MoveTemp(ProbePlan);
			}
		}

		if (bCandidateBuildFailed)
		{
			continue;
		}
		if (!bHaveUpperBracket)
		{
			RememberCandidateError(
				Candidate,
				LowPlan,
				FString::Printf(
					TEXT("Die adaptive Suche konnte keine ausreichende Bruttoleistung eingrenzen; zuletzt wurden %s MW netto erreicht, benötigt werden einschließlich Reserve %s MW."),
					*FSFPNumberFormatting::Decimal(LowPlan.NetPowerMW, 2),
					*FSFPNumberFormatting::Decimal(RequiredNetWithReserve, 2)));
			continue;
		}

		// Refine the valid upper bracket with a safeguarded secant step. The
		// upper plan is always retained, so reaching the refinement budget can
		// affect precision but can never turn an already sufficient plan into a
		// false failure.
		if (HighGeneratorGrossMW > LowGeneratorGrossMW + 0.005)
		{
			constexpr int32 MaxPowerBracketRefinements = 32;
			for (int32 Refinement = 0;
				Refinement < MaxPowerBracketRefinements;
				++Refinement)
			{
				if (HighPlan.NetPowerMW - RequiredNetWithReserve <= NetToleranceMW)
				{
					break;
				}
				const double BracketWidthMW = HighGeneratorGrossMW - LowGeneratorGrossMW;
				if (BracketWidthMW <= FMath::Max(0.005, HighGeneratorGrossMW * 1.0e-12))
				{
					break;
				}

				const double NetWidthMW = HighPlan.NetPowerMW - LowPlan.NetPowerMW;
				double ProbeGeneratorGrossMW = LowGeneratorGrossMW + BracketWidthMW * 0.5;
				if (NetWidthMW > 1.0e-9 && FMath::IsFinite(NetWidthMW))
				{
					ProbeGeneratorGrossMW = LowGeneratorGrossMW
						+ (RequiredNetWithReserve - LowPlan.NetPowerMW)
							* BracketWidthMW / NetWidthMW;
				}
				ProbeGeneratorGrossMW = FMath::Clamp(
					ProbeGeneratorGrossMW,
					LowGeneratorGrossMW + BracketWidthMW * 0.05,
					HighGeneratorGrossMW - BracketWidthMW * 0.05);

				FSFPPlanResult ProbePlan;
				if (!BuildCandidate(Candidate, ProbeGeneratorGrossMW, ProbePlan))
				{
					RememberCandidateError(
						Candidate,
						ProbePlan,
						TEXT("Die eingegrenzte Eigenverbrauchsberechnung konnte nicht abgeschlossen werden."));
					bCandidateBuildFailed = true;
					break;
				}
				if (ProbePlan.NetPowerMW + NetToleranceMW >= RequiredNetWithReserve)
				{
					HighGeneratorGrossMW = ProbeGeneratorGrossMW;
					HighPlan = MoveTemp(ProbePlan);
				}
				else
				{
					LowGeneratorGrossMW = ProbeGeneratorGrossMW;
					LowPlan = MoveTemp(ProbePlan);
				}
			}
		}
		if (bCandidateBuildFailed)
		{
			continue;
		}
		FinalPlan = MoveTemp(HighPlan);

		const bool bSameOutputStability = !bPreferStableAutomaticChoice
			|| Candidate.Generator->bVariableOutput == bBestPlanUsesVariableOutput;
		const bool bBetter = !BestPlan.IsValid()
			|| (bPreferStableAutomaticChoice
				&& !Candidate.Generator->bVariableOutput
				&& bBestPlanUsesVariableOutput)
			|| (bSameOutputStability
				&& (FinalPlan.BuiltGeneratorCount < BestPlan->BuiltGeneratorCount
					|| (FinalPlan.BuiltGeneratorCount == BestPlan->BuiltGeneratorCount
						&& FinalPlan.SelfConsumptionPowerMW
							< BestPlan->SelfConsumptionPowerMW - 0.001)
					|| (FinalPlan.BuiltGeneratorCount == BestPlan->BuiltGeneratorCount
						&& FMath::IsNearlyEqual(
							FinalPlan.SelfConsumptionPowerMW,
							BestPlan->SelfConsumptionPowerMW,
							0.001)
						&& FinalPlan.FuelRatePerMinute < BestPlan->FuelRatePerMinute)));
		if (bBetter)
		{
			BestPlan = MakeShared<FSFPPlanResult>(MoveTemp(FinalPlan));
			bBestPlanUsesVariableOutput = Candidate.Generator->bVariableOutput;
		}
	}

	if (!BestPlan.IsValid())
	{
		InvalidResult.ErrorMessage = LastCandidateError.IsEmpty()
			? TEXT("Für diese Stromvorgabe konnte keine vollständige Erzeugungskette berechnet werden")
			: FString::Printf(
				TEXT("Für diese Stromvorgabe konnte keine vollständige Erzeugungskette berechnet werden. %s"),
				*LastCandidateError);
		return InvalidResult;
	}
	if (Request.GeneratorClassPath.IsEmpty() || Request.FuelClassPath.IsEmpty())
	{
		const FString FuelChoice = BestPlan->SelectedFuelClassPath == TEXT("SFP.FuelFree")
			? TEXT("ohne Brennstoff")
			: FString::Printf(TEXT("mit %s"), *BestPlan->SelectedFuelDisplayName);
		BestPlan->Warnings.AddUnique(FString::Printf(
			TEXT("Stromautomatik: %s %s gewählt"),
			*BestPlan->SelectedGeneratorDisplayName,
			*FuelChoice));
	}
	return *BestPlan;
}

#include "SFPGameWorldModule.h"

#include "SFPExportChatCommand.h"
#include "SFPFactoryPlanner.h"
#include "SFPPlannerTerminalHologram.h"

#include "Buildables/FGBuildable.h"
#include "Engine/Texture2D.h"
#include "FGSchematic.h"
#include "Resources/FGItemDescriptor.h"
#include "Styling/SlateBrush.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

namespace SFPGameWorldModulePrivate
{
bool SetTerminalTemplateObject(UObject* Object, const FName PropertyName, UObject* Value)
{
	if (!IsValid(Object) || !IsValid(Value))
	{
		return false;
	}

	if (FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Object->GetClass(), PropertyName))
	{
		Property->SetObjectPropertyValue_InContainer(Object, Value);
		return Property->GetObjectPropertyValue_InContainer(Object) == Value;
	}

	return false;
}

bool SetTerminalSchematicBrush(UObject* Object, const FName PropertyName, UTexture2D* Texture)
{
	if (!IsValid(Object) || !IsValid(Texture))
	{
		return false;
	}

	FStructProperty* Property = FindFProperty<FStructProperty>(Object->GetClass(), PropertyName);
	if (Property == nullptr)
	{
		return false;
	}

	FSlateBrush* Brush = Property->ContainerPtrToValuePtr<FSlateBrush>(Object);
	if (Brush == nullptr)
	{
		return false;
	}

	Brush->SetResourceObject(Texture);
	Brush->ImageSize = FVector2D(256.0f, 256.0f);
	Brush->DrawAs = ESlateBrushDrawType::Image;
	Brush->Tiling = ESlateBrushTileType::NoTile;
	Brush->TintColor = FSlateColor(FLinearColor::White);
	return Brush->GetResourceObject() == Texture;
}

bool InstallTerminalMenuIcons(
	UClass* TerminalSchematicClass,
	UClass* TerminalDescriptorClass,
	UTexture2D* IconTexture)
{
	if (!IsValid(TerminalSchematicClass) || !IsValid(TerminalDescriptorClass) || !IsValid(IconTexture))
	{
		return false;
	}

	UObject* SchematicCDO = TerminalSchematicClass->GetDefaultObject();
	UObject* DescriptorCDO = TerminalDescriptorClass->GetDefaultObject();
	const bool bSchematicBrushSet = SetTerminalSchematicBrush(
		SchematicCDO,
		TEXT("mSchematicIcon"),
		IconTexture);
	const bool bSmallSchematicIconSet = SetTerminalTemplateObject(
		SchematicCDO,
		TEXT("mSmallSchematicIcon"),
		IconTexture);
	const bool bSmallDescriptorIconSet = SetTerminalTemplateObject(
		DescriptorCDO,
		TEXT("mSmallIcon"),
		IconTexture);
	const bool bBigDescriptorIconSet = SetTerminalTemplateObject(
		DescriptorCDO,
		TEXT("mPersistentBigIcon"),
		IconTexture);
	return bSchematicBrushSet
		&& bSmallSchematicIconSet
		&& bSmallDescriptorIconSet
		&& bBigDescriptorIconSet;
}

bool AssignSafeTerminalHologram(UClass* TerminalBuildableClass)
{
	if (!IsValid(TerminalBuildableClass))
	{
		return false;
	}

	AFGBuildable* BuildableCDO = Cast<AFGBuildable>(TerminalBuildableClass->GetDefaultObject());
	if (!IsValid(BuildableCDO))
	{
		return false;
	}

	BuildableCDO->mHologramClass = ASFPPlannerTerminalHologram::StaticClass();
	return BuildableCDO->mHologramClass == ASFPPlannerTerminalHologram::StaticClass();
}
}

USFPGameWorldModule::USFPGameWorldModule()
{
	bRootModule = true;
	mChatCommands.Add(ASFPExportChatCommand::StaticClass());
}

void USFPGameWorldModule::DispatchLifecycleEvent(const ELifecyclePhase Phase)
{
	if (Phase == ELifecyclePhase::CONSTRUCTION)
	{
		// Populate mSchematics before UGameWorldModule handles the construction
		// phase and registers its default content with SML. Delaying these loads
		// until a real game world exists keeps the main-menu Server Manager clean.
		RegisterDeferredTerminalContent();
	}

	Super::DispatchLifecycleEvent(Phase);
}

void USFPGameWorldModule::RegisterDeferredTerminalContent()
{
	UClass* TerminalSchematicClass = LoadClass<UFGSchematic>(
		nullptr,
		TEXT("/SFPFactoryPlanner/Terminal/Schematic_SFPPlannerTerminal.Schematic_SFPPlannerTerminal_C"));
	if (!IsValid(TerminalSchematicClass))
	{
		UE_LOG(
			LogSFPFactoryPlanner,
			Error,
			TEXT("Could not load deferred SFP terminal schematic for game-world registration"));
		return;
	}

	mSchematics.AddUnique(TerminalSchematicClass);

	// A dedicated server needs the schematic in its content registry, but it
	// never renders the terminal hologram, HUB icon or build-menu icon. Loading
	// and mutating those client-only CDOs would only pull UI assets into the
	// server process.
	if (IsRunningDedicatedServer())
	{
		UE_LOG(
			LogSFPFactoryPlanner,
			Display,
			TEXT("Registered deferred SFP terminal schematic without client-only UI decoration"));
		return;
	}

	UClass* TerminalBuildableClass = LoadClass<AFGBuildable>(
		nullptr,
		TEXT("/SFPFactoryPlanner/Terminal/Build_SFPPlannerTerminal.Build_SFPPlannerTerminal_C"));
	if (IsValid(TerminalBuildableClass) &&
		SFPGameWorldModulePrivate::AssignSafeTerminalHologram(TerminalBuildableClass))
	{
		// Do not traverse or mutate the Blueprint SimpleConstructionScript here.
		// Some equipment/buildable mods also touch component templates during
		// startup, and USCS_Node template resolution is not safe under every mod
		// load order. Instance-only hardening remains in the hologram BeginPlay.
		UE_LOG(
			LogSFPFactoryPlanner,
			Display,
			TEXT("Assigned SFP terminal hologram without Blueprint SCS traversal"));
	}
	else
	{
		UE_LOG(LogSFPFactoryPlanner, Error, TEXT("Could not assign crash-safe SFP terminal hologram"));
	}

	// Prefer the square r13 icon created by the editor setup. Keep the packaged
	// planner display as a safe fallback so older projects can still load without
	// ever returning to Unreal's white placeholder tile.
	UClass* TerminalDescriptorClass = LoadClass<UFGItemDescriptor>(
		nullptr,
		TEXT("/SFPFactoryPlanner/Terminal/Desc_SFPPlannerTerminal.Desc_SFPPlannerTerminal_C"));
	UTexture2D* TerminalIconTexture = LoadObject<UTexture2D>(
		nullptr,
		TEXT("/SFPFactoryPlanner/Terminal/Mesh/TEX_T_PFP_TerminalIcon.TEX_T_PFP_TerminalIcon"));
	if (!IsValid(TerminalIconTexture))
	{
		TerminalIconTexture = LoadObject<UTexture2D>(
			nullptr,
			TEXT("/SFPFactoryPlanner/Terminal/Mesh/TEX_T_SFPPlannerScreen.TEX_T_SFPPlannerScreen"));
	}
	if (IsValid(TerminalDescriptorClass)
		&& IsValid(TerminalIconTexture)
		&& SFPGameWorldModulePrivate::InstallTerminalMenuIcons(
			TerminalSchematicClass,
			TerminalDescriptorClass,
			TerminalIconTexture))
	{
		UE_LOG(LogSFPFactoryPlanner, Display, TEXT("Installed packaged SFP HUB and build-menu icons"));
	}
	else
	{
		UE_LOG(LogSFPFactoryPlanner, Error, TEXT("Could not install packaged SFP HUB and build-menu icons"));
	}
}

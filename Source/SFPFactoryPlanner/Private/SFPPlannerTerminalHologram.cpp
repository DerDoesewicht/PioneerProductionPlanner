#include "SFPPlannerTerminalHologram.h"

#include "SFPFactoryPlanner.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "FGColoredInstanceMeshProxy.h"
#include "Materials/Material.h"
#include "UObject/UnrealType.h"

namespace SFPPlannerTerminalHologramPrivate
{
bool SetHologramComponentBool(UObject* Object, const FName PropertyName, const bool Value)
{
	if (!IsValid(Object))
	{
		return false;
	}

	if (FBoolProperty* Property = FindFProperty<FBoolProperty>(Object->GetClass(), PropertyName))
	{
		Property->SetPropertyValue_InContainer(Object, Value);
		return true;
	}

	return false;
}

void BlockFactoryMeshConversion(UStaticMeshComponent* Component)
{
	if (UFGColoredInstanceMeshProxy* FactoryMesh = Cast<UFGColoredInstanceMeshProxy>(Component))
	{
		FactoryMesh->mBlockInstancing = true;
		FactoryMesh->mBlockInstancingWithLumen = true;
		FactoryMesh->mBlockColoring = true;
		FactoryMesh->mBlockBuildableCustomizationUpdates = true;
	}

	SetHologramComponentBool(Component, TEXT("bDisallowNanite"), true);
}

bool IsTerminalScreenComponent(const UStaticMeshComponent* Component)
{
	if (!IsValid(Component))
	{
		return false;
	}

	if (Component->GetName().Contains(TEXT("TerminalScreen"), ESearchCase::IgnoreCase))
	{
		return true;
	}

	const UStaticMesh* StaticMesh = Component->GetStaticMesh();
	return IsValid(StaticMesh) &&
		StaticMesh->GetName().Contains(TEXT("SFPPlannerTerminalScreen"), ESearchCase::IgnoreCase);
}
}

ASFPPlannerTerminalHologram::ASFPPlannerTerminalHologram()
{
	// FactoryGame provides this switch specifically for meshes/materials that do
	// not need the full factory hologram shader permutation set.
	mUseSimplifiedHologramMaterial = true;
}

void ASFPPlannerTerminalHologram::BeginPlay()
{
	Super::BeginPlay();

	UMaterialInterface* SafeMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	TInlineComponentArray<UStaticMeshComponent*> MeshComponents;
	GetComponents(MeshComponents);

	int32 SanitizedMeshCount = 0;
	for (UStaticMeshComponent* MeshComponent : MeshComponents)
	{
		if (!IsValid(MeshComponent))
		{
			continue;
		}

		// These properties belong to UFGColoredInstanceMeshProxy. Reflection is
		// intentional: the code remains safe if FactoryGame supplies an ordinary
		// UStaticMeshComponent for one of the hologram components.
		SFPPlannerTerminalHologramPrivate::BlockFactoryMeshConversion(MeshComponent);

		MeshComponent->SetCastShadow(false);

		// The textured screen is visual detail for the completed terminal.  The
		// body mesh is sufficient while placing it, and omitting the two-sided
		// unlit overlay removes another unnecessary hologram shader permutation.
		if (SFPPlannerTerminalHologramPrivate::IsTerminalScreenComponent(MeshComponent))
		{
			MeshComponent->SetVisibility(false, true);
			MeshComponent->SetHiddenInGame(true, true);
		}
		else if (IsValid(SafeMaterial))
		{
			for (int32 MaterialIndex = 0; MaterialIndex < MeshComponent->GetNumMaterials(); ++MaterialIndex)
			{
				MeshComponent->SetMaterial(MaterialIndex, SafeMaterial);
			}
		}

		MeshComponent->MarkRenderStateDirty();
		++SanitizedMeshCount;
	}

	UE_LOG(
		LogSFPFactoryPlanner,
		Display,
		TEXT("SFP terminal safe hologram initialized with %d static mesh component(s)"),
		SanitizedMeshCount);
}

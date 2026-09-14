#pragma once

#include "CoreMinimal.h"
#include "Hologram/FGBuildableHologram.h"
#include "SFPPlannerTerminalHologram.generated.h"

/**
 * Conservative placement hologram for the imported SFP terminal mesh.
 *
 * FactoryGame normally routes buildable mesh components through its colored
 * instance/customization renderer.  That path is useful for vanilla factory
 * materials, but the compact OBJ-based terminal uses ordinary materials and a
 * separate unlit screen overlay.  Keeping those components on the ordinary
 * static-mesh path avoids an invalid Windows/D3D12 PSO when the recipe is
 * selected in the build menu.
 */
UCLASS()
class SFPFACTORYPLANNER_API ASFPPlannerTerminalHologram : public AFGBuildableHologram
{
	GENERATED_BODY()

public:
	ASFPPlannerTerminalHologram();

protected:
	virtual void BeginPlay() override;
};

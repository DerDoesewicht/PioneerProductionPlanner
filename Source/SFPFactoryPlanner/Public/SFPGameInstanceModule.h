#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "SFPGameInstanceModule.generated.h"

/**
 * Process-wide SML module for the planner's per-player multiplayer bridge.
 *
 * Registering the RCO here makes SML create one network-owned instance for
 * every player controller on listen and dedicated servers.
 */
UCLASS()
class SFPFACTORYPLANNER_API USFPGameInstanceModule : public UGameInstanceModule
{
	GENERATED_BODY()

public:
	USFPGameInstanceModule();

	virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;
};

#pragma once

#include "CoreMinimal.h"
#include "Module/GameWorldModule.h"
#include "SFPGameWorldModule.generated.h"

/** Root game-world module which registers planner commands and content. */
UCLASS()
class SFPFACTORYPLANNER_API USFPGameWorldModule : public UGameWorldModule
{
	GENERATED_BODY()

public:
	USFPGameWorldModule();

	virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;

private:
	/**
	 * Loads and registers the Blueprint terminal content for an actual game
	 * world. This must never run from the UObject/CDO constructor: doing so
	 * pulls game UI packages into the main-menu load before they are ready.
	 */
	void RegisterDeferredTerminalContent();
};

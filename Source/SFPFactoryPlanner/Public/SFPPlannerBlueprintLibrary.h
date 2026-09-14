#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "SFPPlannerBlueprintLibrary.generated.h"

class APlayerController;

/**
 * Blueprint bridge used by in-world terminals and other mod UI entry points.
 * Requests for a remote controller are forwarded to that controller's owning
 * client, so the existing terminal Blueprint also works on dedicated servers.
 */
UCLASS()
class SFPFACTORYPLANNER_API USFPPlannerBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "SFP Factory Planner")
	static bool OpenPlanner(APlayerController* PlayerController, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "SFP Factory Planner")
	static bool ClosePlanner(APlayerController* PlayerController);

	UFUNCTION(BlueprintCallable, Category = "SFP Factory Planner")
	static bool TogglePlanner(APlayerController* PlayerController, FString& OutError);
};

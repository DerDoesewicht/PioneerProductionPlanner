#include "SFPPlannerBlueprintLibrary.h"

#include "SFPPlannerRemoteCallObject.h"

#include "FGPlayerController.h"
#include "GameFramework/PlayerController.h"

namespace
{
AFGPlayerController* ResolvePlannerController(APlayerController* PlayerController, FString* OutError)
{
	AFGPlayerController* FactoryController = Cast<AFGPlayerController>(PlayerController);
	if (!IsValid(FactoryController))
	{
		if (OutError != nullptr)
		{
			*OutError = TEXT("The planner needs a valid FGPlayerController.");
		}
		return nullptr;
	}

	return FactoryController;
}
}

bool USFPPlannerBlueprintLibrary::OpenPlanner(APlayerController* PlayerController, FString& OutError)
{
	OutError.Reset();
	AFGPlayerController* FactoryController = ResolvePlannerController(PlayerController, &OutError);
	return FactoryController != nullptr
		&& USFPPlannerRemoteCallObject::RequestOpen(FactoryController, OutError);
}

bool USFPPlannerBlueprintLibrary::ClosePlanner(APlayerController* PlayerController)
{
	AFGPlayerController* FactoryController = ResolvePlannerController(PlayerController, nullptr);
	FString Error;
	return FactoryController != nullptr
		&& USFPPlannerRemoteCallObject::RequestClose(FactoryController, Error);
}

bool USFPPlannerBlueprintLibrary::TogglePlanner(APlayerController* PlayerController, FString& OutError)
{
	OutError.Reset();
	AFGPlayerController* FactoryController = ResolvePlannerController(PlayerController, &OutError);
	return FactoryController != nullptr
		&& USFPPlannerRemoteCallObject::RequestToggle(FactoryController, OutError);
}

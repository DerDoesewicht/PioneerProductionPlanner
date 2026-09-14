#include "SFPGameInstanceModule.h"

#include "SFPFactoryPlanner.h"
#include "SFPPlannerRemoteCallObject.h"

USFPGameInstanceModule::USFPGameInstanceModule()
{
	bRootModule = true;
	RemoteCallObjects.AddUnique(USFPPlannerRemoteCallObject::StaticClass());
}

void USFPGameInstanceModule::DispatchLifecycleEvent(const ELifecyclePhase Phase)
{
	Super::DispatchLifecycleEvent(Phase);
	if (Phase == ELifecyclePhase::CONSTRUCTION)
	{
		UE_LOG(
			LogSFPFactoryPlanner,
			Display,
			TEXT("SFP multiplayer bridge registered for per-player client requests"));
	}
}

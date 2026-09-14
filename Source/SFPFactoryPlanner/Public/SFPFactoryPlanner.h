// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSFPFactoryPlanner, Log, All);

class IInputProcessor;

class FSFPFactoryPlannerModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterPlannerInputProcessor();

	TSharedPtr<IInputProcessor> PlannerInputProcessor;
	FDelegateHandle PostEngineInitHandle;
};

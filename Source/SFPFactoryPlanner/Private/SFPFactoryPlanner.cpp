// Copyright Epic Games, Inc. All Rights Reserved.

#include "SFPFactoryPlanner.h"

#include "SFPPlannerHotkey.h"
#include "SFPPlannerUI.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FGPlayerController.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Misc/CoreDelegates.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "FSFPFactoryPlannerModule"

DEFINE_LOG_CATEGORY(LogSFPFactoryPlanner);

namespace
{
AFGPlayerController* FindLocalPlannerController()
{
	if (GEngine == nullptr)
	{
		return nullptr;
	}

	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		UWorld* World = WorldContext.World();
		if (!IsValid(World) || (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE))
		{
			continue;
		}

		for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
		{
			AFGPlayerController* PlayerController = Cast<AFGPlayerController>(Iterator->Get());
			if (IsValid(PlayerController) && PlayerController->IsLocalController())
			{
				return PlayerController;
			}
		}
	}

	return nullptr;
}

class FSFPPlannerInputProcessor final : public IInputProcessor
{
public:
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
	{
	}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		if (InKeyEvent.IsRepeat()
			|| SFPPlannerHotkey::IsCaptureInProgress()
			|| !SFPPlannerHotkey::Matches(InKeyEvent))
		{
			return false;
		}
		const FString TriggerName = SFPPlannerHotkey::GetDisplayName();

		AFGPlayerController* PlayerController = FindLocalPlannerController();
		if (!IsValid(PlayerController))
		{
			return false;
		}

		// Never add or remove viewport widgets while Slate is still dispatching
		// this key event. Deferring by one game tick avoids invalidating Slate's
		// active input path and also keeps runtime asset discovery out of the
		// input-preprocessor call stack.
		const TWeakObjectPtr<AFGPlayerController> WeakController(PlayerController);
		PlayerController->GetWorldTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateLambda([WeakController]()
			{
				AFGPlayerController* DeferredController = WeakController.Get();
				if (!IsValid(DeferredController) || !DeferredController->IsLocalController())
				{
					return;
				}
				if (FSFPPlannerUI::IsOpen(DeferredController))
				{
					FSFPPlannerUI::Close(DeferredController);
					return;
				}

				FString Error;
				if (!FSFPPlannerUI::Open(DeferredController, Error))
				{
					UE_LOG(LogSFPFactoryPlanner, Warning, TEXT("Planner hotkey could not open the planner: %s"), *Error);
				}
			}));

		UE_LOG(LogSFPFactoryPlanner, Verbose, TEXT("Planner hotkey %s scheduled for next tick"), *TriggerName);

		// Do not consume the event. Other mods and the game may legally use the
		// same key; the player can rebind or disable this shortcut without one
		// input preprocessor winning solely because of registration order.
		return false;
	}

	virtual const TCHAR* GetDebugName() const override
	{
		return TEXT("SFPFactoryPlannerInputProcessor");
	}
};
}

void FSFPFactoryPlannerModule::StartupModule()
{
	if (IsRunningDedicatedServer())
	{
		UE_LOG(
			LogSFPFactoryPlanner,
			Display,
			TEXT("Pioneer Production Planner 1.5.1 loaded on dedicated server; client-only Slate input disabled"));
		return;
	}

	if (FSlateApplication::IsInitialized())
	{
		RegisterPlannerInputProcessor();
	}
	else
	{
		PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(
			this,
			&FSFPFactoryPlannerModule::RegisterPlannerInputProcessor);
	}

	const FString HotkeyName = SFPPlannerHotkey::GetDisplayName();
	UE_LOG(
		LogSFPFactoryPlanner,
		Display,
		TEXT("Pioneer Production Planner 1.5.1 loaded; hotkey: %s"),
		HotkeyName.IsEmpty() ? TEXT("disabled") : *HotkeyName);
}

void FSFPFactoryPlannerModule::RegisterPlannerInputProcessor()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}
	if (IsRunningDedicatedServer()
		|| PlannerInputProcessor.IsValid()
		|| !FSlateApplication::IsInitialized())
	{
		return;
	}

	PlannerInputProcessor = MakeShared<FSFPPlannerInputProcessor>();
	FSlateApplication::Get().RegisterInputPreProcessor(PlannerInputProcessor.ToSharedRef());
}

void FSFPFactoryPlannerModule::ShutdownModule()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}
	if (PlannerInputProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(PlannerInputProcessor.ToSharedRef());
	}
	PlannerInputProcessor.Reset();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSFPFactoryPlannerModule, SFPFactoryPlanner)

#include "SFPPlannerUI.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "FGPlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "SFPFactoryPlanner.h"
#include "SFPLocalization.h"
#include "SFPPlannerSolver.h"
#include "SFPPlannerRemoteCallObject.h"
#include "TimerManager.h"
#include "Widgets/SSFPPlannerWindow.h"

namespace
{
	struct FOpenPlannerWindow
	{
		TSharedPtr<SSFPPlannerWindow> Widget;
		bool bPreviousMouseCursor = false;
	};

	TMap<TWeakObjectPtr<AFGPlayerController>, FOpenPlannerWindow> OpenWindows;
	TWeakObjectPtr<UWorld> CachedWorld;
	TSharedPtr<FSFPPlannerSolver> CachedSolver;

	void PurgeInvalidWindows()
	{
		for (auto Iterator = OpenWindows.CreateIterator(); Iterator; ++Iterator)
		{
			if (!Iterator.Key().IsValid() || !Iterator.Value().Widget.IsValid())
			{
				if (Iterator.Value().Widget.IsValid()
					&& GEngine != nullptr
					&& GEngine->GameViewport != nullptr)
				{
					GEngine->GameViewport->RemoveViewportWidgetContent(Iterator.Value().Widget.ToSharedRef());
				}
				Iterator.RemoveCurrent();
			}
		}
	}

	TSharedPtr<SSFPPlannerWindow> FindOpenWindow(AFGPlayerController* PlayerController)
	{
		PurgeInvalidWindows();
		if (!IsValid(PlayerController))
		{
			return nullptr;
		}
		if (FOpenPlannerWindow* State = OpenWindows.Find(TWeakObjectPtr<AFGPlayerController>(PlayerController));
			State != nullptr)
		{
			return State->Widget;
		}
		return nullptr;
	}

	TSharedPtr<FSFPPlannerSolver> GetOrCreateSolver(AFGPlayerController* PlayerController, FString& OutError)
	{
		UWorld* World = IsValid(PlayerController) ? PlayerController->GetWorld() : nullptr;
		if (!IsValid(World))
		{
			OutError = SFPLocalization::Select(
				TEXT("Kein gültiger Spielstand geladen"),
				TEXT("No valid game session is loaded"));
			return nullptr;
		}

		if (CachedWorld.Get() != World)
		{
			CachedSolver.Reset();
			CachedWorld = World;
		}
		if (CachedSolver.IsValid())
		{
			const double StartTime = FPlatformTime::Seconds();
			if (!CachedSolver->RefreshIfRecipeAvailabilityChanged(World, OutError))
			{
				return nullptr;
			}
			UE_LOG(
				LogSFPFactoryPlanner,
				Verbose,
				TEXT("Planner catalog freshness checked in %.3f seconds"),
				FPlatformTime::Seconds() - StartTime);
			return CachedSolver;
		}

		const double StartTime = FPlatformTime::Seconds();
		TSharedPtr<FSFPPlannerSolver> NewSolver = MakeShared<FSFPPlannerSolver>();
		if (!NewSolver->Initialize(World, OutError))
		{
			return nullptr;
		}
		CachedSolver = MoveTemp(NewSolver);
		UE_LOG(
			LogSFPFactoryPlanner,
			Display,
			TEXT("Planner catalog initialized in %.3f seconds"),
			FPlatformTime::Seconds() - StartTime);
		return CachedSolver;
	}

	void ApplyPlannerInputModeAfterChat(
		const TWeakObjectPtr<AFGPlayerController> PlayerController,
		const TWeakPtr<SSFPPlannerWindow> Window)
	{
		AFGPlayerController* Controller = PlayerController.Get();
		const TSharedPtr<SSFPPlannerWindow> PinnedWindow = Window.Pin();
		if (!IsValid(Controller) || !PinnedWindow.IsValid() || !OpenWindows.Contains(PlayerController))
		{
			return;
		}

		Controller->SetShowMouseCursor(true);
		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetWidgetToFocus(PinnedWindow);
		Controller->SetInputMode(InputMode);
		FSlateApplication::Get().SetKeyboardFocus(PinnedWindow, EFocusCause::SetDirectly);
	}
}

bool FSFPPlannerUI::Open(AFGPlayerController* PlayerController, FString& OutError)
{
	PurgeInvalidWindows();
	if (!IsValid(PlayerController) || !PlayerController->IsLocalController())
	{
		OutError = SFPLocalization::Select(
			TEXT("Der Planner kann nur für einen lokalen Spieler geöffnet werden"),
			TEXT("The planner can only be opened for a local player"));
		return false;
	}
	if (GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		OutError = SFPLocalization::Select(
			TEXT("Das Game-Viewport ist noch nicht bereit"),
			TEXT("The game viewport is not ready yet"));
		return false;
	}

	const TWeakObjectPtr<AFGPlayerController> Key(PlayerController);
	if (const FOpenPlannerWindow* Existing = OpenWindows.Find(Key); Existing != nullptr && Existing->Widget.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(Existing->Widget, EFocusCause::SetDirectly);
		return true;
	}

	FString InitializeError;
	const TSharedPtr<FSFPPlannerSolver> Solver = GetOrCreateSolver(PlayerController, InitializeError);
	if (!Solver.IsValid())
	{
		OutError = InitializeError;
		return false;
	}

	TSharedPtr<SSFPPlannerWindow> Window;
	SAssignNew(Window, SSFPPlannerWindow)
		.PlayerController(PlayerController)
		.Solver(Solver)
		.InitializeError(InitializeError)
		.OnClose(FSimpleDelegate::CreateLambda([Key]()
		{
			if (AFGPlayerController* Controller = Key.Get())
			{
				FSFPPlannerUI::Close(Controller);
			}
		}));

	FOpenPlannerWindow State;
	State.Widget = Window;
	State.bPreviousMouseCursor = PlayerController->bShowMouseCursor;
	OpenWindows.Add(Key, State);
	GEngine->GameViewport->AddViewportWidgetContent(Window.ToSharedRef(), 10000);
	FString ResourceInventoryError;
	if (!USFPPlannerRemoteCallObject::RequestResourceNodeInventory(PlayerController, ResourceInventoryError))
	{
		Window->ReceiveResourceNodeInventory(FString(), ResourceInventoryError);
	}

	PlayerController->SetShowMouseCursor(true);
	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetWidgetToFocus(Window);
	PlayerController->SetInputMode(InputMode);
	FSlateApplication::Get().SetKeyboardFocus(Window, EFocusCause::SetDirectly);

	const TWeakPtr<SSFPPlannerWindow> WeakWindow = Window;
	PlayerController->GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda(
		[Key, WeakWindow]()
		{
			ApplyPlannerInputModeAfterChat(Key, WeakWindow);
		}));
	return true;
}

bool FSFPPlannerUI::Close(AFGPlayerController* PlayerController)
{
	PurgeInvalidWindows();
	if (!IsValid(PlayerController))
	{
		return false;
	}

	const TWeakObjectPtr<AFGPlayerController> Key(PlayerController);
	FOpenPlannerWindow* State = OpenWindows.Find(Key);
	if (State == nullptr || !State->Widget.IsValid())
	{
		return false;
	}

	if (GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(State->Widget.ToSharedRef());
	}

	const bool bPreviousMouseCursor = State->bPreviousMouseCursor;
	OpenWindows.Remove(Key);
	FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Cleared);
	FInputModeGameOnly InputMode;
	InputMode.SetConsumeCaptureMouseDown(false);
	PlayerController->SetInputMode(InputMode);
	PlayerController->SetShowMouseCursor(bPreviousMouseCursor);
	PlayerController->ResetIgnoreInputFlags();
	FSlateApplication::Get().SetAllUserFocusToGameViewport(EFocusCause::SetDirectly);
	UE_LOG(LogSFPFactoryPlanner, Verbose, TEXT("Planner closed and game viewport focus restored"));
	return true;
}

bool FSFPPlannerUI::IsOpen(const AFGPlayerController* PlayerController)
{
	PurgeInvalidWindows();
	return IsValid(PlayerController)
		&& OpenWindows.Contains(TWeakObjectPtr<AFGPlayerController>(const_cast<AFGPlayerController*>(PlayerController)));
}

void FSFPPlannerUI::ReceiveSharedPlanCatalog(
	AFGPlayerController* PlayerController,
	const TArray<FSFPSharedPlanSummary>& Plans,
	const FString& Error)
{
	if (const TSharedPtr<SSFPPlannerWindow> Window = FindOpenWindow(PlayerController); Window.IsValid())
	{
		Window->ReceiveSharedPlanCatalog(Plans, Error);
	}
}

void FSFPPlannerUI::ReceiveSharedPlan(
	AFGPlayerController* PlayerController,
	const FSFPSharedPlanSummary& Summary,
	const FString& PlanJson,
	const FString& Error)
{
	if (const TSharedPtr<SSFPPlannerWindow> Window = FindOpenWindow(PlayerController); Window.IsValid())
	{
		Window->ReceiveSharedPlan(Summary, PlanJson, Error);
	}
}

void FSFPPlannerUI::ReceiveSharedPlanSaveResult(
	AFGPlayerController* PlayerController,
	const bool bSuccess,
	const FSFPSharedPlanSummary& Summary,
	const FString& Error)
{
	if (const TSharedPtr<SSFPPlannerWindow> Window = FindOpenWindow(PlayerController); Window.IsValid())
	{
		Window->ReceiveSharedPlanSaveResult(bSuccess, Summary, Error);
	}
}

void FSFPPlannerUI::ReceiveSharedPlanDeleteResult(
	AFGPlayerController* PlayerController,
	const bool bSuccess,
	const FString& FileName,
	const FString& PlanName,
	const FString& Error)
{
	if (const TSharedPtr<SSFPPlannerWindow> Window = FindOpenWindow(PlayerController); Window.IsValid())
	{
		Window->ReceiveSharedPlanDeleteResult(bSuccess, FileName, PlanName, Error);
	}
}

void FSFPPlannerUI::ReceiveResourceNodeInventory(
	AFGPlayerController* PlayerController,
	const FString& InventoryJson,
	const FString& Error)
{
	if (const TSharedPtr<SSFPPlannerWindow> Window = FindOpenWindow(PlayerController); Window.IsValid())
	{
		Window->ReceiveResourceNodeInventory(InventoryJson, Error);
	}
}

void FSFPPlannerUI::NotifySharedPlanChanged(
	AFGPlayerController* PlayerController,
	const FSFPSharedPlanSummary& Summary)
{
	if (const TSharedPtr<SSFPPlannerWindow> Window = FindOpenWindow(PlayerController); Window.IsValid())
	{
		Window->NotifySharedPlanChanged(Summary);
	}
}

void FSFPPlannerUI::NotifySharedPlanDeleted(
	AFGPlayerController* PlayerController,
	const FString& FileName,
	const FString& PlanName,
	const FString& DeletedBy)
{
	if (const TSharedPtr<SSFPPlannerWindow> Window = FindOpenWindow(PlayerController); Window.IsValid())
	{
		Window->NotifySharedPlanDeleted(FileName, PlanName, DeletedBy);
	}
}

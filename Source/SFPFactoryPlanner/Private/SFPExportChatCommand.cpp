#include "SFPExportChatCommand.h"

#include "Command/CommandSender.h"
#include "FGPlayerController.h"
#include "SFPLocalization.h"
#include "SFPPlannerHotkey.h"
#include "SFPPlannerRemoteCallObject.h"
#include "SFPRuntimeExporter.h"

ASFPExportChatCommand::ASFPExportChatCommand()
{
	CommandName = TEXT("sfpplanner");
	Aliases = {TEXT("sfp"), TEXT("sfp-export")};
	Usage = NSLOCTEXT(
		"SFPFactoryPlanner",
		"ChatCommandUsage",
		"/sfpplanner open|close|export|hotkey [KEY|off|reset]");
	MinNumberOfArguments = 0;
	bOnlyUsableByPlayer = false;
}

EExecutionStatus ASFPExportChatCommand::ExecuteCommand_Implementation(
	UCommandSender* Sender,
	const TArray<FString>& Arguments,
	const FString& Label)
{
	if (!IsValid(Sender))
	{
		return EExecutionStatus::UNCOMPLETED;
	}

	const FString Action = Arguments.IsEmpty() ? TEXT("open") : Arguments[0].ToLower();
	if (Action == TEXT("help"))
	{
		PrintCommandUsage(Sender);
		return EExecutionStatus::COMPLETED;
	}

	if (Action == TEXT("open"))
	{
		AFGPlayerController* PlayerController = Sender->GetPlayer();
		FString Error;
		if (!IsValid(PlayerController)
			|| !USFPPlannerRemoteCallObject::RequestOpen(PlayerController, Error))
		{
			Sender->SendChatMessage(
				SFPLocalization::Translate(FString::Printf(TEXT("Planner konnte nicht geöffnet werden: %s"), *Error)),
				FLinearColor::Red);
			return EExecutionStatus::UNCOMPLETED;
		}
		return EExecutionStatus::COMPLETED;
	}

	if (Action == TEXT("close"))
	{
		AFGPlayerController* PlayerController = Sender->GetPlayer();
		FString Error;
		if (!IsValid(PlayerController)
			|| !USFPPlannerRemoteCallObject::RequestClose(PlayerController, Error))
		{
			Sender->SendChatMessage(
				SFPLocalization::Translate(FString::Printf(
					TEXT("Planner konnte nicht geschlossen werden: %s"),
					*Error)),
				FLinearColor::Yellow);
			return EExecutionStatus::UNCOMPLETED;
		}
		return EExecutionStatus::COMPLETED;
	}

	if (Action == TEXT("hotkey") || Action == TEXT("key"))
	{
		AFGPlayerController* PlayerController = Sender->GetPlayer();
		if (!IsValid(PlayerController))
		{
			Sender->SendChatMessage(
				SFPLocalization::Select(
					TEXT("Der Hotkey kann nur für einen Spieler geändert werden"),
					TEXT("The hotkey can only be changed for a player")),
				FLinearColor::Red);
			return EExecutionStatus::UNCOMPLETED;
		}

		if (Arguments.Num() < 2)
		{
			if (PlayerController->IsLocalController())
			{
				const FString CurrentHotkey = SFPPlannerHotkey::GetDisplayName();
				Sender->SendChatMessage(
					CurrentHotkey.IsEmpty()
						? SFPLocalization::Select(TEXT("Planner-Hotkey: deaktiviert"), TEXT("Planner hotkey: disabled"))
						: FString::Printf(TEXT("Planner hotkey: %s"), *CurrentHotkey),
					FLinearColor::Green);
				return EExecutionStatus::COMPLETED;
			}

			FString Error;
			if (!USFPPlannerRemoteCallObject::RequestHotkeyStatus(PlayerController, Error))
			{
				Sender->SendChatMessage(Error, FLinearColor::Red);
				return EExecutionStatus::UNCOMPLETED;
			}
			Sender->SendChatMessage(
				SFPLocalization::Select(
					TEXT("Hotkey-Status wird auf deinem Client angezeigt"),
					TEXT("Hotkey status is being shown on your client")),
				FLinearColor::Green);
			return EExecutionStatus::COMPLETED;
		}

		FString BindingSpec;
		for (int32 ArgumentIndex = 1; ArgumentIndex < Arguments.Num(); ++ArgumentIndex)
		{
			BindingSpec += Arguments[ArgumentIndex];
		}
		FString Error;
		if (!USFPPlannerRemoteCallObject::RequestHotkeyChange(
			PlayerController,
			BindingSpec,
			Error))
		{
			Sender->SendChatMessage(
				SFPLocalization::Translate(FString::Printf(TEXT("Hotkey konnte nicht gespeichert werden: %s"), *Error)),
				FLinearColor::Red);
			return EExecutionStatus::BAD_ARGUMENTS;
		}

		if (PlayerController->IsLocalController())
		{
			const FString CurrentHotkey = SFPPlannerHotkey::GetDisplayName();
			Sender->SendChatMessage(
				CurrentHotkey.IsEmpty()
					? SFPLocalization::Select(TEXT("Planner-Hotkey: deaktiviert"), TEXT("Planner hotkey: disabled"))
					: FString::Printf(TEXT("Planner hotkey: %s"), *CurrentHotkey),
				FLinearColor::Green);
		}
		else
		{
			Sender->SendChatMessage(
				SFPLocalization::Select(
					TEXT("Hotkey-Befehl an deinen Client gesendet"),
					TEXT("Hotkey command sent to your client")),
				FLinearColor::Green);
		}
		return EExecutionStatus::COMPLETED;
	}

	if (Action != TEXT("export"))
	{
		Sender->SendChatMessage(
			SFPLocalization::Translate(FString::Printf(TEXT("Unbekannte Aktion '%s'. Nutze /sfpplanner open, close, export oder hotkey"), *Action)),
			FLinearColor::Red);
		return EExecutionStatus::BAD_ARGUMENTS;
	}

	Sender->SendChatMessage(
		TEXT("Pioneer Production Planner: exporting runtime data..."),
		FLinearColor(0.95f, 0.75f, 0.10f));

	const FSFPExportResult Result = USFPRuntimeExporter::ExportRuntimeData(this);
	if (!Result.bSuccess)
	{
		Sender->SendChatMessage(
			FString::Printf(TEXT("Export failed: %s"), *Result.ErrorMessage),
			FLinearColor::Red);
		return EExecutionStatus::UNCOMPLETED;
	}

	const FString ExportSummary = SFPLocalization::IsGerman()
		? FString::Printf(
			TEXT("Export abgeschlossen: %d Rezepte, %d Items, %d Maschinen, %d Transportstufen, %d Warnungen"),
			Result.RecipeCount, Result.ItemCount, Result.MachineCount, Result.TransportCount, Result.WarningCount)
		: FString::Printf(
			TEXT("Export complete: %d recipes, %d items, %d machines, %d transport tiers, warnings: %d"),
			Result.RecipeCount, Result.ItemCount, Result.MachineCount, Result.TransportCount, Result.WarningCount);
	Sender->SendChatMessage(
		ExportSummary,
		FLinearColor::Green);
	Sender->SendChatMessage(
		FString::Printf(TEXT("File: %s"), *Result.FilePath),
		FLinearColor::Green);

	return EExecutionStatus::COMPLETED;
}

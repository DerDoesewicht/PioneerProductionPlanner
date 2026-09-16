#include "SFPPlannerRemoteCallObject.h"

#include "SFPFactoryPlanner.h"
#include "SFPLocalization.h"
#include "SFPPlannerHotkey.h"
#include "SFPPlannerPersistence.h"
#include "SFPResourceNodeInventory.h"
#include "SFPPlannerUI.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FGPlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

namespace SFPPlannerRemoteCallObjectPrivate
{
	constexpr int32 MaxSharedPlanJsonCharacters = 512 * 1024;
	constexpr int32 MaxSharedPlanUploadCharacters = MaxSharedPlanJsonCharacters - 4096;
	constexpr int32 SharedPlanChunkCharacters = 8192;

	int32 ChunkCountForCharacters(const int32 CharacterCount)
	{
		return CharacterCount > 0
			? (CharacterCount + SharedPlanChunkCharacters - 1) / SharedPlanChunkCharacters
			: 0;
	}

	bool IsValidTransferId(const FString& TransferId)
	{
		return !TransferId.IsEmpty() && TransferId.Len() <= 64;
	}

	void ShowLocalStatus(const FString& Message, const bool bError = false)
	{
		if (bError)
		{
			UE_LOG(LogSFPFactoryPlanner, Warning, TEXT("SFP client: %s"), *Message);
		}
		else
		{
			UE_LOG(LogSFPFactoryPlanner, Display, TEXT("SFP client: %s"), *Message);
		}
		if (GEngine != nullptr)
		{
			GEngine->AddOnScreenDebugMessage(
				-1,
				5.0f,
				bError ? FColor::Red : FColor::Green,
				Message);
		}
	}

	bool IsDisableSpec(const FString& BindingSpec)
	{
		return BindingSpec.Equals(TEXT("off"), ESearchCase::IgnoreCase)
			|| BindingSpec.Equals(TEXT("none"), ESearchCase::IgnoreCase)
			|| BindingSpec.Equals(TEXT("aus"), ESearchCase::IgnoreCase)
			|| BindingSpec.Equals(TEXT("disabled"), ESearchCase::IgnoreCase);
	}

	bool IsResetSpec(const FString& BindingSpec)
	{
		return BindingSpec.Equals(TEXT("reset"), ESearchCase::IgnoreCase)
			|| BindingSpec.Equals(TEXT("standard"), ESearchCase::IgnoreCase);
	}

	void ApplyLocalHotkey(const FString& BindingSpec)
	{
		if (IsDisableSpec(BindingSpec))
		{
			SFPPlannerHotkey::Disable();
			ShowLocalStatus(SFPLocalization::Select(
				TEXT("Planner-Hotkey deaktiviert"),
				TEXT("Planner hotkey disabled")));
			return;
		}
		if (IsResetSpec(BindingSpec))
		{
			SFPPlannerHotkey::ResetToDefault();
			ShowLocalStatus(SFPLocalization::Select(
				TEXT("Planner-Hotkey auf F8 zurückgesetzt"),
				TEXT("Planner hotkey reset to F8")));
			return;
		}

		FString Error;
		if (!SFPPlannerHotkey::SetFromString(BindingSpec, Error))
		{
			ShowLocalStatus(
				SFPLocalization::Translate(FString::Printf(
					TEXT("Hotkey konnte nicht gespeichert werden: %s"),
					*Error)),
				true);
			return;
		}

		ShowLocalStatus(FString::Printf(
			TEXT("Planner hotkey: %s"),
			*SFPPlannerHotkey::GetDisplayName()));
	}

	FString GetPlayerDisplayName(const AFGPlayerController* PlayerController)
	{
		if (IsValid(PlayerController) && IsValid(PlayerController->PlayerState))
		{
			const FString PlayerName = PlayerController->PlayerState->GetPlayerName().TrimStartAndEnd();
			if (!PlayerName.IsEmpty())
			{
				return PlayerName.Left(80);
			}
		}
		return TEXT("Unbekannter Spieler");
	}

	FString GetStablePlayerId(const AFGPlayerController* PlayerController)
	{
		if (IsValid(PlayerController) && IsValid(PlayerController->PlayerState))
		{
			const FString UniqueId = PlayerController->PlayerState->GetUniqueId().ToString();
			if (!UniqueId.IsEmpty() && UniqueId != TEXT("INVALID"))
			{
				return UniqueId.Left(160);
			}
			return TEXT("name:") + GetPlayerDisplayName(PlayerController);
		}
		return FString();
	}

	FSFPSharedPlanSummary MakeSummary(
		const FSFPSavedPlanInfo& Info,
		const FString& RequesterId)
	{
		FSFPSharedPlanSummary Summary;
		Summary.Name = Info.Name;
		Summary.FileName = Info.FileName;
		Summary.TargetName = Info.TargetName;
		Summary.TargetRatePerMinute = Info.TargetRatePerMinute;
		Summary.TargetCount = Info.TargetCount;
		Summary.bPowerProductionPlan = Info.bPowerProductionPlan;
		Summary.Revision = Info.Revision;
		Summary.OwnerName = Info.OwnerName;
		Summary.UpdatedBy = Info.UpdatedBy;
		Summary.bCanDelete = Info.OwnerId.IsEmpty() || Info.OwnerId == RequesterId;
		return Summary;
	}

	USFPPlannerRemoteCallObject* FindRemoteCallObject(AFGPlayerController* PlayerController)
	{
		return IsValid(PlayerController)
			? Cast<USFPPlannerRemoteCallObject>(PlayerController->GetRemoteCallObjectOfClass(
				USFPPlannerRemoteCallObject::StaticClass()))
			: nullptr;
	}

	void BroadcastSharedPlanChanged(UWorld* World, const FSFPSavedPlanInfo& Info)
	{
		if (!IsValid(World))
		{
			return;
		}
		for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
		{
			AFGPlayerController* PlayerController = Cast<AFGPlayerController>(Iterator->Get());
			USFPPlannerRemoteCallObject* RemoteCallObject = FindRemoteCallObject(PlayerController);
			if (IsValid(RemoteCallObject))
			{
				RemoteCallObject->ClientSharedPlanChanged(MakeSummary(
					Info,
					GetStablePlayerId(PlayerController)));
			}
		}
	}

	void BroadcastSharedPlanDeleted(
		UWorld* World,
		const FString& FileName,
		const FString& PlanName,
		const FString& DeletedBy)
	{
		if (!IsValid(World))
		{
			return;
		}
		for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
		{
			AFGPlayerController* PlayerController = Cast<AFGPlayerController>(Iterator->Get());
			if (USFPPlannerRemoteCallObject* RemoteCallObject = FindRemoteCallObject(PlayerController);
				IsValid(RemoteCallObject))
			{
				RemoteCallObject->ClientSharedPlanDeleted(FileName, PlanName, DeletedBy);
			}
		}
	}

	void SendSharedPlanDownload(
		USFPPlannerRemoteCallObject* RemoteCallObject,
		const FSFPSharedPlanSummary& Summary,
		const FString& PlanJson)
	{
		if (!IsValid(RemoteCallObject))
		{
			return;
		}
		const FString TransferId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const int32 ChunkCount = ChunkCountForCharacters(PlanJson.Len());
		RemoteCallObject->ClientBeginSharedPlanDownload(
			TransferId,
			Summary,
			PlanJson.Len(),
			ChunkCount);
		for (int32 ChunkIndex = 0; ChunkIndex < ChunkCount; ++ChunkIndex)
		{
			RemoteCallObject->ClientAppendSharedPlanDownloadChunk(
				TransferId,
				ChunkIndex,
				PlanJson.Mid(ChunkIndex * SharedPlanChunkCharacters, SharedPlanChunkCharacters));
		}
		RemoteCallObject->ClientCommitSharedPlanDownload(TransferId);
	}
}

void USFPPlannerRemoteCallObject::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USFPPlannerRemoteCallObject, bReplicationAnchor);
}

USFPPlannerRemoteCallObject* USFPPlannerRemoteCallObject::ResolveForPlayer(
	AFGPlayerController* PlayerController,
	FString& OutError)
{
	OutError.Reset();
	if (!IsValid(PlayerController))
	{
		OutError = SFPLocalization::Select(
			TEXT("Kein gültiger Spieler für die Planner-Anfrage"),
			TEXT("No valid player is available for the planner request"));
		return nullptr;
	}
	if (!PlayerController->HasAuthority())
	{
		OutError = SFPLocalization::Select(
			TEXT("Die Planner-Anfrage besitzt keine Server-Autorität"),
			TEXT("The planner request has no server authority"));
		return nullptr;
	}

	USFPPlannerRemoteCallObject* RemoteCallObject = Cast<USFPPlannerRemoteCallObject>(
		PlayerController->GetRemoteCallObjectOfClass(StaticClass()));
	if (!IsValid(RemoteCallObject))
	{
		OutError = SFPLocalization::Select(
			TEXT("Die Multiplayer-Verbindung zum Planner-Client ist noch nicht bereit"),
			TEXT("The multiplayer bridge to the planner client is not ready yet"));
		return nullptr;
	}
	return RemoteCallObject;
}

USFPPlannerRemoteCallObject* USFPPlannerRemoteCallObject::ResolveForLocalPlayer(
	AFGPlayerController* PlayerController,
	FString& OutError)
{
	OutError.Reset();
	if (!IsValid(PlayerController) || !PlayerController->IsLocalController())
	{
		OutError = SFPLocalization::Select(
			TEXT("Multiplayer-Pläne können nur vom lokalen Spieler angefragt werden"),
			TEXT("Multiplayer plans can only be requested by the local player"));
		return nullptr;
	}
	USFPPlannerRemoteCallObject* RemoteCallObject = SFPPlannerRemoteCallObjectPrivate::FindRemoteCallObject(
		PlayerController);
	if (!IsValid(RemoteCallObject))
	{
		OutError = SFPLocalization::Select(
			TEXT("Die Multiplayer-Verbindung für Multiplayer-Pläne ist noch nicht bereit"),
			TEXT("The multiplayer bridge for multiplayer plans is not ready yet"));
		return nullptr;
	}
	return RemoteCallObject;
}

AFGPlayerController* USFPPlannerRemoteCallObject::ResolveOwningPlayer() const
{
	return GetTypedOuter<AFGPlayerController>();
}

AFGPlayerController* USFPPlannerRemoteCallObject::ResolveOwningLocalPlayer() const
{
	if (AFGPlayerController* OuterController = GetTypedOuter<AFGPlayerController>();
		IsValid(OuterController) && OuterController->IsLocalController())
	{
		return OuterController;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return nullptr;
	}
	for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		AFGPlayerController* Candidate = Cast<AFGPlayerController>(Iterator->Get());
		if (IsValid(Candidate) && Candidate->IsLocalController())
		{
			return Candidate;
		}
	}
	return nullptr;
}

bool USFPPlannerRemoteCallObject::RequestOpen(
	AFGPlayerController* PlayerController,
	FString& OutError)
{
	OutError.Reset();
	if (IsValid(PlayerController) && PlayerController->IsLocalController())
	{
		return FSFPPlannerUI::Open(PlayerController, OutError);
	}
	USFPPlannerRemoteCallObject* RemoteCallObject = ResolveForPlayer(PlayerController, OutError);
	if (!IsValid(RemoteCallObject))
	{
		return false;
	}
	RemoteCallObject->ClientOpenPlanner();
	return true;
}

bool USFPPlannerRemoteCallObject::RequestClose(
	AFGPlayerController* PlayerController,
	FString& OutError)
{
	OutError.Reset();
	if (IsValid(PlayerController) && PlayerController->IsLocalController())
	{
		FSFPPlannerUI::Close(PlayerController);
		return true;
	}
	USFPPlannerRemoteCallObject* RemoteCallObject = ResolveForPlayer(PlayerController, OutError);
	if (!IsValid(RemoteCallObject))
	{
		return false;
	}
	RemoteCallObject->ClientClosePlanner();
	return true;
}

bool USFPPlannerRemoteCallObject::RequestToggle(
	AFGPlayerController* PlayerController,
	FString& OutError)
{
	OutError.Reset();
	if (IsValid(PlayerController) && PlayerController->IsLocalController())
	{
		if (FSFPPlannerUI::IsOpen(PlayerController))
		{
			FSFPPlannerUI::Close(PlayerController);
			return true;
		}
		return FSFPPlannerUI::Open(PlayerController, OutError);
	}
	USFPPlannerRemoteCallObject* RemoteCallObject = ResolveForPlayer(PlayerController, OutError);
	if (!IsValid(RemoteCallObject))
	{
		return false;
	}
	RemoteCallObject->ClientTogglePlanner();
	return true;
}

bool USFPPlannerRemoteCallObject::RequestHotkeyStatus(
	AFGPlayerController* PlayerController,
	FString& OutError)
{
	OutError.Reset();
	if (IsValid(PlayerController) && PlayerController->IsLocalController())
	{
		const FString CurrentHotkey = SFPPlannerHotkey::GetDisplayName();
		SFPPlannerRemoteCallObjectPrivate::ShowLocalStatus(
			CurrentHotkey.IsEmpty()
				? SFPLocalization::Select(
					TEXT("Planner-Hotkey: deaktiviert"),
					TEXT("Planner hotkey: disabled"))
				: FString::Printf(TEXT("Planner hotkey: %s"), *CurrentHotkey));
		return true;
	}
	USFPPlannerRemoteCallObject* RemoteCallObject = ResolveForPlayer(PlayerController, OutError);
	if (!IsValid(RemoteCallObject))
	{
		return false;
	}
	RemoteCallObject->ClientShowHotkeyStatus();
	return true;
}

bool USFPPlannerRemoteCallObject::RequestHotkeyChange(
	AFGPlayerController* PlayerController,
	const FString& BindingSpec,
	FString& OutError)
{
	OutError.Reset();
	if (BindingSpec.IsEmpty())
	{
		OutError = SFPLocalization::Select(
			TEXT("Keine Hotkey-Belegung angegeben"),
			TEXT("No hotkey binding was specified"));
		return false;
	}
	if (IsValid(PlayerController) && PlayerController->IsLocalController())
	{
		SFPPlannerRemoteCallObjectPrivate::ApplyLocalHotkey(BindingSpec);
		return true;
	}
	USFPPlannerRemoteCallObject* RemoteCallObject = ResolveForPlayer(PlayerController, OutError);
	if (!IsValid(RemoteCallObject))
	{
		return false;
	}
	RemoteCallObject->ClientApplyHotkey(BindingSpec);
	return true;
}

bool USFPPlannerRemoteCallObject::RequestSharedPlanCatalog(
	AFGPlayerController* PlayerController,
	FString& OutError)
{
	USFPPlannerRemoteCallObject* RemoteCallObject = ResolveForLocalPlayer(PlayerController, OutError);
	if (!IsValid(RemoteCallObject))
	{
		return false;
	}
	RemoteCallObject->ServerRequestSharedPlanCatalog();
	return true;
}

bool USFPPlannerRemoteCallObject::RequestSharedPlanDownload(
	AFGPlayerController* PlayerController,
	const FString& FileName,
	FString& OutError)
{
	if (FileName.IsEmpty())
	{
		OutError = TEXT("Kein Multiplayer-Plan ausgewählt");
		return false;
	}
	USFPPlannerRemoteCallObject* RemoteCallObject = ResolveForLocalPlayer(PlayerController, OutError);
	if (!IsValid(RemoteCallObject))
	{
		return false;
	}
	RemoteCallObject->ServerRequestSharedPlanDownload(FileName);
	return true;
}

bool USFPPlannerRemoteCallObject::RequestSharedPlanSave(
	AFGPlayerController* PlayerController,
	const FString& Name,
	const int64 ExpectedRevision,
	const FSFPPlanResult& Plan,
	FString& OutError)
{
	USFPPlannerRemoteCallObject* RemoteCallObject = ResolveForLocalPlayer(PlayerController, OutError);
	if (!IsValid(RemoteCallObject))
	{
		return false;
	}
	const FString CleanName = Name.TrimStartAndEnd();
	FString PlanJson;
	if (!FSFPPlannerPersistence::SerializePlan(CleanName, Plan, PlanJson, OutError))
	{
		return false;
	}
	if (PlanJson.Len() > SFPPlannerRemoteCallObjectPrivate::MaxSharedPlanUploadCharacters)
	{
		OutError = SFPLocalization::Select(
			TEXT("Der Multiplayer-Plan überschreitet das Netzwerklimit von 512 KiB"),
			TEXT("The multiplayer plan exceeds the 512 KiB network limit"));
		return false;
	}
	const FString TransferId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const int32 ChunkCount = SFPPlannerRemoteCallObjectPrivate::ChunkCountForCharacters(PlanJson.Len());
	RemoteCallObject->ServerBeginSharedPlanUpload(
		TransferId,
		CleanName,
		ExpectedRevision,
		PlanJson.Len(),
		ChunkCount);
	for (int32 ChunkIndex = 0; ChunkIndex < ChunkCount; ++ChunkIndex)
	{
		RemoteCallObject->ServerAppendSharedPlanUploadChunk(
			TransferId,
			ChunkIndex,
			PlanJson.Mid(
				ChunkIndex * SFPPlannerRemoteCallObjectPrivate::SharedPlanChunkCharacters,
				SFPPlannerRemoteCallObjectPrivate::SharedPlanChunkCharacters));
	}
	RemoteCallObject->ServerCommitSharedPlanUpload(TransferId);
	return true;
}

bool USFPPlannerRemoteCallObject::RequestSharedPlanDelete(
	AFGPlayerController* PlayerController,
	const FString& FileName,
	const int64 ExpectedRevision,
	FString& OutError)
{
	USFPPlannerRemoteCallObject* RemoteCallObject = ResolveForLocalPlayer(PlayerController, OutError);
	if (!IsValid(RemoteCallObject))
	{
		return false;
	}
	RemoteCallObject->ServerDeleteSharedPlan(FileName, ExpectedRevision);
	return true;
}

bool USFPPlannerRemoteCallObject::RequestResourceNodeInventory(
	AFGPlayerController* PlayerController,
	FString& OutError)
{
	// Single-player/listen-server worlds can be scanned synchronously. Dedicated
	// clients ask the authoritative server so unloaded or non-replicated actors
	// cannot make their free-node count smaller than the save actually contains.
	if (IsValid(PlayerController) && PlayerController->IsLocalController()
		&& PlayerController->HasAuthority())
	{
		TMap<FString, FSFPResourceNodeAvailability> Availability;
		if (!SFPResourceNodeInventory::Scan(PlayerController->GetWorld(), Availability, OutError))
		{
			return false;
		}
		FSFPPlannerUI::ReceiveResourceNodeInventory(
			PlayerController,
			SFPResourceNodeInventory::ToJson(Availability),
			FString());
		return true;
	}
	USFPPlannerRemoteCallObject* RemoteCallObject = ResolveForLocalPlayer(PlayerController, OutError);
	if (!IsValid(RemoteCallObject))
	{
		return false;
	}
	RemoteCallObject->ServerRequestResourceNodeInventory();
	return true;
}

void USFPPlannerRemoteCallObject::ClientOpenPlanner_Implementation()
{
	AFGPlayerController* PlayerController = ResolveOwningLocalPlayer();
	if (!IsValid(PlayerController))
	{
		UE_LOG(LogSFPFactoryPlanner, Warning, TEXT("SFP client open request has no local player controller"));
		return;
	}

	FString Error;
	if (!FSFPPlannerUI::Open(PlayerController, Error))
	{
		SFPPlannerRemoteCallObjectPrivate::ShowLocalStatus(Error, true);
		return;
	}
	UE_LOG(LogSFPFactoryPlanner, Display, TEXT("SFP planner opened on owning multiplayer client"));
}

void USFPPlannerRemoteCallObject::ClientClosePlanner_Implementation()
{
	if (AFGPlayerController* PlayerController = ResolveOwningLocalPlayer(); IsValid(PlayerController))
	{
		FSFPPlannerUI::Close(PlayerController);
		UE_LOG(LogSFPFactoryPlanner, Display, TEXT("SFP planner close request handled on owning client"));
	}
}

void USFPPlannerRemoteCallObject::ClientTogglePlanner_Implementation()
{
	AFGPlayerController* PlayerController = ResolveOwningLocalPlayer();
	if (!IsValid(PlayerController))
	{
		UE_LOG(LogSFPFactoryPlanner, Warning, TEXT("SFP client toggle request has no local player controller"));
		return;
	}
	if (FSFPPlannerUI::IsOpen(PlayerController))
	{
		FSFPPlannerUI::Close(PlayerController);
		return;
	}
	FString Error;
	if (!FSFPPlannerUI::Open(PlayerController, Error))
	{
		SFPPlannerRemoteCallObjectPrivate::ShowLocalStatus(Error, true);
	}
}

void USFPPlannerRemoteCallObject::ClientShowHotkeyStatus_Implementation()
{
	const FString CurrentHotkey = SFPPlannerHotkey::GetDisplayName();
	SFPPlannerRemoteCallObjectPrivate::ShowLocalStatus(
		CurrentHotkey.IsEmpty()
			? SFPLocalization::Select(
				TEXT("Planner-Hotkey: deaktiviert"),
				TEXT("Planner hotkey: disabled"))
			: FString::Printf(TEXT("Planner hotkey: %s"), *CurrentHotkey));
}

void USFPPlannerRemoteCallObject::ClientApplyHotkey_Implementation(const FString& BindingSpec)
{
	SFPPlannerRemoteCallObjectPrivate::ApplyLocalHotkey(BindingSpec);
}

void USFPPlannerRemoteCallObject::ServerRequestSharedPlanCatalog_Implementation()
{
	AFGPlayerController* Requester = ResolveOwningPlayer();
	if (!IsValid(Requester) || !Requester->HasAuthority())
	{
		ClientReceiveSharedPlanCatalog(TArray<FSFPSharedPlanSummary>(), TEXT("Server-Autorität für die Planliste fehlt"));
		return;
	}

	TArray<FSFPSavedPlanInfo> StoredPlans;
	FString Error;
	FSFPPlannerPersistence::ListSharedPlans(StoredPlans, Error);
	const FString RequesterId = SFPPlannerRemoteCallObjectPrivate::GetStablePlayerId(Requester);
	TArray<FSFPSharedPlanSummary> Summaries;
	Summaries.Reserve(StoredPlans.Num());
	for (const FSFPSavedPlanInfo& Plan : StoredPlans)
	{
		Summaries.Add(SFPPlannerRemoteCallObjectPrivate::MakeSummary(Plan, RequesterId));
	}
	ClientReceiveSharedPlanCatalog(Summaries, Error);
}

void USFPPlannerRemoteCallObject::ServerRequestSharedPlanDownload_Implementation(const FString& FileName)
{
	AFGPlayerController* Requester = ResolveOwningPlayer();
	FSFPSavedPlanInfo Info;
	FString PlanJson;
	FString Error;
	if (!IsValid(Requester) || !Requester->HasAuthority())
	{
		ClientReceiveSharedPlan(FSFPSharedPlanSummary(), FString(), TEXT("Server-Autorität für den Planabruf fehlt"));
		return;
	}
	const TSharedPtr<FSFPPlanResult> Plan = FSFPPlannerPersistence::LoadSharedPlan(
		FileName,
		Info,
		PlanJson,
		Error);
	if (!Plan.IsValid())
	{
		ClientReceiveSharedPlan(FSFPSharedPlanSummary(), FString(), Error);
		return;
	}
	if (PlanJson.Len() > SFPPlannerRemoteCallObjectPrivate::MaxSharedPlanJsonCharacters)
	{
		ClientReceiveSharedPlan(
			FSFPSharedPlanSummary(),
			FString(),
			TEXT("Der gespeicherte Multiplayer-Plan überschreitet das Netzwerklimit von 512 KiB"));
		return;
	}
	SFPPlannerRemoteCallObjectPrivate::SendSharedPlanDownload(
		this,
		SFPPlannerRemoteCallObjectPrivate::MakeSummary(
			Info,
			SFPPlannerRemoteCallObjectPrivate::GetStablePlayerId(Requester)),
		PlanJson);
}

void USFPPlannerRemoteCallObject::ResetSharedPlanUpload()
{
	PendingUploadTransferId.Reset();
	PendingUploadName.Reset();
	PendingUploadJson.Reset();
	PendingUploadExpectedRevision = 0;
	PendingUploadTotalCharacters = 0;
	PendingUploadChunkCount = 0;
	PendingUploadNextChunk = 0;
}

void USFPPlannerRemoteCallObject::ResetSharedPlanDownload()
{
	PendingDownloadTransferId.Reset();
	PendingDownloadJson.Reset();
	PendingDownloadSummary = FSFPSharedPlanSummary();
	PendingDownloadTotalCharacters = 0;
	PendingDownloadChunkCount = 0;
	PendingDownloadNextChunk = 0;
}

void USFPPlannerRemoteCallObject::ServerBeginSharedPlanUpload_Implementation(
	const FString& TransferId,
	const FString& Name,
	const int64 ExpectedRevision,
	const int32 TotalCharacters,
	const int32 ChunkCount)
{
	ResetSharedPlanUpload();
	if (!SFPPlannerRemoteCallObjectPrivate::IsValidTransferId(TransferId)
		|| Name.TrimStartAndEnd().IsEmpty()
		|| Name.Len() > 80
		|| TotalCharacters <= 0
		|| TotalCharacters > SFPPlannerRemoteCallObjectPrivate::MaxSharedPlanUploadCharacters
		|| ChunkCount <= 0)
	{
		ClientReceiveSharedPlanSaveResult(
			false,
			FSFPSharedPlanSummary(),
			TEXT("Ungültige Multiplayer-Plan-Übertragung wurde abgewiesen"));
		return;
	}
	const int32 ExpectedChunkCount = SFPPlannerRemoteCallObjectPrivate::ChunkCountForCharacters(
		TotalCharacters);
	if (ChunkCount != ExpectedChunkCount)
	{
		ClientReceiveSharedPlanSaveResult(
			false,
			FSFPSharedPlanSummary(),
			TEXT("Ungültige Multiplayer-Plan-Blockanzahl wurde abgewiesen"));
		return;
	}
	PendingUploadTransferId = TransferId;
	PendingUploadName = Name;
	PendingUploadExpectedRevision = ExpectedRevision;
	PendingUploadTotalCharacters = TotalCharacters;
	PendingUploadChunkCount = ChunkCount;
	PendingUploadJson.Reserve(TotalCharacters);
}

void USFPPlannerRemoteCallObject::ServerAppendSharedPlanUploadChunk_Implementation(
	const FString& TransferId,
	const int32 ChunkIndex,
	const FString& Chunk)
{
	if (PendingUploadTransferId != TransferId)
	{
		return;
	}
	if (ChunkIndex != PendingUploadNextChunk
		|| Chunk.IsEmpty()
		|| Chunk.Len() > SFPPlannerRemoteCallObjectPrivate::SharedPlanChunkCharacters
		|| PendingUploadJson.Len() + Chunk.Len() > PendingUploadTotalCharacters)
	{
		ResetSharedPlanUpload();
		ClientReceiveSharedPlanSaveResult(
			false,
			FSFPSharedPlanSummary(),
			TEXT("Multiplayer-Plan-Datenblöcke sind unvollständig oder ungeordnet"));
		return;
	}
	PendingUploadJson += Chunk;
	++PendingUploadNextChunk;
}

void USFPPlannerRemoteCallObject::ServerCommitSharedPlanUpload_Implementation(
	const FString& TransferId)
{
	if (PendingUploadTransferId.IsEmpty())
	{
		return;
	}
	AFGPlayerController* Requester = ResolveOwningPlayer();
	FSFPSharedPlanSummary EmptySummary;
	if (!IsValid(Requester) || !Requester->HasAuthority())
	{
		ClientReceiveSharedPlanSaveResult(false, EmptySummary, TEXT("Server-Autorität zum Speichern fehlt"));
		ResetSharedPlanUpload();
		return;
	}
	if (PendingUploadTransferId != TransferId
		|| PendingUploadNextChunk != PendingUploadChunkCount
		|| PendingUploadJson.Len() != PendingUploadTotalCharacters)
	{
		ResetSharedPlanUpload();
		ClientReceiveSharedPlanSaveResult(false, EmptySummary, TEXT("Multiplayer-Plan-Übertragung wurde nicht vollständig abgeschlossen"));
		return;
	}
	const FString Name = PendingUploadName;
	const FString PlanJson = MoveTemp(PendingUploadJson);
	const int64 ExpectedRevision = PendingUploadExpectedRevision;
	ResetSharedPlanUpload();

	FString SerializedName;
	FString Error;
	TSharedPtr<FSFPPlanResult> Plan = FSFPPlannerPersistence::DeserializePlan(
		PlanJson,
		SerializedName,
		Error);
	if (!Plan.IsValid())
	{
		ClientReceiveSharedPlanSaveResult(false, EmptySummary, Error);
		return;
	}
	if (!SerializedName.Equals(Name.TrimStartAndEnd(), ESearchCase::CaseSensitive))
	{
		ClientReceiveSharedPlanSaveResult(false, EmptySummary, TEXT("Planname und übertragene Daten stimmen nicht überein"));
		return;
	}

	const FString RequesterId = SFPPlannerRemoteCallObjectPrivate::GetStablePlayerId(Requester);
	const FString RequesterName = SFPPlannerRemoteCallObjectPrivate::GetPlayerDisplayName(Requester);
	FSFPSavedPlanInfo SavedInfo;
	if (!FSFPPlannerPersistence::SaveSharedPlan(
		Name,
		*Plan,
		RequesterId,
		RequesterName,
		ExpectedRevision,
		SavedInfo,
		Error))
	{
		ClientReceiveSharedPlanSaveResult(false, EmptySummary, Error);
		return;
	}

	const FSFPSharedPlanSummary Summary = SFPPlannerRemoteCallObjectPrivate::MakeSummary(
		SavedInfo,
		RequesterId);
	ClientReceiveSharedPlanSaveResult(true, Summary, FString());
	SFPPlannerRemoteCallObjectPrivate::BroadcastSharedPlanChanged(GetWorld(), SavedInfo);
	UE_LOG(
		LogSFPFactoryPlanner,
		Display,
		TEXT("Shared multiplayer plan '%s' revision %lld saved by %s"),
		*SavedInfo.Name,
		static_cast<long long>(SavedInfo.Revision),
		*RequesterName);
}

void USFPPlannerRemoteCallObject::ServerDeleteSharedPlan_Implementation(
	const FString& FileName,
	const int64 ExpectedRevision)
{
	AFGPlayerController* Requester = ResolveOwningPlayer();
	if (!IsValid(Requester) || !Requester->HasAuthority())
	{
		ClientReceiveSharedPlanDeleteResult(false, FileName, FString(), TEXT("Server-Autorität zum Löschen fehlt"));
		return;
	}

	FSFPSavedPlanInfo ExistingInfo;
	FString ExistingJson;
	FString Error;
	if (!FSFPPlannerPersistence::LoadSharedPlan(FileName, ExistingInfo, ExistingJson, Error).IsValid())
	{
		ClientReceiveSharedPlanDeleteResult(false, FileName, FString(), Error);
		return;
	}
	const FString RequesterId = SFPPlannerRemoteCallObjectPrivate::GetStablePlayerId(Requester);
	const FString RequesterName = SFPPlannerRemoteCallObjectPrivate::GetPlayerDisplayName(Requester);
	if (!FSFPPlannerPersistence::DeleteSharedPlan(FileName, RequesterId, ExpectedRevision, Error))
	{
		ClientReceiveSharedPlanDeleteResult(false, FileName, ExistingInfo.Name, Error);
		return;
	}

	ClientReceiveSharedPlanDeleteResult(true, FileName, ExistingInfo.Name, FString());
	SFPPlannerRemoteCallObjectPrivate::BroadcastSharedPlanDeleted(
		GetWorld(),
		FileName,
		ExistingInfo.Name,
		RequesterName);
	UE_LOG(
		LogSFPFactoryPlanner,
		Display,
		TEXT("Shared multiplayer plan '%s' deleted by %s"),
		*ExistingInfo.Name,
		*RequesterName);
}

void USFPPlannerRemoteCallObject::ServerRequestResourceNodeInventory_Implementation()
{
	AFGPlayerController* Requester = ResolveOwningPlayer();
	if (!IsValid(Requester) || !Requester->HasAuthority())
	{
		ClientReceiveResourceNodeInventory(FString(), TEXT("Server-Autorität für die Rohstoffinventur fehlt"));
		return;
	}
	TMap<FString, FSFPResourceNodeAvailability> Availability;
	FString Error;
	if (!SFPResourceNodeInventory::Scan(GetWorld(), Availability, Error))
	{
		ClientReceiveResourceNodeInventory(FString(), Error);
		return;
	}
	ClientReceiveResourceNodeInventory(SFPResourceNodeInventory::ToJson(Availability), FString());
}

void USFPPlannerRemoteCallObject::ClientReceiveSharedPlanCatalog_Implementation(
	const TArray<FSFPSharedPlanSummary>& Plans,
	const FString& Error)
{
	if (AFGPlayerController* PlayerController = ResolveOwningLocalPlayer(); IsValid(PlayerController))
	{
		FSFPPlannerUI::ReceiveSharedPlanCatalog(PlayerController, Plans, Error);
	}
}

void USFPPlannerRemoteCallObject::ClientReceiveSharedPlan_Implementation(
	const FSFPSharedPlanSummary& Summary,
	const FString& PlanJson,
	const FString& Error)
{
	if (AFGPlayerController* PlayerController = ResolveOwningLocalPlayer(); IsValid(PlayerController))
	{
		FSFPPlannerUI::ReceiveSharedPlan(PlayerController, Summary, PlanJson, Error);
	}
}

void USFPPlannerRemoteCallObject::ClientBeginSharedPlanDownload_Implementation(
	const FString& TransferId,
	const FSFPSharedPlanSummary& Summary,
	const int32 TotalCharacters,
	const int32 ChunkCount)
{
	ResetSharedPlanDownload();
	if (!SFPPlannerRemoteCallObjectPrivate::IsValidTransferId(TransferId)
		|| Summary.FileName.IsEmpty()
		|| Summary.Revision <= 0
		|| TotalCharacters <= 0
		|| TotalCharacters > SFPPlannerRemoteCallObjectPrivate::MaxSharedPlanJsonCharacters
		|| ChunkCount != SFPPlannerRemoteCallObjectPrivate::ChunkCountForCharacters(TotalCharacters))
	{
		if (AFGPlayerController* PlayerController = ResolveOwningLocalPlayer(); IsValid(PlayerController))
		{
			FSFPPlannerUI::ReceiveSharedPlan(
				PlayerController,
				FSFPSharedPlanSummary(),
				FString(),
				TEXT("Ungültiger Multiplayer-Plan-Download wurde abgewiesen"));
		}
		return;
	}
	PendingDownloadTransferId = TransferId;
	PendingDownloadSummary = Summary;
	PendingDownloadTotalCharacters = TotalCharacters;
	PendingDownloadChunkCount = ChunkCount;
	PendingDownloadJson.Reserve(TotalCharacters);
}

void USFPPlannerRemoteCallObject::ClientAppendSharedPlanDownloadChunk_Implementation(
	const FString& TransferId,
	const int32 ChunkIndex,
	const FString& Chunk)
{
	if (PendingDownloadTransferId != TransferId)
	{
		return;
	}
	if (ChunkIndex != PendingDownloadNextChunk
		|| Chunk.IsEmpty()
		|| Chunk.Len() > SFPPlannerRemoteCallObjectPrivate::SharedPlanChunkCharacters
		|| PendingDownloadJson.Len() + Chunk.Len() > PendingDownloadTotalCharacters)
	{
		ResetSharedPlanDownload();
		if (AFGPlayerController* PlayerController = ResolveOwningLocalPlayer(); IsValid(PlayerController))
		{
			FSFPPlannerUI::ReceiveSharedPlan(
				PlayerController,
				FSFPSharedPlanSummary(),
				FString(),
				TEXT("Multiplayer-Plan-Download enthält unvollständige oder ungeordnete Datenblöcke"));
		}
		return;
	}
	PendingDownloadJson += Chunk;
	++PendingDownloadNextChunk;
}

void USFPPlannerRemoteCallObject::ClientCommitSharedPlanDownload_Implementation(const FString& TransferId)
{
	if (PendingDownloadTransferId.IsEmpty())
	{
		return;
	}
	if (PendingDownloadTransferId != TransferId
		|| PendingDownloadNextChunk != PendingDownloadChunkCount
		|| PendingDownloadJson.Len() != PendingDownloadTotalCharacters)
	{
		ResetSharedPlanDownload();
		if (AFGPlayerController* PlayerController = ResolveOwningLocalPlayer(); IsValid(PlayerController))
		{
			FSFPPlannerUI::ReceiveSharedPlan(
				PlayerController,
				FSFPSharedPlanSummary(),
				FString(),
				TEXT("Multiplayer-Plan-Download wurde nicht vollständig abgeschlossen"));
		}
		return;
	}
	const FSFPSharedPlanSummary Summary = PendingDownloadSummary;
	const FString PlanJson = MoveTemp(PendingDownloadJson);
	ResetSharedPlanDownload();
	if (AFGPlayerController* PlayerController = ResolveOwningLocalPlayer(); IsValid(PlayerController))
	{
		FSFPPlannerUI::ReceiveSharedPlan(PlayerController, Summary, PlanJson, FString());
		UE_LOG(
			LogSFPFactoryPlanner,
			Display,
			TEXT("Shared multiplayer plan '%s' revision %lld received on owning client"),
			*Summary.Name,
			static_cast<long long>(Summary.Revision));
	}
}

void USFPPlannerRemoteCallObject::ClientReceiveSharedPlanSaveResult_Implementation(
	const bool bSuccess,
	const FSFPSharedPlanSummary& Summary,
	const FString& Error)
{
	if (AFGPlayerController* PlayerController = ResolveOwningLocalPlayer(); IsValid(PlayerController))
	{
		FSFPPlannerUI::ReceiveSharedPlanSaveResult(PlayerController, bSuccess, Summary, Error);
	}
}

void USFPPlannerRemoteCallObject::ClientReceiveSharedPlanDeleteResult_Implementation(
	const bool bSuccess,
	const FString& FileName,
	const FString& PlanName,
	const FString& Error)
{
	if (AFGPlayerController* PlayerController = ResolveOwningLocalPlayer(); IsValid(PlayerController))
	{
		FSFPPlannerUI::ReceiveSharedPlanDeleteResult(
			PlayerController,
			bSuccess,
			FileName,
			PlanName,
			Error);
	}
}

void USFPPlannerRemoteCallObject::ClientReceiveResourceNodeInventory_Implementation(
	const FString& InventoryJson,
	const FString& Error)
{
	if (AFGPlayerController* PlayerController = ResolveOwningLocalPlayer(); IsValid(PlayerController))
	{
		FSFPPlannerUI::ReceiveResourceNodeInventory(PlayerController, InventoryJson, Error);
	}
}

void USFPPlannerRemoteCallObject::ClientSharedPlanChanged_Implementation(
	const FSFPSharedPlanSummary& Summary)
{
	if (AFGPlayerController* PlayerController = ResolveOwningLocalPlayer(); IsValid(PlayerController))
	{
		FSFPPlannerUI::NotifySharedPlanChanged(PlayerController, Summary);
	}
}

void USFPPlannerRemoteCallObject::ClientSharedPlanDeleted_Implementation(
	const FString& FileName,
	const FString& PlanName,
	const FString& DeletedBy)
{
	if (AFGPlayerController* PlayerController = ResolveOwningLocalPlayer(); IsValid(PlayerController))
	{
		FSFPPlannerUI::NotifySharedPlanDeleted(PlayerController, FileName, PlanName, DeletedBy);
	}
}

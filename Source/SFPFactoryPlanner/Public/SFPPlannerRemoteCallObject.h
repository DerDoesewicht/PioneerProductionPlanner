#pragma once

#include "CoreMinimal.h"
#include "FGRemoteCallObject.h"
#include "SFPSharedPlanTypes.h"
#include "SFPPlannerRemoteCallObject.generated.h"

class AFGPlayerController;
class FLifetimeProperty;
struct FSFPPlanResult;

/**
 * Network-owned bridge from server-side interactions/chat commands to the
 * owning player's local planner UI. Personal plans remain client-local; the
 * explicit Server Plans mode exchanges validated JSON with authority-owned
 * storage through this object.
 */
UCLASS()
class SFPFACTORYPLANNER_API USFPPlannerRemoteCallObject : public UFGRemoteCallObject
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Executes immediately for a local controller or routes to its owning client. */
	static bool RequestOpen(AFGPlayerController* PlayerController, FString& OutError);
	static bool RequestClose(AFGPlayerController* PlayerController, FString& OutError);
	static bool RequestToggle(AFGPlayerController* PlayerController, FString& OutError);
	static bool RequestHotkeyStatus(AFGPlayerController* PlayerController, FString& OutError);
	static bool RequestHotkeyChange(
		AFGPlayerController* PlayerController,
		const FString& BindingSpec,
		FString& OutError);
	static bool RequestSharedPlanCatalog(AFGPlayerController* PlayerController, FString& OutError);
	static bool RequestSharedPlanDownload(
		AFGPlayerController* PlayerController,
		const FString& FileName,
		FString& OutError);
	static bool RequestSharedPlanSave(
		AFGPlayerController* PlayerController,
		const FString& Name,
		int64 ExpectedRevision,
		const FSFPPlanResult& Plan,
		FString& OutError);
	static bool RequestSharedPlanDelete(
		AFGPlayerController* PlayerController,
		const FString& FileName,
		int64 ExpectedRevision,
		FString& OutError);
	/** Requests live resource-node totals and occupancy from the authoritative world. */
	static bool RequestResourceNodeInventory(AFGPlayerController* PlayerController, FString& OutError);

	UFUNCTION(Client, Reliable)
	void ClientOpenPlanner();

	UFUNCTION(Client, Reliable)
	void ClientClosePlanner();

	UFUNCTION(Client, Reliable)
	void ClientTogglePlanner();

	UFUNCTION(Client, Reliable)
	void ClientShowHotkeyStatus();

	UFUNCTION(Client, Reliable)
	void ClientApplyHotkey(const FString& BindingSpec);

	UFUNCTION(Server, Reliable)
	void ServerRequestSharedPlanCatalog();

	UFUNCTION(Server, Reliable)
	void ServerRequestSharedPlanDownload(const FString& FileName);

	UFUNCTION(Server, Reliable)
	void ServerBeginSharedPlanUpload(
		const FString& TransferId,
		const FString& Name,
		int64 ExpectedRevision,
		int32 TotalCharacters,
		int32 ChunkCount);

	UFUNCTION(Server, Reliable)
	void ServerAppendSharedPlanUploadChunk(
		const FString& TransferId,
		int32 ChunkIndex,
		const FString& Chunk);

	UFUNCTION(Server, Reliable)
	void ServerCommitSharedPlanUpload(const FString& TransferId);

	UFUNCTION(Server, Reliable)
	void ServerDeleteSharedPlan(const FString& FileName, int64 ExpectedRevision);

	UFUNCTION(Server, Reliable)
	void ServerRequestResourceNodeInventory();

	UFUNCTION(Client, Reliable)
	void ClientReceiveSharedPlanCatalog(const TArray<FSFPSharedPlanSummary>& Plans, const FString& Error);

	UFUNCTION(Client, Reliable)
	void ClientReceiveSharedPlan(
		const FSFPSharedPlanSummary& Summary,
		const FString& PlanJson,
		const FString& Error);

	UFUNCTION(Client, Reliable)
	void ClientBeginSharedPlanDownload(
		const FString& TransferId,
		const FSFPSharedPlanSummary& Summary,
		int32 TotalCharacters,
		int32 ChunkCount);

	UFUNCTION(Client, Reliable)
	void ClientAppendSharedPlanDownloadChunk(
		const FString& TransferId,
		int32 ChunkIndex,
		const FString& Chunk);

	UFUNCTION(Client, Reliable)
	void ClientCommitSharedPlanDownload(const FString& TransferId);

	UFUNCTION(Client, Reliable)
	void ClientReceiveSharedPlanSaveResult(
		bool bSuccess,
		const FSFPSharedPlanSummary& Summary,
		const FString& Error);

	UFUNCTION(Client, Reliable)
	void ClientReceiveSharedPlanDeleteResult(
		bool bSuccess,
		const FString& FileName,
		const FString& PlanName,
		const FString& Error);

	UFUNCTION(Client, Reliable)
	void ClientReceiveResourceNodeInventory(const FString& InventoryJson, const FString& Error);

	UFUNCTION(Client, Reliable)
	void ClientSharedPlanChanged(const FSFPSharedPlanSummary& Summary);

	UFUNCTION(Client, Reliable)
	void ClientSharedPlanDeleted(const FString& FileName, const FString& PlanName, const FString& DeletedBy);

	// UFGRemoteCallObject requires at least one replicated property for its RPC
	// channel to be created reliably by Unreal.
	UPROPERTY(Replicated)
	bool bReplicationAnchor = true;

private:
	static USFPPlannerRemoteCallObject* ResolveForPlayer(
		AFGPlayerController* PlayerController,
		FString& OutError);
	static USFPPlannerRemoteCallObject* ResolveForLocalPlayer(
		AFGPlayerController* PlayerController,
		FString& OutError);
	AFGPlayerController* ResolveOwningPlayer() const;
	AFGPlayerController* ResolveOwningLocalPlayer() const;
	void ResetSharedPlanUpload();
	void ResetSharedPlanDownload();

	FString PendingUploadTransferId;
	FString PendingUploadName;
	FString PendingUploadJson;
	int64 PendingUploadExpectedRevision = 0;
	int32 PendingUploadTotalCharacters = 0;
	int32 PendingUploadChunkCount = 0;
	int32 PendingUploadNextChunk = 0;

	FString PendingDownloadTransferId;
	FString PendingDownloadJson;
	FSFPSharedPlanSummary PendingDownloadSummary;
	int32 PendingDownloadTotalCharacters = 0;
	int32 PendingDownloadChunkCount = 0;
	int32 PendingDownloadNextChunk = 0;
};

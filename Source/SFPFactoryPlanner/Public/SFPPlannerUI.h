#pragma once

#include "CoreMinimal.h"
#include "SFPSharedPlanTypes.h"

class AFGPlayerController;

/** Owns the local player's native Slate planner window. */
class SFPFACTORYPLANNER_API FSFPPlannerUI
{
public:
	static bool Open(AFGPlayerController* PlayerController, FString& OutError);
	static bool Close(AFGPlayerController* PlayerController);
	static bool IsOpen(const AFGPlayerController* PlayerController);
	static void ReceiveSharedPlanCatalog(
		AFGPlayerController* PlayerController,
		const TArray<FSFPSharedPlanSummary>& Plans,
		const FString& Error);
	static void ReceiveSharedPlan(
		AFGPlayerController* PlayerController,
		const FSFPSharedPlanSummary& Summary,
		const FString& PlanJson,
		const FString& Error);
	static void ReceiveSharedPlanSaveResult(
		AFGPlayerController* PlayerController,
		bool bSuccess,
		const FSFPSharedPlanSummary& Summary,
		const FString& Error);
	static void ReceiveSharedPlanDeleteResult(
		AFGPlayerController* PlayerController,
		bool bSuccess,
		const FString& FileName,
		const FString& PlanName,
		const FString& Error);
	static void NotifySharedPlanChanged(
		AFGPlayerController* PlayerController,
		const FSFPSharedPlanSummary& Summary);
	static void NotifySharedPlanDeleted(
		AFGPlayerController* PlayerController,
		const FString& FileName,
		const FString& PlanName,
		const FString& DeletedBy);
};

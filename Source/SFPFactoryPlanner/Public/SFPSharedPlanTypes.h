#pragma once

#include "CoreMinimal.h"
#include "SFPSharedPlanTypes.generated.h"

/** Small, network-safe catalog entry for one server-owned community plan. */
USTRUCT()
struct SFPFACTORYPLANNER_API FSFPSharedPlanSummary
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString FileName;

	UPROPERTY()
	FString TargetName;

	UPROPERTY()
	double TargetRatePerMinute = 0.0;

	UPROPERTY()
	int32 TargetCount = 1;

	UPROPERTY()
	bool bPowerProductionPlan = false;

	/** Optimistic-concurrency token. Zero is reserved for a new plan. */
	UPROPERTY()
	int64 Revision = 0;

	UPROPERTY()
	FString OwnerName;

	UPROPERTY()
	FString UpdatedBy;

	/** Computed for the receiving player; only the creator may delete. */
	UPROPERTY()
	bool bCanDelete = false;
};

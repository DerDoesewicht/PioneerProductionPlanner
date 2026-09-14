#pragma once

#include "CoreMinimal.h"
#include "SFPPlannerTypes.h"

struct FSFPSavedPlanInfo
{
	FString Name;
	FString FileName;
	FString TargetName;
	double TargetRatePerMinute = 0.0;
	int32 TargetCount = 1;
	bool bPowerProductionPlan = false;
	FDateTime ModifiedAt;
	int64 Revision = 0;
	FString OwnerId;
	FString OwnerName;
	FString UpdatedBy;
	bool bCanDelete = false;
};

/** Stores automatic and named production graphs below the game's Saved directory. */
class SFPFACTORYPLANNER_API FSFPPlannerPersistence
{
public:
	static bool SaveLastPlan(const FSFPPlanResult& Plan, FString& OutPath, FString& OutError);
	static TSharedPtr<FSFPPlanResult> LoadLastPlan(FString& OutPath, FString& OutError);
	static bool DeleteLastPlan(FString& OutPath, FString& OutError);
	static bool SaveNamedPlan(const FString& Name, const FSFPPlanResult& Plan, FString& OutFileName, FString& OutPath, FString& OutError);
	static TSharedPtr<FSFPPlanResult> LoadNamedPlan(const FString& FileName, FString& OutPath, FString& OutError);
	static bool DeleteNamedPlan(const FString& FileName, FString& OutPath, FString& OutError);
	static bool ListNamedPlans(TArray<FSFPSavedPlanInfo>& OutPlans, FString& OutError);
	/** Serialize without touching a local file; used by the per-player RCO. */
	static bool SerializePlan(
		const FString& Name,
		const FSFPPlanResult& Plan,
		FString& OutJson,
		FString& OutError);
	/** Deserialize received plan JSON without exposing the server file system. */
	static TSharedPtr<FSFPPlanResult> DeserializePlan(
		const FString& Json,
		FString& OutPlanName,
		FString& OutError);
	/** Authority-only storage used by the multiplayer bridge. */
	static bool SaveSharedPlan(
		const FString& Name,
		const FSFPPlanResult& Plan,
		const FString& EditorId,
		const FString& EditorName,
		int64 ExpectedRevision,
		FSFPSavedPlanInfo& OutInfo,
		FString& OutError);
	/** Returns client-safe JSON without the authority-only owner account ID. */
	static TSharedPtr<FSFPPlanResult> LoadSharedPlan(
		const FString& FileName,
		FSFPSavedPlanInfo& OutInfo,
		FString& OutJson,
		FString& OutError);
	static bool DeleteSharedPlan(
		const FString& FileName,
		const FString& RequesterId,
		int64 ExpectedRevision,
		FString& OutError);
	static bool ListSharedPlans(TArray<FSFPSavedPlanInfo>& OutPlans, FString& OutError);
	static FString GetLastPlanPath();
	static FString GetNamedPlansDirectory();
	static FString GetSharedPlansDirectory();
};

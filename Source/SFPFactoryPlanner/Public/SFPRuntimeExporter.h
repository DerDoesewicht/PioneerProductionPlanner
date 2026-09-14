#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SFPRuntimeExporter.generated.h"

USTRUCT(BlueprintType)
struct SFPFACTORYPLANNER_API FSFPExportResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SFP Factory Planner")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "SFP Factory Planner")
	FString FilePath;

	UPROPERTY(BlueprintReadOnly, Category = "SFP Factory Planner")
	FString ErrorMessage;

	UPROPERTY(BlueprintReadOnly, Category = "SFP Factory Planner")
	int32 RecipeCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SFP Factory Planner")
	int32 ItemCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SFP Factory Planner")
	int32 MachineCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SFP Factory Planner")
	int32 TransportCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SFP Factory Planner")
	int32 WarningCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SFP Factory Planner")
	bool bSatisfactoryPlusDetected = false;

	UPROPERTY(BlueprintReadOnly, Category = "SFP Factory Planner")
	FString SatisfactoryPlusVersion;
};

/**
 * Crash-safe runtime data access used by both the chat command and the future
 * in-game planner UI.
 */
UCLASS()
class SFPFACTORYPLANNER_API USFPRuntimeExporter : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "SFP Factory Planner", meta = (WorldContext = "WorldContextObject"))
	static FSFPExportResult ExportRuntimeData(const UObject* WorldContextObject);
};

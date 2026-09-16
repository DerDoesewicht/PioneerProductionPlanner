#pragma once

#include "CoreMinimal.h"

class UWorld;

/** Live save-world counts for one resource descriptor, split by node purity. */
struct FSFPResourceNodeAvailability
{
	int32 ImpureTotal = 0;
	int32 ImpureOccupied = 0;
	int32 NormalTotal = 0;
	int32 NormalOccupied = 0;
	int32 PureTotal = 0;
	int32 PureOccupied = 0;

	int32 TotalForPurity(const FString& Purity) const;
	int32 OccupiedForPurity(const FString& Purity) const;
	int32 FreeForPurity(const FString& Purity) const;
};

/** Server-authoritative resource-node inventory exchanged as compact JSON. */
namespace SFPResourceNodeInventory
{
	bool Scan(UWorld* World, TMap<FString, FSFPResourceNodeAvailability>& OutAvailability, FString& OutError);
	FString ToJson(const TMap<FString, FSFPResourceNodeAvailability>& Availability);
	bool FromJson(const FString& Json, TMap<FString, FSFPResourceNodeAvailability>& OutAvailability, FString& OutError);
}

#pragma once

#include "CoreMinimal.h"

/** Consistent, compact German/English number formatting for all visible planner text. */
class SFPFACTORYPLANNER_API FSFPNumberFormatting
{
public:
	/** Uses the active planner language's separators and removes trailing zeroes. */
	static FString Decimal(double Value, int32 MaxFractionalDigits = 3);
};

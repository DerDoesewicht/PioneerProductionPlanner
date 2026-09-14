#pragma once

#include "CoreMinimal.h"

/**
 * Lightweight runtime localization for the Slate-only planner UI.
 * German is selected for de-* game languages; every other language uses English.
 */
namespace SFPLocalization
{
	SFPFACTORYPLANNER_API bool IsGerman();
	SFPFACTORYPLANNER_API FString Select(const TCHAR* German, const TCHAR* English);
	SFPFACTORYPLANNER_API FString Translate(const FString& Source);
	SFPFACTORYPLANNER_API FText Text(const FString& Source);
	SFPFACTORYPLANNER_API FText Text(const TCHAR* Source);
}

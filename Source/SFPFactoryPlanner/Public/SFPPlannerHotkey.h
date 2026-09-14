#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

struct FKeyEvent;

/**
 * User-owned key binding for opening and closing the planner.
 *
 * This deliberately stays independent of ConfigLib and other optional mods. The
 * binding is stored in the game's normal user settings ini and is therefore
 * local to the player rather than the save file.
 */
struct FSFPPlannerHotkeyBinding
{
	FKey Key;
	bool bShift = false;
	bool bControl = false;
	bool bAlt = false;
	bool bCommand = false;

	bool IsEnabled() const
	{
		return Key.IsValid();
	}
};

namespace SFPPlannerHotkey
{
	/** Returns the persisted binding. F8 is used until the player changes it. */
	SFPFACTORYPLANNER_API FSFPPlannerHotkeyBinding Get();

	/** Human-readable key chord, or an empty string when the hotkey is disabled. */
	SFPFACTORYPLANNER_API FString GetDisplayName();

	/** True only when the complete key chord matches the configured binding. */
	SFPFACTORYPLANNER_API bool Matches(const FKeyEvent& KeyEvent);

	/** Validates and persists a key chord. Modifier-only bindings are rejected. */
	SFPFACTORYPLANNER_API bool Set(const FSFPPlannerHotkeyBinding& Binding, FString& OutError);

	/** Parses names such as F9 or Ctrl+F9 and persists the resulting chord. */
	SFPFACTORYPLANNER_API bool SetFromString(const FString& ChordText, FString& OutError);

	/** Disables the keyboard shortcut while keeping terminal/chat access intact. */
	SFPFACTORYPLANNER_API void Disable();

	/** Restores the original F8 binding. */
	SFPFACTORYPLANNER_API void ResetToDefault();

	/** Suppresses global hotkey handling while the planner captures a new key. */
	SFPFACTORYPLANNER_API void SetCaptureInProgress(bool bInProgress);
	SFPFACTORYPLANNER_API bool IsCaptureInProgress();
}

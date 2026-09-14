#include "SFPPlannerHotkey.h"

#include "SFPFactoryPlanner.h"

#include "Input/Events.h"
#include "Misc/ConfigCacheIni.h"

namespace SFPPlannerHotkeyPrivate
{
	const TCHAR* ConfigSection = TEXT("SFPFactoryPlanner.Input");
	const TCHAR* ConfigKey = TEXT("OpenPlannerKey");
	const TCHAR* ConfigShift = TEXT("OpenPlannerShift");
	const TCHAR* ConfigControl = TEXT("OpenPlannerControl");
	const TCHAR* ConfigAlt = TEXT("OpenPlannerAlt");
	const TCHAR* ConfigCommand = TEXT("OpenPlannerCommand");
	const TCHAR* DisabledKeyName = TEXT("Disabled");

	FSFPPlannerHotkeyBinding CachedBinding;
	bool bBindingLoaded = false;
	bool bCaptureInProgress = false;

	FSFPPlannerHotkeyBinding MakeDefaultBinding()
	{
		FSFPPlannerHotkeyBinding Binding;
		Binding.Key = EKeys::F8;
		return Binding;
	}

	bool IsModifierKey(const FKey& Key)
	{
		return Key == EKeys::LeftShift
			|| Key == EKeys::RightShift
			|| Key == EKeys::LeftControl
			|| Key == EKeys::RightControl
			|| Key == EKeys::LeftAlt
			|| Key == EKeys::RightAlt
			|| Key == EKeys::LeftCommand
			|| Key == EKeys::RightCommand;
	}

	void LoadBinding()
	{
		if (bBindingLoaded)
		{
			return;
		}

		bBindingLoaded = true;
		CachedBinding = MakeDefaultBinding();
		if (GConfig == nullptr)
		{
			return;
		}

		FString KeyName;
		if (!GConfig->GetString(ConfigSection, ConfigKey, KeyName, GGameUserSettingsIni))
		{
			return;
		}

		if (KeyName.Equals(DisabledKeyName, ESearchCase::IgnoreCase))
		{
			CachedBinding = FSFPPlannerHotkeyBinding();
			return;
		}

		const FKey LoadedKey{FName(*KeyName)};
		if (!LoadedKey.IsValid() || IsModifierKey(LoadedKey))
		{
			UE_LOG(
				LogSFPFactoryPlanner,
				Warning,
				TEXT("Ignoring invalid configured planner hotkey '%s'; using F8"),
				*KeyName);
			return;
		}

		CachedBinding.Key = LoadedKey;
		GConfig->GetBool(ConfigSection, ConfigShift, CachedBinding.bShift, GGameUserSettingsIni);
		GConfig->GetBool(ConfigSection, ConfigControl, CachedBinding.bControl, GGameUserSettingsIni);
		GConfig->GetBool(ConfigSection, ConfigAlt, CachedBinding.bAlt, GGameUserSettingsIni);
		GConfig->GetBool(ConfigSection, ConfigCommand, CachedBinding.bCommand, GGameUserSettingsIni);
	}

	void SaveBinding()
	{
		if (GConfig == nullptr)
		{
			return;
		}

		const FString KeyName = CachedBinding.IsEnabled()
			? CachedBinding.Key.GetFName().ToString()
			: FString(DisabledKeyName);
		GConfig->SetString(ConfigSection, ConfigKey, *KeyName, GGameUserSettingsIni);
		GConfig->SetBool(ConfigSection, ConfigShift, CachedBinding.bShift, GGameUserSettingsIni);
		GConfig->SetBool(ConfigSection, ConfigControl, CachedBinding.bControl, GGameUserSettingsIni);
		GConfig->SetBool(ConfigSection, ConfigAlt, CachedBinding.bAlt, GGameUserSettingsIni);
		GConfig->SetBool(ConfigSection, ConfigCommand, CachedBinding.bCommand, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}

	FKey FindRegisteredKey(const FString& KeyText)
	{
		TArray<FKey> Keys;
		EKeys::GetAllKeys(Keys);
		for (const FKey& Candidate : Keys)
		{
			if (Candidate.GetFName().ToString().Equals(KeyText, ESearchCase::IgnoreCase)
				|| Candidate.GetDisplayName(false).ToString().Equals(KeyText, ESearchCase::IgnoreCase))
			{
				return Candidate;
			}
		}
		return FKey();
	}
}

FSFPPlannerHotkeyBinding SFPPlannerHotkey::Get()
{
	SFPPlannerHotkeyPrivate::LoadBinding();
	return SFPPlannerHotkeyPrivate::CachedBinding;
}

FString SFPPlannerHotkey::GetDisplayName()
{
	const FSFPPlannerHotkeyBinding Binding = Get();
	if (!Binding.IsEnabled())
	{
		return FString();
	}

	TArray<FString> Parts;
	if (Binding.bControl)
	{
		Parts.Add(TEXT("Ctrl"));
	}
	if (Binding.bAlt)
	{
		Parts.Add(TEXT("Alt"));
	}
	if (Binding.bShift)
	{
		Parts.Add(TEXT("Shift"));
	}
	if (Binding.bCommand)
	{
		Parts.Add(TEXT("Cmd"));
	}
	Parts.Add(Binding.Key.GetDisplayName(false).ToString());
	return FString::Join(Parts, TEXT("+"));
}

bool SFPPlannerHotkey::Matches(const FKeyEvent& KeyEvent)
{
	const FSFPPlannerHotkeyBinding Binding = Get();
	return Binding.IsEnabled()
		&& KeyEvent.GetKey() == Binding.Key
		&& KeyEvent.IsShiftDown() == Binding.bShift
		&& KeyEvent.IsControlDown() == Binding.bControl
		&& KeyEvent.IsAltDown() == Binding.bAlt
		&& KeyEvent.IsCommandDown() == Binding.bCommand;
}

bool SFPPlannerHotkey::Set(const FSFPPlannerHotkeyBinding& Binding, FString& OutError)
{
	OutError.Reset();
	if (!Binding.Key.IsValid())
	{
		OutError = TEXT("Die gewählte Taste ist ungültig");
		return false;
	}
	if (SFPPlannerHotkeyPrivate::IsModifierKey(Binding.Key))
	{
		OutError = TEXT("Umschalt-, Strg-, Alt- oder Cmd-Taste benötigen eine zusätzliche Taste");
		return false;
	}

	SFPPlannerHotkeyPrivate::CachedBinding = Binding;
	SFPPlannerHotkeyPrivate::bBindingLoaded = true;
	SFPPlannerHotkeyPrivate::SaveBinding();
	UE_LOG(LogSFPFactoryPlanner, Display, TEXT("Planner hotkey changed to %s"), *GetDisplayName());
	return true;
}

bool SFPPlannerHotkey::SetFromString(const FString& ChordText, FString& OutError)
{
	OutError.Reset();
	TArray<FString> Tokens;
	ChordText.ParseIntoArray(Tokens, TEXT("+"), true);
	if (Tokens.IsEmpty())
	{
		OutError = TEXT("Keine Taste angegeben");
		return false;
	}

	FSFPPlannerHotkeyBinding Binding;
	for (FString Token : Tokens)
	{
		Token.TrimStartAndEndInline();
		if (Token.Equals(TEXT("Ctrl"), ESearchCase::IgnoreCase)
			|| Token.Equals(TEXT("Control"), ESearchCase::IgnoreCase)
			|| Token.Equals(TEXT("Strg"), ESearchCase::IgnoreCase))
		{
			Binding.bControl = true;
			continue;
		}
		if (Token.Equals(TEXT("Shift"), ESearchCase::IgnoreCase)
			|| Token.Equals(TEXT("Umschalt"), ESearchCase::IgnoreCase))
		{
			Binding.bShift = true;
			continue;
		}
		if (Token.Equals(TEXT("Alt"), ESearchCase::IgnoreCase))
		{
			Binding.bAlt = true;
			continue;
		}
		if (Token.Equals(TEXT("Cmd"), ESearchCase::IgnoreCase)
			|| Token.Equals(TEXT("Command"), ESearchCase::IgnoreCase))
		{
			Binding.bCommand = true;
			continue;
		}

		if (Binding.Key.IsValid())
		{
			OutError = TEXT("Bitte genau eine Haupttaste angeben");
			return false;
		}
		Binding.Key = SFPPlannerHotkeyPrivate::FindRegisteredKey(Token);
		if (!Binding.Key.IsValid())
		{
			OutError = FString::Printf(TEXT("Unbekannte Taste '%s'"), *Token);
			return false;
		}
	}

	return Set(Binding, OutError);
}

void SFPPlannerHotkey::Disable()
{
	SFPPlannerHotkeyPrivate::CachedBinding = FSFPPlannerHotkeyBinding();
	SFPPlannerHotkeyPrivate::bBindingLoaded = true;
	SFPPlannerHotkeyPrivate::SaveBinding();
	UE_LOG(LogSFPFactoryPlanner, Display, TEXT("Planner hotkey disabled"));
}

void SFPPlannerHotkey::ResetToDefault()
{
	SFPPlannerHotkeyPrivate::CachedBinding = SFPPlannerHotkeyPrivate::MakeDefaultBinding();
	SFPPlannerHotkeyPrivate::bBindingLoaded = true;
	SFPPlannerHotkeyPrivate::SaveBinding();
	UE_LOG(LogSFPFactoryPlanner, Display, TEXT("Planner hotkey reset to F8"));
}

void SFPPlannerHotkey::SetCaptureInProgress(const bool bInProgress)
{
	SFPPlannerHotkeyPrivate::bCaptureInProgress = bInProgress;
}

bool SFPPlannerHotkey::IsCaptureInProgress()
{
	return SFPPlannerHotkeyPrivate::bCaptureInProgress;
}

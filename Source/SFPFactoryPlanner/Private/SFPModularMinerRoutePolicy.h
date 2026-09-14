#pragma once

// Generic module metadata does not establish playable gangue conversion
// (invalid routes reported for dirt and peat). Fail closed for ALL synthetic
// gangue processing routes until their resource/module eligibility can be
// verified. This does not filter actual game recipes or raw extraction.
inline bool SFPIsUnsupportedMinerConversion(const FString& /*ResourcePath*/, const FString& ProductPath)
{
	return ProductPath == TEXT("/KLib/Assets/Parts/Production/Powder/Desc_Gangue.Desc_Gangue_C");
}

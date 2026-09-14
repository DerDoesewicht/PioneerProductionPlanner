#pragma once

// Generic module metadata does not establish playable gangue conversion
// (invalid routes reported for dirt and peat). Fail closed for ALL synthetic
// gangue processing routes until their resource/module eligibility can be
// verified. This does not filter actual game recipes or raw extraction.
inline bool SFPIsUnsupportedMinerConversion(const FString& /*ResourcePath*/, const FString& ProductPath)
{
	return ProductPath == TEXT("/KLib/Assets/Parts/Production/Powder/Desc_Gangue.Desc_Gangue_C");
}

// mNeededModules describes a processed output (for example an ingot requiring
// the Smelter Module). A raw resource output is already satisfied by its drill
// head and must not inherit those processing-module requirements.
inline bool SFPRequiredModulesApplyToMinerOutput(const bool bRawMiningOutput)
{
	return !bRawMiningOutput;
}

// Satisfactory Plus reuses and patches some base-game resource descriptors
// (for example Iron Ore becomes Siderite Ore). Descriptor mount paths therefore
// cannot separate the two miner systems. When a concrete modular raw route was
// discovered for the resource, do not add the three synthetic Vanilla miners
// for the same selector entry. Vanilla remains unchanged without such a route.
inline bool SFPShouldAddStandardMinerRoute(
	const bool bVanillaStandardMiner,
	const bool bHasModularRawRoute)
{
	return !bVanillaStandardMiner || !bHasModularRawRoute;
}

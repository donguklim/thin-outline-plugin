// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThinOutlineSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ThinOutlineSettings)

UThinOutlineSettings::UThinOutlineSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("ThinOutline");

	// Keep in sync with the console variable defaults in ThinOutlineCVars.cpp.
	bEnable = true;
	OutlineColor = FLinearColor::Black;
	SilhouetteThreshold = 0.01f;
	SilhouetteScale = 50.0f;
	CreaseRidgeThreshold = 0.25f;
	CreaseValleyThreshold = 0.25f;
	CreaseScale = 4.0f;
	CreaseThickness = 1.0f;
	EstimatorDecay = 0.04f;
	CoTriggerThreshold = 0.05f;
	SlopeStandardErrorThreshold = 0.05f;
	HistoryDepthThreshold = 0.02f;
}

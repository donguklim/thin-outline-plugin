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
	bDrawSilhouettes = true;
	SilhouetteColor = FLinearColor::Black;
	SilhouetteThreshold = 0.01f;
	SilhouetteScale = 50.0f;
	bSilhouetteSymmetricMeasure = true;
	SilhouetteThickness = 1.0f;
	SilhouetteHistoryBackgroundTest = 0;
	SilhouetteHistoryViewAngle = 0.0f;
	bSilhouetteHistorySurfaceTurn = true;
	SilhouetteHistoryDepthThreshold = 0.05f;
	SilhouetteCreaseTakeoverSampleCount = 0.5f;
	bDrawCreases = true;
	CreaseColor = FLinearColor::Black;
	CreaseRidgeThreshold = 0.25f;
	CreaseValleyThreshold = 0.25f;
	CreaseScale = 4.0f;
	CreaseThickness = 1.0f;
	CreaseHistoryDepthThreshold = 0.05f;
	bCreaseHistoryCreaseTest = true;
	CreaseHistoryCreaseTestThreshold = 0.5f;
	CreaseHistoryFramesWithoutCrease = 3;
	EstimatorDecay = 0.04f;
	CoTriggerThreshold = 0.05f;
	SlopeStandardErrorThreshold = 0.05f;
	HistoryReprojection = 3;
}

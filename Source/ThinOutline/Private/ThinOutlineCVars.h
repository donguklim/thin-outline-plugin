// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/IConsoleManager.h"

extern TAutoConsoleVariable<int32> CVarThinOutlineEnable;

// Draw silhouette / crease outlines (the records are kept either way)
extern TAutoConsoleVariable<int32> CVarThinOutlineSilhouette;
extern TAutoConsoleVariable<int32> CVarThinOutlineCrease;

// 0 = draw before the temporal upscaler at rendering resolution, 1 = after it at display resolution
extern TAutoConsoleVariable<int32> CVarThinOutlineDrawAfterUpscaler;

// 1 = the composite runs only on the tiles that have edge records within reach
extern TAutoConsoleVariable<int32> CVarThinOutlineTileClassification;

// 0 = composite the outline into scene color, 1..5 = debug views (see the CVar help)
extern TAutoConsoleVariable<int32> CVarThinOutlineDebugView;

// Silhouette (depth discontinuity)
extern TAutoConsoleVariable<float> CVarThinOutlineSilhouetteThreshold;
extern TAutoConsoleVariable<float> CVarThinOutlineSilhouetteScale;
extern TAutoConsoleVariable<int32> CVarThinOutlineSilhouetteSymmetricMeasure;
extern TAutoConsoleVariable<float> CVarThinOutlineSilhouetteThickness;
extern TAutoConsoleVariable<float> CVarThinOutlineSilhouetteHistoryViewAngle;
extern TAutoConsoleVariable<int32> CVarThinOutlineSilhouetteHistorySurfaceTurn;
extern TAutoConsoleVariable<float> CVarThinOutlineSilhouetteHistoryDepthThreshold;
extern TAutoConsoleVariable<int32> CVarThinOutlineSilhouetteHistoryBackgroundTest;
extern TAutoConsoleVariable<float> CVarThinOutlineSilhouetteCreaseTakeoverSampleCount;

// Crease (normal discontinuity)
extern TAutoConsoleVariable<float> CVarThinOutlineCreaseRidgeThreshold;
extern TAutoConsoleVariable<float> CVarThinOutlineCreaseValleyThreshold;
extern TAutoConsoleVariable<float> CVarThinOutlineCreaseScale;
extern TAutoConsoleVariable<float> CVarThinOutlineCreaseThickness;
extern TAutoConsoleVariable<float> CVarThinOutlineCreaseHistoryDepthThreshold;
extern TAutoConsoleVariable<int32> CVarThinOutlineCreaseHistoryCreaseTest;
extern TAutoConsoleVariable<float> CVarThinOutlineCreaseHistoryCreaseTestThreshold;
extern TAutoConsoleVariable<int32> CVarThinOutlineCreaseHistoryFramesWithoutCrease;
extern TAutoConsoleVariable<int32> CVarThinOutlineSpatialFilter;
extern TAutoConsoleVariable<float> CVarThinOutlineSpatialFilterSigma;
extern TAutoConsoleVariable<int32> CVarThinOutlineAxisBlend;
extern TAutoConsoleVariable<float> CVarThinOutlineAxisBlendScale;

// Temporal edge estimator (per-pixel OLS edge records)
extern TAutoConsoleVariable<float> CVarThinOutlineEstimatorDecay;
extern TAutoConsoleVariable<int32> CVarThinOutlineEstimatorFadeFrames;
extern TAutoConsoleVariable<float> CVarThinOutlineEstimatorFadeMaxSpeed;
extern TAutoConsoleVariable<float> CVarThinOutlineEstimatorSlopeSEThreshold;
extern TAutoConsoleVariable<int32> CVarThinOutlineEstimatorHistoryReprojection;

// Debugging
extern TAutoConsoleVariable<float> CVarThinOutlineDebugCameraPan;
extern TAutoConsoleVariable<float> CVarThinOutlineDebugCameraOrbit;
extern TAutoConsoleVariable<float> CVarThinOutlineDebugCameraOrbitDistance;
extern TAutoConsoleVariable<float> CVarThinOutlineDebugPawnSpin;

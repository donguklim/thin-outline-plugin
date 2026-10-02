// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/IConsoleManager.h"

extern TAutoConsoleVariable<int32> CVarThinOutlineEnable;

// 0 = composite the outline into scene color, 1..5 = debug views (see the CVar help)
extern TAutoConsoleVariable<int32> CVarThinOutlineDebugView;

// Silhouette (depth discontinuity)
extern TAutoConsoleVariable<float> CVarThinOutlineSilhouetteThreshold;
extern TAutoConsoleVariable<float> CVarThinOutlineSilhouetteScale;
extern TAutoConsoleVariable<float> CVarThinOutlineSilhouetteThickness;
extern TAutoConsoleVariable<float> CVarThinOutlineSilhouetteHistoryViewAngle;

// Crease (normal discontinuity)
extern TAutoConsoleVariable<float> CVarThinOutlineCreaseRidgeThreshold;
extern TAutoConsoleVariable<float> CVarThinOutlineCreaseValleyThreshold;
extern TAutoConsoleVariable<float> CVarThinOutlineCreaseScale;
extern TAutoConsoleVariable<float> CVarThinOutlineCreaseThickness;

// Temporal edge estimator (per-pixel OLS edge records)
extern TAutoConsoleVariable<float> CVarThinOutlineEstimatorDecay;
extern TAutoConsoleVariable<float> CVarThinOutlineEstimatorCoTriggerThreshold;
extern TAutoConsoleVariable<float> CVarThinOutlineEstimatorSlopeSEThreshold;
extern TAutoConsoleVariable<float> CVarThinOutlineEstimatorHistoryDepthThreshold;

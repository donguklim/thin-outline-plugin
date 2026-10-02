// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/IConsoleManager.h"

extern TAutoConsoleVariable<int32> CVarThinOutlineEnable;

// 0 = composite outline into scene color, 1 = show edge strengths (R = silhouette, G = ridge, B = valley)
extern TAutoConsoleVariable<int32> CVarThinOutlineDebugView;

// Silhouette (depth discontinuity)
extern TAutoConsoleVariable<float> CVarThinOutlineSilhouetteThreshold;
extern TAutoConsoleVariable<float> CVarThinOutlineSilhouetteScale;

// Crease (normal discontinuity)
extern TAutoConsoleVariable<float> CVarThinOutlineCreaseRidgeThreshold;
extern TAutoConsoleVariable<float> CVarThinOutlineCreaseValleyThreshold;
extern TAutoConsoleVariable<float> CVarThinOutlineCreaseScale;

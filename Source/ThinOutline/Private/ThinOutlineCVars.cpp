// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThinOutlineCVars.h"

TAutoConsoleVariable<int32> CVarThinOutlineEnable(
	TEXT("r.ThinOutline.Enable"),
	1,
	TEXT("Enable G-buffer outlines: 0/1\n"),
	ECVF_Default
);

TAutoConsoleVariable<int32> CVarThinOutlineDebugView(
	TEXT("r.ThinOutline.DebugView"),
	0,
	TEXT("0 = composite outlines into scene color\n")
	TEXT("1 = show edge strengths instead of scene color (R = silhouette, G = crease ridge, B = crease valley)\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineSilhouetteThreshold(
	TEXT("r.ThinOutline.Silhouette.Threshold"),
	0.01f,
	TEXT("Silhouette edge threshold. The edge measure is min(|second difference|, |first difference|) of linear depth,\n")
	TEXT("divided by the target pixel's linear depth. Values at or below the threshold produce no silhouette.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineSilhouetteScale(
	TEXT("r.ThinOutline.Silhouette.Scale"),
	50.0f,
	TEXT("Silhouette strength = saturate((Measure - Threshold) * Scale).\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineCreaseRidgeThreshold(
	TEXT("r.ThinOutline.Crease.RidgeThreshold"),
	0.25f,
	TEXT("Crease threshold for convex edges (ridges). The edge measure is min(|cross(Nc, Nn) - cross(No, Nc)|, |cross(Nc, Nn)|)\n")
	TEXT("of world normals, so for a sharp edge it is the sine of the angle between the two normals.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineCreaseValleyThreshold(
	TEXT("r.ThinOutline.Crease.ValleyThreshold"),
	0.25f,
	TEXT("Crease threshold for concave edges (valleys). Same measure as r.ThinOutline.Crease.RidgeThreshold.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineCreaseScale(
	TEXT("r.ThinOutline.Crease.Scale"),
	4.0f,
	TEXT("Crease strength = saturate((Measure - Threshold) * Scale), with the ridge or valley threshold.\n"),
	ECVF_Default
);

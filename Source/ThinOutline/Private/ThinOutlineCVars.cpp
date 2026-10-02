// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThinOutlineCVars.h"

TAutoConsoleVariable<int32> CVarThinOutlineEnable(
	TEXT("r.ThinOutline.Enable"),
	1,
	TEXT("Enable silhouette and crease outlines reconstructed from G-buffer edge checks: 0/1\n"),
	ECVF_Default
);

TAutoConsoleVariable<int32> CVarThinOutlineDebugView(
	TEXT("r.ThinOutline.DebugView"),
	0,
	TEXT("0 = composite the outline into scene color\n")
	TEXT("1 = this frame's edge check strengths (R = silhouette, G = crease ridge, B = crease valley)\n")
	TEXT("2 = crease records (R = horizontal-inducer sample count, G = vertical-inducer sample count, relative to a record\n")
	TEXT("    that gets a sample every frame; B = co-trigger ratio)\n")
	TEXT("3 = outline alpha of the reconstructed edge (R = horizontal-inducer edge, G = vertical-inducer edge,\n")
	TEXT("    B = the edge is a silhouette)\n")
	TEXT("4 = silhouette records (R, G = sample counts as in 2; B = fraction of the samples from the left (top) check of\n")
	TEXT("    the axis with more samples: 1 = foreground on the left (top), 0 = on the right (bottom))\n")
	TEXT("5 = R = depth and velocity overwritten with the foreground's, G = silhouette outline alpha\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineSilhouetteThreshold(
	TEXT("r.ThinOutline.Silhouette.Threshold"),
	0.01f,
	TEXT("Silhouette edge threshold. The edge measure is min(|second difference|, |first difference|) of linear depth,\n")
	TEXT("divided by the target pixel's linear depth. Values at or below the threshold produce no silhouette.\n")
	TEXT("A pixel with a silhouette is not a crease candidate.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineSilhouetteScale(
	TEXT("r.ThinOutline.Silhouette.Scale"),
	50.0f,
	TEXT("Silhouette strength = saturate((Measure - Threshold) * Scale).\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineSilhouetteThickness(
	TEXT("r.ThinOutline.Silhouette.Thickness"),
	1.0f,
	TEXT("Silhouette outline thickness in display pixels (pixels after the temporal upscaler). The outline lies on the\n")
	TEXT("background side of the silhouette only. It is reconstructed from the two pixels next to the edge, so it does not\n")
	TEXT("reach further than about one rendering pixel beyond the edge.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineSilhouetteHistoryViewAngle(
	TEXT("r.ThinOutline.Silhouette.HistoryViewAngle"),
	5.0f,
	TEXT("Silhouette records are rejected when the direction their foreground is seen from turns by more than this many\n")
	TEXT("degrees in one frame, since a silhouette slides over the surface as the view direction changes.\n"),
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

TAutoConsoleVariable<float> CVarThinOutlineCreaseThickness(
	TEXT("r.ThinOutline.Crease.Thickness"),
	1.0f,
	TEXT("Crease outline thickness in display pixels (pixels after the temporal upscaler).\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineEstimatorDecay(
	TEXT("r.ThinOutline.Estimator.Decay"),
	0.04f,
	TEXT("Decay rate d of the edge records' running statistics, in (0, 1]. Samples are weighted by (1 - d)^age in frames,\n")
	TEXT("so a record that gets a sample every frame holds about 1 / d samples. Lower is steadier, higher follows changes faster.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineEstimatorCoTriggerThreshold(
	TEXT("r.ThinOutline.Estimator.CoTriggerThreshold"),
	0.05f,
	TEXT("Maximum fraction of a record's samples taken on frames where both checks of its axis fired (more than one edge\n")
	TEXT("across the pixel). Records above it are left out of the pooled fit of the pixel and its neighbours.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineEstimatorSlopeSEThreshold(
	TEXT("r.ThinOutline.Estimator.SlopeSEThreshold"),
	0.05f,
	TEXT("When a pixel has both a horizontal- and a vertical-inducer edge, the one with the smaller slope standard error\n")
	TEXT("is drawn if the two differ by more than this. Otherwise the one with the smaller absolute slope is drawn.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineEstimatorHistoryDepthThreshold(
	TEXT("r.ThinOutline.Estimator.HistoryDepthThreshold"),
	0.02f,
	TEXT("Relative depth tolerance for keeping a reprojected edge record. A crease record is kept if its depth lies within the\n")
	TEXT("depth range of the pixel's surface neighbourhood, widened by this fraction of the depth. A silhouette record is kept\n")
	TEXT("if its foreground depth is within this fraction of the current foreground depth.\n"),
	ECVF_Default
);

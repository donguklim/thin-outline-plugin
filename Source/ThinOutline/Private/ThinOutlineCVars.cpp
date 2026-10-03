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
	TEXT("Silhouette edge threshold. The edge measure is the distance of the neighbour's world position from the line through\n")
	TEXT("the opposite neighbour's and the target pixel's (see also r.ThinOutline.Silhouette.SymmetricMeasure), divided by the\n")
	TEXT("target pixel's linear depth. Values at or below the threshold produce no silhouette, and count as one surface for\n")
	TEXT("creases and the history depth tests.\n")
	TEXT("A pixel with a silhouette is not a crease candidate.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineSilhouetteScale(
	TEXT("r.ThinOutline.Silhouette.Scale"),
	50.0f,
	TEXT("Silhouette strength = saturate((Measure - Threshold) * Scale).\n"),
	ECVF_Default
);

TAutoConsoleVariable<int32> CVarThinOutlineSilhouetteSymmetricMeasure(
	TEXT("r.ThinOutline.Silhouette.SymmetricMeasure"),
	1,
	TEXT("1 = the silhouette edge measure between a target pixel C and its neighbour N is the smaller of the distance of N\n")
	TEXT("from the line through the opposite neighbour O and C, and the distance of C from the line through F (the pixel beyond\n")
	TEXT("N) and N. The second term keeps junctions continuous where N's surface meets C's (a wall standing on a floor seen at\n")
	TEXT("a grazing angle), so they stay creases. 0 = only the first term.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineSilhouetteHistoryDepthThreshold(
	TEXT("r.ThinOutline.Silhouette.HistoryDepthThreshold"),
	0.05f,
	TEXT("Relative depth tolerance for keeping a reprojected silhouette record: its foreground depth (the mean depth of the\n")
	TEXT("foreground neighbours of its samples) must match the depth of the pixel itself, or of one of the two pixels toward\n")
	TEXT("the record's foreground side, within this fraction of the depth.\n"),
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
	1.0f,
	TEXT("Silhouette records are rejected when the view direction turns out of the plane through the camera and the edge by\n")
	TEXT("more than this many degrees in one frame. At a smooth contour that plane is the surface's tangent plane: turning\n")
	TEXT("within it moves the contour along itself, turning out of it slides the contour over the surface.\n"),
	ECVF_Default
);

TAutoConsoleVariable<int32> CVarThinOutlineSilhouetteHistorySurfaceTurn(
	TEXT("r.ThinOutline.Silhouette.HistorySurfaceTurn"),
	1,
	TEXT("1 = the view angle of r.ThinOutline.Silhouette.HistoryViewAngle is measured relative to the surface inside the\n")
	TEXT("contour, whose turn since the previous frame is tracked with the velocities of two of its pixels, so silhouettes of\n")
	TEXT("objects turning in front of the camera are rejected too. 0 = the view direction's own turn only (cheaper: about\n")
	TEXT("0.015 ms at 1280x720 on an RTX 5080, a separate shader permutation).\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineSilhouetteCreaseTakeoverSampleCount(
	TEXT("r.ThinOutline.Silhouette.CreaseTakeoverSampleCount"),
	0.5f,
	TEXT("A pixel keeps one edge record per axis, of either type. A crease sample takes over the axis's silhouette record\n")
	TEXT("only when that record's decayed sample count is below this; otherwise the crease sample is ignored. This keeps the\n")
	TEXT("record of a silhouette's edge pixel, whose sample alternates between the background and a foreground with creases.\n")
	TEXT("Below 1, so that a silhouette that just got its first sample survives the next frame's crease sample.\n")
	TEXT("A silhouette sample always takes over a crease record.\n"),
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

TAutoConsoleVariable<float> CVarThinOutlineCreaseHistoryDepthThreshold(
	TEXT("r.ThinOutline.Crease.HistoryDepthThreshold"),
	0.05f,
	TEXT("Relative depth tolerance for keeping a reprojected crease record: its depth must be within this fraction of the\n")
	TEXT("pixel's depth, moved to the previous frame. The jitter moves the samples by up to a pixel, so it has to cover a\n")
	TEXT("pixel's depth step on surfaces seen at a grazing angle, which grows as the screen percentage drops.\n"),
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

TAutoConsoleVariable<int32> CVarThinOutlineEstimatorHistoryReprojection(
	TEXT("r.ThinOutline.Estimator.HistoryReprojection"),
	3,
	TEXT("How the edge records of the previous frame are fetched at the reprojected position:\n")
	TEXT("0 = nearest history pixel. A record moves by whole pixels while its edge moves by fractions, so under motion\n")
	TEXT("    records slip past the edge and linger in pixels that can no longer sample it\n")
	TEXT("1 = nearest, and records whose edge has left the pixel and its two neighbours along the axis are dropped\n")
	TEXT("2 = bilinear: the four history pixels around the position, each moved into this pixel's coordinates, merged with\n")
	TEXT("    bilinear weights, so records follow the edge continuously\n")
	TEXT("3 = bilinear, and records outside the sampling range are dropped\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineDebugCameraPan(
	TEXT("r.ThinOutline.Debug.CameraPan"),
	0.0f,
	TEXT("Debugging: moves game cameras to the right by this many more cm every frame (0 = off), to test the outline under\n")
	TEXT("steady camera motion where nothing drives the camera, e.g. offscreen captures.\n"),
	ECVF_Cheat
);

TAutoConsoleVariable<float> CVarThinOutlineDebugCameraOrbit(
	TEXT("r.ThinOutline.Debug.CameraOrbit"),
	0.0f,
	TEXT("Debugging: turns game cameras around a vertical axis through a point in front of them by this many more degrees\n")
	TEXT("every frame (0 = off), to test silhouettes of smooth objects under a steady orbit, e.g. offscreen captures.\n"),
	ECVF_Cheat
);

TAutoConsoleVariable<float> CVarThinOutlineDebugCameraOrbitDistance(
	TEXT("r.ThinOutline.Debug.CameraOrbitDistance"),
	400.0f,
	TEXT("Debugging: distance in cm in front of the camera of the axis r.ThinOutline.Debug.CameraOrbit turns around.\n"),
	ECVF_Cheat
);

TAutoConsoleVariable<float> CVarThinOutlineDebugPawnSpin(
	TEXT("r.ThinOutline.Debug.PawnSpin"),
	0.0f,
	TEXT("Debugging: turns the first player's pawn in place around the vertical axis by this many more degrees every frame\n")
	TEXT("(0 = off), to test silhouettes of an object turning in front of a still camera, e.g. offscreen captures.\n"),
	ECVF_Cheat
);

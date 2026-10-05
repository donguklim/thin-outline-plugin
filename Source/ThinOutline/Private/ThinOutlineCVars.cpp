// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThinOutlineCVars.h"

TAutoConsoleVariable<int32> CVarThinOutlineEnable(
	TEXT("r.ThinOutline.Enable"),
	1,
	TEXT("Enable silhouette and crease outlines reconstructed from G-buffer edge checks: 0/1\n"),
	ECVF_Default
);

TAutoConsoleVariable<int32> CVarThinOutlineSilhouette(
	TEXT("r.ThinOutline.Silhouette"),
	1,
	TEXT("Draw silhouette outlines: 0/1. Silhouette records are still kept, so turning it back on shows them at once.\n")
	TEXT("With 0, no pixel gets the foreground's depth and velocity. Debug views 3 and 5 follow it.\n"),
	ECVF_Default
);

TAutoConsoleVariable<int32> CVarThinOutlineCrease(
	TEXT("r.ThinOutline.Crease"),
	1,
	TEXT("Draw crease outlines: 0/1. Crease records are still kept, so turning it back on shows them at once.\n")
	TEXT("Debug views 3 and 5 follow it.\n"),
	ECVF_Default
);

TAutoConsoleVariable<int32> CVarThinOutlineDrawAfterUpscaler(
	TEXT("r.ThinOutline.DrawAfterUpscaler"),
	0,
	TEXT("Where the outline is drawn (the edge records are kept at rendering resolution before the upscaler either way):\n")
	TEXT("0 = into scene color at rendering resolution, before the temporal upscaler (TSR, TAA, third party). Pixels painted with\n")
	TEXT("    a silhouette get the foreground's depth and velocity, so the upscaler moves the outline with the foreground.\n")
	TEXT("1 = into scene color at display resolution, after the temporal upscaler, depth of field, motion blur and translucency,\n")
	TEXT("    before bloom and tonemapping. Each display pixel gets the exact coverage of the outline band, so the outline does\n")
	TEXT("    not go through the upscaler's history. Depth and velocity are left untouched. Debug views are drawn there too.\n"),
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
	TEXT("4 = silhouette records, kept by the foreground pixels (R, G = sample counts as in 2; B = fraction of the samples\n")
	TEXT("    from the left (top) check of the axis with more samples: 1 = background on the left (top), 0 = on the right\n")
	TEXT("    (bottom))\n")
	TEXT("5 = R = depth and velocity overwritten with the foreground's, G = silhouette outline alpha\n")
	TEXT("With r.ThinOutline.DrawAfterUpscaler 1 they are drawn at display resolution: 1, 2 and 4 show the rendering pixel under\n")
	TEXT("each display pixel (not blurred by the upscaler), 3 and 5 the display pixel's alpha, and 5's R stays 0.\n"),
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
	TEXT("foreground pixel when it recorded its samples) must match the depth of the pixel itself, or of its neighbour toward\n")
	TEXT("the record's foreground side, within this fraction of the depth. Also the relative depth step of\n")
	TEXT("r.ThinOutline.Silhouette.HistoryBackgroundTest 1.\n"),
	ECVF_Default
);

TAutoConsoleVariable<int32> CVarThinOutlineSilhouetteHistoryBackgroundTest(
	TEXT("r.ThinOutline.Silhouette.HistoryBackgroundTest"),
	0,
	TEXT("Silhouette records are kept by the foreground pixels of the contour, and a reprojected record is rejected when no\n")
	TEXT("background is found within two pixels toward its background side. How the pixel two steps away is tested (the\n")
	TEXT("nearer ones use this frame's edge checks):\n")
	TEXT("0 = the silhouette measure seen from the pixel between (without its symmetric term)\n")
	TEXT("1 = a relative depth step of r.ThinOutline.Silhouette.HistoryDepthThreshold (cheaper)\n")
	TEXT("Separate shader permutations.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineSilhouetteThickness(
	TEXT("r.ThinOutline.Silhouette.Thickness"),
	1.0f,
	TEXT("Silhouette outline thickness in display pixels (pixels after the temporal upscaler). The outline lies on the\n")
	TEXT("background side of the silhouette only. It is drawn by the two pixels next to the edge on that side, so it does\n")
	TEXT("not reach further than about one rendering pixel beyond the edge.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineSilhouetteHistoryViewAngle(
	TEXT("r.ThinOutline.Silhouette.HistoryViewAngle"),
	0.0f,
	TEXT("Testing: silhouette records are rejected when the view direction turns out of the plane through the camera and the\n")
	TEXT("edge by more than this many degrees in one frame (0 = off, a separate shader permutation). At a smooth contour that\n")
	TEXT("plane is the surface's tangent plane: turning within it moves the contour along itself, turning out of it slides the\n")
	TEXT("contour over the surface. The background test (r.ThinOutline.Silhouette.HistoryBackgroundTest) replaces it.\n"),
	ECVF_Default
);

TAutoConsoleVariable<int32> CVarThinOutlineSilhouetteHistorySurfaceTurn(
	TEXT("r.ThinOutline.Silhouette.HistorySurfaceTurn"),
	1,
	TEXT("Testing, with r.ThinOutline.Silhouette.HistoryViewAngle > 0: 1 = the view angle is measured relative to the surface\n")
	TEXT("inside the contour, whose turn since the previous frame is tracked with the velocities of two of its pixels, so\n")
	TEXT("silhouettes of objects turning in front of the camera are rejected too. 0 = the view direction's own turn only\n")
	TEXT("(a separate shader permutation).\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineSilhouetteCreaseTakeoverSampleCount(
	TEXT("r.ThinOutline.Silhouette.CreaseTakeoverSampleCount"),
	0.5f,
	TEXT("A pixel keeps one edge record per axis, of either type. A crease sample takes over the axis's silhouette record\n")
	TEXT("only when that record's decayed sample count is below this; otherwise the crease sample is ignored. This keeps the\n")
	TEXT("record of a silhouette's edge pixel, whose sample alternates between a foreground with creases and the background.\n")
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

TAutoConsoleVariable<int32> CVarThinOutlineCreaseHistoryCreaseTest(
	TEXT("r.ThinOutline.Crease.HistoryCreaseTest"),
	1,
	TEXT("1 = a crease record is dropped after r.ThinOutline.Crease.HistoryFramesWithoutCrease frames without a crease found\n")
	TEXT("at the pixel along its axis (the crease measure above the keep level of\n")
	TEXT("r.ThinOutline.Crease.HistoryCreaseTestThreshold).\n")
	TEXT("The depth test alone keeps records that slid along one depth-continuous surface (a foot's crease left on the floor\n")
	TEXT("it stood on, a junction's crease carried onto a curved wall, copies spread over a face widening on screen).\n")
	TEXT("0 = depth test only. Separate shader permutations.\n"),
	ECVF_Default
);

TAutoConsoleVariable<int32> CVarThinOutlineCreaseHistoryFramesWithoutCrease(
	TEXT("r.ThinOutline.Crease.HistoryFramesWithoutCrease"),
	3,
	TEXT("Frames without a crease found at a crease record's pixel after which r.ThinOutline.Crease.HistoryCreaseTest drops the\n")
	TEXT("record; 0 = a whole jitter cycle (the temporal upscaler's jitter sequence length). Short, because a face that widens\n")
	TEXT("on screen (turning toward the camera) receives copies of the crease records next to it, each claiming the crease\n")
	TEXT("runs through its own pixel, and they are drawn until dropped. A crease thinner than a pixel is only found on some\n")
	TEXT("frames: one found on fewer than about half of them loses its records more often with a short limit.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineCreaseHistoryCreaseTestThreshold(
	TEXT("r.ThinOutline.Crease.HistoryCreaseTestThreshold"),
	0.5f,
	TEXT("Keep level of r.ThinOutline.Crease.HistoryCreaseTest, as a fraction of r.ThinOutline.Crease.RidgeThreshold and\n")
	TEXT("ValleyThreshold. Below 1, so that a crease that fires only on some frames (near the threshold) keeps its record.\n"),
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
	1.0f,
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
	TEXT("1 = nearest, and crease records whose edge has left the pixel and its two neighbours along the axis are dropped\n")
	TEXT("2 = bilinear: the four history pixels around the position, each moved into this pixel's coordinates, merged with\n")
	TEXT("    bilinear weights, so records follow the edge continuously\n")
	TEXT("3 = bilinear, and crease records outside the sampling range are dropped\n")
	TEXT("Silhouette records are never range dropped: they are rejected when their background is out of reach.\n"),
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

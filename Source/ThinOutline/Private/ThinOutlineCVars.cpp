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
	1,
	TEXT("Where the outline is drawn (the edge records are kept at rendering resolution before the upscaler either way):\n")
	TEXT("0 = into scene color at rendering resolution, before the temporal upscaler (TSR, TAA, third party). Pixels painted with\n")
	TEXT("    a silhouette get the foreground's depth and velocity, so the upscaler moves the outline with the foreground.\n")
	TEXT("1 = into scene color at display resolution, after the temporal upscaler, depth of field, motion blur and translucency,\n")
	TEXT("    before bloom and tonemapping. Each display pixel gets the exact coverage of the outline band, so the outline does\n")
	TEXT("    not go through the upscaler's history. Depth and velocity are left untouched. Debug views are drawn there too.\n"),
	ECVF_Default
);

TAutoConsoleVariable<int32> CVarThinOutlineTileClassification(
	TEXT("r.ThinOutline.TileClassification"),
	1,
	TEXT("Run the composite only where there are edges: 0/1. The screen is cut into tiles of 8x8 pixels, sorted by the edge\n")
	TEXT("records within their reach (none, crease records only, silhouette records only, both), and only the non-empty tiles\n")
	TEXT("are drawn, by one indirect dispatch in place in the scene color; before the upscaler the depth and velocity overwrite\n")
	TEXT("draws those tiles only too. 0 draws every pixel as before. Needs a scene color with typed UAV loads, else every pixel\n")
	TEXT("is drawn. Debug views 1, 2 and 4 always draw every pixel; 6 shows the tiles and the record types found.\n"),
	ECVF_Default
);

TAutoConsoleVariable<int32> CVarThinOutlineDebugView(
	TEXT("r.ThinOutline.DebugView"),
	0,
	TEXT("0 = composite the outline into scene color\n")
	TEXT("1 = this frame's edge check strengths (R = silhouette, G = crease ridge, B = crease valley)\n")
	TEXT("2 = crease records (R = horizontal-inducer sample count, G = vertical-inducer sample count, relative to a record\n")
	TEXT("    that gets a sample every frame; B = the larger presence)\n")
	TEXT("3 = outline alpha of the reconstructed edge (R = horizontal-inducer edge, G = vertical-inducer edge,\n")
	TEXT("    B = the edge is a silhouette)\n")
	TEXT("4 = silhouette records, kept by the foreground pixels (R, G = sample counts as in 2; B = fraction of the samples\n")
	TEXT("    from the left (top) check of the axis with more samples: 1 = background on the left (top), 0 = on the right\n")
	TEXT("    (bottom))\n")
	TEXT("5 = R = depth and velocity overwritten with the foreground's, G = silhouette outline alpha\n")
	TEXT("6 = the tiles of r.ThinOutline.TileClassification, tinted by class: crease records only green, silhouette records\n")
	TEXT("    only red, both yellow; tiles without records keep the scene color (after the upscaler only)\n")
	TEXT("7 = the suppressions: R = outline alpha removed by r.ThinOutline.DenseEdgeSuppression, G = outline alpha drawn,\n")
	TEXT("    B = outline alpha removed by r.ThinOutline.IsolatedEdgeSuppression\n")
	TEXT("8 = (was the alpha stabilization, removed on 2026-10-11)\n")
	TEXT("9 = r.ThinOutline.Presence: R, G = the presence of the horizontal and vertical records (either type); B = a check of\n")
	TEXT("    the pixel was skipped this frame as a normal spike or a thin foreground (r.ThinOutline.SpikeFilter)\n")
	TEXT("With r.ThinOutline.DrawAfterUpscaler 1 they are drawn at display resolution: 1, 2, 4 and 9 show the rendering pixel\n")
	TEXT("under each display pixel (not blurred by the upscaler), 3, 5 and 7 the display pixel's alpha, and 5's R stays 0.\n"),
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
	TEXT("A silhouette sample takes over a crease record as the record's presence sinks (r.ThinOutline.Presence: a silhouette\n")
	TEXT("sample is a frame on which the crease is not found), or at once on a moving pixel.\n"),
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

TAutoConsoleVariable<int32> CVarThinOutlinePresence(
	TEXT("r.ThinOutline.Presence"),
	1,
	TEXT("1 = each edge record (crease or silhouette) carries its presence, the fraction of recent frames on which its edge\n")
	TEXT("was found at its pixel along its axis (the crease or depth measure above the keep level of\n")
	TEXT("r.ThinOutline.Presence.KeepLevel) and its history tests passed, as an exponential average over about\n")
	TEXT("r.ThinOutline.Presence.Frames frames. The drawn strength is scaled by a ramp on it (0 at .DrawMin, 1 at .DrawMax)\n")
	TEXT("and the record is dropped below .DropLevel. So an edge found on a few frames only (a groove wall or a wire thinner\n")
	TEXT("than a pixel, hit by the jittered sample now and then: the pops of a distant surface) stays faint or invisible, a\n")
	TEXT("junction found every other frame draws steadily at part strength, a new edge fades in over a few frames, a record\n")
	TEXT("that fails its history tests (a moved surface, a contour that left) fades out, and one that passes them but no\n")
	TEXT("longer describes an edge at its pixel (footprints, records slid along a surface, copies on a face widening on\n")
	TEXT("screen) fades out and is dropped. It replaced the crease miss limit (2026-10-10) and the fade counts of both types\n")
	TEXT("(bad frames in a row, dropped at a hard limit; 2026-10-11). The presence is always kept; 0 = it is not used: the\n")
	TEXT("strength as recorded, records dropped only by their history tests on moving pixels and by sample decay.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlinePresenceFrames(
	TEXT("r.ThinOutline.Presence.Frames"),
	4.0f,
	TEXT("Memory of the presence average of r.ThinOutline.Presence, in frames (the presence moves by 1 / this toward 1 or 0\n")
	TEXT("every frame). With 4: edges found on 1 frame in 4 or fewer stay below the draw ramp, a new edge fades in over about\n")
	TEXT("9 frames (to 90%, with the ramp ending at 1), a lost one fades out and is dropped after about 12. Larger is\n")
	TEXT("steadier and slower.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlinePresenceDrawMin(
	TEXT("r.ThinOutline.Presence.DrawMin"),
	0.3f,
	TEXT("Presence at and below which an edge is not drawn (r.ThinOutline.Presence); the strength ramps up to\n")
	TEXT("r.ThinOutline.Presence.DrawMax.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlinePresenceDrawMax(
	TEXT("r.ThinOutline.Presence.DrawMax"),
	1.0f,
	TEXT("Presence at and above which an edge is drawn at its recorded strength (r.ThinOutline.Presence). 1 (the author's\n")
	TEXT("choice, 2026-10-11; 0.6 before): only an edge found on every recent frame draws at full strength, one found on half\n")
	TEXT("of them at about 0.3, and a new edge fades in over about 9 frames (to 90%).\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlinePresenceDropLevel(
	TEXT("r.ThinOutline.Presence.DropLevel"),
	0.05f,
	TEXT("Presence below which a record is dropped (r.ThinOutline.Presence).\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlinePresenceKeepLevel(
	TEXT("r.ThinOutline.Presence.KeepLevel"),
	0.5f,
	TEXT("Keep level at which an edge counts as found at a pixel for r.ThinOutline.Presence, as a fraction of\n")
	TEXT("r.ThinOutline.Crease.RidgeThreshold and ValleyThreshold (creases) and of r.ThinOutline.Silhouette.Threshold\n")
	TEXT("(silhouettes, the depth step seen from either side). Below 1, so that an edge that fires only on some frames (near\n")
	TEXT("the threshold) keeps its presence.\n"),
	ECVF_Default
);

TAutoConsoleVariable<int32> CVarThinOutlineSpikeFilter(
	TEXT("r.ThinOutline.SpikeFilter"),
	1,
	TEXT("1 = the crease checks skip one-pixel spikes of the normal field (a pixel whose normal differs from both of its\n")
	TEXT("neighbours along an axis while those two are one surface, or a neighbour that differs from the pixel while the pixel\n")
	TEXT("beyond it and the pixel are one surface), and the silhouette checks skip one-pixel foregrounds (a pixel nearer than\n")
	TEXT("both of its neighbours along an axis while those two are one surface). One surface: normals agreeing within\n")
	TEXT("r.ThinOutline.SpikeThreshold and lying on one plane within .SpikePlaneTolerance. Such spikes are groove walls, wires\n")
	TEXT("and poles thinner than a pixel, hit by the jittered sample on some frames, which pop at a distance and fed records\n")
	TEXT("that blinked or stayed as dots. A real ridge or a real sliver one pixel wide is dropped with them; a step is kept\n")
	TEXT("by the plane test. Loads the normal of the pixel two steps away where a crease check fired (a few percent of the\n")
	TEXT("pixels). Debug view 9's B shows the skipped checks.\n")
	TEXT("0 = off. Separate shader permutations of the record pass.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineSpikeThreshold(
	TEXT("r.ThinOutline.SpikeThreshold"),
	0.125f,
	TEXT("Two normals agree, for r.ThinOutline.SpikeFilter, when the sine of the angle between them (|cross|) is below this:\n")
	TEXT("0.125 is about 7 degrees, half the crease thresholds.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineSpikePlaneTolerance(
	TEXT("r.ThinOutline.SpikePlaneTolerance"),
	0.003f,
	TEXT("The two surfaces around a spike, for r.ThinOutline.SpikeFilter, lie on one plane when the second one's sample is\n")
	TEXT("within this fraction of the depth of the first one's plane (along its normal). A groove's two sides are one plane; a\n")
	TEXT("step's two risers are offset by the tread, which is not a spike. 0.003 = 3 cm at 10 m.\n"),
	ECVF_Default
);

TAutoConsoleVariable<int32> CVarThinOutlineSpatialFilter(
	TEXT("r.ThinOutline.SpatialFilter"),
	1,
	TEXT("1 = a drawn edge (crease or silhouette) takes its slope from the edge's positions in the two pixels across its axis\n")
	TEXT("(the rows above and below for a near-vertical edge) and its own, as far as the three line up, keeping its own\n")
	TEXT("position. One pixel's slope is the least certain part of its fit, and along a static edge every pixel has the same\n")
	TEXT("slope error, which shows as a sawtooth below 100% screen percentage when drawing after the upscaler.\n")
	TEXT("0 = each pixel's own slope.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineSpatialFilterSigma(
	TEXT("r.ThinOutline.SpatialFilterSigma"),
	0.25f,
	TEXT("Scale, in rendering pixels, of r.ThinOutline.SpatialFilter's test that the edge positions of the two pixels\n")
	TEXT("across the axis line up with the pixel's own: the neighbours weigh exp(-(d / sigma)^2), d the distance of the\n")
	TEXT("pixel's position from the midpoint of theirs (0 on a straight edge of any slope, about 0.5 for a parallel edge one\n")
	TEXT("pixel over, more at a corner).\n"),
	ECVF_Default
);

TAutoConsoleVariable<int32> CVarThinOutlineAxisBlend(
	TEXT("r.ThinOutline.AxisBlend"),
	0,
	TEXT("When a pixel has an edge on both inducer axes (a corner, or an edge near 45 degrees):\n")
	TEXT("1 = both are drawn, weighted by how clearly one is preferred: by the difference of their absolute slopes in standard\n")
	TEXT("errors (one alone at r.ThinOutline.AxisBlendScale), moving to the one with the smaller slope standard error as the\n")
	TEXT("standard errors differ by one to two times r.ThinOutline.Estimator.SlopeSEThreshold. Noise in the fits then cannot\n")
	TEXT("flip the drawn edge from frame to frame (flicker when drawing after the upscaler).\n")
	TEXT("0 = one is drawn: the one with the clearly smaller slope standard error, otherwise the smaller absolute slope.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineAxisBlendScale(
	TEXT("r.ThinOutline.AxisBlendScale"),
	2.0f,
	TEXT("r.ThinOutline.AxisBlend: the difference of the two axes' absolute slopes, in standard errors of that difference, at\n")
	TEXT("which the axis with the smaller slope is drawn alone (equal slopes: half each).\n"),
	ECVF_Default
);

TAutoConsoleVariable<int32> CVarThinOutlineDenseEdgeSuppression(
	TEXT("r.ThinOutline.DenseEdgeSuppression"),
	1,
	TEXT("Where edges crowd within a pixel or two of each other along a pixel's edge axis, the jitter decides which edge each\n")
	TEXT("pixel's checks pick up on a frame, the records mix the edges' samples, and the drawn lines jump from frame to frame.\n")
	TEXT("A side (left or right, top or bottom) along the drawn edge's inducer axis holds another edge when the pixel two steps\n")
	TEXT("away holds a record, when the neighbour's own record is of another kind (type, or silhouette side), or when the\n")
	TEXT("neighbour's own fit puts the edge elsewhere (r.ThinOutline.DenseEdgeTolerance).\n")
	TEXT("0 = off\n")
	TEXT("1 = the edge is not drawn when both sides hold another edge\n")
	TEXT("2 = the edge is not drawn when either side holds another edge (the blog's reach)\n")
	TEXT("Debug view 7 shows the edges removed.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineDenseEdgeTolerance(
	TEXT("r.ThinOutline.DenseEdgeTolerance"),
	2.0f,
	TEXT("r.ThinOutline.DenseEdgeSuppression: a neighbour along the axis counts as another edge when the position of its fitted\n")
	TEXT("edge at its center, in the pixel's coordinates, differs from the pixel's own by more than this many standard errors of\n")
	TEXT("the difference; within it, the two pixels have reconstructed one edge with some error.\n"),
	ECVF_Default
);

TAutoConsoleVariable<int32> CVarThinOutlineIsolatedEdgeSuppression(
	TEXT("r.ThinOutline.IsolatedEdgeSuppression"),
	1,
	TEXT("1 = a drawn edge that runs over fewer than three pixels across its inducer axis (the rows above and below for a\n")
	TEXT("near-vertical edge) is not drawn: an edge of one or two pixels is a record that comes and goes with the jitter, not a\n")
	TEXT("line, and flickers. The edge continues into a pixel when that pixel's pooled record of the edge's kind on the same\n")
	TEXT("axis (the one the spatial filter also uses) can be fitted; it must continue into both neighbours across the axis, or\n")
	TEXT("into one and the pixel beyond it (an edge end, a corner). Tried on 2026-10-09, reverted and brought back on 2026-10-10\n")
	TEXT("(the author's request, after the crease presence and the spike filter).\n")
	TEXT("0 = off. Debug view 7 shows the edges removed in blue.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineEstimatorDecay(
	TEXT("r.ThinOutline.Estimator.Decay"),
	0.04f,
	TEXT("Decay rate d of the edge records' running statistics, in (0, 1]. Samples are weighted by (1 - d)^age in frames,\n")
	TEXT("so a record that gets a sample every frame holds about 1 / d samples. Lower is steadier, higher follows changes faster.\n"),
	ECVF_Default
);

TAutoConsoleVariable<float> CVarThinOutlineEstimatorFadeMaxSpeed(
	TEXT("r.ThinOutline.Estimator.FadeMaxSpeed"),
	0.05f,
	TEXT("Testing: only a pixel moving slower than this (viewport pixels per frame) keeps records through bad frames (their\n")
	TEXT("presence sinks, r.ThinOutline.Presence); on faster ones a record that fails a history test is dropped at once and a\n")
	TEXT("silhouette sample takes a crease record over at once, so that records do not trail moving edges and contours that\n")
	TEXT("move in are not held back. A large value keeps them at any speed.\n"),
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

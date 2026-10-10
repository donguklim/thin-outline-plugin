// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettingsBackedByCVars.h"
#include "ThinOutlineSettings.generated.h"

/**
 * Project-wide defaults of the outline passes (Project Settings > Plugins > Thin Outline).
 * Properties with a ConsoleVariable are mirrored to that console variable, so they can also be tweaked at runtime.
 */
UCLASS(config = Engine, defaultconfig, meta = (DisplayName = "Thin Outline"))
class THINOUTLINE_API UThinOutlineSettings : public UDeveloperSettingsBackedByCVars
{
	GENERATED_BODY()

public:
	UThinOutlineSettings(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/**
	 * Draws silhouette and crease outlines into scene color, by default at rendering resolution before TSR/TAA/third party upscalers.
	 * The edges are reconstructed over time from the jittered G-buffer edge checks, so they need temporal anti-aliasing.
	 */
	UPROPERTY(config, EditAnywhere, Category = "General", meta = (ConsoleVariable = "r.ThinOutline.Enable", DisplayName = "Enable Outlines"))
	bool bEnable;

	/**
	 * Draw the outline at display resolution after the temporal upscaler (and after depth of field, motion blur and translucency, before
	 * bloom and tonemapping) instead of at rendering resolution before it. Each display pixel gets the exact coverage of the outline band,
	 * so the outline does not go through the upscaler's history, and depth and velocity are left untouched. The edge records are kept at
	 * rendering resolution before the upscaler either way.
	 */
	UPROPERTY(config, EditAnywhere, Category = "General", meta = (ConsoleVariable = "r.ThinOutline.DrawAfterUpscaler"))
	bool bDrawAfterUpscaler;

	/** Draw silhouette outlines. Their records are kept when off, so turning them back on shows them at once. */
	UPROPERTY(config, EditAnywhere, Category = "Silhouette", meta = (ConsoleVariable = "r.ThinOutline.Silhouette", DisplayName = "Draw Silhouettes"))
	bool bDrawSilhouettes;

	/**
	 * Silhouette outline color. Written into pre-exposed HDR scene color, so it does not depend on exposure, but it is still tonemapped.
	 * Pixels painted with a silhouette also get the depth and velocity of the foreground, so the temporal upscaler moves the outline with it.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Silhouette", meta = (HideAlphaChannel))
	FLinearColor SilhouetteColor;

	/**
	 * Silhouette edges sit on depth discontinuities and are drawn on the background side only. A pixel with a silhouette is not a crease candidate.
	 * Edge measure: distance of the neighbour's world position from the line through the opposite neighbour's and the target pixel's,
	 * divided by the target pixel's linear depth. Measures at or below this threshold produce no silhouette, and count as one surface
	 * for creases and the history depth tests.
	 * Sky pixels (no depth) next to any geometry are always full silhouettes, regardless of this threshold.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Silhouette", meta = (ConsoleVariable = "r.ThinOutline.Silhouette.Threshold", ClampMin = "0.0", UIMax = "0.2"))
	float SilhouetteThreshold;

	/** Silhouette strength = saturate((Measure - Threshold) * Scale). */
	UPROPERTY(config, EditAnywhere, Category = "Silhouette", meta = (ConsoleVariable = "r.ThinOutline.Silhouette.Scale", ClampMin = "0.0", UIMax = "500.0"))
	float SilhouetteScale;

	/**
	 * Also measure the edge from the neighbour's side: the smaller of the distance of the neighbour N from the line through the opposite
	 * neighbour and the target pixel C, and the distance of C from the line through the pixel beyond N and N. Keeps junctions where N's
	 * surface meets C's (a wall standing on a floor seen at a grazing angle) continuous, so they stay creases instead of silhouettes.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Silhouette", meta = (ConsoleVariable = "r.ThinOutline.Silhouette.SymmetricMeasure", DisplayName = "Symmetric Silhouette Measure"))
	bool bSilhouetteSymmetricMeasure;

	/**
	 * Silhouette outline thickness in display pixels (pixels after the temporal upscaler), on the background side of the edge.
	 * It is drawn by the two pixels next to the edge on that side, so it does not reach further than about one rendering pixel beyond the edge.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Silhouette", meta = (ConsoleVariable = "r.ThinOutline.Silhouette.Thickness", ClampMin = "0.0", UIMax = "4.0"))
	float SilhouetteThickness;

	/**
	 * Silhouette records are kept by the foreground pixels of the contour, and a reprojected record is rejected when no background is found
	 * within two pixels toward its background side. How the pixel two steps away is tested: 0 = the silhouette measure seen from the pixel
	 * between; 1 = a relative depth step of the history depth threshold (cheaper). Separate shader permutations.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Silhouette", meta = (ConsoleVariable = "r.ThinOutline.Silhouette.HistoryBackgroundTest", ClampMin = "0", ClampMax = "1"))
	int32 SilhouetteHistoryBackgroundTest;

	/**
	 * Testing: silhouette records are rejected when the view direction turns out of the plane through the camera and the edge by more than
	 * this many degrees in one frame (0 = off, a separate shader permutation). At a smooth contour that plane is the surface's tangent plane:
	 * turning within it moves the contour along itself, turning out of it slides the contour over the surface.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Silhouette", meta = (ConsoleVariable = "r.ThinOutline.Silhouette.HistoryViewAngle", ClampMin = "0.0", ClampMax = "90.0", UIMax = "10.0", Units = "Degrees"))
	float SilhouetteHistoryViewAngle;

	/**
	 * Testing, with a history view angle above 0: measure it relative to the surface inside the contour, whose turn since the previous frame
	 * is tracked with the velocities of two of its pixels, so silhouettes of objects turning in front of the camera are rejected too. Off:
	 * the view direction's own turn only (a separate shader permutation).
	 */
	UPROPERTY(config, EditAnywhere, Category = "Silhouette", meta = (ConsoleVariable = "r.ThinOutline.Silhouette.HistorySurfaceTurn", DisplayName = "Silhouette History Follows Surface Turn"))
	bool bSilhouetteHistorySurfaceTurn;

	/**
	 * Relative depth tolerance for keeping a reprojected silhouette record: its foreground depth (the mean depth of the foreground pixel when it
	 * recorded its samples) must match the depth of the pixel itself, or of its neighbour toward the record's foreground side, within this
	 * fraction of the depth.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Silhouette", meta = (ConsoleVariable = "r.ThinOutline.Silhouette.HistoryDepthThreshold", ClampMin = "0.0", UIMax = "0.2"))
	float SilhouetteHistoryDepthThreshold;

	/**
	 * A pixel keeps one edge record per axis, of either type. A crease sample takes over the axis's silhouette record only when that
	 * record's decayed sample count is below this; otherwise the crease sample is ignored. This keeps the record of a silhouette's edge
	 * pixel, whose sample alternates between the background and a foreground with creases. Below 1, so that a silhouette that just got
	 * its first sample survives the next frame's crease sample. A silhouette sample takes over a crease record after Fade Frames frames in a row.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Silhouette", meta = (ConsoleVariable = "r.ThinOutline.Silhouette.CreaseTakeoverSampleCount", ClampMin = "0.0", UIMax = "1.0"))
	float SilhouetteCreaseTakeoverSampleCount;

	/** Draw crease outlines. Their records are kept when off, so turning them back on shows them at once. */
	UPROPERTY(config, EditAnywhere, Category = "Crease", meta = (ConsoleVariable = "r.ThinOutline.Crease", DisplayName = "Draw Creases"))
	bool bDrawCreases;

	/** Crease outline color. Written into pre-exposed HDR scene color, so it does not depend on exposure, but it is still tonemapped. */
	UPROPERTY(config, EditAnywhere, Category = "Crease", meta = (HideAlphaChannel))
	FLinearColor CreaseColor;

	/**
	 * Threshold for convex creases. Creases are only drawn on pixels without a silhouette.
	 * Edge measure: min(|cross(Nc, Nn) - cross(No, Nc)|, |cross(Nc, Nn)|) of world normals, i.e. the sine of the normal angle for a sharp edge.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Crease", meta = (ConsoleVariable = "r.ThinOutline.Crease.RidgeThreshold", ClampMin = "0.0", UIMax = "1.0"))
	float CreaseRidgeThreshold;

	/** Threshold for concave creases. Same measure as the ridge threshold. */
	UPROPERTY(config, EditAnywhere, Category = "Crease", meta = (ConsoleVariable = "r.ThinOutline.Crease.ValleyThreshold", ClampMin = "0.0", UIMax = "1.0"))
	float CreaseValleyThreshold;

	/** Crease strength = saturate((Measure - RidgeOrValleyThreshold) * Scale). */
	UPROPERTY(config, EditAnywhere, Category = "Crease", meta = (ConsoleVariable = "r.ThinOutline.Crease.Scale", ClampMin = "0.0", UIMax = "50.0"))
	float CreaseScale;

	/** Crease outline thickness in display pixels (pixels after the temporal upscaler). */
	UPROPERTY(config, EditAnywhere, Category = "Crease", meta = (ConsoleVariable = "r.ThinOutline.Crease.Thickness", ClampMin = "0.0", UIMax = "4.0"))
	float CreaseThickness;

	/**
	 * Relative depth tolerance for keeping a reprojected crease record: its depth must be within this fraction of the pixel's depth, moved
	 * to the previous frame. The jitter moves the samples by up to a pixel, so it has to cover a pixel's depth step on surfaces seen at a
	 * grazing angle, which grows as the screen percentage drops.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Crease", meta = (ConsoleVariable = "r.ThinOutline.Crease.HistoryDepthThreshold", ClampMin = "0.0", UIMax = "0.2"))
	float CreaseHistoryDepthThreshold;

	/**
	 * Each edge record (crease or silhouette) carries its presence, the fraction of recent frames on which its edge was found at its pixel
	 * along its axis and its history tests passed, as an exponential average over about Frames frames. The drawn strength is scaled by a
	 * ramp on it (0 at Draw Min, 1 at Draw Max) and the record is dropped below Drop Level. So an edge found on a few frames only (a groove
	 * wall or a wire thinner than a pixel, hit by the jittered sample now and then: the pops of a distant surface) stays faint or
	 * invisible, a junction found every other frame draws steadily at part strength, a new edge fades in over a few frames, and a record
	 * that fails its tests or no longer describes an edge at its pixel fades out and is dropped. Off: the strength as recorded, records
	 * dropped only by their history tests on moving pixels and by sample decay. Debug view 9.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Presence", meta = (ConsoleVariable = "r.ThinOutline.Presence", DisplayName = "Presence"))
	bool bPresence;

	/**
	 * Memory of the presence average, in frames. With 4: edges found on 1 frame in 4 or fewer stay below the draw ramp, a new edge fades in
	 * over about 9 frames (to 90%, with the draw ramp ending at 1), a lost one fades out and is dropped after about 12. Larger is steadier
	 * and slower.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Presence", meta = (ConsoleVariable = "r.ThinOutline.Presence.Frames", ClampMin = "1.0", UIMax = "16.0"))
	float PresenceFrames;

	/** Presence at and below which an edge is not drawn; the strength ramps up to Draw Max. */
	UPROPERTY(config, EditAnywhere, Category = "Presence", meta = (ConsoleVariable = "r.ThinOutline.Presence.DrawMin", ClampMin = "0.0", ClampMax = "1.0"))
	float PresenceDrawMin;

	/** Presence at and above which an edge is drawn at its recorded strength (1: only an edge found on every recent frame). */
	UPROPERTY(config, EditAnywhere, Category = "Presence", meta = (ConsoleVariable = "r.ThinOutline.Presence.DrawMax", ClampMin = "0.0", ClampMax = "1.0"))
	float PresenceDrawMax;

	/** Presence below which a record is dropped. */
	UPROPERTY(config, EditAnywhere, Category = "Presence", meta = (ConsoleVariable = "r.ThinOutline.Presence.DropLevel", ClampMin = "0.0", ClampMax = "1.0"))
	float PresenceDropLevel;

	/**
	 * Keep level at which an edge counts as found at a pixel, as a fraction of the crease thresholds (creases) and of the silhouette
	 * threshold (silhouettes, the depth step seen from either side). Below 1, so that an edge that fires only on some frames (near the
	 * threshold) keeps its presence.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Presence", meta = (ConsoleVariable = "r.ThinOutline.Presence.KeepLevel", ClampMin = "0.0", ClampMax = "1.0"))
	float PresenceKeepLevel;

	/**
	 * The crease checks skip one-pixel spikes of the normal field (a pixel whose normal differs from both of its neighbours along an axis
	 * while those two are one surface, or a neighbour that differs from the pixel while the pixel beyond it and the pixel are one surface),
	 * and the silhouette checks skip one-pixel foregrounds (a pixel nearer than both of its neighbours along an axis while those two are one
	 * surface). Such spikes are groove walls, wires and poles thinner than a pixel, hit by the jittered sample on some frames, which pop at
	 * a distance and fed records that blinked or stayed as dots. A real ridge or sliver one pixel wide is dropped with them; a step is kept
	 * by the plane test. Separate shader permutations. Debug view 9's B shows the skipped checks.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Spike Filter", meta = (ConsoleVariable = "r.ThinOutline.SpikeFilter", DisplayName = "Spike Filter"))
	bool bSpikeFilter;

	/** Two normals agree, for the spike filter, when the sine of the angle between them is below this: 0.125 is about 7 degrees, half the crease thresholds. */
	UPROPERTY(config, EditAnywhere, Category = "Spike Filter", meta = (ConsoleVariable = "r.ThinOutline.SpikeThreshold", ClampMin = "0.0", ClampMax = "1.0"))
	float SpikeThreshold;

	/**
	 * The two surfaces around a spike lie on one plane when the second one's sample is within this fraction of the depth of the first one's
	 * plane. A groove's two sides are one plane; a step's two risers are offset by the tread, which is not a spike. 0.003 = 3 cm at 10 m.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Spike Filter", meta = (ConsoleVariable = "r.ThinOutline.SpikePlaneTolerance", ClampMin = "0.0", UIMax = "0.05"))
	float SpikePlaneTolerance;

	/**
	 * Decay rate d of the edge records' running statistics. Samples are weighted by (1 - d)^age in frames, so a record that gets
	 * a sample every frame holds about 1 / d samples. Lower is steadier, higher follows changes faster.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Estimator", meta = (ConsoleVariable = "r.ThinOutline.Estimator.Decay", ClampMin = "0.001", ClampMax = "1.0", UIMax = "0.2"))
	float EstimatorDecay;

	/**
	 * When a pixel has both a horizontal- and a vertical-inducer edge, the one with the smaller slope standard error is drawn if the
	 * two differ by more than this. Otherwise the one with the smaller absolute slope is drawn.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Estimator", meta = (ConsoleVariable = "r.ThinOutline.Estimator.SlopeSEThreshold", ClampMin = "0.0", UIMax = "1.0"))
	float SlopeStandardErrorThreshold;

	/**
	 * A drawn edge (crease or silhouette) takes its slope from the edge's positions in the two pixels across its axis and its own, as far as
	 * the three line up, keeping its own position. One pixel's slope is the least certain part of its fit, and along a static edge every pixel
	 * has the same slope error, which shows as a sawtooth below 100% screen percentage when drawing after the upscaler.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Estimator", meta = (ConsoleVariable = "r.ThinOutline.SpatialFilter", DisplayName = "Spatial Filter"))
	bool bSpatialFilter;

	/**
	 * Scale, in rendering pixels, of the spatial filter's test that the edge positions of the two pixels across the axis line up with the
	 * pixel's own: the neighbours weigh exp(-(d / sigma)^2), d the distance of the pixel's position from the midpoint of theirs (0 on a
	 * straight edge of any slope, about 0.5 for a parallel edge one pixel over, more at a corner).
	 */
	UPROPERTY(config, EditAnywhere, Category = "Estimator", meta = (ConsoleVariable = "r.ThinOutline.SpatialFilterSigma", ClampMin = "0.001", UIMax = "1.0"))
	float SpatialFilterSigma;

	/**
	 * When a pixel has an edge on both inducer axes (a corner, or an edge near 45 degrees), both are drawn, weighted by how clearly one is
	 * preferred (the difference of their absolute slopes in standard errors, moving to the one with the smaller slope standard error as
	 * the standard errors differ by one to two times the slope SE threshold), so that noise in the fits cannot flip the drawn edge from
	 * frame to frame. Off: one is drawn, by the slope SE threshold rule.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Estimator", meta = (ConsoleVariable = "r.ThinOutline.AxisBlend", DisplayName = "Axis Blend"))
	bool bAxisBlend;

	/** The difference of the two axes' absolute slopes, in standard errors of that difference, at which the axis with the smaller slope is drawn alone. */
	UPROPERTY(config, EditAnywhere, Category = "Estimator", meta = (ConsoleVariable = "r.ThinOutline.AxisBlendScale", ClampMin = "0.001", UIMax = "5.0"))
	float AxisBlendScale;

	/**
	 * Where edges crowd within a pixel or two of each other, the jitter decides which edge each pixel's checks pick up on a frame, the
	 * records mix the edges' samples, and the drawn lines jump from frame to frame. 0 = off; 1 = a pixel's edge is not drawn when another
	 * edge lies within two pixels on both sides along its inducer axis; 2 = on either side (the blog's reach). Debug view 7 shows the
	 * edges removed.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Estimator", meta = (ConsoleVariable = "r.ThinOutline.DenseEdgeSuppression", ClampMin = "0", ClampMax = "2"))
	int32 DenseEdgeSuppression;

	/**
	 * Dense edge suppression: a neighbour along the axis counts as another edge when the position of its fitted edge at its center, in
	 * the pixel's coordinates, differs from the pixel's own by more than this many standard errors of the difference; within it, the two
	 * pixels have reconstructed one edge with some error.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Estimator", meta = (ConsoleVariable = "r.ThinOutline.DenseEdgeTolerance", ClampMin = "0.0", UIMax = "5.0"))
	float DenseEdgeTolerance;

	/**
	 * A drawn edge that runs over fewer than three pixels across its inducer axis (the rows above and below for a near-vertical edge) is
	 * not drawn: an edge of one or two pixels is a record that comes and goes with the jitter, not a line, and flickers. The edge continues
	 * into a pixel when that pixel's pooled record of the edge's kind on the same axis can be fitted; it must continue into both neighbours
	 * across the axis, or into one and the pixel beyond it (an edge end, a corner). Debug view 7 shows the edges removed in blue.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Estimator", meta = (ConsoleVariable = "r.ThinOutline.IsolatedEdgeSuppression", DisplayName = "Isolated Edge Suppression"))
	bool bIsolatedEdgeSuppression;

	/**
	 * How the previous frame's edge records are fetched at the reprojected position. 0 = nearest history pixel (records slip past a
	 * moving edge and linger); 1 = nearest, dropping crease records whose edge left the pixel's sampling range; 2 = bilinear (records
	 * follow the edge continuously); 3 = bilinear with the range drop. Silhouette records are rejected by their background test instead.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Estimator", meta = (ConsoleVariable = "r.ThinOutline.Estimator.HistoryReprojection", ClampMin = "0", ClampMax = "3"))
	int32 HistoryReprojection;
};

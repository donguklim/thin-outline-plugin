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
	 * Drop a crease record after a number of frames without a crease found at the pixel along its axis. The depth test alone keeps records
	 * that slid along one depth-continuous surface (a foot's crease left on the floor it stood on, a junction's crease carried onto a curved
	 * wall, copies spread over a face widening on screen). Separate shader permutations.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Crease", meta = (ConsoleVariable = "r.ThinOutline.Crease.HistoryCreaseTest", DisplayName = "Crease History Crease Test"))
	bool bCreaseHistoryCreaseTest;

	/**
	 * Keep level of the crease history test, as a fraction of the ridge and valley thresholds. Below 1, so that a crease that fires only on
	 * some frames (near the threshold) keeps its record.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Crease", meta = (ConsoleVariable = "r.ThinOutline.Crease.HistoryCreaseTestThreshold", ClampMin = "0.0", ClampMax = "1.0"))
	float CreaseHistoryCreaseTestThreshold;

	/**
	 * Frames without a crease found after which the crease history test drops a record; 0 = a whole jitter cycle. It removes crease records
	 * that pass every history test but no longer describe a crease at their pixel: footprints, records slid along a surface, copies on a face
	 * widening on screen. A short limit also drops the records of creases found only on some frames, which then blink. 1000 has no effect in
	 * practice: a record without samples decays to empty first.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Crease", meta = (ConsoleVariable = "r.ThinOutline.Crease.HistoryFramesWithoutCrease", ClampMin = "0", UIMax = "32"))
	int32 CreaseHistoryFramesWithoutCrease;


	/**
	 * Decay rate d of the edge records' running statistics. Samples are weighted by (1 - d)^age in frames, so a record that gets
	 * a sample every frame holds about 1 / d samples. Lower is steadier, higher follows changes faster.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Estimator", meta = (ConsoleVariable = "r.ThinOutline.Estimator.Decay", ClampMin = "0.001", ClampMax = "1.0", UIMax = "0.2"))
	float EstimatorDecay;

	/**
	 * A record that fails a history test, or a crease record that meets a silhouette sample, is kept (and drawn as before) instead of being
	 * dropped at once; after this many such frames in a row it is dropped, or the silhouette takes the axis over. 1 = dropped on the first one.
	 * Where a pixel's jittered sample misses an edge on one frame of the jitter cycle, the outline then stays instead of disappearing.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Estimator", meta = (ConsoleVariable = "r.ThinOutline.Estimator.FadeFrames", ClampMin = "1", UIMax = "8"))
	int32 FadeFrames;

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
	 * How the previous frame's edge records are fetched at the reprojected position. 0 = nearest history pixel (records slip past a
	 * moving edge and linger); 1 = nearest, dropping crease records whose edge left the pixel's sampling range; 2 = bilinear (records
	 * follow the edge continuously); 3 = bilinear with the range drop. Silhouette records are rejected by their background test instead.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Estimator", meta = (ConsoleVariable = "r.ThinOutline.Estimator.HistoryReprojection", ClampMin = "0", ClampMax = "3"))
	int32 HistoryReprojection;
};

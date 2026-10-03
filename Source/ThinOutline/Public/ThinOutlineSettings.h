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
	 * Draws silhouette and crease outlines into scene color at rendering resolution, before TSR/TAA/third party upscalers.
	 * The edges are reconstructed over time from the jittered G-buffer edge checks, so they need temporal anti-aliasing.
	 */
	UPROPERTY(config, EditAnywhere, Category = "General", meta = (ConsoleVariable = "r.ThinOutline.Enable", DisplayName = "Enable Outlines"))
	bool bEnable;

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
	 * It does not reach further than about one rendering pixel beyond the edge.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Silhouette", meta = (ConsoleVariable = "r.ThinOutline.Silhouette.Thickness", ClampMin = "0.0", UIMax = "4.0"))
	float SilhouetteThickness;

	/**
	 * Silhouette records are rejected when the direction their foreground is seen from turns by more than this many degrees in one frame,
	 * since a silhouette slides over the surface as the view direction changes.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Silhouette", meta = (ConsoleVariable = "r.ThinOutline.Silhouette.HistoryViewAngle", ClampMin = "0.0", UIMax = "30.0", Units = "Degrees"))
	float SilhouetteHistoryViewAngle;

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
	 * Decay rate d of the edge records' running statistics. Samples are weighted by (1 - d)^age in frames, so a record that gets
	 * a sample every frame holds about 1 / d samples. Lower is steadier, higher follows changes faster.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Estimator", meta = (ConsoleVariable = "r.ThinOutline.Estimator.Decay", ClampMin = "0.001", ClampMax = "1.0", UIMax = "0.2"))
	float EstimatorDecay;

	/**
	 * Maximum fraction of a record's samples taken on frames where both checks of its axis fired (more than one edge across the pixel).
	 * Records above it are left out of the pooled fit of the pixel and its neighbours.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Estimator", meta = (ConsoleVariable = "r.ThinOutline.Estimator.CoTriggerThreshold", ClampMin = "0.0", ClampMax = "1.0"))
	float CoTriggerThreshold;

	/**
	 * When a pixel has both a horizontal- and a vertical-inducer edge, the one with the smaller slope standard error is drawn if the
	 * two differ by more than this. Otherwise the one with the smaller absolute slope is drawn.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Estimator", meta = (ConsoleVariable = "r.ThinOutline.Estimator.SlopeSEThreshold", ClampMin = "0.0", UIMax = "1.0"))
	float SlopeStandardErrorThreshold;

	/**
	 * Relative depth tolerance for keeping a reprojected edge record. A crease record is kept if its depth lies within the depth range
	 * of the pixel's surface neighbourhood, widened by this fraction of the depth. A silhouette record is kept if its foreground depth
	 * is within this fraction of the current foreground depth.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Estimator", meta = (ConsoleVariable = "r.ThinOutline.Estimator.HistoryDepthThreshold", ClampMin = "0.0", UIMax = "0.2"))
	float HistoryDepthThreshold;

	/**
	 * How the previous frame's edge records are fetched at the reprojected position. 0 = nearest history pixel (records slip past a
	 * moving edge and linger); 1 = nearest, dropping records whose edge left the pixel's sampling range; 2 = bilinear (records follow
	 * the edge continuously); 3 = bilinear with the range drop.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Estimator", meta = (ConsoleVariable = "r.ThinOutline.Estimator.HistoryReprojection", ClampMin = "0", ClampMax = "3"))
	int32 HistoryReprojection;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "SceneViewExtension.h"

struct FPostProcessMaterialInputs;
struct FScreenPassTexture;

/** Snapshot of the outline settings, gathered on the game thread once per view family. */
struct FThinOutlineRenderSettings
{
	bool bDrawAfterUpscaler = false;
	bool bDrawSilhouettes = true;
	bool bDrawCreases = true;
	FLinearColor SilhouetteColor = FLinearColor::Black;
	FLinearColor CreaseColor = FLinearColor::Black;
	float SilhouetteThreshold = 0.0f;
	float SilhouetteScale = 0.0f;
	bool bSilhouetteSymmetricMeasure = true;
	float SilhouetteThickness = 0.0f;
	bool bSilhouetteHistoryBackgroundDepthStep = false;
	float SilhouetteHistoryViewAngle = 0.0f;
	bool bSilhouetteHistorySurfaceTurn = true;
	float SilhouetteCreaseTakeoverSampleCount = 0.0f;
	float CreaseRidgeThreshold = 0.0f;
	float CreaseValleyThreshold = 0.0f;
	float CreaseScale = 0.0f;
	float CreaseThickness = 0.0f;
	float EstimatorDecay = 1.0f;
	float CoTriggerThreshold = 0.0f;
	float SlopeStandardErrorThreshold = 0.0f;
	float CreaseHistoryDepthThreshold = 0.0f;
	bool bCreaseHistoryCreaseTest = true;
	float CreaseHistoryCreaseTestThreshold = 0.0f;
	int32 CreaseHistoryFramesWithoutCrease = 0;
	float SilhouetteHistoryDepthThreshold = 0.0f;
	int32 HistoryReprojection = 0;
	int32 DebugView = 0;
};

/**
 * Textures of the edge records (see ThinOutlineCommon.ush and ThinOutlineRecord.usf): one record per inducer axis, either a
 * crease or a silhouette record (the sign of its sample count).
 */
namespace EThinOutlineHistoryTexture
{
	enum Type : int32
	{
		HorizontalA,
		HorizontalB,
		VerticalA,
		VerticalB,
		/** Kept ratio and foreground depth of the horizontal and vertical records, used by silhouette records only. */
		SilhouetteForeground,
		/** Linear depth of the pixel's surface when the records were written. */
		Depth,
		Num
	};
}

/** Edge records of one view state, carried from frame to frame. */
struct FThinOutlineHistory
{
	TRefCountPtr<IPooledRenderTarget> Textures[EThinOutlineHistoryTexture::Num];
	FIntPoint ViewSize = FIntPoint::ZeroValue;
	/** FSceneViewState::GetFrameIndex() of the frame that wrote the records; they only reproject by one frame. */
	uint32 ViewStateFrameIndex = 0;
	uint32 LastFrameNumber = 0;
};

/**
 * Draws silhouette and crease outlines into scene color at the BeforeDOF post-processing pass, i.e. at rendering
 * resolution before the temporal upscaler (TAA/TSR/third party) runs. The edges are reconstructed from per-pixel
 * temporal records of the jittered G-buffer edge checks. Pixels painted with a silhouette get the foreground's depth
 * and velocity, so the upscaler moves the outline with the foreground.
 * With r.ThinOutline.DrawAfterUpscaler, the records are still updated at BeforeDOF, but the outline is drawn after the
 * MotionBlur pass instead, at display resolution after the upscaler.
 */
class FThinOutlineSceneViewExtension : public FSceneViewExtensionBase
{
public:
	FThinOutlineSceneViewExtension(const FAutoRegister& AutoRegister);

	//~ Begin ISceneViewExtension interface
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void SetupViewPoint(APlayerController* Player, FMinimalViewInfo& InViewInfo) override;
	virtual void SubscribeToPostProcessingPass(
		EPostProcessingPass Pass,
		const FSceneView& InView,
		FPostProcessingPassDelegateArray& InOutPassCallbacks,
		bool bIsPassEnabled) override;
	//~ End ISceneViewExtension interface

	void ReleaseHistories_RenderThread();

protected:
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

private:
	/** Updates the edge records, and draws the outline unless it is drawn after the upscaler. */
	FScreenPassTexture AddOutlinePass_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	/** r.ThinOutline.DrawAfterUpscaler: draws the outline from the records AddOutlinePass_RenderThread left for the view. */
	FScreenPassTexture AddAfterUpscalerPass_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	/** Returns the history of the view's state, or null for views without state. Drops histories of views that stopped rendering. */
	FThinOutlineHistory* FindOrAddHistory_RenderThread(const FSceneView& View);

	/** Updated from BeginRenderViewFamily through a render command, so it always matches the family being rendered. */
	FThinOutlineRenderSettings RenderSettings_RenderThread;
	/** GFrameCounter when r.ThinOutline.Debug.CameraPan or CameraOrbit started moving the camera (game thread). */
	TOptional<uint64> DebugCameraStartFrame;

	/**
	 * Keyed by FSceneViewStateInterface::GetViewKey(). Heap allocated, because the render graph writes extracted
	 * textures into them when it executes, after other views may have added entries.
	 */
	TMap<uint32, TUniquePtr<FThinOutlineHistory>> Histories_RenderThread;
};

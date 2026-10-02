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
	FLinearColor SilhouetteColor = FLinearColor::Black;
	FLinearColor CreaseColor = FLinearColor::Black;
	float SilhouetteThreshold = 0.0f;
	float SilhouetteScale = 0.0f;
	float SilhouetteThickness = 0.0f;
	float SilhouetteHistoryViewAngle = 0.0f;
	float CreaseRidgeThreshold = 0.0f;
	float CreaseValleyThreshold = 0.0f;
	float CreaseScale = 0.0f;
	float CreaseThickness = 0.0f;
	float EstimatorDecay = 1.0f;
	float CoTriggerThreshold = 0.0f;
	float SlopeStandardErrorThreshold = 0.0f;
	float HistoryDepthThreshold = 0.0f;
	int32 DebugView = 0;
};

/** Textures of the edge records (see ThinOutlineCommon.ush and ThinOutlineRecord.usf). */
namespace EThinOutlineHistoryTexture
{
	enum Type : int32
	{
		CreaseHorizontalA,
		CreaseHorizontalB,
		CreaseVerticalA,
		CreaseVerticalB,
		SilhouetteHorizontalA,
		SilhouetteHorizontalB,
		SilhouetteVerticalA,
		SilhouetteVerticalB,
		/** Kept ratio of the horizontal and vertical silhouette records. */
		SilhouetteKept,
		/** Linear depth of the pixel's surface and of its foreground when the records were written. */
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
 */
class FThinOutlineSceneViewExtension : public FSceneViewExtensionBase
{
public:
	FThinOutlineSceneViewExtension(const FAutoRegister& AutoRegister);

	//~ Begin ISceneViewExtension interface
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;
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
	FScreenPassTexture AddOutlinePass_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	/** Returns the history of the view's state, or null for views without state. Drops histories of views that stopped rendering. */
	FThinOutlineHistory* FindOrAddHistory_RenderThread(const FSceneView& View);

	/** Updated from BeginRenderViewFamily through a render command, so it always matches the family being rendered. */
	FThinOutlineRenderSettings RenderSettings_RenderThread;

	/**
	 * Keyed by FSceneViewStateInterface::GetViewKey(). Heap allocated, because the render graph writes extracted
	 * textures into them when it executes, after other views may have added entries.
	 */
	TMap<uint32, TUniquePtr<FThinOutlineHistory>> Histories_RenderThread;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

struct FPostProcessMaterialInputs;
struct FScreenPassTexture;

/** Snapshot of the outline settings, gathered on the game thread once per view family. */
struct FThinOutlineRenderSettings
{
	FLinearColor OutlineColor = FLinearColor::Black;
	float SilhouetteThreshold = 0.0f;
	float SilhouetteScale = 0.0f;
	float CreaseRidgeThreshold = 0.0f;
	float CreaseValleyThreshold = 0.0f;
	float CreaseScale = 0.0f;
	int32 DebugView = 0;
};

/**
 * Draws G-buffer outlines into scene color at the BeforeDOF post-processing pass, i.e. at rendering resolution
 * before the temporal upscaler (TAA/TSR/third party) runs.
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

protected:
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

private:
	FScreenPassTexture AddOutlinePass_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	/** Updated from BeginRenderViewFamily through a render command, so it always matches the family being rendered. */
	FThinOutlineRenderSettings RenderSettings_RenderThread;
};

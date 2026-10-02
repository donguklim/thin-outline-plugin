// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThinOutlineSceneViewExtension.h"
#include "ThinOutlineCVars.h"
#include "ThinOutlineSettings.h"
#include "ThinOutlineShaders.h"

#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "SceneRendering.h"
#include "ScreenPass.h"

DECLARE_GPU_STAT_NAMED(ThinOutline, TEXT("ThinOutline"));

FThinOutlineSceneViewExtension::FThinOutlineSceneViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

bool FThinOutlineSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return CVarThinOutlineEnable.GetValueOnGameThread() > 0;
}

void FThinOutlineSceneViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	FThinOutlineRenderSettings Settings;
	Settings.OutlineColor          = GetDefault<UThinOutlineSettings>()->OutlineColor;
	Settings.SilhouetteThreshold   = FMath::Max(0.0f, CVarThinOutlineSilhouetteThreshold.GetValueOnGameThread());
	Settings.SilhouetteScale       = FMath::Max(0.0f, CVarThinOutlineSilhouetteScale.GetValueOnGameThread());
	Settings.CreaseRidgeThreshold  = FMath::Max(0.0f, CVarThinOutlineCreaseRidgeThreshold.GetValueOnGameThread());
	Settings.CreaseValleyThreshold = FMath::Max(0.0f, CVarThinOutlineCreaseValleyThreshold.GetValueOnGameThread());
	Settings.CreaseScale           = FMath::Max(0.0f, CVarThinOutlineCreaseScale.GetValueOnGameThread());
	Settings.DebugView             = CVarThinOutlineDebugView.GetValueOnGameThread();

	ENQUEUE_RENDER_COMMAND(ThinOutlineUpdateSettings)(
		[WeakThis = AsWeak(), Settings](FRHICommandListImmediate& RHICmdList)
		{
			if (TSharedPtr<FSceneViewExtensionBase, ESPMode::ThreadSafe> This = WeakThis.Pin())
			{
				StaticCastSharedPtr<FThinOutlineSceneViewExtension>(This)->RenderSettings_RenderThread = Settings;
			}
		});
}

void FThinOutlineSceneViewExtension::SubscribeToPostProcessingPass(
	EPostProcessingPass Pass,
	const FSceneView& InView,
	FPostProcessingPassDelegateArray& InOutPassCallbacks,
	bool bIsPassEnabled)
{
	// BeforeDOF runs at rendering resolution, before the temporal upscaler.
	if (Pass != EPostProcessingPass::BeforeDOF)
	{
		return;
	}

	// The pass needs the deferred G-buffer and FViewInfo (for Substrate).
	if (!InView.bIsViewInfo || InView.GetFeatureLevel() < ERHIFeatureLevel::SM5)
	{
		return;
	}

	const FEngineShowFlags& ShowFlags = InView.Family->EngineShowFlags;
	if (ShowFlags.Wireframe || ShowFlags.VisualizeBuffer)
	{
		return;
	}

	InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FThinOutlineSceneViewExtension::AddOutlinePass_RenderThread));
}

FScreenPassTexture FThinOutlineSceneViewExtension::AddOutlinePass_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	if (!SceneColor.IsValid() || !Inputs.SceneTextures.SceneTextures)
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(View);
	const FIntPoint ViewSize = SceneColor.ViewRect.Size();

	RDG_EVENT_SCOPE_STAT(GraphBuilder, ThinOutline, "ThinOutline %dx%d", ViewSize.X, ViewSize.Y);

	FScreenPassRenderTarget Output = Inputs.OverrideOutput;
	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, SceneColor, View.GetOverwriteLoadAction(), TEXT("ThinOutline.SceneColor"));
	}

	const FScreenPassTextureViewport InputViewport(SceneColor);
	const FScreenPassTextureViewport OutputViewport(Output);
	const FThinOutlineRenderSettings& Settings = RenderSettings_RenderThread;

	FThinOutlinePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FThinOutlinePS::FParameters>();
	PassParameters->View                   = View.ViewUniformBuffer;
	PassParameters->SceneTexturesStruct    = Inputs.SceneTextures.SceneTextures;
	PassParameters->Substrate              = ViewInfo.SubstrateViewData.SubstrateGlobalUniformParameters;
	PassParameters->Input                  = GetScreenPassTextureViewportParameters(InputViewport);
	PassParameters->Output                 = GetScreenPassTextureViewportParameters(OutputViewport);
	PassParameters->InputSceneColorTexture = SceneColor.Texture;
	PassParameters->OutlineColor           = FVector3f(Settings.OutlineColor.R, Settings.OutlineColor.G, Settings.OutlineColor.B);
	PassParameters->SilhouetteThreshold    = Settings.SilhouetteThreshold;
	PassParameters->SilhouetteScale        = Settings.SilhouetteScale;
	PassParameters->CreaseRidgeThreshold   = Settings.CreaseRidgeThreshold;
	PassParameters->CreaseValleyThreshold  = Settings.CreaseValleyThreshold;
	PassParameters->CreaseScale            = Settings.CreaseScale;
	PassParameters->DebugView              = static_cast<uint32>(FMath::Max(Settings.DebugView, 0));
	PassParameters->RenderTargets[0]       = Output.GetRenderTargetBinding();

	TShaderMapRef<FThinOutlinePS> PixelShader(GetGlobalShaderMap(View.GetFeatureLevel()));

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("ThinOutline"),
		View,
		OutputViewport,
		InputViewport,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}

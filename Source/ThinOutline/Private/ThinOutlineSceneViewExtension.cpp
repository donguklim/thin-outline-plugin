// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThinOutlineSceneViewExtension.h"
#include "ThinOutlineCVars.h"
#include "ThinOutlineSettings.h"
#include "ThinOutlineShaders.h"

#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneRendering.h"
#include "SceneViewState.h"
#include "ScreenPass.h"
#include "SystemTextures.h"

DECLARE_GPU_STAT_NAMED(ThinOutline, TEXT("ThinOutline"));

namespace ThinOutline
{
	// Histories of view states that have not rendered for this many frames are released.
	constexpr int64 HistoryLifetimeFrames = 300;
}

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
	Settings.OutlineColor                    = GetDefault<UThinOutlineSettings>()->OutlineColor;
	Settings.SilhouetteThreshold             = FMath::Max(0.0f, CVarThinOutlineSilhouetteThreshold.GetValueOnGameThread());
	Settings.SilhouetteScale                 = FMath::Max(0.0f, CVarThinOutlineSilhouetteScale.GetValueOnGameThread());
	Settings.CreaseRidgeThreshold            = FMath::Max(0.0f, CVarThinOutlineCreaseRidgeThreshold.GetValueOnGameThread());
	Settings.CreaseValleyThreshold           = FMath::Max(0.0f, CVarThinOutlineCreaseValleyThreshold.GetValueOnGameThread());
	Settings.CreaseScale                     = FMath::Max(0.0f, CVarThinOutlineCreaseScale.GetValueOnGameThread());
	Settings.CreaseThickness                 = FMath::Max(0.0f, CVarThinOutlineCreaseThickness.GetValueOnGameThread());
	Settings.EstimatorDecay                  = FMath::Clamp(CVarThinOutlineEstimatorDecay.GetValueOnGameThread(), 0.001f, 1.0f);
	Settings.CoTriggerThreshold              = FMath::Max(0.0f, CVarThinOutlineEstimatorCoTriggerThreshold.GetValueOnGameThread());
	Settings.SlopeStandardErrorThreshold     = FMath::Max(0.0f, CVarThinOutlineEstimatorSlopeSEThreshold.GetValueOnGameThread());
	Settings.HistoryDepthThreshold           = FMath::Max(0.0f, CVarThinOutlineEstimatorHistoryDepthThreshold.GetValueOnGameThread());
	Settings.DebugView                       = CVarThinOutlineDebugView.GetValueOnGameThread();

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

	// The pass needs the deferred G-buffer and FViewInfo (for Substrate and the temporal jitter).
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

void FThinOutlineSceneViewExtension::ReleaseHistories_RenderThread()
{
	Histories_RenderThread.Empty();
}

FThinOutlineHistory* FThinOutlineSceneViewExtension::FindOrAddHistory_RenderThread(const FSceneView& View)
{
	const int64 FrameNumber = View.Family->FrameNumber;

	for (auto It = Histories_RenderThread.CreateIterator(); It; ++It)
	{
		if (FrameNumber - int64(It.Value()->LastFrameNumber) > ThinOutline::HistoryLifetimeFrames)
		{
			It.RemoveCurrent();
		}
	}

	if (!View.State)
	{
		return nullptr;
	}

	TUniquePtr<FThinOutlineHistory>& History = Histories_RenderThread.FindOrAdd(View.State->GetViewKey());
	if (!History.IsValid())
	{
		History = MakeUnique<FThinOutlineHistory>();
	}
	History->LastFrameNumber = View.Family->FrameNumber;
	return History.Get();
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
	const FIntPoint ViewSize = ViewInfo.ViewRect.Size();
	const FThinOutlineRenderSettings& Settings = RenderSettings_RenderThread;

	RDG_EVENT_SCOPE_STAT(GraphBuilder, ThinOutline, "ThinOutline %dx%d", ViewSize.X, ViewSize.Y);

	FThinOutlineEdgeCheckParameters EdgeCheck;
	EdgeCheck.SilhouetteThreshold   = Settings.SilhouetteThreshold;
	EdgeCheck.SilhouetteScale       = Settings.SilhouetteScale;
	EdgeCheck.CreaseRidgeThreshold  = Settings.CreaseRidgeThreshold;
	EdgeCheck.CreaseValleyThreshold = Settings.CreaseValleyThreshold;
	EdgeCheck.CreaseScale           = Settings.CreaseScale;

	// The projection jitter moves the scene by TemporalJitterPixels, so every pixel samples the G-buffer at its
	// center minus the jitter.
	const FVector2f SampleLocalPosition = FVector2f(0.5f, 0.5f) - FVector2f(ViewInfo.TemporalJitterPixels);

	FThinOutlineHistory* History = FindOrAddHistory_RenderThread(View);
	const uint32 ViewStateFrameIndex = ViewInfo.ViewState ? ViewInfo.ViewState->GetFrameIndex() : 0;
	const bool bHistoryValid = History
		&& History->Depth.IsValid()
		&& History->ViewSize == ViewSize
		&& (ViewInfo.bStatePrevViewInfoIsReadOnly || ViewStateFrameIndex == History->ViewStateFrameIndex + 1)
		&& !View.bCameraCut
		&& !ViewInfo.bPrevTransformsReset;

	// Edge record update.
	const FRDGTextureDesc RecordDesc = FRDGTextureDesc::Create2D(ViewSize, PF_FloatRGBA, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
	const FRDGTextureDesc DepthDesc = FRDGTextureDesc::Create2D(ViewSize, PF_R32_FLOAT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);

	static const TCHAR* const RecordNames[4] =
	{
		TEXT("ThinOutline.HorizontalRecordA"),
		TEXT("ThinOutline.HorizontalRecordB"),
		TEXT("ThinOutline.VerticalRecordA"),
		TEXT("ThinOutline.VerticalRecordB"),
	};

	FRDGTextureRef Records[4];
	FRDGTextureRef HistoryRecords[4];
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Records[Index] = GraphBuilder.CreateTexture(RecordDesc, RecordNames[Index]);
		HistoryRecords[Index] = bHistoryValid ? GraphBuilder.RegisterExternalTexture(History->Records[Index]) : GSystemTextures.GetBlackDummy(GraphBuilder);
	}
	FRDGTextureRef Depth = GraphBuilder.CreateTexture(DepthDesc, TEXT("ThinOutline.RecordDepth"));
	FRDGTextureRef HistoryDepth = bHistoryValid ? GraphBuilder.RegisterExternalTexture(History->Depth) : GSystemTextures.GetBlackDummy(GraphBuilder);

	{
		FThinOutlineRecordCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FThinOutlineRecordCS::FParameters>();
		PassParameters->View                  = View.ViewUniformBuffer;
		PassParameters->SceneTexturesStruct   = Inputs.SceneTextures.SceneTextures;
		PassParameters->Substrate             = ViewInfo.SubstrateViewData.SubstrateGlobalUniformParameters;
		PassParameters->EdgeCheck             = EdgeCheck;
		PassParameters->HistoryHorizontalA    = HistoryRecords[0];
		PassParameters->HistoryHorizontalB    = HistoryRecords[1];
		PassParameters->HistoryVerticalA      = HistoryRecords[2];
		PassParameters->HistoryVerticalB      = HistoryRecords[3];
		PassParameters->HistoryDepth          = HistoryDepth;
		PassParameters->RWHorizontalA         = GraphBuilder.CreateUAV(Records[0]);
		PassParameters->RWHorizontalB         = GraphBuilder.CreateUAV(Records[1]);
		PassParameters->RWVerticalA           = GraphBuilder.CreateUAV(Records[2]);
		PassParameters->RWVerticalB           = GraphBuilder.CreateUAV(Records[3]);
		PassParameters->RWDepth               = GraphBuilder.CreateUAV(Depth);
		PassParameters->SampleLocalPosition   = SampleLocalPosition;
		PassParameters->SampleCountDecay      = 1.0f - Settings.EstimatorDecay;
		PassParameters->HistoryDepthThreshold = Settings.HistoryDepthThreshold;
		PassParameters->bHistoryValid         = bHistoryValid ? 1 : 0;

		TShaderMapRef<FThinOutlineRecordCS> ComputeShader(GetGlobalShaderMap(View.GetFeatureLevel()));

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ThinOutline.Records"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(ViewSize, FThinOutlineRecordCS::ThreadGroupSize));
	}

	// Views that must not advance the view state's temporal history only read it.
	if (History && !ViewInfo.bStatePrevViewInfoIsReadOnly)
	{
		for (int32 Index = 0; Index < 4; ++Index)
		{
			GraphBuilder.QueueTextureExtraction(Records[Index], &History->Records[Index]);
		}
		GraphBuilder.QueueTextureExtraction(Depth, &History->Depth);
		History->ViewSize = ViewSize;
		History->ViewStateFrameIndex = ViewStateFrameIndex;
	}

	// Edge reconstruction and composite.
	FScreenPassRenderTarget Output = Inputs.OverrideOutput;
	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, SceneColor, View.GetOverwriteLoadAction(), TEXT("ThinOutline.SceneColor"));
	}

	const FScreenPassTextureViewport InputViewport(SceneColor);
	const FScreenPassTextureViewport OutputViewport(Output);

	// Display pixels are the output pixels of the temporal upscaler; without one, temporal AA runs at rendering resolution.
	const FIntPoint DisplaySize = View.PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale
		? ViewInfo.GetSecondaryViewRectSize()
		: ViewSize;
	const FVector2f RenderPixelsPerDisplayPixel(
		float(ViewSize.X) / float(FMath::Max(DisplaySize.X, 1)),
		float(ViewSize.Y) / float(FMath::Max(DisplaySize.Y, 1)));

	FThinOutlinePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FThinOutlinePS::FParameters>();
	PassParameters->View                            = View.ViewUniformBuffer;
	PassParameters->SceneTexturesStruct             = Inputs.SceneTextures.SceneTextures;
	PassParameters->Substrate                       = ViewInfo.SubstrateViewData.SubstrateGlobalUniformParameters;
	PassParameters->EdgeCheck                       = EdgeCheck;
	PassParameters->Input                           = GetScreenPassTextureViewportParameters(InputViewport);
	PassParameters->Output                          = GetScreenPassTextureViewportParameters(OutputViewport);
	PassParameters->InputSceneColorTexture          = SceneColor.Texture;
	PassParameters->RecordHorizontalA               = Records[0];
	PassParameters->RecordHorizontalB               = Records[1];
	PassParameters->RecordVerticalA                 = Records[2];
	PassParameters->RecordVerticalB                 = Records[3];
	PassParameters->OutlineColor                    = FVector3f(Settings.OutlineColor.R, Settings.OutlineColor.G, Settings.OutlineColor.B);
	PassParameters->SampleLocalPosition             = SampleLocalPosition;
	PassParameters->RenderPixelsPerDisplayPixel     = RenderPixelsPerDisplayPixel;
	PassParameters->HalfThickness                   = 0.25f * Settings.CreaseThickness * (RenderPixelsPerDisplayPixel.X + RenderPixelsPerDisplayPixel.Y);
	PassParameters->CoTriggerThreshold              = Settings.CoTriggerThreshold;
	PassParameters->SlopeStandardErrorThreshold     = Settings.SlopeStandardErrorThreshold;
	PassParameters->DistinctSampleScale             = FMath::Min(1.0f, Settings.EstimatorDecay * float(FMath::Max(ViewInfo.TemporalJitterSequenceLength, 1)));
	PassParameters->SaturatedSampleCount            = 1.0f / Settings.EstimatorDecay;
	PassParameters->DebugView                       = static_cast<uint32>(FMath::Max(Settings.DebugView, 0));
	PassParameters->RenderTargets[0]                = Output.GetRenderTargetBinding();

	TShaderMapRef<FThinOutlinePS> PixelShader(GetGlobalShaderMap(View.GetFeatureLevel()));

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("ThinOutline.Composite"),
		View,
		OutputViewport,
		InputViewport,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThinOutlineSceneViewExtension.h"
#include "ThinOutlineCVars.h"
#include "ThinOutlineSettings.h"
#include "ThinOutlineShaders.h"

#include "PixelShaderUtils.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneRendering.h"
#include "SceneViewState.h"
#include "ScreenPass.h"
#include "SystemTextures.h"
#include "Camera/CameraTypes.h"

DECLARE_GPU_STAT_NAMED(ThinOutline, TEXT("ThinOutline"));

namespace ThinOutline
{
	// Histories of view states that have not rendered for this many frames are released.
	constexpr int64 HistoryLifetimeFrames = 300;

	struct FHistoryTextureInfo
	{
		EPixelFormat Format;
		const TCHAR* Name;
	};

	static const FHistoryTextureInfo HistoryTextureInfos[EThinOutlineHistoryTexture::Num] =
	{
		{ PF_FloatRGBA, TEXT("ThinOutline.RecordHorizontalA") },
		{ PF_FloatRGBA, TEXT("ThinOutline.RecordHorizontalB") },
		{ PF_FloatRGBA, TEXT("ThinOutline.RecordVerticalA") },
		{ PF_FloatRGBA, TEXT("ThinOutline.RecordVerticalB") },
		{ PF_FloatRGBA, TEXT("ThinOutline.SilhouetteForeground") },
		{ PF_R32_FLOAT, TEXT("ThinOutline.RecordDepth") },
	};
}

FThinOutlineSceneViewExtension::FThinOutlineSceneViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

void FThinOutlineSceneViewExtension::SetupViewPoint(APlayerController* Player, FMinimalViewInfo& InViewInfo)
{
	// Steady camera motion for offscreen tests, where nothing drives the camera. The view point is set up more than once
	// per frame, so the motion follows the frame counter.
	const float Pan = CVarThinOutlineDebugCameraPan.GetValueOnGameThread();
	const float Orbit = CVarThinOutlineDebugCameraOrbit.GetValueOnGameThread();
	if (Pan == 0.0f && Orbit == 0.0f)
	{
		DebugCameraStartFrame.Reset();
		return;
	}

	if (!DebugCameraStartFrame.IsSet())
	{
		DebugCameraStartFrame = GFrameCounter;
	}
	const double Frames = double(GFrameCounter - DebugCameraStartFrame.GetValue() + 1);

	if (Pan != 0.0f)
	{
		InViewInfo.Location += InViewInfo.Rotation.RotateVector(FVector(0.0, Pan * Frames, 0.0));
	}
	if (Orbit != 0.0f)
	{
		const FVector Pivot = InViewInfo.Location + InViewInfo.Rotation.Vector() * CVarThinOutlineDebugCameraOrbitDistance.GetValueOnGameThread();
		const FQuat Yaw(FVector::UpVector, FMath::DegreesToRadians(Orbit * Frames));
		InViewInfo.Location = Pivot + Yaw.RotateVector(InViewInfo.Location - Pivot);
		InViewInfo.Rotation = (Yaw * InViewInfo.Rotation.Quaternion()).Rotator();
	}
}

bool FThinOutlineSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return CVarThinOutlineEnable.GetValueOnGameThread() > 0;
}

void FThinOutlineSceneViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	FThinOutlineRenderSettings Settings;
	Settings.bDrawSilhouettes                    = CVarThinOutlineSilhouette.GetValueOnGameThread() != 0;
	Settings.bDrawCreases                        = CVarThinOutlineCrease.GetValueOnGameThread() != 0;
	Settings.SilhouetteColor                     = GetDefault<UThinOutlineSettings>()->SilhouetteColor;
	Settings.CreaseColor                         = GetDefault<UThinOutlineSettings>()->CreaseColor;
	Settings.SilhouetteThreshold                 = FMath::Max(0.0f, CVarThinOutlineSilhouetteThreshold.GetValueOnGameThread());
	Settings.SilhouetteScale                     = FMath::Max(0.0f, CVarThinOutlineSilhouetteScale.GetValueOnGameThread());
	Settings.bSilhouetteSymmetricMeasure         = CVarThinOutlineSilhouetteSymmetricMeasure.GetValueOnGameThread() != 0;
	Settings.SilhouetteThickness                 = FMath::Max(0.0f, CVarThinOutlineSilhouetteThickness.GetValueOnGameThread());
	Settings.bSilhouetteHistoryBackgroundDepthStep = CVarThinOutlineSilhouetteHistoryBackgroundTest.GetValueOnGameThread() == 1;
	Settings.SilhouetteHistoryViewAngle          = FMath::Clamp(CVarThinOutlineSilhouetteHistoryViewAngle.GetValueOnGameThread(), 0.0f, 90.0f);
	Settings.SilhouetteCreaseTakeoverSampleCount = FMath::Max(0.0f, CVarThinOutlineSilhouetteCreaseTakeoverSampleCount.GetValueOnGameThread());
	Settings.bSilhouetteHistorySurfaceTurn       = CVarThinOutlineSilhouetteHistorySurfaceTurn.GetValueOnGameThread() != 0;
	Settings.CreaseRidgeThreshold                = FMath::Max(0.0f, CVarThinOutlineCreaseRidgeThreshold.GetValueOnGameThread());
	Settings.CreaseValleyThreshold               = FMath::Max(0.0f, CVarThinOutlineCreaseValleyThreshold.GetValueOnGameThread());
	Settings.CreaseScale                         = FMath::Max(0.0f, CVarThinOutlineCreaseScale.GetValueOnGameThread());
	Settings.CreaseThickness                     = FMath::Max(0.0f, CVarThinOutlineCreaseThickness.GetValueOnGameThread());
	Settings.EstimatorDecay                      = FMath::Clamp(CVarThinOutlineEstimatorDecay.GetValueOnGameThread(), 0.001f, 1.0f);
	Settings.CoTriggerThreshold                  = FMath::Max(0.0f, CVarThinOutlineEstimatorCoTriggerThreshold.GetValueOnGameThread());
	Settings.SlopeStandardErrorThreshold         = FMath::Max(0.0f, CVarThinOutlineEstimatorSlopeSEThreshold.GetValueOnGameThread());
	Settings.CreaseHistoryDepthThreshold         = FMath::Max(0.0f, CVarThinOutlineCreaseHistoryDepthThreshold.GetValueOnGameThread());
	Settings.bCreaseHistoryCreaseTest            = CVarThinOutlineCreaseHistoryCreaseTest.GetValueOnGameThread() != 0;
	// At most 1: a crease sample then always counts as a crease found.
	Settings.CreaseHistoryCreaseTestThreshold    = FMath::Clamp(CVarThinOutlineCreaseHistoryCreaseTestThreshold.GetValueOnGameThread(), 0.0f, 1.0f);
	Settings.CreaseHistoryCreaseTestFrames       = FMath::Max(0, CVarThinOutlineCreaseHistoryCreaseTestFrames.GetValueOnGameThread());
	Settings.SilhouetteHistoryDepthThreshold     = FMath::Max(0.0f, CVarThinOutlineSilhouetteHistoryDepthThreshold.GetValueOnGameThread());
	Settings.HistoryReprojection                 = FMath::Clamp(CVarThinOutlineEstimatorHistoryReprojection.GetValueOnGameThread(), 0, 3);
	Settings.DebugView                           = CVarThinOutlineDebugView.GetValueOnGameThread();

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
	EdgeCheck.SilhouetteThreshold    = Settings.SilhouetteThreshold;
	EdgeCheck.SilhouetteScale        = Settings.SilhouetteScale;
	EdgeCheck.bSymmetricDepthMeasure = Settings.bSilhouetteSymmetricMeasure ? 1 : 0;
	EdgeCheck.CreaseRidgeThreshold   = Settings.CreaseRidgeThreshold;
	EdgeCheck.CreaseValleyThreshold  = Settings.CreaseValleyThreshold;
	EdgeCheck.CreaseScale            = Settings.CreaseScale;

	// The projection jitter moves the scene by TemporalJitterPixels, so every pixel samples the G-buffer at its
	// center minus the jitter.
	const FVector2f SampleLocalPosition = FVector2f(0.5f, 0.5f) - FVector2f(ViewInfo.TemporalJitterPixels);

	FThinOutlineHistory* History = FindOrAddHistory_RenderThread(View);
	const uint32 ViewStateFrameIndex = ViewInfo.ViewState ? ViewInfo.ViewState->GetFrameIndex() : 0;
	const bool bHistoryValid = History
		&& History->Textures[EThinOutlineHistoryTexture::Depth].IsValid()
		&& History->ViewSize == ViewSize
		&& (ViewInfo.bStatePrevViewInfoIsReadOnly || ViewStateFrameIndex == History->ViewStateFrameIndex + 1)
		&& !View.bCameraCut
		&& !ViewInfo.bPrevTransformsReset;

	using namespace EThinOutlineHistoryTexture;

	// Edge record update.
	FRDGTextureRef Records[Num];
	FRDGTextureRef HistoryRecords[Num];
	for (int32 Index = 0; Index < Num; ++Index)
	{
		const ThinOutline::FHistoryTextureInfo& Info = ThinOutline::HistoryTextureInfos[Index];
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(ViewSize, Info.Format, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
		Records[Index] = GraphBuilder.CreateTexture(Desc, Info.Name);
		HistoryRecords[Index] = bHistoryValid ? GraphBuilder.RegisterExternalTexture(History->Textures[Index]) : GSystemTextures.GetBlackDummy(GraphBuilder);
	}

	{
		FThinOutlineRecordCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FThinOutlineRecordCS::FParameters>();
		PassParameters->View                                = View.ViewUniformBuffer;
		PassParameters->SceneTexturesStruct                 = Inputs.SceneTextures.SceneTextures;
		PassParameters->Substrate                           = ViewInfo.SubstrateViewData.SubstrateGlobalUniformParameters;
		PassParameters->EdgeCheck                           = EdgeCheck;
		PassParameters->HistoryHorizontalA                  = HistoryRecords[HorizontalA];
		PassParameters->HistoryHorizontalB                  = HistoryRecords[HorizontalB];
		PassParameters->HistoryVerticalA                    = HistoryRecords[VerticalA];
		PassParameters->HistoryVerticalB                    = HistoryRecords[VerticalB];
		PassParameters->HistorySilhouetteForeground         = HistoryRecords[SilhouetteForeground];
		PassParameters->HistoryDepth                        = HistoryRecords[Depth];
		PassParameters->RWHorizontalA                       = GraphBuilder.CreateUAV(Records[HorizontalA]);
		PassParameters->RWHorizontalB                       = GraphBuilder.CreateUAV(Records[HorizontalB]);
		PassParameters->RWVerticalA                         = GraphBuilder.CreateUAV(Records[VerticalA]);
		PassParameters->RWVerticalB                         = GraphBuilder.CreateUAV(Records[VerticalB]);
		PassParameters->RWSilhouetteForeground              = GraphBuilder.CreateUAV(Records[SilhouetteForeground]);
		PassParameters->RWDepth                             = GraphBuilder.CreateUAV(Records[Depth]);
		PassParameters->SampleLocalPosition                 = SampleLocalPosition;
		PassParameters->SampleCountDecay                    = 1.0f - Settings.EstimatorDecay;
		PassParameters->CreaseHistoryDepthThreshold         = Settings.CreaseHistoryDepthThreshold;
		PassParameters->SilhouetteHistoryDepthThreshold     = Settings.SilhouetteHistoryDepthThreshold;
		PassParameters->SilhouetteHistorySinAngle           = FMath::Sin(FMath::DegreesToRadians(Settings.SilhouetteHistoryViewAngle));
		PassParameters->SilhouetteCreaseTakeoverSampleCount = Settings.SilhouetteCreaseTakeoverSampleCount;
		PassParameters->CreaseHistoryKeepRidgeThreshold     = Settings.CreaseHistoryCreaseTestThreshold * Settings.CreaseRidgeThreshold;
		PassParameters->CreaseHistoryKeepValleyThreshold    = Settings.CreaseHistoryCreaseTestThreshold * Settings.CreaseValleyThreshold;
		PassParameters->CreaseHistoryMissLimit              = float(Settings.CreaseHistoryCreaseTestFrames > 0
			? Settings.CreaseHistoryCreaseTestFrames
			: FMath::Max(ViewInfo.TemporalJitterSequenceLength, 1));
		PassParameters->bHistoryValid                       = bHistoryValid ? 1 : 0;
		PassParameters->HistoryReprojectionMode             = static_cast<uint32>(Settings.HistoryReprojection);

		FThinOutlineRecordCS::FPermutationDomain PermutationVector;
		const bool bViewAngleTest = Settings.SilhouetteHistoryViewAngle > 0.0f;
		PermutationVector.Set<FThinOutlineRecordCS::FBackgroundDepthStepDim>(Settings.bSilhouetteHistoryBackgroundDepthStep);
		PermutationVector.Set<FThinOutlineRecordCS::FCreaseHistoryTestDim>(Settings.bCreaseHistoryCreaseTest);
		PermutationVector.Set<FThinOutlineRecordCS::FViewAngleTestDim>(bViewAngleTest);
		PermutationVector.Set<FThinOutlineRecordCS::FSurfaceTurnDim>(bViewAngleTest && Settings.bSilhouetteHistorySurfaceTurn);
		TShaderMapRef<FThinOutlineRecordCS> ComputeShader(GetGlobalShaderMap(View.GetFeatureLevel()), PermutationVector);

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
		for (int32 Index = 0; Index < Num; ++Index)
		{
			GraphBuilder.QueueTextureExtraction(Records[Index], &History->Textures[Index]);
		}
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
	const float MeanRenderPixelsPerDisplayPixel = 0.5f * (RenderPixelsPerDisplayPixel.X + RenderPixelsPerDisplayPixel.Y);

	// Depth and encoded velocity of the foreground of the pixels painted with a silhouette.
	const FRDGTextureDesc ForegroundDeviceZDesc = FRDGTextureDesc::Create2D(ViewSize, PF_R32_FLOAT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
	const FRDGTextureDesc ForegroundVelocityDesc = FRDGTextureDesc::Create2D(ViewSize, PF_A32B32G32R32F, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef ForegroundDeviceZ = GraphBuilder.CreateTexture(ForegroundDeviceZDesc, TEXT("ThinOutline.ForegroundDeviceZ"));
	FRDGTextureRef ForegroundVelocity = GraphBuilder.CreateTexture(ForegroundVelocityDesc, TEXT("ThinOutline.ForegroundVelocity"));

	{
		FThinOutlinePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FThinOutlinePS::FParameters>();
		PassParameters->View                        = View.ViewUniformBuffer;
		PassParameters->SceneTexturesStruct         = Inputs.SceneTextures.SceneTextures;
		PassParameters->Substrate                   = ViewInfo.SubstrateViewData.SubstrateGlobalUniformParameters;
		PassParameters->EdgeCheck                   = EdgeCheck;
		PassParameters->Input                       = GetScreenPassTextureViewportParameters(InputViewport);
		PassParameters->Output                      = GetScreenPassTextureViewportParameters(OutputViewport);
		PassParameters->InputSceneColorTexture      = SceneColor.Texture;
		PassParameters->HorizontalA                 = Records[HorizontalA];
		PassParameters->HorizontalB                 = Records[HorizontalB];
		PassParameters->VerticalA                   = Records[VerticalA];
		PassParameters->VerticalB                   = Records[VerticalB];
		PassParameters->SilhouetteForeground        = Records[SilhouetteForeground];
		PassParameters->RWForegroundDeviceZ         = GraphBuilder.CreateUAV(ForegroundDeviceZ);
		PassParameters->RWForegroundVelocity        = GraphBuilder.CreateUAV(ForegroundVelocity);
		PassParameters->bDrawSilhouettes            = Settings.bDrawSilhouettes ? 1 : 0;
		PassParameters->bDrawCreases                = Settings.bDrawCreases ? 1 : 0;
		PassParameters->CreaseColor                 = FVector3f(Settings.CreaseColor.R, Settings.CreaseColor.G, Settings.CreaseColor.B);
		PassParameters->SilhouetteColor             = FVector3f(Settings.SilhouetteColor.R, Settings.SilhouetteColor.G, Settings.SilhouetteColor.B);
		PassParameters->SampleLocalPosition         = SampleLocalPosition;
		PassParameters->RenderPixelsPerDisplayPixel = RenderPixelsPerDisplayPixel;
		PassParameters->CreaseThickness             = Settings.CreaseThickness * MeanRenderPixelsPerDisplayPixel;
		PassParameters->SilhouetteThickness         = Settings.SilhouetteThickness * MeanRenderPixelsPerDisplayPixel;
		PassParameters->CoTriggerThreshold          = Settings.CoTriggerThreshold;
		PassParameters->SlopeStandardErrorThreshold = Settings.SlopeStandardErrorThreshold;
		PassParameters->DistinctSampleScale         = FMath::Min(1.0f, Settings.EstimatorDecay * float(FMath::Max(ViewInfo.TemporalJitterSequenceLength, 1)));
		PassParameters->SaturatedSampleCount        = 1.0f / Settings.EstimatorDecay;
		PassParameters->DebugView                   = static_cast<uint32>(FMath::Max(Settings.DebugView, 0));
		PassParameters->RenderTargets[0]            = Output.GetRenderTargetBinding();

		TShaderMapRef<FThinOutlinePS> PixelShader(GetGlobalShaderMap(View.GetFeatureLevel()));

		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("ThinOutline.Composite"),
			View,
			OutputViewport,
			InputViewport,
			PixelShader,
			PassParameters);
	}

	// The pixels painted with a silhouette take their foreground's depth and velocity, in the textures the temporal
	// upscaler reads later (built-in and third party upscalers get the same ones), so it moves the outline with the
	// foreground. Depth of field and motion blur see them too.
	const FSceneTextureUniformParameters* SceneTextureParameters = Inputs.SceneTextures.SceneTextures->GetContents();
	FRDGTextureRef SceneDepthTexture = SceneTextureParameters->SceneDepthTexture;
	FRDGTextureRef SceneVelocityTexture = SceneTextureParameters->GBufferVelocityTexture;

	if (SceneDepthTexture && EnumHasAnyFlags(SceneDepthTexture->Desc.Flags, TexCreate_DepthStencilTargetable))
	{
		// Without a velocity pass the velocity texture is a system dummy, and the upscaler derives the velocity of every
		// pixel from its depth and the camera motion, which the overwritten depth already takes care of.
		const bool bWriteVelocity = SceneVelocityTexture
			&& EnumHasAnyFlags(SceneVelocityTexture->Desc.Flags, TexCreate_RenderTargetable)
			&& SceneVelocityTexture->Desc.Extent == SceneDepthTexture->Desc.Extent;

		FThinOutlineForegroundPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FThinOutlineForegroundPS::FWriteVelocityDim>(bWriteVelocity);
		TShaderMapRef<FThinOutlineForegroundPS> PixelShader(GetGlobalShaderMap(View.GetFeatureLevel()), PermutationVector);

		FThinOutlineForegroundPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FThinOutlineForegroundPS::FParameters>();
		PassParameters->ForegroundDeviceZ  = ForegroundDeviceZ;
		PassParameters->ForegroundVelocity = ForegroundVelocity;
		PassParameters->ViewRectMin        = ViewInfo.ViewRect.Min;
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			SceneDepthTexture,
			ERenderTargetLoadAction::ELoad,
			SceneDepthTexture->Desc.Format == PF_DepthStencil ? ERenderTargetLoadAction::ELoad : ERenderTargetLoadAction::ENoAction,
			FExclusiveDepthStencil::DepthWrite_StencilNop);
		if (bWriteVelocity)
		{
			PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneVelocityTexture, ERenderTargetLoadAction::ELoad);
		}

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			GetGlobalShaderMap(View.GetFeatureLevel()),
			RDG_EVENT_NAME("ThinOutline.ForegroundDepthVelocity"),
			PixelShader,
			PassParameters,
			ViewInfo.ViewRect,
			nullptr,
			nullptr,
			TStaticDepthStencilState<true, CF_Always>::GetRHI());
	}

	return MoveTemp(Output);
}

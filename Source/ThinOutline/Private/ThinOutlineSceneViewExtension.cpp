// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThinOutlineSceneViewExtension.h"
#include "ThinOutlineCVars.h"
#include "ThinOutlineSettings.h"
#include "ThinOutlineShaders.h"

#include "CommonRenderResources.h"
#include "PipelineStateCache.h"
#include "PixelFormat.h"
#include "PixelShaderUtils.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBlackboard.h"
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

	/** This frame's edge records of a view, for the composite after the temporal upscaler. */
	struct FViewRecords
	{
		const FSceneView* View = nullptr;
		FRDGTextureRef Textures[EThinOutlineHistoryTexture::Num] = {};
		/** r.ThinOutline.TileClassification: the records' positions per 8x8 group, at 2x2 blocks (FThinOutlineRecordMaskCS). */
		FRDGTextureRef RecordMask = nullptr;
		/** The edge each rendering pixel draws (FThinOutlineEdgeCS). */
		FRDGTextureRef DrawnEdge = nullptr;
	};

	/** Render graph blackboard entry: the records the BeforeDOF pass leaves for the MotionBlur after-pass of the same graph. */
	struct FFrameRecords
	{
		TArray<FViewRecords, TInlineAllocator<2>> Views;
	};

	FThinOutlineEdgeCheckParameters GetEdgeCheckParameters(const FThinOutlineRenderSettings& Settings)
	{
		FThinOutlineEdgeCheckParameters EdgeCheck;
		EdgeCheck.SilhouetteThreshold    = Settings.SilhouetteThreshold;
		EdgeCheck.SilhouetteScale        = Settings.SilhouetteScale;
		EdgeCheck.bSymmetricDepthMeasure = Settings.bSilhouetteSymmetricMeasure ? 1 : 0;
		EdgeCheck.CreaseRidgeThreshold   = Settings.CreaseRidgeThreshold;
		EdgeCheck.CreaseValleyThreshold  = Settings.CreaseValleyThreshold;
		EdgeCheck.CreaseScale            = Settings.CreaseScale;
		EdgeCheck.bCreaseSpikeFilter     = Settings.bCreaseSpikeFilter ? 1 : 0;
		EdgeCheck.CreaseSpikeThreshold   = Settings.CreaseSpikeThreshold;
		EdgeCheck.CreaseSpikePlaneTolerance = Settings.CreaseSpikePlaneTolerance;
		return EdgeCheck;
	}

	/** DisplaySize: size of the temporal upscaler's output, whose pixels the outline thicknesses are given in. */
	FThinOutlineCompositeParameters GetCompositeParameters(
		const FThinOutlineRenderSettings& Settings,
		const FViewInfo& ViewInfo,
		const FRDGTextureRef (&Records)[EThinOutlineHistoryTexture::Num],
		FIntPoint DisplaySize)
	{
		const FIntPoint ViewSize = ViewInfo.ViewRect.Size();
		const FVector2f RenderPixelsPerDisplayPixel(
			float(ViewSize.X) / float(FMath::Max(DisplaySize.X, 1)),
			float(ViewSize.Y) / float(FMath::Max(DisplaySize.Y, 1)));
		const float MeanRenderPixelsPerDisplayPixel = 0.5f * (RenderPixelsPerDisplayPixel.X + RenderPixelsPerDisplayPixel.Y);

		FThinOutlineCompositeParameters Parameters;
		Parameters.HorizontalA                 = Records[EThinOutlineHistoryTexture::HorizontalA];
		Parameters.HorizontalB                 = Records[EThinOutlineHistoryTexture::HorizontalB];
		Parameters.VerticalA                   = Records[EThinOutlineHistoryTexture::VerticalA];
		Parameters.VerticalB                   = Records[EThinOutlineHistoryTexture::VerticalB];
		Parameters.SilhouetteForeground        = Records[EThinOutlineHistoryTexture::SilhouetteForeground];
		Parameters.bDrawSilhouettes            = Settings.bDrawSilhouettes ? 1 : 0;
		Parameters.bDrawCreases                = Settings.bDrawCreases ? 1 : 0;
		Parameters.CreaseColor                 = FVector3f(Settings.CreaseColor.R, Settings.CreaseColor.G, Settings.CreaseColor.B);
		Parameters.SilhouetteColor             = FVector3f(Settings.SilhouetteColor.R, Settings.SilhouetteColor.G, Settings.SilhouetteColor.B);
		Parameters.RenderPixelsPerDisplayPixel = RenderPixelsPerDisplayPixel;
		Parameters.CreaseThickness             = Settings.CreaseThickness * MeanRenderPixelsPerDisplayPixel;
		Parameters.SilhouetteThickness         = Settings.SilhouetteThickness * MeanRenderPixelsPerDisplayPixel;
		Parameters.FadeLimit                   = float(Settings.FadeFrames);
		Parameters.SlopeStandardErrorThreshold = Settings.SlopeStandardErrorThreshold;
		Parameters.bSpatialFilter              = Settings.bSpatialFilter ? 1 : 0;
		Parameters.SpatialFilterSigma          = Settings.SpatialFilterSigma;
		Parameters.bAxisBlend                  = Settings.bAxisBlend ? 1 : 0;
		Parameters.AxisBlendScale              = Settings.AxisBlendScale;
		Parameters.DenseEdgeSuppression        = static_cast<uint32>(Settings.DenseEdgeSuppression);
		Parameters.DenseEdgeTolerance          = Settings.DenseEdgeTolerance;
		Parameters.bIsolatedEdgeSuppression    = Settings.bIsolatedEdgeSuppression ? 1 : 0;
		Parameters.bCreasePresence             = Settings.bCreasePresence ? 1 : 0;
		Parameters.CreasePresenceDrawMin       = Settings.CreasePresenceDrawMin;
		Parameters.CreasePresenceDrawMax       = Settings.CreasePresenceDrawMax;
		Parameters.DistinctSampleScale         = FMath::Min(1.0f, Settings.EstimatorDecay * float(FMath::Max(ViewInfo.TemporalJitterSequenceLength, 1)));
		Parameters.SaturatedSampleCount        = 1.0f / Settings.EstimatorDecay;
		Parameters.DebugView                   = static_cast<uint32>(FMath::Max(Settings.DebugView, 0));
		return Parameters;
	}

	/**
	 * r.ThinOutline.TileClassification: where this frame's records are, per 8x8 group of rendering pixels at 2x2 blocks
	 * (FThinOutlineRecordMaskCS), so that the composites can find the tiles with records within their reach without
	 * reading the records themselves.
	 */
	FRDGTextureRef AddRecordMaskPass(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& ViewInfo,
		const FRDGTextureRef (&Records)[EThinOutlineHistoryTexture::Num])
	{
		using namespace EThinOutlineHistoryTexture;

		const FIntPoint ViewSize = ViewInfo.ViewRect.Size();
		const FIntPoint GroupCount = FIntPoint::DivideAndRoundUp(ViewSize, FThinOutlineRecordMaskCS::TileSize);
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(GroupCount, PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef RecordMask = GraphBuilder.CreateTexture(Desc, TEXT("ThinOutline.RecordMask"));

		FThinOutlineRecordMaskCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FThinOutlineRecordMaskCS::FParameters>();
		PassParameters->View         = ViewInfo.ViewUniformBuffer;
		PassParameters->HorizontalA  = Records[HorizontalA];
		PassParameters->VerticalA    = Records[VerticalA];
		PassParameters->RWRecordMask = GraphBuilder.CreateUAV(RecordMask);

		TShaderMapRef<FThinOutlineRecordMaskCS> ComputeShader(GetGlobalShaderMap(ViewInfo.GetFeatureLevel()));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ThinOutline.RecordMask %dx%d", GroupCount.X, GroupCount.Y),
			ComputeShader,
			PassParameters,
			FIntVector(GroupCount.X, GroupCount.Y, 1));
		return RecordMask;
	}

	/** The tiles of a composite pass that have records within reach, and the indirect arguments of its dispatch and of the overwrite draw. */
	struct FTileLists
	{
		FRDGBufferRef TileList = nullptr;
		FRDGBufferRef DispatchArgs = nullptr;
		FRDGBufferRef DrawArgs = nullptr;
	};

	/**
	 * r.ThinOutline.TileClassification: lists the tiles of TargetSize (display pixels after the upscaler, rendering pixels
	 * before it) that have records within reach, from the record mask (FThinOutlineTileClassifyCS), and turns their
	 * count into indirect arguments (FThinOutlineTileSetupCS), so nothing is read back to the CPU.
	 */
	FTileLists AddTileListPasses(FRDGBuilder& GraphBuilder, const FViewInfo& ViewInfo, FRDGTextureRef RecordMask, FIntPoint TargetSize)
	{
		const FIntPoint ViewSize = ViewInfo.ViewRect.Size();
		const FIntPoint TileCount = FIntPoint::DivideAndRoundUp(TargetSize, FThinOutlineRecordMaskCS::TileSize);
		const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ViewInfo.GetFeatureLevel());

		FTileLists Lists;
		FRDGBufferRef TileCounter = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("ThinOutline.TileCounter"));
		Lists.TileList = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), TileCount.X * TileCount.Y), TEXT("ThinOutline.TileList"));
		Lists.DispatchArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("ThinOutline.TileIndirectArgs"));
		Lists.DrawArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(1), TEXT("ThinOutline.TileDrawIndirectArgs"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(TileCounter), 0u);

		{
			FThinOutlineTileClassifyCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FThinOutlineTileClassifyCS::FParameters>();
			PassParameters->View                        = ViewInfo.ViewUniformBuffer;
			PassParameters->RecordMask                  = RecordMask;
			PassParameters->TileCount                   = FUintVector2(uint32(TileCount.X), uint32(TileCount.Y));
			PassParameters->RenderPixelsPerDisplayPixel = FVector2f(
				float(ViewSize.X) / float(FMath::Max(TargetSize.X, 1)),
				float(ViewSize.Y) / float(FMath::Max(TargetSize.Y, 1)));
			PassParameters->RWTileCounter               = GraphBuilder.CreateUAV(TileCounter);
			PassParameters->RWTileList                  = GraphBuilder.CreateUAV(Lists.TileList);

			TShaderMapRef<FThinOutlineTileClassifyCS> ComputeShader(ShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ThinOutline.TileClassify %dx%d", TileCount.X, TileCount.Y),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(TileCount, FThinOutlineTileClassifyCS::ThreadGroupSize));
		}

		{
			FThinOutlineTileSetupCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FThinOutlineTileSetupCS::FParameters>();
			PassParameters->TileCounter            = GraphBuilder.CreateSRV(TileCounter);
			PassParameters->RWTileIndirectArgs     = GraphBuilder.CreateUAV(Lists.DispatchArgs, PF_R32_UINT);
			PassParameters->RWTileDrawIndirectArgs = GraphBuilder.CreateUAV(Lists.DrawArgs, PF_R32_UINT);

			TShaderMapRef<FThinOutlineTileSetupCS> ComputeShader(ShaderMap);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("ThinOutline.TileSetup"), ComputeShader, PassParameters, FIntVector(1, 1, 1));
		}
		return Lists;
	}

	/**
	 * The edge pass (FThinOutlineEdgeCS): the edge each rendering pixel draws, reconstructed from the records once per
	 * rendering pixel for the composites of both modes. On the listed rendering tiles when there is a tile list, else
	 * on every tile of the view; the texture is cleared first, so the pixels of tiles that do not run draw nothing.
	 */
	FRDGTextureRef AddEdgePass(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& ViewInfo,
		const FPostProcessMaterialInputs& Inputs,
		const FThinOutlineRenderSettings& Settings,
		const FRDGTextureRef (&Records)[EThinOutlineHistoryTexture::Num],
		FIntPoint DisplaySize,
		const FTileLists* TileLists)
	{
		const FIntPoint ViewSize = ViewInfo.ViewRect.Size();
		const FIntPoint TileCount = FIntPoint::DivideAndRoundUp(ViewSize, FThinOutlineRecordMaskCS::TileSize);

		FRDGTextureRef DrawnEdge = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(ViewSize, PF_A32B32G32R32F, FClearValueBinding::Transparent, TexCreate_ShaderResource | TexCreate_UAV), TEXT("ThinOutline.DrawnEdge"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(DrawnEdge), FLinearColor::Transparent);
		FThinOutlineEdgeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FThinOutlineEdgeCS::FParameters>();
		PassParameters->View                   = ViewInfo.ViewUniformBuffer;
		PassParameters->SceneTexturesStruct    = Inputs.SceneTextures.SceneTextures;
		PassParameters->Substrate              = ViewInfo.SubstrateViewData.SubstrateGlobalUniformParameters;
		PassParameters->EdgeCheck              = GetEdgeCheckParameters(Settings);
		PassParameters->Composite              = GetCompositeParameters(Settings, ViewInfo, Records, DisplaySize);
		PassParameters->TileList               = GraphBuilder.CreateSRV(TileLists ? TileLists->TileList : GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(uint32)));
		PassParameters->bUseTileList           = TileLists ? 1 : 0;
		PassParameters->TileCountX             = uint32(TileCount.X);
		PassParameters->RWDrawnEdge            = GraphBuilder.CreateUAV(DrawnEdge);

		TShaderMapRef<FThinOutlineEdgeCS> ComputeShader(GetGlobalShaderMap(ViewInfo.GetFeatureLevel()));
		if (TileLists)
		{
			PassParameters->TileIndirectArgs = TileLists->DispatchArgs;
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("ThinOutline.Edges(Tiles)"), ComputeShader, PassParameters, TileLists->DispatchArgs, 0);
		}
		else
		{
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ThinOutline.Edges %dx%d", TileCount.X, TileCount.Y),
				ComputeShader,
				PassParameters,
				FIntVector(TileCount.X * TileCount.Y, 1, 1));
		}
		return DrawnEdge;
	}

	/**
	 * Whether a composite can run on tiles, in place in the scene color through a UAV: a UAV-capable scene color whose
	 * format supports typed UAV loads, no override output, and a view drawn per tile (debug views 1, 2 and 4 show every
	 * pixel).
	 */
	bool CanCompositeTiles(const FThinOutlineRenderSettings& Settings, const FPostProcessMaterialInputs& Inputs, const FScreenPassTexture& SceneColor)
	{
		const bool bPerPixelDebugView = Settings.DebugView == 1 || Settings.DebugView == 2 || Settings.DebugView == 4 || Settings.DebugView == 9;
		return Settings.bTileClassification
			&& !bPerPixelDebugView
			&& !Inputs.OverrideOutput.IsValid()
			&& EnumHasAnyFlags(SceneColor.Texture->Desc.Flags, TexCreate_UAV)
			&& UE::PixelFormat::HasCapabilities(SceneColor.Texture->Desc.Format, EPixelFormatCapabilities::TypedUAVLoad);
	}

	/** Debug views 3, 5 and 7 show the outline alpha on black: the tiles write theirs, the rest stays cleared. */
	FRDGTextureUAVRef CreateTileSceneColorUAV(FRDGBuilder& GraphBuilder, const FThinOutlineRenderSettings& Settings, const FScreenPassTexture& SceneColor)
	{
		FRDGTextureUAVRef SceneColorUAV = GraphBuilder.CreateUAV(SceneColor.Texture);
		if (Settings.DebugView == 3 || Settings.DebugView == 5 || Settings.DebugView == 7)
		{
			AddClearUAVPass(GraphBuilder, SceneColorUAV, FLinearColor::Black);
		}
		return SceneColorUAV;
	}

	/**
	 * r.ThinOutline.TileClassification: the composite after the upscaler on the display tiles that have records within
	 * reach only, one indirect dispatch drawing them in place in the scene color (FThinOutlineAfterUpscalerCS).
	 */
	void AddAfterUpscalerTilePasses(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& ViewInfo,
		const FPostProcessMaterialInputs& Inputs,
		const FThinOutlineRenderSettings& Settings,
		const FViewRecords& ViewRecords,
		const FScreenPassTexture& SceneColor)
	{
		const FIntPoint DisplaySize = SceneColor.ViewRect.Size();
		const FTileLists Lists = AddTileListPasses(GraphBuilder, ViewInfo, ViewRecords.RecordMask, DisplaySize);
		const FScreenPassTextureViewportParameters ViewportParameters = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(SceneColor));

		FThinOutlineAfterUpscalerCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FThinOutlineAfterUpscalerCS::FParameters>();
		PassParameters->View                = ViewInfo.ViewUniformBuffer;
		PassParameters->SceneTexturesStruct = Inputs.SceneTextures.SceneTextures;
		PassParameters->Substrate           = ViewInfo.SubstrateViewData.SubstrateGlobalUniformParameters;
		PassParameters->EdgeCheck           = GetEdgeCheckParameters(Settings);
		PassParameters->Composite           = GetCompositeParameters(Settings, ViewInfo, ViewRecords.Textures, DisplaySize);
		PassParameters->DrawnEdge           = ViewRecords.DrawnEdge;
		PassParameters->Input               = ViewportParameters;
		PassParameters->Output              = ViewportParameters;
		PassParameters->TileList            = GraphBuilder.CreateSRV(Lists.TileList);
		PassParameters->RWSceneColor        = CreateTileSceneColorUAV(GraphBuilder, Settings, SceneColor);
		PassParameters->TileIndirectArgs    = Lists.DispatchArgs;

		TShaderMapRef<FThinOutlineAfterUpscalerCS> ComputeShader(GetGlobalShaderMap(ViewInfo.GetFeatureLevel()));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ThinOutline.CompositeAfterUpscaler(Tiles)"),
			ComputeShader,
			PassParameters,
			Lists.DispatchArgs,
			0);
	}
}

RDG_REGISTER_BLACKBOARD_STRUCT(ThinOutline::FFrameRecords);

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
	Settings.bDrawAfterUpscaler                  = CVarThinOutlineDrawAfterUpscaler.GetValueOnGameThread() != 0;
	Settings.bTileClassification                 = CVarThinOutlineTileClassification.GetValueOnGameThread() != 0;
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
	Settings.FadeFrames                          = FMath::Max(1, CVarThinOutlineEstimatorFadeFrames.GetValueOnGameThread());
	Settings.FadeMaxSpeed                        = FMath::Max(0.0f, CVarThinOutlineEstimatorFadeMaxSpeed.GetValueOnGameThread());
	Settings.SlopeStandardErrorThreshold         = FMath::Max(0.0f, CVarThinOutlineEstimatorSlopeSEThreshold.GetValueOnGameThread());
	Settings.CreaseHistoryDepthThreshold         = FMath::Max(0.0f, CVarThinOutlineCreaseHistoryDepthThreshold.GetValueOnGameThread());
	// At most 1: a crease sample then always counts as a crease found.
	Settings.CreaseHistoryCreaseTestThreshold    = FMath::Clamp(CVarThinOutlineCreaseHistoryCreaseTestThreshold.GetValueOnGameThread(), 0.0f, 1.0f);
	Settings.bCreasePresence                     = CVarThinOutlineCreasePresence.GetValueOnGameThread() != 0;
	Settings.CreasePresenceFrames                = FMath::Max(1.0f, CVarThinOutlineCreasePresenceFrames.GetValueOnGameThread());
	Settings.CreasePresenceDrawMin               = FMath::Clamp(CVarThinOutlineCreasePresenceDrawMin.GetValueOnGameThread(), 0.0f, 1.0f);
	Settings.CreasePresenceDrawMax               = FMath::Clamp(CVarThinOutlineCreasePresenceDrawMax.GetValueOnGameThread(), 0.0f, 1.0f);
	Settings.CreasePresenceDropLevel             = FMath::Clamp(CVarThinOutlineCreasePresenceDropLevel.GetValueOnGameThread(), 0.0f, 1.0f);
	Settings.bCreaseSpikeFilter                  = CVarThinOutlineCreaseSpikeFilter.GetValueOnGameThread() != 0;
	Settings.CreaseSpikeThreshold                = FMath::Max(0.0f, CVarThinOutlineCreaseSpikeThreshold.GetValueOnGameThread());
	Settings.CreaseSpikePlaneTolerance           = FMath::Max(0.0f, CVarThinOutlineCreaseSpikePlaneTolerance.GetValueOnGameThread());
	Settings.bSpatialFilter                      = CVarThinOutlineSpatialFilter.GetValueOnGameThread() != 0;
	Settings.SpatialFilterSigma                  = FMath::Max(0.001f, CVarThinOutlineSpatialFilterSigma.GetValueOnGameThread());
	Settings.bAxisBlend                          = CVarThinOutlineAxisBlend.GetValueOnGameThread() != 0;
	Settings.AxisBlendScale                      = FMath::Max(0.001f, CVarThinOutlineAxisBlendScale.GetValueOnGameThread());
	Settings.DenseEdgeSuppression                = FMath::Clamp(CVarThinOutlineDenseEdgeSuppression.GetValueOnGameThread(), 0, 2);
	Settings.DenseEdgeTolerance                  = FMath::Max(0.0f, CVarThinOutlineDenseEdgeTolerance.GetValueOnGameThread());
	Settings.bIsolatedEdgeSuppression            = CVarThinOutlineIsolatedEdgeSuppression.GetValueOnGameThread() != 0;
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
	// BeforeDOF runs at rendering resolution, before the temporal upscaler. The MotionBlur after-pass runs after it, at
	// display resolution, also when motion blur itself is off. (Called on the render thread, with this family's settings.)
	const bool bAfterUpscalerPass = Pass == EPostProcessingPass::MotionBlur && RenderSettings_RenderThread.bDrawAfterUpscaler;
	if (Pass != EPostProcessingPass::BeforeDOF && !bAfterUpscalerPass)
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

	InOutPassCallbacks.Add(bAfterUpscalerPass
		? FPostProcessingPassDelegate::CreateRaw(this, &FThinOutlineSceneViewExtension::AddAfterUpscalerPass_RenderThread)
		: FPostProcessingPassDelegate::CreateRaw(this, &FThinOutlineSceneViewExtension::AddOutlinePass_RenderThread));
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

	const FThinOutlineEdgeCheckParameters EdgeCheck = ThinOutline::GetEdgeCheckParameters(Settings);

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

	// r.ThinOutline.TileClassification: last frame's record mask, so the record pass skips the history fetch where
	// there is nothing to fetch. Only with a valid history (the mask was extracted with the records).
	const bool bHistoryMaskValid = bHistoryValid && History->RecordMask.IsValid();

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
		PassParameters->HistoryRecordMask                   = bHistoryMaskValid ? GraphBuilder.RegisterExternalTexture(History->RecordMask) : GSystemTextures.GetZeroUIntDummy(GraphBuilder);
		PassParameters->bHistoryMaskValid                   = bHistoryMaskValid ? 1 : 0;
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
		PassParameters->FadeLimit                           = float(Settings.FadeFrames);
		PassParameters->FadeMaxSpeed                        = Settings.FadeMaxSpeed;
		PassParameters->CreaseHistoryKeepRidgeThreshold     = Settings.CreaseHistoryCreaseTestThreshold * Settings.CreaseRidgeThreshold;
		PassParameters->CreaseHistoryKeepValleyThreshold    = Settings.CreaseHistoryCreaseTestThreshold * Settings.CreaseValleyThreshold;
		PassParameters->CreasePresenceRate                  = 1.0f / Settings.CreasePresenceFrames;
		PassParameters->CreasePresenceDropLevel             = Settings.CreasePresenceDropLevel;
		PassParameters->bHistoryValid                       = bHistoryValid ? 1 : 0;
		PassParameters->HistoryReprojectionMode             = static_cast<uint32>(Settings.HistoryReprojection);

		FThinOutlineRecordCS::FPermutationDomain PermutationVector;
		const bool bViewAngleTest = Settings.SilhouetteHistoryViewAngle > 0.0f;
		PermutationVector.Set<FThinOutlineRecordCS::FBackgroundDepthStepDim>(Settings.bSilhouetteHistoryBackgroundDepthStep);
		PermutationVector.Set<FThinOutlineRecordCS::FCreasePresenceDim>(Settings.bCreasePresence);
		PermutationVector.Set<FThinOutlineRecordCS::FCreaseSpikeFilterDim>(Settings.bCreaseSpikeFilter);
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

	// r.ThinOutline.TileClassification: where this frame's records are, for the composites' tile lists and for the next
	// frame's history fetch.
	FRDGTextureRef RecordMask = Settings.bTileClassification ? ThinOutline::AddRecordMaskPass(GraphBuilder, ViewInfo, Records) : nullptr;

	// Display pixels are the output pixels of the temporal upscaler; without one, temporal AA runs at rendering resolution.
	const FIntPoint DisplaySize = View.PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale
		? ViewInfo.GetSecondaryViewRectSize()
		: ViewSize;

	// The edge each rendering pixel draws, on the rendering tiles with records within reach (the list also serves
	// mode 0's composite and overwrite).
	ThinOutline::FTileLists TileLists;
	const bool bRenderTiles = RecordMask != nullptr;
	if (bRenderTiles)
	{
		TileLists = ThinOutline::AddTileListPasses(GraphBuilder, ViewInfo, RecordMask, ViewSize);
	}
	FRDGTextureRef DrawnEdge = ThinOutline::AddEdgePass(GraphBuilder, ViewInfo, Inputs, Settings, Records, DisplaySize, bRenderTiles ? &TileLists : nullptr);

	// Views that must not advance the view state's temporal history only read it.
	if (History && !ViewInfo.bStatePrevViewInfoIsReadOnly)
	{
		for (int32 Index = 0; Index < Num; ++Index)
		{
			GraphBuilder.QueueTextureExtraction(Records[Index], &History->Textures[Index]);
		}
		if (RecordMask)
		{
			GraphBuilder.QueueTextureExtraction(RecordMask, &History->RecordMask);
		}
		else
		{
			History->RecordMask = nullptr;
		}
		History->ViewSize = ViewSize;
		History->ViewStateFrameIndex = ViewStateFrameIndex;
	}

	if (Settings.bDrawAfterUpscaler)
	{
		ThinOutline::FViewRecords& ViewRecords = GraphBuilder.Blackboard.GetOrCreate<ThinOutline::FFrameRecords>().Views.AddDefaulted_GetRef();
		ViewRecords.View = &View;
		for (int32 Index = 0; Index < Num; ++Index)
		{
			ViewRecords.Textures[Index] = Records[Index];
		}
		ViewRecords.RecordMask = RecordMask;
		ViewRecords.DrawnEdge = DrawnEdge;
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	// Depth and encoded velocity of the foreground of the pixels painted with a silhouette.
	const FRDGTextureDesc ForegroundDeviceZDesc = FRDGTextureDesc::Create2D(ViewSize, PF_R32_FLOAT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
	const FRDGTextureDesc ForegroundVelocityDesc = FRDGTextureDesc::Create2D(ViewSize, PF_A32B32G32R32F, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef ForegroundDeviceZ = GraphBuilder.CreateTexture(ForegroundDeviceZDesc, TEXT("ThinOutline.ForegroundDeviceZ"));
	FRDGTextureRef ForegroundVelocity = GraphBuilder.CreateTexture(ForegroundVelocityDesc, TEXT("ThinOutline.ForegroundVelocity"));

	// Edge reconstruction and composite. With r.ThinOutline.TileClassification, only on the rendering tiles that have
	// records within reach, in place in the scene color; the overwrite then draws those tiles only (every pixel of a
	// listed tile gets its foreground values, the other tiles are never read).
	const bool bTileClassification = bRenderTiles && ThinOutline::CanCompositeTiles(Settings, Inputs, SceneColor);
	FScreenPassTexture Output;

	if (bTileClassification)
	{
		const FScreenPassTextureViewportParameters ViewportParameters = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(SceneColor));

		FThinOutlineCompositeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FThinOutlineCompositeCS::FParameters>();
		PassParameters->View                 = View.ViewUniformBuffer;
		PassParameters->SceneTexturesStruct  = Inputs.SceneTextures.SceneTextures;
		PassParameters->Substrate            = ViewInfo.SubstrateViewData.SubstrateGlobalUniformParameters;
		PassParameters->EdgeCheck            = EdgeCheck;
		PassParameters->Composite            = ThinOutline::GetCompositeParameters(Settings, ViewInfo, Records, DisplaySize);
		PassParameters->DrawnEdge            = DrawnEdge;
		PassParameters->Input                = ViewportParameters;
		PassParameters->Output               = ViewportParameters;
		PassParameters->RWForegroundDeviceZ  = GraphBuilder.CreateUAV(ForegroundDeviceZ);
		PassParameters->RWForegroundVelocity = GraphBuilder.CreateUAV(ForegroundVelocity);
		PassParameters->SampleLocalPosition  = SampleLocalPosition;
		PassParameters->TileList             = GraphBuilder.CreateSRV(TileLists.TileList);
		PassParameters->RWSceneColor         = ThinOutline::CreateTileSceneColorUAV(GraphBuilder, Settings, SceneColor);
		PassParameters->TileIndirectArgs     = TileLists.DispatchArgs;

		TShaderMapRef<FThinOutlineCompositeCS> ComputeShader(GetGlobalShaderMap(View.GetFeatureLevel()));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ThinOutline.Composite(Tiles)"),
			ComputeShader,
			PassParameters,
			TileLists.DispatchArgs,
			0);
		Output = SceneColor;
	}
	else
	{
		FScreenPassRenderTarget OutputTarget = Inputs.OverrideOutput;
		if (!OutputTarget.IsValid())
		{
			OutputTarget = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, SceneColor, View.GetOverwriteLoadAction(), TEXT("ThinOutline.SceneColor"));
		}

		const FScreenPassTextureViewport InputViewport(SceneColor);
		const FScreenPassTextureViewport OutputViewport(OutputTarget);

		FThinOutlinePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FThinOutlinePS::FParameters>();
		PassParameters->View                        = View.ViewUniformBuffer;
		PassParameters->SceneTexturesStruct         = Inputs.SceneTextures.SceneTextures;
		PassParameters->Substrate                   = ViewInfo.SubstrateViewData.SubstrateGlobalUniformParameters;
		PassParameters->EdgeCheck                   = EdgeCheck;
		PassParameters->Composite                   = ThinOutline::GetCompositeParameters(Settings, ViewInfo, Records, DisplaySize);
		PassParameters->DrawnEdge                   = DrawnEdge;
		PassParameters->Input                       = GetScreenPassTextureViewportParameters(InputViewport);
		PassParameters->Output                      = GetScreenPassTextureViewportParameters(OutputViewport);
		PassParameters->InputSceneColorTexture      = SceneColor.Texture;
		PassParameters->RWForegroundDeviceZ         = GraphBuilder.CreateUAV(ForegroundDeviceZ);
		PassParameters->RWForegroundVelocity        = GraphBuilder.CreateUAV(ForegroundVelocity);
		PassParameters->SampleLocalPosition         = SampleLocalPosition;
		PassParameters->RenderTargets[0]            = OutputTarget.GetRenderTargetBinding();

		TShaderMapRef<FThinOutlinePS> PixelShader(GetGlobalShaderMap(View.GetFeatureLevel()));

		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("ThinOutline.Composite"),
			View,
			OutputViewport,
			InputViewport,
			PixelShader,
			PassParameters);
		Output = OutputTarget;
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

		if (bTileClassification)
		{
			// One quad per listed tile, placed by FThinOutlineTileVS, with the pixel shader and its states as the full-screen
			// pass below. Writing SV_Depth costs every fragment drawn, so the 86% or so of empty tiles are not drawn at all.
			PassParameters->VS.View             = View.ViewUniformBuffer;
			PassParameters->VS.TileList         = GraphBuilder.CreateSRV(TileLists.TileList);
			PassParameters->TileDrawIndirectArgs = TileLists.DrawArgs;

			TShaderMapRef<FThinOutlineTileVS> VertexShader(GetGlobalShaderMap(View.GetFeatureLevel()));
			const FIntRect ViewRect = ViewInfo.ViewRect;
			FRDGBufferRef DrawArgs = TileLists.DrawArgs;
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ThinOutline.ForegroundDepthVelocity(Tiles)"),
				PassParameters,
				ERDGPassFlags::Raster,
				[PassParameters, VertexShader, PixelShader, ViewRect, DrawArgs](FRDGAsyncTask, FRHICommandList& RHICmdList)
				{
					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
					GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_Always>::GetRHI();
					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					GraphicsPSOInit.PrimitiveType = PT_TriangleList;
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
					SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VS);
					SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);
					RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);
					RHICmdList.SetStreamSource(0, nullptr, 0);
					DrawArgs->MarkResourceAsUsed();
					RHICmdList.DrawPrimitiveIndirect(DrawArgs->GetIndirectRHICallBuffer(), 0);
				});
		}
		else
		{
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
	}

	return Output;
}

FScreenPassTexture FThinOutlineSceneViewExtension::AddAfterUpscalerPass_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	const ThinOutline::FFrameRecords* FrameRecords = GraphBuilder.Blackboard.Get<ThinOutline::FFrameRecords>();
	const ThinOutline::FViewRecords* ViewRecords = FrameRecords
		? FrameRecords->Views.FindByPredicate([&View](const ThinOutline::FViewRecords& Entry) { return Entry.View == &View; })
		: nullptr;
	if (!SceneColor.IsValid() || !Inputs.SceneTextures.SceneTextures || !ViewRecords)
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(View);
	const FThinOutlineRenderSettings& Settings = RenderSettings_RenderThread;

	// The temporal upscaler's output; without one, the view rect.
	const FIntPoint DisplaySize = SceneColor.ViewRect.Size();

	RDG_EVENT_SCOPE_STAT(GraphBuilder, ThinOutline, "ThinOutline.AfterUpscaler %dx%d", DisplaySize.X, DisplaySize.Y);

	// r.ThinOutline.TileClassification: only the display tiles with records within reach are drawn, in place in the scene
	// color through a UAV (the upscaler's output has no render target flag, so the tiles cannot be drawn with blending
	// either).
	if (ViewRecords->RecordMask && ThinOutline::CanCompositeTiles(Settings, Inputs, SceneColor))
	{
		ThinOutline::AddAfterUpscalerTilePasses(GraphBuilder, ViewInfo, Inputs, Settings, *ViewRecords, SceneColor);
		return SceneColor;
	}

	FScreenPassRenderTarget Output = Inputs.OverrideOutput;
	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, SceneColor, View.GetOverwriteLoadAction(), TEXT("ThinOutline.SceneColor"));
	}

	const FScreenPassTextureViewport InputViewport(SceneColor);
	const FScreenPassTextureViewport OutputViewport(Output);

	FThinOutlineAfterUpscalerPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FThinOutlineAfterUpscalerPS::FParameters>();
	PassParameters->View                   = View.ViewUniformBuffer;
	PassParameters->SceneTexturesStruct    = Inputs.SceneTextures.SceneTextures;
	PassParameters->Substrate              = ViewInfo.SubstrateViewData.SubstrateGlobalUniformParameters;
	PassParameters->EdgeCheck              = ThinOutline::GetEdgeCheckParameters(Settings);
	PassParameters->Composite              = ThinOutline::GetCompositeParameters(Settings, ViewInfo, ViewRecords->Textures, DisplaySize);
	PassParameters->DrawnEdge              = ViewRecords->DrawnEdge;
	PassParameters->Input                  = GetScreenPassTextureViewportParameters(InputViewport);
	PassParameters->Output                 = GetScreenPassTextureViewportParameters(OutputViewport);
	PassParameters->InputSceneColorTexture = SceneColor.Texture;
	PassParameters->RenderTargets[0]       = Output.GetRenderTargetBinding();

	TShaderMapRef<FThinOutlineAfterUpscalerPS> PixelShader(GetGlobalShaderMap(View.GetFeatureLevel()));

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("ThinOutline.CompositeAfterUpscaler"),
		View,
		OutputViewport,
		InputViewport,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}

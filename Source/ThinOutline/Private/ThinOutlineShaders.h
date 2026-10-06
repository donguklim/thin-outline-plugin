// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "SceneTexturesConfig.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"
#include "Substrate/Substrate.h"

// Thresholds of the G-buffer edge checks (ThinOutlineCommon.ush).
BEGIN_SHADER_PARAMETER_STRUCT(FThinOutlineEdgeCheckParameters, )
	SHADER_PARAMETER(float, SilhouetteThreshold)
	SHADER_PARAMETER(float, SilhouetteScale)
	SHADER_PARAMETER(uint32, bSymmetricDepthMeasure)
	SHADER_PARAMETER(float, CreaseRidgeThreshold)
	SHADER_PARAMETER(float, CreaseValleyThreshold)
	SHADER_PARAMETER(float, CreaseScale)
END_SHADER_PARAMETER_STRUCT()

// Adds this frame's jittered edge check results to the per-pixel edge records, carrying the records over from the
// previous frame by reprojection.
class FThinOutlineRecordCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FThinOutlineRecordCS);
	SHADER_USE_PARAMETER_STRUCT(FThinOutlineRecordCS, FGlobalShader);

	static constexpr int32 ThreadGroupSize = 8;

	// r.ThinOutline.Silhouette.HistoryBackgroundTest 1: the silhouette background test uses a depth step two pixels away.
	class FBackgroundDepthStepDim : SHADER_PERMUTATION_BOOL("BACKGROUND_DEPTH_STEP");
	// r.ThinOutline.Silhouette.HistoryViewAngle > 0: the silhouette view-angle test (testing).
	class FViewAngleTestDim : SHADER_PERMUTATION_BOOL("VIEW_ANGLE_TEST");
	// r.ThinOutline.Silhouette.HistorySurfaceTurn: the view-angle test relative to the surface's own turn.
	class FSurfaceTurnDim : SHADER_PERMUTATION_BOOL("SURFACE_TURN");
	// r.ThinOutline.Crease.HistoryCreaseTest: crease records are dropped after a jitter cycle without a crease found.
	class FCreaseHistoryTestDim : SHADER_PERMUTATION_BOOL("CREASE_HISTORY_TEST");
	using FPermutationDomain = TShaderPermutationDomain<FBackgroundDepthStepDim, FViewAngleTestDim, FSurfaceTurnDim, FCreaseHistoryTestDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
		SHADER_PARAMETER_STRUCT_INCLUDE(FThinOutlineEdgeCheckParameters, EdgeCheck)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HistoryHorizontalA)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HistoryHorizontalB)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HistoryVerticalA)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HistoryVerticalB)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HistorySilhouetteForeground)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, HistoryDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, HistoryRecordMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWHorizontalA)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWHorizontalB)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWVerticalA)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWVerticalB)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWSilhouetteForeground)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWDepth)
		SHADER_PARAMETER(FVector2f, SampleLocalPosition)
		SHADER_PARAMETER(float, SampleCountDecay)
		SHADER_PARAMETER(float, CreaseHistoryDepthThreshold)
		SHADER_PARAMETER(float, SilhouetteHistoryDepthThreshold)
		SHADER_PARAMETER(float, SilhouetteHistorySinAngle)
		SHADER_PARAMETER(float, SilhouetteCreaseTakeoverSampleCount)
		SHADER_PARAMETER(float, FadeLimit)
		SHADER_PARAMETER(float, FadeMaxSpeed)
		SHADER_PARAMETER(float, CreaseHistoryKeepRidgeThreshold)
		SHADER_PARAMETER(float, CreaseHistoryKeepValleyThreshold)
		SHADER_PARAMETER(float, CreaseHistoryMissLimit)
		SHADER_PARAMETER(uint32, bHistoryValid)
		SHADER_PARAMETER(uint32, bHistoryMaskValid)
		SHADER_PARAMETER(uint32, HistoryReprojectionMode)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// The surface turn is part of the view-angle test.
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FSurfaceTurnDim>() && !PermutationVector.Get<FViewAngleTestDim>())
		{
			return false;
		}
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
	}
};

// Edge records and drawing settings shared by the two composite passes (ThinOutline.usf).
BEGIN_SHADER_PARAMETER_STRUCT(FThinOutlineCompositeParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HorizontalA)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HorizontalB)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, VerticalA)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, VerticalB)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SilhouetteForeground)
	SHADER_PARAMETER(uint32, bDrawSilhouettes)
	SHADER_PARAMETER(uint32, bDrawCreases)
	SHADER_PARAMETER(FVector3f, CreaseColor)
	SHADER_PARAMETER(FVector3f, SilhouetteColor)
	SHADER_PARAMETER(FVector2f, RenderPixelsPerDisplayPixel)
	SHADER_PARAMETER(float, CreaseThickness)
	SHADER_PARAMETER(float, SilhouetteThickness)
	SHADER_PARAMETER(float, FadeLimit)
	SHADER_PARAMETER(float, SlopeStandardErrorThreshold)
	SHADER_PARAMETER(uint32, bSpatialFilter)
	SHADER_PARAMETER(float, SpatialFilterSigma)
	SHADER_PARAMETER(uint32, bAxisBlend)
	SHADER_PARAMETER(float, AxisBlendScale)
	SHADER_PARAMETER(float, DistinctSampleScale)
	SHADER_PARAMETER(float, SaturatedSampleCount)
	SHADER_PARAMETER(uint32, DebugView)
END_SHADER_PARAMETER_STRUCT()

// Reconstructs the edges from the edge records and composites the outline into scene color at rendering resolution,
// before the temporal upscaler. Also picks the foreground depth and velocity for the pixels painted with a silhouette.
class FThinOutlinePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FThinOutlinePS);
	SHADER_USE_PARAMETER_STRUCT(FThinOutlinePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
		SHADER_PARAMETER_STRUCT_INCLUDE(FThinOutlineEdgeCheckParameters, EdgeCheck)
		SHADER_PARAMETER_STRUCT_INCLUDE(FThinOutlineCompositeParameters, Composite)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Output)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWForegroundDeviceZ)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWForegroundVelocity)
		SHADER_PARAMETER(FVector2f, SampleLocalPosition)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// r.ThinOutline.TileClassification: FThinOutlinePS's composite for the listed rendering tiles only, one thread group per
// tile, reading and writing the scene color in place.
class FThinOutlineCompositeCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FThinOutlineCompositeCS);
	SHADER_USE_PARAMETER_STRUCT(FThinOutlineCompositeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
		SHADER_PARAMETER_STRUCT_INCLUDE(FThinOutlineEdgeCheckParameters, EdgeCheck)
		SHADER_PARAMETER_STRUCT_INCLUDE(FThinOutlineCompositeParameters, Composite)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Output)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWForegroundDeviceZ)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWForegroundVelocity)
		SHADER_PARAMETER(FVector2f, SampleLocalPosition)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileList)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWSceneColor)
		RDG_BUFFER_ACCESS(TileIndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// r.ThinOutline.DrawAfterUpscaler: composites the outline reconstructed from the edge records into scene color at display
// resolution, after the temporal upscaler.
class FThinOutlineAfterUpscalerPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FThinOutlineAfterUpscalerPS);
	SHADER_USE_PARAMETER_STRUCT(FThinOutlineAfterUpscalerPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
		SHADER_PARAMETER_STRUCT_INCLUDE(FThinOutlineEdgeCheckParameters, EdgeCheck)
		SHADER_PARAMETER_STRUCT_INCLUDE(FThinOutlineCompositeParameters, Composite)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Output)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneColorTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// r.ThinOutline.TileClassification: for every 8x8 group of rendering pixels, which 2x2 blocks hold a crease or a
// silhouette record (ThinOutlineTileClassify.usf, RecordMaskCS).
class FThinOutlineRecordMaskCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FThinOutlineRecordMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FThinOutlineRecordMaskCS, FGlobalShader);

	// THIN_OUTLINE_TILE_SIZE of ThinOutlineTile.ush: the mask's groups, and the composites' tiles, are 8x8 pixels.
	static constexpr int32 TileSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HorizontalA)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, VerticalA)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWRecordMask)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// r.ThinOutline.TileClassification: appends every tile of a composite pass that has edge records within its reach to
// the tile list, with the record types found (ThinOutlineTileClassify.usf, TileClassifyCS).
class FThinOutlineTileClassifyCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FThinOutlineTileClassifyCS);
	SHADER_USE_PARAMETER_STRUCT(FThinOutlineTileClassifyCS, FGlobalShader);

	// One thread per tile, in groups of this many tiles along each axis.
	static constexpr int32 ThreadGroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RecordMask)
		SHADER_PARAMETER(FUintVector2, TileCount)
		SHADER_PARAMETER(FVector2f, RenderPixelsPerDisplayPixel)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWTileCounter)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWTileList)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// Turns the tile counter into the arguments of the indirect dispatch over the listed tiles (the composites) and of the
// indirect draw of one quad per tile (the overwrite pass).
class FThinOutlineTileSetupCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FThinOutlineTileSetupCS);
	SHADER_USE_PARAMETER_STRUCT(FThinOutlineTileSetupCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileCounter)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWTileIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWTileDrawIndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// r.ThinOutline.TileClassification: FThinOutlineAfterUpscalerPS's composite for the listed display tiles only, one
// thread group per tile, reading and writing the scene color in place.
class FThinOutlineAfterUpscalerCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FThinOutlineAfterUpscalerCS);
	SHADER_USE_PARAMETER_STRUCT(FThinOutlineAfterUpscalerCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
		SHADER_PARAMETER_STRUCT_INCLUDE(FThinOutlineEdgeCheckParameters, EdgeCheck)
		SHADER_PARAMETER_STRUCT_INCLUDE(FThinOutlineCompositeParameters, Composite)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Output)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileList)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWSceneColor)
		RDG_BUFFER_ACCESS(TileIndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// r.ThinOutline.TileClassification: places one quad per listed tile, in the view rect, for the overwrite pass drawn on
// the tiles only (ThinOutlineForeground.usf, TileVS).
class FThinOutlineTileVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FThinOutlineTileVS);
	SHADER_USE_PARAMETER_STRUCT(FThinOutlineTileVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileList)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// Copies the foreground depth and velocity picked by FThinOutlinePS into the scene depth and velocity textures. Drawn
// full screen, or (r.ThinOutline.TileClassification) as one quad per listed tile with FThinOutlineTileVS.
class FThinOutlineForegroundPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FThinOutlineForegroundPS);
	SHADER_USE_PARAMETER_STRUCT(FThinOutlineForegroundPS, FGlobalShader);

	class FWriteVelocityDim : SHADER_PERMUTATION_BOOL("WRITE_VELOCITY");
	using FPermutationDomain = TShaderPermutationDomain<FWriteVelocityDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ForegroundDeviceZ)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, ForegroundVelocity)
		SHADER_PARAMETER(FIntPoint, ViewRectMin)
		SHADER_PARAMETER_STRUCT_INCLUDE(FThinOutlineTileVS::FParameters, VS)
		RDG_BUFFER_ACCESS(TileDrawIndirectArgs, ERHIAccess::IndirectArgs)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

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
	using FPermutationDomain = TShaderPermutationDomain<FBackgroundDepthStepDim, FViewAngleTestDim, FSurfaceTurnDim>;

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
		SHADER_PARAMETER(uint32, bHistoryValid)
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

// Reconstructs the edges from the edge records and composites the outline into scene color. Also picks the
// foreground depth and velocity for the pixels painted with a silhouette.
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
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Output)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HorizontalA)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HorizontalB)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, VerticalA)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, VerticalB)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SilhouetteForeground)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWForegroundDeviceZ)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWForegroundVelocity)
		SHADER_PARAMETER(FVector3f, CreaseColor)
		SHADER_PARAMETER(FVector3f, SilhouetteColor)
		SHADER_PARAMETER(FVector2f, SampleLocalPosition)
		SHADER_PARAMETER(FVector2f, RenderPixelsPerDisplayPixel)
		SHADER_PARAMETER(float, CreaseThickness)
		SHADER_PARAMETER(float, SilhouetteThickness)
		SHADER_PARAMETER(float, CoTriggerThreshold)
		SHADER_PARAMETER(float, SlopeStandardErrorThreshold)
		SHADER_PARAMETER(float, DistinctSampleScale)
		SHADER_PARAMETER(float, SaturatedSampleCount)
		SHADER_PARAMETER(uint32, DebugView)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// Copies the foreground depth and velocity picked by FThinOutlinePS into the scene depth and velocity textures.
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
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

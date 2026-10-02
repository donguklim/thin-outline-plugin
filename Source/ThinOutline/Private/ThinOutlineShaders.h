// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "SceneTexturesConfig.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"
#include "Substrate/Substrate.h"

// Thresholds of the G-buffer edge checks (ThinOutlineCommon.ush).
BEGIN_SHADER_PARAMETER_STRUCT(FThinOutlineEdgeCheckParameters, )
	SHADER_PARAMETER(float, SilhouetteThreshold)
	SHADER_PARAMETER(float, SilhouetteScale)
	SHADER_PARAMETER(float, CreaseRidgeThreshold)
	SHADER_PARAMETER(float, CreaseValleyThreshold)
	SHADER_PARAMETER(float, CreaseScale)
END_SHADER_PARAMETER_STRUCT()

// Adds this frame's jittered crease check results to the per-pixel edge records, carrying the records over from the
// previous frame by reprojection.
class FThinOutlineRecordCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FThinOutlineRecordCS);
	SHADER_USE_PARAMETER_STRUCT(FThinOutlineRecordCS, FGlobalShader);

	static constexpr int32 ThreadGroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
		SHADER_PARAMETER_STRUCT_INCLUDE(FThinOutlineEdgeCheckParameters, EdgeCheck)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HistoryHorizontalA)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HistoryHorizontalB)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HistoryVerticalA)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HistoryVerticalB)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, HistoryDepth)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWHorizontalA)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWHorizontalB)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWVerticalA)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWVerticalB)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWDepth)
		SHADER_PARAMETER(FVector2f, SampleLocalPosition)
		SHADER_PARAMETER(float, SampleCountDecay)
		SHADER_PARAMETER(float, HistoryDepthThreshold)
		SHADER_PARAMETER(uint32, bHistoryValid)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
	}
};

// Reconstructs the crease edges from the edge records and composites the outline into scene color.
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
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, RecordHorizontalA)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, RecordHorizontalB)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, RecordVerticalA)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, RecordVerticalB)
		SHADER_PARAMETER(FVector3f, OutlineColor)
		SHADER_PARAMETER(FVector2f, SampleLocalPosition)
		SHADER_PARAMETER(FVector2f, RenderPixelsPerDisplayPixel)
		SHADER_PARAMETER(float, HalfThickness)
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

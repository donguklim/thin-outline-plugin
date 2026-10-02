// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "SceneTexturesConfig.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"
#include "Substrate/Substrate.h"

// Detects silhouettes (depth discontinuities) and creases (normal discontinuities) from the G-buffer at rendering
// resolution and composites the outline into scene color.
class FThinOutlinePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FThinOutlinePS);
	SHADER_USE_PARAMETER_STRUCT(FThinOutlinePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Output)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneColorTexture)
		SHADER_PARAMETER(FVector3f, OutlineColor)
		SHADER_PARAMETER(float, SilhouetteThreshold)
		SHADER_PARAMETER(float, SilhouetteScale)
		SHADER_PARAMETER(float, CreaseRidgeThreshold)
		SHADER_PARAMETER(float, CreaseValleyThreshold)
		SHADER_PARAMETER(float, CreaseScale)
		SHADER_PARAMETER(uint32, DebugView)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

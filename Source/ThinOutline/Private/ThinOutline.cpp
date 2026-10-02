// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThinOutline.h"

#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ConfigUtilities.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FThinOutlineModule"

void FThinOutlineModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ThinOutline"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("ThinOutline: plugin descriptor is not available"));
		return;
	}

	const FString PluginShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/ThinOutline"), PluginShaderDir);

	// Project Settings > Plugins > Thin Outline stores its CVar-backed values under the CVar names.
	// Only apply keys that are registered console variables, so non-CVar settings (e.g. SilhouetteColor)
	// do not create dummy console variables.
	UE::ConfigUtilities::ForEachCVarInSectionFromIni(TEXT("/Script/ThinOutline.ThinOutlineSettings"), *GEngineIni,
		[](IConsoleVariable* CVar, const FString& KeyString, const FString& ValueString)
		{
			CVar->Set(*ValueString, ECVF_SetByProjectSetting);
		});
}

void FThinOutlineModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FThinOutlineModule, ThinOutline)

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "ThinOutlineSubsystem.generated.h"

/** Owns the scene view extension that draws the G-buffer outlines for every view. */
UCLASS()
class UThinOutlineSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	TSharedPtr<class FThinOutlineSceneViewExtension, ESPMode::ThreadSafe> ViewExtension;
};

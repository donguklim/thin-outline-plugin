// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
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
	/** r.ThinOutline.Debug.PawnSpin: turns the player pawn before the actors tick, so the frame renders it with its velocity. */
	void OnWorldPreActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);

	TSharedPtr<class FThinOutlineSceneViewExtension, ESPMode::ThreadSafe> ViewExtension;
	FDelegateHandle WorldPreActorTickHandle;
	/** GFrameCounter and pawn yaw when r.ThinOutline.Debug.PawnSpin started turning the pawn. */
	TOptional<uint64> DebugPawnSpinStartFrame;
	double DebugPawnSpinStartYaw = 0.0;
};

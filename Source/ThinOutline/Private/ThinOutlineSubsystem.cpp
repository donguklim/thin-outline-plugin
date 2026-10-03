// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThinOutlineSubsystem.h"
#include "ThinOutlineCVars.h"
#include "ThinOutlineSceneViewExtension.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "RenderingThread.h"
#include "SceneViewExtension.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ThinOutlineSubsystem)

void UThinOutlineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	ViewExtension = FSceneViewExtensions::NewExtension<FThinOutlineSceneViewExtension>();
	WorldPreActorTickHandle = FWorldDelegates::OnWorldPreActorTick.AddUObject(this, &UThinOutlineSubsystem::OnWorldPreActorTick);
}

void UThinOutlineSubsystem::OnWorldPreActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	const float Spin = CVarThinOutlineDebugPawnSpin.GetValueOnGameThread();
	if (Spin == 0.0f)
	{
		DebugPawnSpinStartFrame.Reset();
		return;
	}

	APlayerController* Controller = World && World->IsGameWorld() ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}

	// Follows the frame counter, like the debug camera motion.
	if (!DebugPawnSpinStartFrame.IsSet())
	{
		DebugPawnSpinStartFrame = GFrameCounter;
		DebugPawnSpinStartYaw = Pawn->GetActorRotation().Yaw;
	}
	const double Frames = double(GFrameCounter - DebugPawnSpinStartFrame.GetValue() + 1);
	Pawn->SetActorRotation(FRotator(0.0, DebugPawnSpinStartYaw + Spin * Frames, 0.0));
}

void UThinOutlineSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldPreActorTick.Remove(WorldPreActorTickHandle);

	if (ViewExtension.IsValid())
	{
		// The renderer may still hold a reference for frames in flight; make sure it stops running.
		ViewExtension->IsActiveThisFrameFunctions.Empty();

		FSceneViewExtensionIsActiveFunctor IsActiveFunctor;
		IsActiveFunctor.IsActiveFunction = [](const ISceneViewExtension*, const FSceneViewExtensionContext&)
		{
			return TOptional<bool>(false);
		};

		ViewExtension->IsActiveThisFrameFunctions.Add(IsActiveFunctor);

		// Pooled render targets are released on the render thread, after the frames in flight.
		ENQUEUE_RENDER_COMMAND(ThinOutlineReleaseHistories)(
			[ViewExtension = ViewExtension](FRHICommandListImmediate& RHICmdList)
			{
				ViewExtension->ReleaseHistories_RenderThread();
			});
	}

	ViewExtension.Reset();
}

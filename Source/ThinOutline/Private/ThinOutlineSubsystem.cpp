// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThinOutlineSubsystem.h"
#include "ThinOutlineSceneViewExtension.h"
#include "RenderingThread.h"
#include "SceneViewExtension.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ThinOutlineSubsystem)

void UThinOutlineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	ViewExtension = FSceneViewExtensions::NewExtension<FThinOutlineSceneViewExtension>();
}

void UThinOutlineSubsystem::Deinitialize()
{
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

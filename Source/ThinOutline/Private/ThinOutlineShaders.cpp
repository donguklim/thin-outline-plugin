// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThinOutlineShaders.h"

IMPLEMENT_GLOBAL_SHADER(FThinOutlineRecordCS, "/Plugin/ThinOutline/Private/ThinOutlineRecord.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FThinOutlinePS, "/Plugin/ThinOutline/Private/ThinOutline.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FThinOutlineForegroundPS, "/Plugin/ThinOutline/Private/ThinOutlineForeground.usf", "MainPS", SF_Pixel);

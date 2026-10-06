// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThinOutlineShaders.h"

IMPLEMENT_GLOBAL_SHADER(FThinOutlineRecordCS, "/Plugin/ThinOutline/Private/ThinOutlineRecord.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FThinOutlinePS, "/Plugin/ThinOutline/Private/ThinOutline.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FThinOutlineAfterUpscalerPS, "/Plugin/ThinOutline/Private/ThinOutline.usf", "AfterUpscalerPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FThinOutlineAfterUpscalerCS, "/Plugin/ThinOutline/Private/ThinOutline.usf", "AfterUpscalerCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FThinOutlineRecordMaskCS, "/Plugin/ThinOutline/Private/ThinOutlineTileClassify.usf", "RecordMaskCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FThinOutlineTileClassifyCS, "/Plugin/ThinOutline/Private/ThinOutlineTileClassify.usf", "TileClassifyCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FThinOutlineTileSetupCS, "/Plugin/ThinOutline/Private/ThinOutlineTileClassify.usf", "TileSetupCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FThinOutlineCompositeCS, "/Plugin/ThinOutline/Private/ThinOutline.usf", "CompositeCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FThinOutlineTileVS, "/Plugin/ThinOutline/Private/ThinOutlineForeground.usf", "TileVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FThinOutlineForegroundPS, "/Plugin/ThinOutline/Private/ThinOutlineForeground.usf", "MainPS", SF_Pixel);

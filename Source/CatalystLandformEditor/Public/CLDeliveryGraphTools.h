#pragma once
#include "CoreMinimal.h"
class UVoxelVolumeGraph;
/** Builds a native VPP graph using public nodes and editor reflection, never private runtime graph data. */
CATALYSTLANDFORMEDITOR_API UVoxelVolumeGraph* CLCreateDeliveryGraph(FString& Error,bool SaveAsset=false);

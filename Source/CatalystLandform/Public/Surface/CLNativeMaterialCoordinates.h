#pragma once
#include "CoreMinimal.h"
struct FVoxelVolumeTransform;
struct FCLPlanetStamp;
struct CATALYSTLANDFORM_API FCLNativeMaterialCoordinates
{
    FLinearColor High, Low;
    static FVoxelVolumeTransform Transform(const FTransform& StampToWorld);
    static FCLNativeMaterialCoordinates Make(const FVoxelVolumeTransform &, bool Section,
                                             double ReferenceMeters);
    bool Apply(FCLPlanetStamp &, FString &Error) const;
};

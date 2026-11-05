#include "Surface/CLNativeMaterialCoordinates.h"
#include "CLPlanetStamp.h"
#include "VoxelLinearColorMetadata.h"
#include "VoxelPinValue.h"
FCLNativeMaterialCoordinates FCLNativeMaterialCoordinates::Make(const FVoxelVolumeTransform &Transform,
                                                                bool Section, double ReferenceMeters)
{
    check(Transform.Position.IsNearlyZero() && Transform.Rotation.IsIdentity() &&
          Transform.Scale.X == Transform.Scale.Y);
    const FVector3d Value(1. / double(Transform.Scale.X), 1. / double(Transform.Scale.Z),
                          ReferenceMeters * 100.);
    const FVector3f High(Value), Low(Value - FVector3d(High));
    return {FLinearColor(High.X, High.Y, High.Z, Section ? 2 : 1), FLinearColor(Low.X, Low.Y, Low.Z, 0)};
}
bool FCLNativeMaterialCoordinates::Apply(FCLPlanetStamp &Stamp, FString &Error) const
{
    for (const auto &Item : TArray<TPair<FString, FLinearColor>>{{TEXT("MD_PreviewMappingHigh"), High},
                                                                 {TEXT("MD_PreviewMappingLow"), Low}})
    {
        auto *Metadata = LoadObject<UVoxelLinearColorMetadata>(
            nullptr, *(TEXT("/CatalystLandform/Surfaces/") + Item.Key + TEXT(".") + Item.Key));
        if (!Metadata)
        {
            Error = TEXT("Missing native coordinate metadata: ") + Item.Key;
            return false;
        }
        auto &Override = Stamp.MetadataOverrides.Overrides.AddDefaulted_GetRef();
        Override.Metadata = Metadata;
        Override.Value = FVoxelPinValue::Make(Item.Value);
    }
    return true;
}

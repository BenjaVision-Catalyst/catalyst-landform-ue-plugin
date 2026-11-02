#pragma once

#include "CoreMinimal.h"

/** Rigid local/native frame inside a canonical body. No presentation bend or scale.
 * Body coordinates are double metres; native positions are local centimetres. */
struct CATALYSTLANDFORM_API FCLBodyFrame
{
    FVector3d AnchorMeters = FVector3d::ZeroVector;
    FQuat4d BodyFromLocal = FQuat4d::Identity;

    bool Validate(FString& Error) const;
    FVector3d NativeToBodyMeters(const FVector3d& LocalCentimeters) const;
    FVector3d BodyToNativeCentimeters(const FVector3d& BodyMeters) const;
};

#pragma once
#include "CoreMinimal.h"

/** Canonical cube-sphere patch frames. Patch ownership never changes procedural seeds. */
struct CATALYSTLANDFORM_API FCLSphericalPatches
{
    static constexpr int32 Count = 6;
    static constexpr int32 WholeSphere = -2;
    static bool IsFace(int32 Face) { return Face>=0 && Face<Count; }
    static void Frame(int32 Face,FVector& Normal,FVector& X,FVector& Y);
    static FVector Direction(int32 Face,const FVector2D& UV);
    static bool Coordinates(int32 Face,const FVector& Direction,FVector2D& UV);
    /** Deterministic dominant-axis ownership, including edge/corner ties. */
    static int32 Owner(const FVector& Direction);
    /** Conservative face cap plus the source's true native rectangular support. */
    static bool MayAffect(int32 Face,const FVector& Anchor,double SupportAngle);
};

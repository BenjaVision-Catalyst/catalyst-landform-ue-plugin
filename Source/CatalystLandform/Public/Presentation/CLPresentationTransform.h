#pragma once
#include "CoreMinimal.h"
class FCLPlanetProjection;

/** Bounded local affine-frame approximation for small render-only objects. Never a simulation transform. */
struct CATALYSTLANDFORM_API FCLPresentationTransform
{
    static bool TryProject(const FCLPlanetProjection& Projection,const FTransform& Source,
        const FBox& LocalBounds,FTransform& Presented,double& MaxCornerErrorCm);
    static bool TryProjectCamera(const FCLPlanetProjection& Projection,const FVector& Position,
        const FQuat& Rotation,FVector& PresentedPosition,FQuat& PresentedRotation);
};

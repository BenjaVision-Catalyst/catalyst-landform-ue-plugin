#pragma once
#include "CoreMinimal.h"
#include "Presentation/CLPlanetProjection.h"

/** Rigid fit plus a proven residual bound for one admitted source mesh box.
 * Fit and source geometry use the same canonical coordinate frame. This is render-only data:
 * retain original transforms separately, and never apply this fit to collision or physics actors.
 */
class CATALYSTLANDFORM_API FCLProjectionChunk
{
public:
    static bool TryCreate(const FCLPlanetProjection& Projection, const FBox& CanonicalBounds, FCLProjectionChunk& Out);
    FVector ProjectRigidPosition(const FVector& Canonical) const;
    FTransform ProjectRigidTransform(const FTransform& Canonical) const;
    const FQuat& GetRotation() const { return Rotation; }
    double GetMaxResidualCm() const { return MaxResidualCm; }
    double GetMaxStretch() const { return MaxStretch; }
    const FBox& GetPresentedBounds() const { return PresentedBounds; }
private:
    FVector SourcePivot = FVector::ZeroVector;
    FVector PresentedPivot = FVector::ZeroVector;
    FQuat Rotation = FQuat::Identity;
    double MaxResidualCm = 0;
    double MaxStretch = 1;
    FBox PresentedBounds{ForceInit};
};

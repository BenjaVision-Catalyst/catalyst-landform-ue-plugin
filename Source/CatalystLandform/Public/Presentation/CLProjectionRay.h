#pragma once
#include "CoreMinimal.h"
#include "Presentation/CLPlanetProjection.h"

/** Admitted inverse of one finite presented ray. All distances are centimetres.
 * The entire segment must fit the chart/altitude band. Chord error uses a
 * conservative second-derivative bound, not midpoint sampling.
 */
class CATALYSTLANDFORM_API FCLProjectionRay
{
public:
    static bool TryCreate(const FCLPlanetProjection& Projection,const FVector& Origin,
        const FVector& Direction,double LengthCm,FCLProjectionRay& Out);
    bool TryGetCanonicalPoint(double DistanceCm,FVector& Out) const;
    double GetMaxChordErrorCm(double IntervalLengthCm) const;
    double GetMaxForwardStretch() const { return MaxForwardStretch; }
    double GetLengthCm() const { return Length; }
    const FVector& GetOrigin() const { return Origin; }
    const FVector& GetDirection() const { return Direction; }
private:
    FCLPlanetProjection Projection;
    FVector Origin=FVector::ZeroVector,Direction=FVector::XAxisVector;
    double Length=0,SecondDerivativeBound=0,MaxForwardStretch=1;
    bool bValid=false;
};

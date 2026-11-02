#pragma once
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "Presentation/CLPlanetProjection.h"
class UWorld;
struct FCollisionQueryParams;

enum class ECLProjectionTraceStatus : uint8 { Hit,Miss,InvalidRequest,OutsideChart,BudgetExceeded,UnresolvedContact };
struct CATALYSTLANDFORM_API FCLProjectionTraceSettings
{
    double MaxDistanceCm=2000;
    double HitToleranceCm=.5;
    int32 MaxWorldQueries=64;
    int32 MaxDepth=12;
};
struct CATALYSTLANDFORM_API FCLProjectionTraceResult
{
    ECLProjectionTraceStatus Status=ECLProjectionTraceStatus::InvalidRequest;
    // Unmodified native collision hit. TraceStart/End and Distance describe its
    // canonical chord/sweep; PresentedDistanceCm is the screen-ray distance.
    FHitResult CanonicalHit;
    FVector PresentedPoint=FVector::ZeroVector,PresentedNormal=FVector::ZeroVector;
    double PresentedDistanceCm=0,RayErrorCm=0;
    int32 WorldQueries=0,DeepestLevel=0;
};
/** Game-thread, synchronous, read-only query; caller owns collision residency.
 * Conservative swept envelopes prevent a curved ray being declared clear from
 * endpoint chords alone. Hits have an explicit presented-space tolerance.
 * Query params must include initial blocking overlaps and full narrow phase.
 * Failure/budget exhaustion is never an ordinary Miss. No per-query heap buffer.
 */
class CATALYSTLANDFORM_API FCLProjectionTrace
{
public:
    static FCLProjectionTraceResult Trace(UWorld& World,const FCLPlanetProjection& Projection,
        const FVector& PresentedOrigin,const FVector& PresentedDirection,
        ECollisionChannel Channel,const FCollisionQueryParams& Params,
        const FCLProjectionTraceSettings& Settings=FCLProjectionTraceSettings());
};

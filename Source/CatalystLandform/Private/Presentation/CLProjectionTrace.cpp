#include "Presentation/CLProjectionTrace.h"
#include "Presentation/CLProjectionRay.h"
#include "Engine/World.h"
#include "CollisionShape.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

FCLProjectionTraceResult FCLProjectionTrace::Trace(UWorld& World,const FCLPlanetProjection& P,
    const FVector& Origin,const FVector& Direction,ECollisionChannel Channel,const FCollisionQueryParams& Params,
    const FCLProjectionTraceSettings& S)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(CLProjection_Interaction);
    FCLProjectionTraceResult Result;
    if(!IsInGameThread()||!Params.bFindInitialOverlaps||Params.bIgnoreBlocks||Params.bSkipNarrowPhase||
       Channel<0||Channel>=ECC_MAX||Origin.ContainsNaN()||Direction.ContainsNaN()||Direction.IsNearlyZero()||
       !FMath::IsFinite(S.MaxDistanceCm)||S.MaxDistanceCm<=0||!FMath::IsFinite(S.HitToleranceCm)||S.HitToleranceCm<.01||
       S.MaxWorldQueries<1||S.MaxWorldQueries>64||S.MaxDepth<0||S.MaxDepth>12)return Result;
    FCLProjectionRay Ray;
    if(!FCLProjectionRay::TryCreate(P,Origin,Direction,S.MaxDistanceCm,Ray))
    {Result.Status=ECLProjectionTraceStatus::OutsideChart;return Result;}
    struct FInterval { double Start,End;FVector A,B;int32 Depth; };
    FInterval Stack[14];int32 Pending=1;
    Ray.TryGetCanonicalPoint(0,Stack[0].A);Ray.TryGetCanonicalPoint(S.MaxDistanceCm,Stack[0].B);
    Stack[0].Start=0;Stack[0].End=S.MaxDistanceCm;Stack[0].Depth=0;
    const bool Natural=P.GetSettings().Curvature==1;
    const double Slack=.01+P.GetSettings().ReferenceRadiusCm*1.e-10;
    while(Pending)
    {
        if(Result.WorldQueries>=S.MaxWorldQueries){Result.Status=ECLProjectionTraceStatus::BudgetExceeded;return Result;}
        const auto I=Stack[--Pending];Result.DeepestLevel=FMath::Max(Result.DeepestLevel,I.Depth);
        const double Envelope=Natural?0:Ray.GetMaxChordErrorCm(I.End-I.Start)+Slack;
        FHitResult Hit;++Result.WorldQueries;
        const bool Blocking=Natural?World.LineTraceSingleByChannel(Hit,I.A,I.B,Channel,Params):
            World.SweepSingleByChannel(Hit,I.A,I.B,FQuat::Identity,Channel,FCollisionShape::MakeSphere(Envelope),Params);
        if(!Blocking)continue;
        const bool Fine=Natural||2*Envelope*Ray.GetMaxForwardStretch()<=S.HitToleranceCm;
        if(Fine&&!Hit.bStartPenetrating)
        {
            FVector Point,Normal;
            if(P.TryProjectPosition(Hit.ImpactPoint,Point)&&P.TryProjectNormal(Hit.ImpactPoint,Hit.ImpactNormal,Normal))
            {
                const double Distance=FVector::DotProduct(Point-Origin,Ray.GetDirection());
                const double Error=FVector::Distance(Point,Origin+Distance*Ray.GetDirection());
                if(Distance>=0&&Distance<=S.MaxDistanceCm&&Distance>=I.Start-S.HitToleranceCm&&
                   Distance<=I.End+S.HitToleranceCm&&Error<=S.HitToleranceCm)
                {
                    Result.Status=ECLProjectionTraceStatus::Hit;Result.CanonicalHit=Hit;
                    Result.PresentedPoint=Point;Result.PresentedNormal=Normal;
                    Result.PresentedDistanceCm=Distance;Result.RayErrorCm=Error;return Result;
                }
            }
        }
        if(Natural||I.Depth>=S.MaxDepth){Result.Status=ECLProjectionTraceStatus::UnresolvedContact;return Result;}
        const double Middle=(I.Start+I.End)*.5;FVector M;
        if(!Ray.TryGetCanonicalPoint(Middle,M)){Result.Status=ECLProjectionTraceStatus::OutsideChart;return Result;}
        // LIFO, left first: an unresolved earlier candidate cannot be skipped.
        Stack[Pending++]={Middle,I.End,M,I.B,I.Depth+1};
        Stack[Pending++]={I.Start,Middle,I.A,M,I.Depth+1};
    }
    Result.Status=ECLProjectionTraceStatus::Miss;return Result;
}

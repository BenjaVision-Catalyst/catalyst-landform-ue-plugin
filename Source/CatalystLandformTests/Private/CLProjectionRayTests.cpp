#include "Misc/AutomationTest.h"
#include "Presentation/CLProjectionRay.h"
#include <limits>
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLProjectionRayTest,"Catalyst.Landform.Presentation.InverseRayBounds",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLProjectionRayTest::RunTest(const FString&)
{
    int32 Samples=0;double MaxRatio=0;
    for(double R:{1000.,47000.,1000000.})for(double C:{0.,1.e-12,.25,.5,.999,1.})
    for(FVector Up:{FVector::ZAxisVector,FVector(.3,-.4,.8).GetSafeNormal()})
    {
        FCLPlanetProjectionSettings S;S.ReferenceRadiusCm=R;S.Curvature=C;S.AnchorDirection=Up;
        S.Center=FVector(2,-3,.4)*R;S.MinAltitudeCm=-.4*R;S.MaxAltitudeCm=.3*R;
        FCLPlanetProjection P;FCLPlanetProjection::TryCreate(S,P);FVector X,Y;Up.FindBestAxisVectors(X,Y);
        const FVector Origin=S.Center+R*Up+.02*R*Up+.04*R*Y,Dir=(X-.1*Up).GetSafeNormal();
        FCLProjectionRay Ray;if(!TestTrue(TEXT("Interior ray admitted"),FCLProjectionRay::TryCreate(P,Origin,Dir,.12*R,Ray)))return false;
        for(int32 Partitions:{1,4,16})for(int32 J=0;J<Partitions;++J)
        {
            const double T0=.12*R*J/Partitions,T1=.12*R*(J+1)/Partitions;
            FVector A,B;Ray.TryGetCanonicalPoint(T0,A);Ray.TryGetCanonicalPoint(T1,B);
            const double Bound=Ray.GetMaxChordErrorCm(T1-T0),Slack=R*1.e-10;
            for(int32 I=0;I<=32;++I)
            {
                const double T=double(I)/32;FVector Point;
                if(!Ray.TryGetCanonicalPoint(FMath::Lerp(T0,T1,T),Point)){AddError(TEXT("Interior ray sample rejected"));return false;}
                const double Error=FVector::Distance(Point,FMath::Lerp(A,B,T));
                if(!TestTrue(TEXT("Dense curve lies inside analytical chord envelope"),Error<=Bound+Slack))return false;
                if(Bound>0)MaxRatio=FMath::Max(MaxRatio,Error/Bound);++Samples;
            }
        }
    }
    FCLPlanetProjectionSettings S;S.Curvature=0;FCLPlanetProjection P;FCLPlanetProjection::TryCreate(S,P);
    FCLProjectionRay Ray;TestTrue(TEXT("Sentinel ray"),FCLProjectionRay::TryCreate(P,FVector(0,0,10000),FVector::XAxisVector,100,Ray));
    const auto Before=Ray;TestFalse(TEXT("Whole ray outside cap refused"),FCLProjectionRay::TryCreate(P,FVector(0,0,10000),FVector::XAxisVector,20000,Ray));
    TestEqual(TEXT("Rejected construction preserves previous ray"),Ray.GetLengthCm(),Before.GetLengthCm());
    FVector Sentinel(1,2,3),Out=Sentinel;
    TestFalse(TEXT("Outside ray parameter refused"),Ray.TryGetCanonicalPoint(101,Out));TestTrue(TEXT("Failed point preserves output"),Out==Sentinel);
    TestFalse(TEXT("Zero direction refused"),FCLProjectionRay::TryCreate(P,FVector(0,0,10000),FVector::ZeroVector,100,Ray));
    TestFalse(TEXT("Nonfinite length refused"),FCLProjectionRay::TryCreate(P,FVector(0,0,10000),FVector::XAxisVector,std::numeric_limits<double>::infinity(),Ray));
    AddInfo(FString::Printf(TEXT("%d dense inverse-ray samples; largest observed/bound ratio %.9g"),Samples,MaxRatio));return true;
}

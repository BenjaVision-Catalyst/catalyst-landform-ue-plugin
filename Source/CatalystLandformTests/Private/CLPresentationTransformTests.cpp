#include "Misc/AutomationTest.h"
#include "Presentation/CLPresentationTransform.h"
#include "Presentation/CLPlanetProjection.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPresentationTransformTest,"Catalyst.Landform.Presentation.LocalFrames",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPresentationTransformTest::RunTest(const FString&)
{
    const FBox Box(FVector(-15,-15,-35),FVector(15,15,35));
    for(double C:{0.,.25,.5,.75,1.})for(const FVector Anchor:{FVector::ZAxisVector,FVector(.3,-.4,.8).GetSafeNormal()})
    {
        FCLPlanetProjectionSettings S;S.ReferenceRadiusCm=47000;S.AnchorDirection=Anchor;S.Curvature=C;S.MinAltitudeCm=-18800;S.MaxAltitudeCm=14100;
        FCLPlanetProjection P;TestTrue(TEXT("Valid chart"),FCLPlanetProjection::TryCreate(S,P));
        const FVector X=FVector::CrossProduct(FMath::Abs(Anchor.Z)<.9?FVector::ZAxisVector:FVector::XAxisVector,Anchor).GetSafeNormal();
        const FTransform Source(FRotationMatrix::MakeFromXZ(X,Anchor).ToQuat(),Anchor*47100);
        FTransform Mapped;double Error=-1;TestTrue(TEXT("Small visual frame maps"),FCLPresentationTransform::TryProject(P,Source,Box,Mapped,Error));
        TestTrue(TEXT("Small frame corner error under 2 cm"),Error<2);
        if(C==1)TestTrue(TEXT("Natural frame identity"),Mapped.Equals(Source,1.e-8));
        FVector Position;FQuat Q;TestTrue(TEXT("Camera frame maps"),FCLPresentationTransform::TryProjectCamera(P,Source.GetLocation(),Source.GetRotation(),Position,Q));
        FVector Back;TestTrue(TEXT("Presented camera has canonical inverse"),P.TryUnprojectPosition(Position,Back));TestTrue(TEXT("Camera round trip"),Back.Equals(Source.GetLocation(),1.e-5));
        TestTrue(TEXT("Right-handed orthonormal camera"),Q.IsNormalized()&&FVector::DotProduct(FVector::CrossProduct(Q.GetAxisX(),Q.GetAxisY()),Q.GetAxisZ())>.999999);
    }
    FCLPlanetProjectionSettings S;S.MinAltitudeCm=-100;S.MaxAltitudeCm=100;FCLPlanetProjection P;FCLPlanetProjection::TryCreate(S,P);
    FTransform Out(FVector(7,8,9));const auto Before=Out;double Error=123;
    TestFalse(TEXT("Out-of-chart object refused"),FCLPresentationTransform::TryProject(P,FTransform(FVector(0,0,-10000)),Box,Out,Error));
    TestTrue(TEXT("Failed output preserved"),Out.Equals(Before,0)&&Error==123);return true;
}

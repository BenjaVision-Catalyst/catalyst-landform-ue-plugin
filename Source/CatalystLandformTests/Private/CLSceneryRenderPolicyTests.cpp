#include "Misc/AutomationTest.h"
#include "Presentation/CLSceneryRenderPolicy.h"
#include "Presentation/CLPlanetProjection.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSceneryBudgetTest,"Catalyst.Landform.Presentation.SceneryBudgetsAndBounds",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLSceneryBudgetTest::RunTest(const FString&)
{
    const auto Tree=FCLSceneryRenderPolicy::ForSize(2200),Shrub=FCLSceneryRenderPolicy::ForSize(350),Grass=FCLSceneryRenderPolicy::ForSize(80);
    TestTrue(TEXT("Distant canopy never receives a draw-distance cutoff"),Tree.bDistantCanopy&&Tree.DrawEndCm==0);
    TestTrue(TEXT("Small ground detail fades before its distance cutoff"),Grass.FadeStartCm>0&&Grass.FadeStartCm<Grass.DrawEndCm);
    TestTrue(TEXT("Ground animation and shadows are local"),Grass.WpoEndCm<Grass.DrawEndCm&&Grass.ShadowEndCm<Grass.DrawEndCm);
    TestTrue(TEXT("Shrubs have a longer budget than ground detail"),Shrub.DrawEndCm>Grass.DrawEndCm&&Shrub.DrawEndCm>0);
    FRandomStream Random(1729);double WorstScale=0;
    for(double Curvature:{0.,.15,.5,1.})for(double Angle:{0.,.3,.8,1.2})
    {
        FCLPlanetProjectionSettings S;S.Center=FVector(9000,-7000,4000);S.ReferenceRadiusCm=65000;S.AnchorDirection=FVector::ZAxisVector;S.Curvature=Curvature;S.MaxArcRadians=FMath::DegreesToRadians(89.);S.MinAltitudeCm=-58500;S.MaxAltitudeCm=65000;
        FCLPlanetProjection P;if(!TestTrue(TEXT("Valid walking chart"),FCLPlanetProjection::TryCreate(S,P)))return false;
        const FVector D(FMath::Sin(Angle),0,FMath::Cos(Angle));const FBox Source(S.Center+D*65000-FVector(700,1100,900),S.Center+D*65000+FVector(700,1100,900));
        const double MinRadius=FVector::Distance(Source.GetClosestPointTo(S.Center),S.Center);
        const double Ratio=Curvature==0?S.MaxArcRadians/FMath::Sin(S.MaxArcRadians):FMath::Sin(Curvature*S.MaxArcRadians)/(Curvature*FMath::Sin(S.MaxArcRadians));
        const double Stretch=FMath::Max(1.,65000/MinRadius)*Ratio+2.e-6;FBox Bounds;
        if(!TestTrue(TEXT("Regional projected enclosure exists"),FCLSceneryRenderPolicy::ProjectBounds(Source,P,Stretch,Bounds)))return false;
        const double Scale=FCLSceneryRenderPolicy::CenteredBoundsScale(Source,Bounds);WorstScale=FMath::Max(WorstScale,Scale);
        const FBox StaticBounds(Source.GetCenter()-Source.GetExtent()*Scale,Source.GetCenter()+Source.GetExtent()*Scale);
        TestTrue(TEXT("Stock static bounds contain the projected enclosure"),StaticBounds.ExpandBy(.001).IsInside(Bounds));
        for(int32 I=0;I<1024;++I)
        {
            const FVector Input=Source.GetCenter()+FVector(Random.FRandRange(-1.f,1.f),Random.FRandRange(-1.f,1.f),Random.FRandRange(-1.f,1.f))*Source.GetExtent();FVector Output;
            if(TestTrue(TEXT("Interior point stays in chart"),P.TryProjectPosition(Input,Output)))TestTrue(TEXT("Lipschitz enclosure contains curved interiors"),Bounds.IsInside(Output));
        }
    }
    TestTrue(TEXT("Regional bounds avoid the old four-radius inflation"),WorstScale<80);
    AddInfo(FString::Printf(TEXT("16384 curved interior probes; maximum conservative static scale %.3f"),WorstScale));return !HasAnyErrors();
}

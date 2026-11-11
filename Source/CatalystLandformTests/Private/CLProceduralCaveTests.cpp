#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLSpatialCave.h"
#include "Scene/CLScene.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLApprovedCaveAnatomyTest,"Catalyst.Landform.Caves.ApprovedPlacement.AnatomyFloorsAndStrata",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLApprovedCaveAnatomyTest::RunTest(const FString&)
{
    const auto* Asset=LoadObject<UCLPlanetRecipeAsset>(nullptr,TEXT("/Game/LandformDesigner/Provinces/DA_ContinentalRiftReview"));
    if(!TestNotNull(TEXT("Actual review planet"),Asset))return false;
    for(int32 Seed:{176,731,2049})
    {
        auto R=Asset->Recipe;R.Caves.bEnabled=true;R.Caves.Seed=Seed;R.Caves.ApplyProgression(1.);FString E;
        const auto P=FCLPlanetCompiler::Compile(R,E);if(!P||!P->Caves){AddError(E);return false;}
        const auto& C=*P->Caves;const auto& Shape=*C.Spatial;
        TestEqual(TEXT("One approved chamber per placed composition"),C.Chambers.Num(),C.Networks);
        TestEqual(TEXT("One approved passage per placed composition; no closed gallery"),C.Passages.Num(),C.Networks);
        TestEqual(TEXT("No appended entrance tails"),C.Entrances,0);
        auto Floor=[&](double X,double Y)
        {double Low=-8,High=8;for(int K=0;K<19;++K){const double Z=(Low+High)*.5;if(C.Cutters->Compose(Shape.BodyPoint({X,Y,Z}),-1000)>0)High=Z;else Low=Z;}return (Low+High)*.5;};
        double Minimum=MAX_dbl,Maximum=-MAX_dbl,Grade=0;TArray<FVector> Probes;
        for(double X=-100;X<=25;X+=.5)
        {
            const double Y=-3+2*FMath::Sin(X*.045),F=Floor(X,Y);Minimum=FMath::Min(Minimum,F);Maximum=FMath::Max(Maximum,F);Grade=FMath::Max(Grade,FMath::Abs(Floor(X+.1,Y)-F)/.1);
            for(double Side:{-.5,0.,.5})
            {
                const double SideFloor=Floor(X,Y+Side);const auto Head=Shape.BodyPoint({X,Y+Side,F+1.92}),Foot=Shape.BodyPoint({X,Y+Side,SideFloor-.35});Probes.Add(Head);Probes.Add(Foot);
                TestTrue(TEXT("Full-width head clearance through passage and chamber transition"),C.Cutters->Compose(Head,-1000)>.35);
                TestTrue(TEXT("Continuous actual rock foundation"),C.Cutters->Compose(Foot,-1000)<-.10);
            }
        }
        TestTrue(TEXT("Approved rolling floor retains metre-scale relief"),Maximum-Minimum>1.);
        TestTrue(TEXT("Approved floor remains walkable"),Grade<FMath::Tan(FMath::DegreesToRadians(30.)));
        TArray<double> Widths,Roofs;
        for(double X:{-90.,-65.,-40.,-15.,0.,20.})
        {
            const double F=Floor(X,-3);double Left=0,Right=0,Roof=0;
            for(double Y=-3;Y>-60;Y-=.25){if(C.Cutters->Compose(Shape.BodyPoint({X,Y,F+3}),-1000)<=0)break;Left=-3-Y;}
            for(double Y=-3;Y<60;Y+=.25){if(C.Cutters->Compose(Shape.BodyPoint({X,Y,F+3}),-1000)<=0)break;Right=Y+3;}
            for(double Z=F+2;Z<50;Z+=.25){if(C.Cutters->Compose(Shape.BodyPoint({X,-3,Z}),-1000)<=0)break;Roof=Z-F;}
            Widths.Add(Left+Right);Roofs.Add(Roof);
        }
        TestTrue(TEXT("Mammoth passage retains broad proportions"),Widths[0]>20&&Roofs[0]>8);
        TestTrue(TEXT("Carlsbad chamber retains its large independent roof and width"),Widths.Last()>65&&Roofs.Last()>27);
        TestTrue(TEXT("Chamber widens substantially from passage"),Widths.Last()>Widths[0]*2);
        TArray<double> Native,Scalar;Shape.Sample(Probes,Native);Shape.Sample(Probes,Scalar,false);double Residual=0;
        for(int I=0;I<Probes.Num();++I)Residual=FMath::Max(Residual,FMath::Abs(Native[I]-Scalar[I]));
        TestTrue(TEXT("Approved scalar and native SIMD morphology agree"),Residual<.001);
        auto Off=R;Off.Caves.bEnabled=false;const auto Uncut=FCLPlanetCompiler::Compile(Off,E);if(!Uncut){AddError(E);return false;}
        const auto Scene=FCLScene::FromPlanet(P,E),Natural=FCLScene::FromPlanet(Uncut,E);TArray<FCLSceneSurfaceSample> A,B;TArray<FCLRockSample> GA,GB;
        Scene->SampleSurfaces({Probes},A,E);Natural->SampleSurfaces({Probes},B,E);Scene->SampleGeology({Probes},GA,E);Natural->SampleGeology({Probes},GB,E);
        for(int I=0;I<Probes.Num();++I){TestEqual(TEXT("Caves preserve rock stratum identity"),GA[I].StratumId,GB[I].StratumId);TestEqual(TEXT("Caves preserve geological material binding count"),A[I].Weights.Num(),B[I].Weights.Num());for(int J=0;J<A[I].Weights.Num();++J){TestEqual(TEXT("Strata surface IDs are unchanged"),A[I].Weights[J].Id,B[I].Weights[J].Id);TestEqual(TEXT("Strata surface weights are unchanged"),A[I].Weights[J].Weight,B[I].Weights[J].Weight);}}
        AddInfo(FString::Printf(TEXT("APPROVED_CAVE seed=%d networks=%d passageWidth=%.2f chamberWidth=%.2f chamberHeight=%.2f floorRelief=%.3f grade=%.4f scalarResidual=%.9g"),Seed,C.Networks,Widths[0],Widths.Last(),Roofs.Last(),Maximum-Minimum,Grade,Residual));
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLApprovedCaveSelectionTest,"Catalyst.Landform.Caves.ApprovedPlacement.DeterminismAndSelection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLApprovedCaveSelectionTest::RunTest(const FString&)
{
    auto R=FCLPlanetCompiler::BasePlanet();R.NoiseLayers.Reset();R.Caves.bEnabled=true;R.Caves.ApplyProgression(1.);FString E;
    const auto P=FCLPlanetCompiler::Compile(R,E),Again=FCLPlanetCompiler::Compile(R,E);if(!P||!Again){AddError(E);return false;}
    const auto& C=*P->Caves;TestTrue(TEXT("Density retains multiple independent approved caves"),C.Networks>1);
    TestEqual(TEXT("Seed preserves count"),C.Networks,Again->Caves->Networks);
    for(int I=0;I<C.Passages.Num();++I)TestTrue(TEXT("Seed preserves exact placement and floor route"),C.Passages[I].Points==Again->Caves->Passages[I].Points);
    const auto Selected=C.Cutters->Select(C.NetworkInfo[0].Id);
    for(const auto& N:C.Nodes)TestTrue(TEXT("Native inspection selection contains only its own composition"),N.Network==C.NetworkInfo[0].Id?Selected->Compose(N.Position,-1000)>.5:Selected->Compose(N.Position,-1000)<0);
    for(bool Entrances:{false,true}){R.Caves.bSurfaceEntrances=Entrances;const auto V=FCLPlanetCompiler::Compile(R,E);if(!V){AddError(E);return false;}TestEqual(TEXT("Saved entrance setting cannot reinstate rejected tails"),V->Caves->Entrances,0);for(const auto& Room:V->Caves->Chambers)TestEqual(TEXT("All delivered chambers use the approved anatomy"),Room.GeometryProfile,8);}
    return !HasAnyErrors();
}
#endif

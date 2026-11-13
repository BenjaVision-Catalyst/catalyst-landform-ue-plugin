#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Model/CLPlanetRecipe.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "UObject/StrongObjectPtr.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLOrganicKarstTest,"Catalyst.Landform.Caves.OrganicKarst.ReliefAndOpenings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLOrganicKarstTest::RunTest(const FString&)
{
    TestEqual(TEXT("New typed cave settings select the current profile"),FCLCaveSettings().AlgorithmVersion,7);
    FString E,J;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/OrganicKarst.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());if(!A->ImportRecipeJson(J,E)){AddError(E);return false;}
    const auto P=FCLPlanetCompiler::Compile(A->Recipe,E);if(!P||!P->Caves||P->Caves->Chambers.IsEmpty()){AddError(E);return false;}
    const auto& C=*P->Caves;double LowRelief=MAX_dbl,HighRelief=-MAX_dbl,MaxGrade=0,Asymmetry=0;int32 Floors=0,Widths=0;
    auto Floor=[&](const FVector& Q,double Datum)
    {
        const auto Up=Q.GetSafeNormal();const double R=Datum+P->Rock->SampleColumn(Up).DeformationMeters;
        double Low=-3.,High=4.;
        TestTrue(TEXT("Foundation remains solid beneath the rolling floor"),C.Cutters->Compose(Up*(R+Low),-1000)<0);
        TestTrue(TEXT("Walking space remains open above the rolling floor"),C.Cutters->Compose(Up*(R+High),-1000)>0);
        for(int32 K=0;K<14;++K){const double Mid=(Low+High)*.5;if(C.Cutters->Compose(Up*(R+Mid),-1000)>0)High=Mid;else Low=Mid;}
        const double Relief=(Low+High)*.5;LowRelief=FMath::Min(LowRelief,Relief);HighRelief=FMath::Max(HighRelief,Relief);++Floors;
        const FVector F=Up*(R+Relief);TestTrue(TEXT("Standing capsule has air clearance over the actual floor"),C.Cutters->Compose(F+Up*1.92,-1000)>.12);return F;
    };
    for(const auto& V:C.Passages)if(!V.Entrance)
    {
        TestEqual(TEXT("New shape is explicitly versioned"),V.GeometryProfile,7);
        for(int32 I=0;I<V.Points.Num();I+=3)
        {
            const FVector F=Floor(V.Points[I],V.FloorRadii[I]);
            const FVector Along=(V.Points[FMath::Min(I+1,V.Points.Num()-1)]-V.Points[FMath::Max(I-1,0)]).GetSafeNormal();
            if(!Along.IsNearlyZero())
            {
                const FVector Other=Floor(V.Points[I]+Along*.5,V.FloorRadii[I]);
                const FVector Up=(F+Other).GetSafeNormal();const double Run=FVector::VectorPlaneProject(Other-F,Up).Length();
                if(Run>.1)MaxGrade=FMath::Max(MaxGrade,FMath::Abs(FVector::DotProduct(Other-F,Up))/Run);
            }
        }
        const int32 I=V.Points.Num()/2;const FVector F=Floor(V.Points[I],V.FloorRadii[I]),Up=F.GetSafeNormal();
        const FVector Side=FVector::CrossProduct(Up,V.Points.Last()-V.Points[0]).GetSafeNormal();double W[2]={0,0};
        for(int32 K=0;K<2;++K)for(double D=.25;D<=30;D+=.25)
        {if(C.Cutters->Compose(F+Up*3+Side*(K?-D:D),-1000)<=0)break;W[K]=D;}
        Asymmetry+=FMath::Abs(W[0]-W[1]);++Widths;
    }
    for(const auto& Room:C.Chambers)
    {
        TestEqual(TEXT("Room keeps the same organic floor/shape profile"),Room.GeometryProfile,7);
        const FVector Node=C.Nodes[Room.Node].Position;
        for(double X:{-10.,0.,10.})for(double Y:{-6.,0.,6.})Floor(Node+Room.Rotation.RotateVector(FVector(X,Y,0)),Room.FloorRadius);
    }
    TestTrue(TEXT("Floor has visible large-scale relief rather than one broad plane"),HighRelief-LowRelief>.50);
    TestTrue(TEXT("Continuous walking floor grades stay below 35 degrees at junction probes"),MaxGrade<FMath::Tan(FMath::DegreesToRadians(35.)));
    TestTrue(TEXT("Actual passage openings have unequal lateral extents"),Widths>0&&Asymmetry/Widths>.30);
    AddInfo(FString::Printf(TEXT("ORGANIC_KARST %d networks, %d passages, %d rooms; %d floor probes; relief %.3f..%.3f m; maximum grade %.3f; mean opening asymmetry %.3f m"),C.Networks,C.Passages.Num(),C.Chambers.Num(),Floors,LowRelief,HighRelief,MaxGrade,Widths?Asymmetry/Widths:0));
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLOrganicKarstPackageTest,"Catalyst.Landform.Caves.OrganicKarst.NativePackage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLOrganicKarstPackageTest::RunTest(const FString&)
{
    const auto* Saved=LoadObject<UCLPlanetRecipeAsset>(nullptr,TEXT("/Game/LandformDesigner/Caves/DA_OrganicKarst.DA_OrganicKarst"));
    if(!TestNotNull(TEXT("Separate organic review asset exists"),Saved))return false;
    TestEqual(TEXT("Current family is explicitly saved as profile 7"),Saved->Recipe.Caves.AlgorithmVersion,7);
    const auto* Historical=LoadObject<UCLPlanetRecipeAsset>(nullptr,TEXT("/Game/LandformDesigner/Caves/DA_BeddedKarst.DA_BeddedKarst"));
    if(TestNotNull(TEXT("Historical review asset remains available"),Historical))TestEqual(TEXT("Historical asset retains profile 6"),Historical->Recipe.Caves.AlgorithmVersion,6);
    FString E,J;TStrongObjectPtr<UCLPlanetRecipeAsset> Copy(NewObject<UCLPlanetRecipeAsset>());
    if(!Saved->ExportJsonString(J,E)||!Copy->ImportRecipeJson(J,E)){AddError(E);return false;}
    const auto A=FCLPlanetCompiler::Compile(Saved->Recipe,E),B=FCLPlanetCompiler::Compile(Copy->Recipe,E);if(!A||!B){AddError(E);return false;}
    for(const auto& P:A->Caves->Passages)for(const auto& Q:P.Points)
        TestTrue(TEXT("Saved JSON reproduces the native organic field"),FMath::Abs(A->Caves->Cutters->Compose(Q,-1000)-B->Caves->Cutters->Compose(Q,-1000))<1.e-6);
    return !HasAnyErrors();
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Model/CLPlanetRecipe.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "UObject/StrongObjectPtr.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLBeddedKarstTest,"Catalyst.Landform.Caves.BeddedKarst.FloorsAndScale",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLBeddedKarstTest::RunTest(const FString&)
{
    FString E,J;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/BeddedKarst.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());if(!A->ImportRecipeJson(J,E)){AddError(E);return false;}
    const auto P=FCLPlanetCompiler::Compile(A->Recipe,E);if(!P||!P->Caves||P->Caves->Chambers.IsEmpty()){AddError(E);return false;}
    const auto& C=*P->Caves;TestTrue(TEXT("Fixture develops complete eligible networks"),C.Networks>0);
    double MaxFloorError=0;int32 FloorProbes=0;double MinWidth=MAX_dbl,MaxWidth=0;
    auto Floor=[&](FVector Q,double Expected)
    {
        const FVector D=Q.GetSafeNormal();const double Radius=Expected+P->Rock->SampleColumn(D).DeformationMeters;
        double Low=-.7,High=.7;
        TestTrue(TEXT("Solid foundation below the walking floor"),C.Cutters->Compose(D*(Radius+Low),-1000)<-.2);
        const double Air=C.Cutters->Compose(D*(Radius+High),-1000);TestTrue(FString::Printf(TEXT("Air above floor at %s: %.4f"),*Q.ToString(),Air),Air>.2);
        if(Air<=.2)for(const auto& B:C.Infills)if(FVector::Distance(Q,B.Center)<B.HalfExtents.GetMax()+10)AddInfo(FString::Printf(TEXT("Nearby infill pier=%d center=%s size=%s local=%s provenance=%s"),B.BedrockPier,*B.Center.ToString(),*B.HalfExtents.ToString(),*B.Rotation.UnrotateVector(Q-B.Center).ToString(),*C.Cutters->GeologicalPosition(D*(Radius+High)).ToString()));
        for(int32 I=0;I<12;++I){const double Mid=(Low+High)*.5;if(C.Cutters->Compose(D*(Radius+Mid),-1000)>0)High=Mid;else Low=Mid;}
        MaxFloorError=FMath::Max(MaxFloorError,FMath::Abs((Low+High)*.5));++FloorProbes;
    };
    for(const auto& V:C.Passages)
    {
        TestEqual(TEXT("One bedded passage family, including entrance"),V.Form,ECLCaveForm::BeddedPassage);
        if(V.Entrance){for(int32 I=2;I<V.Points.Num()-2;I+=2)Floor(V.Points[I],V.FloorRadii[I]);continue;}
        for(int32 I=0;I<V.Points.Num();++I)
        {
            MinWidth=FMath::Min(MinWidth,V.Sections[I].X*2);MaxWidth=FMath::Max(MaxWidth,V.Sections[I].X*2);
            // Exact shared nodes and independent lateral points test the floor,
            // not merely the open graph core or an ellipsoid's metadata.
            if(I==0||I==V.Points.Num()-1||I==V.Points.Num()/2)
            {
                Floor(V.Points[I],V.FloorRadii[I]);
                const FVector Side=FVector::CrossProduct(V.Points[I].GetSafeNormal(),V.Points.Last()-V.Points[0]).GetSafeNormal();
                for(double Sign:{-1.,1.})Floor(V.Points[I]+Side*V.Sections[I].X*.50*Sign,V.FloorRadii[I]);
            }
        }
    }
    for(const auto& Room:C.Chambers)
    {
        TestTrue(TEXT("Chamber has major scale above passage height"),Room.HalfExtents.X*2>=75&&Room.HalfExtents.Z*2>=26);
        const FVector Node=C.Nodes[Room.Node].Position;
        for(double X:{-8.,0.,8.})for(double Y:{-5.,0.,5.})Floor(Node+Room.Rotation.RotateVector(FVector(X,Y,0)),Room.FloorRadius);
        int32 OpenWidth=0;
        for(int32 I=-20;I<=20;++I)
        {
            FVector Q=Node+Room.Rotation.GetAxisY()*(I*2.);const FVector D=Q.GetSafeNormal();
            Q=D*(Room.FloorRadius+P->Rock->SampleColumn(D).DeformationMeters+1.64);
            OpenWidth+=C.Cutters->Compose(Q,-1000)>.2;
        }
        TestTrue(TEXT("Actual chamber air spans at least 30 m across at human eye height"),OpenWidth*2>=30);
        const FVector D=Node.GetSafeNormal();const FVector High=D*(Room.FloorRadius+P->Rock->SampleColumn(D).DeformationMeters+20);
        TestTrue(TEXT("Actual chamber has at least 20 m of vertical air above the common floor"),C.Cutters->Compose(High,-1000)>.2);
    }
    TestTrue(TEXT("Broad floor remains within 30 cm of common bedding datum at mouths and across rooms"),MaxFloorError<.30);
    TestTrue(TEXT("Review passage width is human-scale and exceeds a narrow tube"),MinWidth>7&&MaxWidth>20);
    TestTrue(TEXT("Room buttresses are native solid terrain with unchanged in-place geology"),!C.Infills.IsEmpty());
    int32 Piers=0,Blocks=0,Remnants=0;for(const auto& B:C.Infills){Piers+=B.BedrockPier;Blocks+=!B.BedrockPier&&!B.BeddedRemnant;Remnants+=B.BeddedRemnant;}
    TestTrue(TEXT("Both buttresses and marginal breakdown remain after reserving the walking corridor"),Piers>0&&Blocks>0&&Remnants>0);
    for(const auto& B:C.Infills)if(B.BedrockPier){TestTrue(TEXT("In-place buttress preserves rock position"),B.BedrockPier&&B.Center==B.SourceCenter&&C.Cutters->GeologicalPosition(B.Center)==B.Center);}
    AddInfo(FString::Printf(TEXT("BEDDED_KARST %d networks; %d passages; %d rooms; widths %.2f..%.2f m; %d floor probes; maximum floor offset %.4f m; %d piers, %d blocks and %d broken wall remnants"),C.Networks,C.Passages.Num(),C.Chambers.Num(),MinWidth,MaxWidth,FloorProbes,MaxFloorError,Piers,Blocks,Remnants));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLBeddedKarstPackageTest,"Catalyst.Landform.Caves.BeddedKarst.NativePackage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLBeddedKarstPackageTest::RunTest(const FString&)
{
    const auto* Saved=LoadObject<UCLPlanetRecipeAsset>(nullptr,TEXT("/Game/LandformDesigner/Caves/DA_BeddedKarst.DA_BeddedKarst"));
    if(!TestNotNull(TEXT("Separate create-only review package exists"),Saved))return false;
    TestEqual(TEXT("Saved native package pins the new morphology"),Saved->Recipe.Caves.AlgorithmVersion,6);
    FString E,JFixture;FFileHelper::LoadFileToString(JFixture,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/BeddedKarst.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> Fixture(NewObject<UCLPlanetRecipeAsset>());if(!Fixture->ImportRecipeJson(JFixture,E)){AddError(E);return false;}
    TestEqual(TEXT("Native package retains the reference's thin partings"),Saved->Recipe.Rock.InterbedFraction,Fixture->Recipe.Rock.InterbedFraction);
    for(int32 I=0;I<Saved->Recipe.Rock.Units.Num();++I)TestEqual(TEXT("Native carbonate faces retain the reference's existing rock material"),Saved->Recipe.Rock.Units[I].SurfaceId,Fixture->Recipe.Rock.Units[I].SurfaceId);
    const auto P=FCLPlanetCompiler::Compile(Saved->Recipe,E);if(!P||!P->Caves){AddError(E);return false;}
    TStrongObjectPtr<UCLPlanetRecipeAsset> Reload(NewObject<UCLPlanetRecipeAsset>());FString J;
    if(!Saved->ExportJsonString(J,E)||!Reload->ImportRecipeJson(J,E)){AddError(E);return false;}
    const auto Q=FCLPlanetCompiler::Compile(Reload->Recipe,E);if(!Q||!Q->Caves){AddError(E);return false;}
    TestEqual(TEXT("JSON round trip retains complete floor-bearing passages"),P->Caves->Passages.Num(),Q->Caves->Passages.Num());
    for(int32 I=0;I<P->Caves->Passages.Num();++I)
    {
        const auto& A=P->Caves->Passages[I];const auto& B=Q->Caves->Passages[I];
        TestTrue(TEXT("Native package and frozen JSON reproduce floor, roof and plan"),A.Points==B.Points&&A.Sections==B.Sections&&A.FloorRadii==B.FloorRadii&&A.Form==B.Form);
        for(int32 K=0;K<A.Points.Num();++K)TestTrue(TEXT("Native field reproduces walking height"),FMath::Abs(P->Caves->Cutters->Compose(A.Points[K],-1000)-Q->Caves->Cutters->Compose(A.Points[K],-1000))<1.e-6);
    }
    return !HasAnyErrors();
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLSpatialCave.h"
#include "Scene/CLScene.h"
#include "CLRadialGuideEditing.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
FCLRadialGuide* Guide(FCLPlanetRecipe& R,FName Name)
{return R.RadialGuides.Guides.FindByPredicate([&](const auto& G){return G.Name==Name;});}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLCaveGuidesTest,"Catalyst.Landform.Caves.Placement.GuidesAndPersistence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLCaveGuidesTest::RunTest(const FString&)
{
    FString Error,Json;auto R=FCLPlanetCompiler::BasePlanet();
    TestTrue(TEXT("CavesMax defaults five physical metres below SurfaceMax"),FMath::IsNearlyEqual((Guide(R,TEXT("SurfaceMax"))->NormalizedHeight-Guide(R,TEXT("CavesMax"))->NormalizedHeight)*R.RadiusMeters,5.));
    const double Fixed=Guide(R,TEXT("CavesMin"))->NormalizedHeight;
    Guide(R,TEXT("SurfaceMin"))->NormalizedHeight=.4;Guide(R,TEXT("SurfaceMax"))->NormalizedHeight=.8;
    R.Caves.AddDefaultGuides(R.RadialGuides,R.RadiusMeters,R.BaseSurfaceNormalized);
    TestEqual(TEXT("CavesMin remains fixed when surface guides move"),Guide(R,TEXT("CavesMin"))->NormalizedHeight,Fixed);
    TestEqual(TEXT("Existing CavesMax is not overwritten"),Guide(R,TEXT("CavesMax"))->NormalizedHeight,.595);
    const int32 Index=R.RadialGuides.Guides.IndexOfByPredicate([](const auto& G){return G.Name==TEXT("CavesMin");});
    TestTrue(TEXT("Existing guide editor renames the cave binding"),FCLRadialGuideEditing::Rename(R,Index,TEXT("DeepCaves"),Error));
    TestEqual(TEXT("Placement follows the renamed guide"),R.Caves.MinimumGuide,FName(TEXT("DeepCaves")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> Asset(NewObject<UCLPlanetRecipeAsset>()),Copy(NewObject<UCLPlanetRecipeAsset>());
    Asset->Recipe=R;TestTrue(TEXT("Cave guides export"),Asset->ExportJsonString(Json,Error));TestTrue(TEXT("Cave guides import"),Copy->ImportRecipeJson(Json,Error));
    TestEqual(TEXT("Named cave binding persists"),Copy->Recipe.Caves.MinimumGuide,R.Caves.MinimumGuide);
    TestEqual(TEXT("Edited cave radius persists"),Guide(Copy->Recipe,TEXT("DeepCaves"))->NormalizedHeight,Fixed);
    // An old catalog must not repeatedly replace the new planet-local defaults.
    TStrongObjectPtr<UCLRadialGuideLibraryAsset> Library(NewObject<UCLRadialGuideLibraryAsset>());
    Library->Library.Sets.Add(FCLRadialGuidePresets::Defaults()[0]);Asset->SetRadialGuideSet(Library.Get(),Library->Library.Sets[0].Id,Error);
    Asset->Recipe.Caves.MinimumGuide=TEXT("CavesMin");Asset->Recipe.Caves.AddDefaultGuides(Asset->Recipe.RadialGuides,R.RadiusMeters,R.BaseSurfaceNormalized);
    Guide(Asset->Recipe,TEXT("CavesMin"))->NormalizedHeight=.1;Asset->SynchronizeRadialGuides(Error);
    TestEqual(TEXT("Legacy catalog synchronization retains cave edits"),Guide(Asset->Recipe,TEXT("CavesMin"))->NormalizedHeight,.1);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLCaveDepthTest,"Catalyst.Landform.Caves.Placement.DepthRangeAndShapes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLCaveDepthTest::RunTest(const FString&)
{
    auto R=FCLPlanetCompiler::BasePlanet();R.RadiusMeters=10000;R.BaseSurfaceNormalized=.7;R.Caves.bEnabled=true;R.Caves.AlgorithmVersion=9;R.Caves.bSurfaceEntrances=false;R.Caves.NetworksPerSquareKilometer=.015;
    Guide(R,TEXT("SurfaceMin"))->NormalizedHeight=.68;Guide(R,TEXT("SurfaceMax"))->NormalizedHeight=.72;
    Guide(R,TEXT("CavesMin"))->NormalizedHeight=.2;Guide(R,TEXT("CavesMax"))->NormalizedHeight=.7195;
    FString Error;const auto A=FCLPlanetCompiler::Compile(R,Error);if(!A){AddError(Error);return false;}
    double Low,High,Cover;R.Caves.ResolvePlacement(*A,Low,High,Cover,Error);
    double Shallow=0,Deep=MAX_dbl;
    for(const auto& N:A->Caves->NetworkInfo)
    {
        double Mean=0;int32 Count=0;for(const auto& V:A->Caves->Nodes)if(V.Network==N.Id){Mean+=V.Position.Length();++Count;TestTrue(TEXT("Cave nodes stay within editable radial band"),V.Position.Length()>=Low&&V.Position.Length()<=High);}
        Mean/=Count;Shallow=FMath::Max(Shallow,Mean);Deep=FMath::Min(Deep,Mean);
    }
    TestTrue(TEXT("Networks occupy different radial depths, not one shell"),Shallow-Deep>1000);
    TestEqual(TEXT("Disabling entrances publishes no surface openings"),A->Caves->Entrances,0);
    auto Changed=R;Guide(Changed,TEXT("CavesMin"))->NormalizedHeight=.3;Guide(Changed,TEXT("CavesMax"))->NormalizedHeight=.67;
    const auto B=FCLPlanetCompiler::Compile(Changed,Error);if(!B){AddError(Error);return false;}
    TestTrue(TEXT("Guide edits actually move generated caves"),!A->Caves->Nodes[0].Position.Equals(B->Caves->Nodes[0].Position,.01));
    TestEqual(TEXT("Same stream retains the first network identity"),A->Caves->NetworkInfo[0].Id,B->Caves->NetworkInfo[0].Id);
    TestEqual(TEXT("Moving guides preserves its generated topology"),A->Caves->Passages[0].Points.Num(),B->Caves->Passages[0].Points.Num());
    for(int32 I=0;I<A->Caves->Passages[0].Points.Num();++I)
    {
        const auto PA=A->Caves->Spatial->BeddingPoint(A->Caves->Passages[0].Points[I]),PB=B->Caves->Spatial->BeddingPoint(B->Caves->Passages[0].Points[I]);
        TestTrue(TEXT("Guide edits preserve local passage shape"),PA.Equals(PB,.001));
    }
    const auto Again=FCLPlanetCompiler::Compile(R,Error);if(!Again){AddError(Error);return false;}
    for(int32 I=0;I<A->Caves->Passages.Num();++I)TestTrue(TEXT("Depth placement regenerates exactly"),A->Caves->Passages[I].Points==Again->Caves->Passages[I].Points);
    auto Named=R;const int32 MinimumIndex=Named.RadialGuides.Guides.IndexOfByPredicate([](const auto& G){return G.Name==TEXT("CavesMin");});
    FCLRadialGuideEditing::Rename(Named,MinimumIndex,TEXT("DeepCaves"),Error);
    const auto Renamed=FCLPlanetCompiler::Compile(Named,Error);if(!Renamed){AddError(Error);return false;}
    TestFalse(TEXT("Cave base compilation does not invent an alias for a renamed guide"),Renamed->Guides.ContainsByPredicate([](const auto& G){return G.Name==TEXT("CavesMin");}));
    Guide(Changed,TEXT("CavesMin"))->NormalizedHeight=.9;
    TestFalse(TEXT("Reversed placement guides are diagnosed"),FCLPlanetCompiler::Compile(Changed,Error).IsValid());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLCaveSurfaceOpeningTest,"Catalyst.Landform.Caves.Placement.RiftSurfaceOpenings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLCaveSurfaceOpeningTest::RunTest(const FString&)
{
    const auto* Asset=LoadObject<UCLPlanetRecipeAsset>(nullptr,TEXT("/Game/LandformDesigner/Provinces/DA_ContinentalRiftReview"));if(!TestNotNull(TEXT("Saved Rift authoring asset"),Asset))return false;
    auto R=Asset->Recipe;R.Caves.bEnabled=true;R.Caves.bSurfaceEntrances=true;FString Error;
    const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}
    TestTrue(TEXT("Saved Rift has broad openings to the outside"),P->Caves->Entrances>0);
    TestEqual(TEXT("Entrance count records actual terrain intersections"),P->Caves->Entrances,P->Caves->SurfaceOpenings.Num());
    auto Off=R;Off.Caves.bEnabled=false;const auto Base=FCLPlanetCompiler::Compile(Off,Error);if(!Base){AddError(Error);return false;}
    const auto Scene=FCLScene::FromPlanet(P,Error),Uncut=FCLScene::FromPlanet(Base,Error);
    for(const auto& E:P->Caves->SurfaceOpenings)
    {
        TestTrue(TEXT("The mouth has a broad physical aperture"),E.WidthMeters>3.);
        const auto Up=E.SurfacePoint.GetSafeNormal();FVector X,Y;Up.FindBestAxisVectors(X,Y);
        TArray<FVector> Probes;for(double Offset:{-.3,0.,.3})
        {const auto D=(E.SurfacePoint+X*(E.WidthMeters*Offset)).GetSafeNormal();Probes.Add(D*(P->RadiusMeters+P->SampleHeightMeters(D)-.25));}
        TArray<FCLSceneShapeSample> Cut,Natural;Scene->SampleShape({Probes},Cut,Error);Uncut->SampleShape({Probes},Natural,Error);
        for(int32 I=0;I<Probes.Num();++I){TestTrue(TEXT("Actual solid terrain is cut open across the mouth"),Natural[I].FieldMeters<0&&Cut[I].FieldMeters>0);}
        TestTrue(TEXT("Mouth connects to real covered cave air"),P->Caves->Cutters->Compose(E.InteriorPoint,-1000)>.3&&P->RadiusMeters+P->SampleHeightMeters(E.InteriorPoint.GetSafeNormal())-E.InteriorPoint.Length()>3.);
        TArray<FVector> Local;
        for(int32 A=0;A<8;++A)for(int32 B=0;B<8;++B)for(int32 C=0;C<8;++C)Local.Add(E.SurfacePoint+X*((A-3.5)*1.5)+Y*((B-3.5)*1.5)+Up*(C*1.5-12));
        TArray<double> Bulk,Exact,First,Second;P->Caves->Spatial->Sample(Local,Bulk);P->Caves->Spatial->Sample(Local,Exact,false);
        P->Caves->Spatial->Sample(MakeArrayView(Local).Left(256),First);P->Caves->Spatial->Sample(MakeArrayView(Local).Right(256),Second);
        double Residual=0;for(int32 I=0;I<Local.Num();++I){Residual=FMath::Max(Residual,FMath::Abs(Bulk[I]-Exact[I]));Residual=FMath::Max(Residual,FMath::Abs(Bulk[I]-(I<256?First[I]:Second[I-256])));}
        TestTrue(TEXT("Native bulk, split queries and exact canonical inputs agree at the entrance"),Residual<.001);
        AddInfo(FString::Printf(TEXT("CAVE_PLACEMENT_INPUT_RESIDUAL metres=%.9g"),Residual));
    }
    TestFalse(TEXT("Openings do not append substitute entrance geometry"),P->Caves->Passages.ContainsByPredicate([](const auto& V){return V.Entrance;}));
    AddInfo(FString::Printf(TEXT("CAVE_PLACEMENT networks=%d openings=%d savedCharacter=%.3f"),P->Caves->Networks,P->Caves->Entrances,R.Caves.Character));
    return !HasAnyErrors();
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Model/CLPlanetRecipe.h"
#include "Scene/CLScene.h"
#include "CLPlanetStamp.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Async/Async.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
#include "Surface/VoxelSurfaceTypeTable.h"
namespace
{
FCLPlanetRecipe WatershedRecipe(FString& E)
{
    FString J;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/Watershed.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());if(!A->ImportRecipeJson(J,E))return FCLPlanetRecipe();return A->Recipe;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLWatershedGraphTest,"Catalyst.Landform.Watershed.FinalTopologyAndRunoff",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLWatershedGraphTest::RunTest(const FString&)
{
    FString E;auto R=WatershedRecipe(E);if(!E.IsEmpty()){AddError(E);return false;}const auto P=FCLPlanetCompiler::Compile(R,E);if(!P){AddError(E);return false;}const auto W=P->Watershed;const auto& C=W->GetCells();
    TestTrue(TEXT("Native terrain has generated catchments, channels and an explicit potential lake"),W->Catchments>0&&W->GetChannels().Num()>0&&W->LakeCells>0);
    TestTrue(TEXT("Exact spherical control-cell area closes"),FMath::IsNearlyEqual(W->TotalAreaSquareMeters,4*UE_DOUBLE_PI*FMath::Square(W->ReferenceRadiusMeters),1.e-6));
    TMap<int32,double> Independent;double WorstHeight=0;
    for(int32 I=0;I<C.Num();++I)
    {
        const auto& V=C[I];WorstHeight=FMath::Max(WorstHeight,FMath::Abs(P->SampleHeightMeters(V.Direction)-V.HeightMeters));
        TestTrue(TEXT("Spill potential never lies below final terrain"),V.SpillMeters>=V.HeightMeters-1.e-8);
        if(V.Receiver>=0)TestTrue(TEXT("Receivers retain strict flood-rank order and shared outlet identity"),C[V.Receiver].Rank<V.Rank&&V.Outlet==C[V.Receiver].Outlet);
        else TestTrue(TEXT("Every terminal is ocean or declared internal drainage"),V.bTerminal&&V.Outlet==I);
        if(V.bOcean)continue;int32 At=I,Steps=0;while(C[At].Receiver>=0&&++Steps<=C.Num())At=C[At].Receiver;
        TestTrue(TEXT("Every land source reaches its recorded outlet without a cycle"),Steps<C.Num()&&At==V.Outlet);
        Independent.FindOrAdd(At)+=V.AreaSquareMeters*R.Watershed.ReferenceRainMetersPerYear/(365.25*86400)*R.Watershed.RunoffFraction;
    }
    for(const auto& V:Independent)TestTrue(TEXT("Independent source-to-outlet tracing closes reference runoff"),FMath::IsNearlyEqual(V.Value,C[V.Key].DischargeCubicMetersPerSecond,1.e-10));
    TestTrue(TEXT("Reference water accounting closes globally"),FMath::Abs(W->InputDischarge-W->TerminalDischarge)<1.e-10);
    int32 Graded=0,BadGrades=0;double WorstRise=0;
    for(const auto& Edge:W->GetChannels())
    {
        TestEqual(TEXT("Published graded channels still belong to final drainage"),C[Edge.From].Receiver,Edge.To);
        double Previous=DBL_MAX;
        for(int32 K=0;K<=32;++K){const auto D=FMath::Lerp(C[Edge.From].Direction,C[Edge.To].Direction,K/32.).GetSafeNormal();const double H=P->SampleHeightMeters(D);if(H>Previous+1.e-8){++BadGrades;WorstRise=FMath::Max(WorstRise,H-Previous);}Previous=H;++Graded;}
    }
    AddInfo(FString::Printf(TEXT("Uphill probes %d, worst rise %.12g m"),BadGrades,WorstRise));TestEqual(TEXT("Accepted native channel centerlines descend through their full edge"),BadGrades,0);
    AddInfo(FString::Printf(TEXT("Final nodal bulk/scalar discrepancy %.12g m"),WorstHeight));TestTrue(TEXT("Final node heights match the accepted native field within 0.1 mm"),WorstHeight<.0001);
    const auto F=Async(EAsyncExecution::ThreadPool,[P]{return P->SampleHeightMeters(FVector(1,2,3));});TestEqual(TEXT("Native spatial queries retain worker parity"),F.Get(),P->SampleHeightMeters(FVector(1,2,3)));
    AddInfo(FString::Printf(TEXT("%d cells, %d catchment outlets, %d graded reaches, %d lake cells, %d grade probes; cut %.3f m, fill %.3f m"),C.Num(),W->Catchments,W->GetChannels().Num(),W->LakeCells,Graded,W->MaximumMeasuredCutMeters,W->MaximumMeasuredFillMeters));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLWatershedCharacterTest,"Catalyst.Landform.Watershed.CharacterAndPhysicalResolution",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLWatershedCharacterTest::RunTest(const FString&)
{
    FString E;auto R=WatershedRecipe(E);if(!E.IsEmpty()){AddError(E);return false;}FString CSV=TEXT("seed,character,cells,catchments,graded_reaches,drainage_links,lake_cells,max_edge_m,max_cut_m,max_fill_m,runoff_error\n");
    for(int32 Seed:{1729,817,529})
    {
        double PreviousDepth=0;int32 FirstChannels=0,LastChannels=0;
        for(double T:{0.,.2,.4,.6,.8,1.})
        {
            R.Seed=Seed;R.Watershed.ApplyProgression(T);const auto P=FCLPlanetCompiler::Compile(R,E);if(!P){AddError(E);return false;}const auto W=P->Watershed;
            TestTrue(TEXT("Every reviewed knob position has bounded physical resolution and some active graded drainage"),W->MaximumEdgeMeters<=R.Watershed.ValleyWidthMeters&&W->GetChannels().Num()>0);
            TestTrue(TEXT("Character increases incision while retaining independently authored reference runoff"),R.Watershed.IncisionMeters>PreviousDepth&&R.Watershed.ReferenceRainMetersPerYear==1);PreviousDepth=R.Watershed.IncisionMeters;
            if(T==0)FirstChannels=W->GetChannels().Num();if(T==1)LastChannels=W->GetChannels().Num();
            CSV+=FString::Printf(TEXT("%d,%.2f,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.12g\n"),Seed,T,W->GetCells().Num(),W->Catchments,W->GetChannels().Num(),W->GetDrainageLinks().Num(),W->LakeCells,W->MaximumEdgeMeters,W->MaximumMeasuredCutMeters,W->MaximumMeasuredFillMeters,FMath::Abs(W->InputDischarge-W->TerminalDischarge));
        }
        TestTrue(TEXT("The knob changes drainage hierarchy, not just channel depth"),LastChannels>FirstChannels);
    }
    const FString Dir=FPaths::ProjectSavedDir()/TEXT("Verification/watershed");IFileManager::Get().MakeDirectory(*Dir,true);TestTrue(TEXT("Physical watershed metrics export"),FFileHelper::SaveStringToFile(CSV,*(Dir/TEXT("character.csv"))));
    auto On=FCLPlanetCompiler::Compile(R,E);auto Dry=R;Dry.Watershed.ReferenceRainMetersPerYear=0;const auto P=FCLPlanetCompiler::Compile(Dry,E);if(!P){AddError(E);return false;}
    TestEqual(TEXT("Dry forcing retains historical landform geometry"),P->SampleHeightMeters(FVector(1,2,3)),On->SampleHeightMeters(FVector(1,2,3)));TestEqual(TEXT("Zero reference rain has zero reference discharge"),P->Watershed->TerminalDischarge,0.);
    R.Watershed.MaximumCells=1026;TestFalse(TEXT("Insufficient analysis budget refuses without silent coarsening"),FCLPlanetCompiler::Compile(R,E).IsValid());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLWatershedPersistenceTest,"Catalyst.Landform.Watershed.VersionsGeologyAndVolumes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLWatershedPersistenceTest::RunTest(const FString&)
{
    FString E;auto R=WatershedRecipe(E);if(!E.IsEmpty()){AddError(E);return false;}R.Rock.bEnabled=true;R.Rock.ApplyProgression(.58);const auto P=FCLPlanetCompiler::Compile(R,E);if(!P){AddError(E);return false;}
    auto Off=R;Off.Watershed.bEnabled=false;const auto A=FCLPlanetCompiler::Compile(Off,E);Off.SchemaVersion=21;const auto B=FCLPlanetCompiler::Compile(Off,E);if(!A||!B){AddError(E);return false;}
    for(int32 I=0;I<256;++I){const FVector D=FVector(I+1,37-I,17).GetSafeNormal();TestEqual(TEXT("Disabled schema-22 state preserves schema-21 native heights exactly"),A->SampleHeightMeters(D),B->SampleHeightMeters(D));const auto V=D*600;TestEqual(TEXT("Water shaping exposes existing immutable strata"),P->Rock->Sample(V).StratumId,A->Rock->Sample(V).StratumId);}
    R.Volumes.bEnabled=true;FCLSolidOperation Cut;Cut.Id=FGuid(1,2,3,4);Cut.Name=TEXT("Post-watershed excavation");Cut.Primitive=ECLSolidPrimitive::Sphere;Cut.Composition=ECLSolidComposition::Subtract;Cut.RadiusMeters=8;Cut.CenterMeters=FVector::ZAxisVector*(R.RadiusMeters+P->SampleHeightMeters(FVector::ZAxisVector)-15);R.Volumes.Operations={Cut};
    const auto Cave=FCLPlanetCompiler::Compile(R,E);if(!Cave){AddError(E);return false;}TestEqual(TEXT("Excavation never regenerates natural catchments"),Cave->Watershed->GetCells()[300].Outlet,P->Watershed->GetCells()[300].Outlet);TestEqual(TEXT("Caves retain the accepted watershed surface"),Cave->SampleHeightMeters(FVector::ZAxisVector),P->SampleHeightMeters(FVector::ZAxisVector));
    const auto Scene=FCLScene::FromPlanet(Cave,E);TArray<FCLSceneShapeSample> Shape;Scene->SampleShape({TArray<FVector>{Cut.CenterMeters}},Shape,E);TestTrue(TEXT("Native volume composition actually excavates the watershed terrain"),Shape[0].FieldMeters>0);
    TStrongObjectPtr<UCLPlanetRecipeAsset> X(NewObject<UCLPlanetRecipeAsset>()),Y(NewObject<UCLPlanetRecipeAsset>());X->Recipe=R;FString J;TestTrue(TEXT("Resolved watershed exports"),X->ExportJsonString(J,E));TestTrue(TEXT("Resolved watershed imports"),Y->ImportRecipeJson(J,E));TestTrue(TEXT("Portable watershed data round-trips"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&X->Recipe,&Y->Recipe,0));
    R.SchemaVersion=21;TestFalse(TEXT("Historical schema cannot silently acquire watershed behavior"),FCLPlanetCompiler::Compile(R,E).IsValid());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLWatershedNativeTest,"Catalyst.Landform.Watershed.NativeZeroSetAndClosurePolicies",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLWatershedNativeTest::RunTest(const FString&)
{
    FString E;auto R=WatershedRecipe(E);if(!E.IsEmpty()){AddError(E);return false;}const auto P=FCLPlanetCompiler::Compile(R,E);if(!P){AddError(E);return false;}
    FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Radius=R.RadiusMeters*100;Stamp.Recipe=R;const auto Ref=FVoxelStampRef::New(Stamp);const auto Native=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,Ref,nullptr));if(!Native){AddError(TEXT("Native VPP refused watershed field"));return false;}
    FVoxelDoubleVectorBuffer Points;Points.Allocate(2048);TArray<FVector> D;
    for(int32 I=0;I<2048;++I){const double Z=1-2*(I+.5)/2048.,A=I*2.399963229728653;D.Add(FVector(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z));}
    TArray<double> H;P->SampleHeightsMeters(D,H);for(int32 I=0;I<D.Num();++I)Points.Set(I,D[I]*(R.RadiusMeters+H[I])*100);
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.Watershed"));const auto Table=FVoxelSurfaceTypeTable::Get();FVoxelQuery Query(0,FVoxelLayers::Empty(),*Table,Dependencies);const auto Dist=Native->SampleDistances(Query,Points);double Worst=0;for(int32 I=0;I<D.Num();++I)Worst=FMath::Max(Worst,FMath::Abs(double(Dist[I])));TestTrue(TEXT("Native VPP owns the accepted watershed zero set"),Worst<.03);
    auto Constant=R;Constant.NoiseLayers.Reset();Constant.BaseStructure.Reset();Constant.TerrainRegions.Reset();Constant.Continents.bEnabled=Constant.Tectonics.bEnabled=Constant.ElevationDistribution.bEnabled=false;Constant.BaseSurfaceNormalized=.5;
    auto Ocean=FCLPlanetCompiler::Compile(Constant,E);if(!Ocean){AddError(E);return false;}TestTrue(TEXT("All-ocean profiles remain valid without invented rivers"),Ocean->Watershed->OceanCells==Ocean->Watershed->GetCells().Num()&&Ocean->Watershed->GetChannels().IsEmpty());
    Constant.BaseSurfaceNormalized=.99;auto Land=FCLPlanetCompiler::Compile(Constant,E);if(!Land){AddError(E);return false;}TestTrue(TEXT("All-land profiles have an explicit closed terminal and no fabricated ocean"),Land->Watershed->OceanCells==0&&Land->Watershed->ClosedTerminals==1);
    AddInfo(FString::Printf(TEXT("2048 native zero-set probes; worst residual %.9f cm"),Worst));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLWatershedOceanTest,"Catalyst.Landform.Watershed.OceanLakeAndClosedBasin",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLWatershedOceanTest::RunTest(const FString&)
{
    FString E;auto R=WatershedRecipe(E);if(!E.IsEmpty()){AddError(E);return false;}
    R.ElevationDistribution.bEnabled=false;R.NoiseLayers.Reset();R.BaseStructure.Reset();R.BaseSurfaceNormalized=.7;R.RadiusMeters=1000;
    for(auto& G:R.RadialGuides.Guides)if(G.Name==TEXT("SurfaceMin"))G.NormalizedHeight=.4;else if(G.Name==TEXT("SurfaceMax"))G.NormalizedHeight=.8;else if(G.Name==TEXT("OceanHeight"))G.NormalizedHeight=.6;
    FCLPlanetFeature Ocean;Ocean.Id=FGuid(8,0,0,1);Ocean.Name=TEXT("Broad ocean basin");Ocean.LocationDegrees=FVector2D(90,0);Ocean.HalfExtentDegrees=FVector2D(60,60);Ocean.Relief.Fraction=-.5;
    auto Lake=Ocean;Lake.Id=FGuid(8,0,0,2);Lake.Name=TEXT("Isolated sub-sea depression");Lake.LocationDegrees=FVector2D(-90,0);Lake.HalfExtentDegrees=FVector2D(10,10);Lake.Relief.Fraction=-.625;R.BaseStructure={Ocean,Lake};
    const auto P=FCLPlanetCompiler::Compile(R,E);if(!P){AddError(E);return false;}const auto& C=P->Watershed->GetCells();
    TestTrue(TEXT("The broad connected sea is the reference ocean"),C[0].bOcean&&C[0].bTerminal);
    TestTrue(TEXT("An isolated depression below sea level remains an inland potential lake"),!C[1].bOcean&&C[1].bLake&&C[1].HeightMeters<-.4*R.RadiusMeters&&C[1].SpillMeters>C[1].HeightMeters);
    auto Before=R;Before.Watershed.bEnabled=false;const auto Base=FCLPlanetCompiler::Compile(Before,E);TestTrue(TEXT("The lake floor is not excavated into a through-channel"),Base&&FMath::IsNearlyEqual(Base->SampleHeightMeters(FVector(-1,0,0)),P->SampleHeightMeters(FVector(-1,0,0)),1.e-8));
    R.Watershed.ClosedBasinDirections={FVector(-1,0,0)};const auto Closed=FCLPlanetCompiler::Compile(R,E);if(!Closed){AddError(E);return false;}
    TestTrue(TEXT("An explicitly closed inland basin remains an internal terminal"),Closed->Watershed->GetCells()[1].bTerminal&&!Closed->Watershed->GetCells()[1].bOcean&&Closed->Watershed->GetCells()[1].Outlet==1);
    R.Watershed.ClosedBasinDirections.Add(FVector(-1,0,0));TestFalse(TEXT("Aliased terminals fail closed"),FCLPlanetCompiler::Compile(R,E).IsValid());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLWatershedContinentalTest,"Catalyst.Landform.Watershed.ContinentalRockIntegration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLWatershedContinentalTest::RunTest(const FString&)
{
    FString J,E;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/WatershedContinental.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());if(!A->ImportRecipeJson(J,E)){AddError(E);return false;}
    for(double T:{0.,.5,1.})
    {
        auto R=A->Recipe;R.Watershed.ApplyProgression(T);const auto P=FCLPlanetCompiler::Compile(R,E);if(!P){AddError(E);return false;}
        TestTrue(TEXT("Current crust, plates and physical strata remain shared with watersheds"),P->Continents&&P->Tectonics&&P->Rock&&P->Watershed);
        TestTrue(TEXT("Every reviewed continental position has active graded drainage"),P->Watershed->GetChannels().Num()>0);
        AddInfo(FString::Printf(TEXT("Continental character %.2f: %d graded reaches; measured cut %.3f m, fill %.3f m"),T,P->Watershed->GetChannels().Num(),P->Watershed->MaximumMeasuredCutMeters,P->Watershed->MaximumMeasuredFillMeters));
    }
    return true;
}
#endif

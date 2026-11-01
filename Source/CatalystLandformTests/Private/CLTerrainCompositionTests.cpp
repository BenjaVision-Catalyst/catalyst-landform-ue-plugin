#if WITH_DEV_AUTOMATION_TESTS
#include "Model/CLTerrainType.h"
#include "CLPlanetStamp.h"
#include "CLSectionGeometry.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Algo/Reverse.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
#include "Async/Async.h"
#include "UObject/UnrealType.h"

namespace
{
UCLTerrainTypeAsset* CompositionSource(const TCHAR* Name,FString& Error)
{
    FString Json;FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/TerrainTypes")/(FString(Name)+TEXT(".json"))));
    auto* Source=NewObject<UCLTerrainTypeAsset>();return Source->ImportTerrainTypeJson(Json,Error)?Source:nullptr;
}
bool CompositionRecipe(FCLPlanetRecipe& Recipe,FString& Error)
{
    auto* Mountains=CompositionSource(TEXT("MountainRidgesLocal"),Error);
    auto* Plateaus=CompositionSource(TEXT("PlateausLocal"),Error);if(!Mountains||!Plateaus)return false;
    Recipe=FCLPlanetCompiler::RollingPlains();Recipe.RadiusMeters=1000;
    if(!Mountains->TerrainType.ApplyTo(Recipe,Error))return false;
    FCLTerrainRegion Region;Region.Id=FGuid(0x434C5247,0,0,1);Region.Name=TEXT("Plateau band");Recipe.TerrainRegions.Add(Region);
    return Plateaus->TerrainType.ApplyToRegion(Recipe,Region.Id,Error);
}
TArray<double> CompositionSamples(const FCLCompiledPlanetRecipe& P,const TArray<FVector>& XY)
{TArray<double> H;P.SampleLocalHeightsMeters(XY,H);return H;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRegionMasksTest,"Catalyst.Landform.Composition.MasksAndValidation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRegionMasksTest::RunTest(const FString&)
{
    FString Error;FCLTerrainRegionMask Mask;
    TestTrue(TEXT("Default mask validates"),Mask.Validate(Error));
    for(double Sign:{-1.,1.})
    {
        TestEqual(TEXT("Outside band is pure upstream terrain"),Mask.Weight(FVector2D(Sign*250,0)),0.);
        TestEqual(TEXT("Inside band is pure region terrain"),Mask.Weight(FVector2D(Sign*50,0)),1.);
        TestTrue(TEXT("Nominal boundary is a half blend"),FMath::IsNearlyEqual(Mask.Weight(FVector2D(Sign*150,0)),.5,1.e-12));
        for(double Boundary:{50.,250.})
        {
            const double H=.01,X=Sign*Boundary;
            const double L=Mask.Weight(FVector2D(X-H,0)),C=Mask.Weight(FVector2D(X,0)),R=Mask.Weight(FVector2D(X+H,0));
            TestTrue(TEXT("Mask joins have zero first and second derivatives"),FMath::Abs((R-L)/(2*H))<1.e-7&&FMath::Abs((R-2*C+L)/(H*H))<1.e-6);
        }
    }
    Mask.CenterMeters=FVector2D(40,60);Mask.HeadingDegrees=90;
    TestTrue(TEXT("Mask heading and location are independent planet controls"),FMath::IsNearlyEqual(Mask.Weight(FVector2D(400,210)),.5,1.e-12));
    Mask.Shape=ECLTerrainRegionShape::Disc;TestEqual(TEXT("Disc core is full strength"),Mask.Weight(Mask.CenterMeters),1.);
    Mask.Shape=ECLTerrainRegionShape::HalfPlane;TestEqual(TEXT("Half-plane boundary is half strength"),Mask.Weight(Mask.CenterMeters),.5);
    Mask.TransitionMeters=0;TestFalse(TEXT("Zero-width seams are refused"),Mask.Validate(Error));
    FCLPlanetRecipe Recipe;if(!CompositionRecipe(Recipe,Error)){AddError(Error);return false;}
    auto Bad=Recipe;Bad.SchemaVersion=6;TestFalse(TEXT("Old schema cannot silently compose terrain"),FCLPlanetCompiler::Compile(Bad,Error).IsValid());
    Bad=Recipe;Bad.Coordinates=ECLTerrainCoordinates::Sphere;TestFalse(TEXT("Regional composition requires a shared local chart"),FCLPlanetCompiler::Compile(Bad,Error).IsValid());
    Bad=Recipe;const auto Duplicate=Bad.TerrainRegions[0];Bad.TerrainRegions.Add(Duplicate);TestFalse(TEXT("Duplicate region identities are refused"),FCLPlanetCompiler::Compile(Bad,Error).IsValid());
    Bad=Recipe;Bad.TerrainRegions[0].NoiseLayers[0].Minimum.Guide=TEXT("Missing");TestFalse(TEXT("Regions must resolve the parent guides"),FCLPlanetCompiler::Compile(Bad,Error).IsValid());
    Bad.TerrainRegions[0].bEnabled=false;TestTrue(TEXT("Disabled field does not consume compilation or require its hidden guides"),FCLPlanetCompiler::Compile(Bad,Error).IsValid());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRegionBlendTest,"Catalyst.Landform.Composition.IndependentFieldsAndSnapshots",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRegionBlendTest::RunTest(const FString&)
{
    FString Error;FCLPlanetRecipe R;if(!CompositionRecipe(R,Error)){AddError(Error);return false;}
    const auto Plan=FCLPlanetCompiler::Compile(R,Error);if(!Plan){AddError(Error);return false;}
    auto MountainRecipe=R;MountainRecipe.TerrainRegions.Reset();const auto Mountains=FCLPlanetCompiler::Compile(MountainRecipe,Error);
    auto PlateauRecipe=MountainRecipe;PlateauRecipe.NoiseLayers=R.TerrainRegions[0].NoiseLayers;const auto Plateaus=FCLPlanetCompiler::Compile(PlateauRecipe,Error);
    if(!Mountains||!Plateaus){AddError(Error);return false;}
    TestTrue(TEXT("Plateau terracing stays inside its independent field"),Plateaus->Layers.ContainsByPredicate([](const auto& L){return L.Operation==ECLTerrainOperation::Terrace;}));
    TArray<FVector> XY;for(int32 Y=-20;Y<=20;++Y)for(int32 X=-50;X<=50;++X)XY.Add(FVector(X*10.,Y*10.,0));
    const auto A=CompositionSamples(*Mountains,XY),B=CompositionSamples(*Plateaus,XY),C=CompositionSamples(*Plan,XY);
    double ErrorMax=0;int32 PureMountain=0,PurePlateau=0,Blended=0;
    for(int32 I=0;I<XY.Num();++I)
    {
        const double W=R.TerrainRegions[0].Mask.Weight(FVector2D(XY[I]));
        ErrorMax=FMath::Max(ErrorMax,FMath::Abs(C[I]-FMath::Lerp(A[I],B[I],W)));
        if(W==0){++PureMountain;TestEqual(TEXT("Outside mask preserves Mountain ridges exactly"),C[I],A[I]);}
        else if(W==1){++PurePlateau;TestEqual(TEXT("Plateau interior preserves terraced field exactly"),C[I],B[I]);}
        else ++Blended;
    }
    TestTrue(TEXT("Both pure terrains and their transition are sampled"),PureMountain>0&&PurePlateau>0&&Blended>0);
    TestTrue(TEXT("One convex blend of independent field heights"),ErrorMax<1.e-7);
    for(double Width:{100.,500.,1000.})
    {
        const FBox2D Region(FVector2D(-Width*.5),FVector2D(Width*.5));const auto Small=FCLPlanetCompiler::Compile(R,Error,nullptr,&Region);
        if(!Small){AddError(Error);return false;}
        const TArray<FVector> Probe={FVector(-40,0,0),FVector(0,0,0),FVector(40,10,0)};
        const auto Full=CompositionSamples(*Plan,Probe),Part=CompositionSamples(*Small,Probe);
        for(int32 I=0;I<Full.Num();++I)TestEqual(TEXT("Regional query windows preserve the composed field"),Full[I],Part[I]);
    }
    auto* Asset=NewObject<UCLPlanetRecipeAsset>();Asset->Recipe=R;FString Json;
    TestTrue(TEXT("Composed snapshot exports"),Asset->ExportJsonString(Json,Error));
    TestFalse(TEXT("Server recipe has no client asset paths"),Json.Contains(TEXT("/Game/"))||Json.Contains(TEXT("/CatalystLandform/")));
    auto* Imported=NewObject<UCLPlanetRecipeAsset>();TestTrue(TEXT("Composed snapshot imports"),Imported->ImportRecipeJson(Json,Error));
    const auto RoundTrip=FCLPlanetCompiler::Compile(Imported->Recipe,Error);if(!RoundTrip){AddError(Error);return false;}
    const auto Again=CompositionSamples(*RoundTrip,XY);for(int32 I=0;I<C.Num();++I)TestEqual(TEXT("Round trip preserves the full field"),C[I],Again[I]);
    // Canonical priority/GUID order, never authoring array order.
    auto Two=R;auto Extra=Two.TerrainRegions[0];Extra.Id=FGuid(0x434C5247,0,0,2);Extra.Priority=10;
    Extra.SeedOffset=991;Extra.Mask.Shape=ECLTerrainRegionShape::Disc;Extra.Mask.CenterMeters=FVector2D(90,30);Two.TerrainRegions.Add(Extra);
    const auto Ordered=FCLPlanetCompiler::Compile(Two,Error);Algo::Reverse(Two.TerrainRegions);const auto Reordered=FCLPlanetCompiler::Compile(Two,Error);
    if(!Ordered||!Reordered){AddError(Error);return false;}
    const auto H1=CompositionSamples(*Ordered,XY),H2=CompositionSamples(*Reordered,XY);
    for(int32 I=0;I<H1.Num();++I)TestEqual(TEXT("Array reorder cannot change explicit region priority"),H1[I],H2[I]);
    R.TerrainRegions[0].Mask.TransitionMeters=20;R.TerrainRegions[0].NoiseLayers.Reset();
    const auto Work=Async(EAsyncExecution::ThreadPool,[Plan,XY]{return CompositionSamples(*Plan,XY);});
    const auto Frozen=Work.Get();for(int32 I=0;I<C.Num();++I)TestEqual(TEXT("Compiled worker plan does not read authoring mutations"),C[I],Frozen[I]);
    AddInfo(FString::Printf(TEXT("Terrain composition: %d samples (%d mountain, %d plateau, %d blend); %.12g m oracle error"),XY.Num(),PureMountain,PurePlateau,Blended,ErrorMax));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRegionParityTest,"Catalyst.Landform.Composition.PreviewNativeSphereParity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRegionParityTest::RunTest(const FString&)
{
    FString Error;FCLPlanetRecipe R;if(!CompositionRecipe(R,Error)){AddError(Error);return false;}
    const auto Plan=FCLPlanetCompiler::Compile(R,Error);if(!Plan){AddError(Error);return false;}
    FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Radius=R.RadiusMeters*100;Stamp.Recipe=R;
    const auto Ref=FVoxelStampRef::New(Stamp);const auto Runtime=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,Ref,nullptr));
    if(!TestTrue(TEXT("Composed native volume initializes"),Runtime.IsValid()))return false;
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.ComposedTerrainParity"));const auto Surfaces=FVoxelSurfaceTypeTable::Get();const FVoxelQuery Query(0,FVoxelLayers::Empty(),*Surfaces,Dependencies);
    double MaxError=0;int32 Count=0;
    for(double Width:{100.,500.,1000.})
    {
        const auto G=FCLSectionGeometry::Build(*Plan,0,Width);FVoxelDoubleVectorBuffer Points;Points.Allocate(G.SurfaceVertexCount);
        for(int32 I=0;I<G.SurfaceVertexCount;++I)Points.Set(I,G.SourceSurfacePoints[I]);
        const auto D=Runtime->SampleDistances(Query,Points);for(int32 I=0;I<G.SurfaceVertexCount;++I)MaxError=FMath::Max(MaxError,FMath::Abs(double(D[I])));Count+=G.SurfaceVertexCount;
    }
    TestTrue(TEXT("Composed preview matches native spherical field within 0.1mm"),MaxError<.01);
    AddInfo(FString::Printf(TEXT("Composed preview/native sphere: %d vertices; %.9f cm residual"),Count,MaxError));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRegionEditorEventsTest,"Catalyst.Landform.Composition.EditorCommitAndSourceOwnership",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRegionEditorEventsTest::RunTest(const FString&)
{
    FString Error;FCLPlanetRecipe Recipe;if(!CompositionRecipe(Recipe,Error)){AddError(Error);return false;}
    auto* Asset=NewObject<UCLPlanetRecipeAsset>();Asset->Recipe=Recipe;
    int32 Changes=0;const auto Handle=Asset->OnRecipeChanged.AddLambda([&Changes]{++Changes;});
    FProperty* Property=FindFProperty<FProperty>(FCLPlanetRecipe::StaticStruct(),TEXT("TerrainRegions"));
    FPropertyChangedEvent Interactive(Property,EPropertyChangeType::Interactive);Asset->PostEditChangeProperty(Interactive);
    TestEqual(TEXT("Interactive mask dragging does not broadcast a rebuild for each mouse sample"),Changes,0);
    Asset->Recipe.TerrainRegions[0].Mask.TransitionMeters=180;
    FPropertyChangedEvent Finished(Property,EPropertyChangeType::ValueSet);Asset->PostEditChangeProperty(Finished);
    TestEqual(TEXT("Finished mask edit broadcasts one committed rebuild"),Changes,1);
    auto* Source=CompositionSource(TEXT("PlateausLocal"),Error);if(!Source){AddError(Error);return false;}
    const auto Before=Asset->Recipe;const auto Mask=Before.TerrainRegions[0].Mask;
    Source->TerrainType.NoiseLayers[0].Seed+=17;
    TestTrue(TEXT("Applying a source refreshes only the region snapshot"),Source->ApplyToRegion(Asset,Before.TerrainRegions[0].Id,Error));
    TestTrue(TEXT("Region retains its planet-owned mask"),Asset->Recipe.TerrainRegions[0].Mask.TransitionMeters==Mask.TransitionMeters);
    TestEqual(TEXT("Parent radius remains owned by planet"),Asset->Recipe.RadiusMeters,Before.RadiusMeters);
    TestEqual(TEXT("Parent seed remains owned by planet"),Asset->Recipe.Seed,Before.Seed);
    TestTrue(TEXT("Parent Strata is unchanged"),FCLStrata::StaticStruct()->CompareScriptStruct(&Asset->Recipe.Strata,&Before.Strata,0));
    TestTrue(TEXT("Base terrain is unchanged"),Asset->Recipe.NoiseLayers[0].Seed==Before.NoiseLayers[0].Seed);
    TestEqual(TEXT("Region source snapshot is refreshed"),Asset->Recipe.TerrainRegions[0].NoiseLayers[0].Seed,Source->TerrainType.NoiseLayers[0].Seed);
    Asset->OnRecipeChanged.Remove(Handle);return true;
}
#endif

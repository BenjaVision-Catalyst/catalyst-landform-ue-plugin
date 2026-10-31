#if WITH_DEV_AUTOMATION_TESTS
#include "Model/CLTerrainType.h"
#include "Stamps/CLStampLibrary.h"
#include "CLSectionGeometry.h"
#include "CLPlanet.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Async/Async.h"
#include "Algo/Reverse.h"
#include "UObject/GarbageCollection.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"

namespace
{
UCLTerrainTypeAsset* DistributionType(FString& Error)
{
    FString Json;FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/TerrainTypes/StampDistribution.json")));
    auto* T=NewObject<UCLTerrainTypeAsset>();return T->ImportTerrainTypeJson(Json,Error)?T:nullptr;
}
FCLPlanetRecipe DistributionRecipe(UCLTerrainTypeAsset* T,FString& Error)
{
    auto R=FCLPlanetCompiler::RollingPlains();R.RadiusMeters=1000;T->TerrainType.ApplyTo(R,Error);return R;
}
FVector DistributionDirection(int32 I,int32 Count)
{
    const double Z=1-2*(I+.5)/Count,A=I*2.399963229728653;
    return FVector(FMath::Sqrt(1-Z*Z)*FMath::Cos(A),FMath::Sqrt(1-Z*Z)*FMath::Sin(A),Z);
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPCGDistributionTest,"Catalyst.Landform.StampDistribution.DeterministicPCG",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPCGDistributionTest::RunTest(const FString&)
{
    FString Error;auto* T=DistributionType(Error);if(!TestNotNull(TEXT("Distribution definition imports"),T)){AddError(Error);return false;}
    auto R=DistributionRecipe(T,Error);auto A=FCLPlanetCompiler::Compile(R,Error);auto B=FCLPlanetCompiler::Compile(R,Error);
    if(!TestTrue(TEXT("PCG/native collection pipeline compiles"),A&&B)){AddError(Error);return false;}
    TArray<FCLResolvedStampPlacement> PA,PB;A->GetStampPlacements(PA);B->GetStampPlacements(PB);
    TestTrue(TEXT("Distribution creates a bounded nonempty placement list"),PA.Num()>30&&PA.Num()<=1024);
    TestEqual(TEXT("Repeated PCG compilation has equal count"),PA.Num(),PB.Num());
    TSet<FGuid> Ids;TSet<FName> Sources;
    for(int32 I=0;I<PA.Num();++I)
    {
        TestEqual(TEXT("Placement ID is reproducible"),PA[I].Id,PB[I].Id);
        TestEqual(TEXT("Weighted source selection is reproducible"),PA[I].SourceId,PB[I].SourceId);
        TestTrue(TEXT("Anchors and headings are reproducible"),PA[I].Anchor.Equals(PB[I].Anchor,1.e-12)&&PA[I].TangentX.Equals(PB[I].TangentX,1.e-12));
        TestTrue(TEXT("PCG local heading remains tangent at every latitude"),FMath::Abs(FVector::DotProduct(PA[I].Anchor,PA[I].TangentX))<1.e-6);
        TestTrue(TEXT("Spherical positions are unit directions"),FMath::Abs(PA[I].Anchor.Size()-1)<1.e-9);
        TestTrue(TEXT("PCG size and relief are inside configured ranges"),PA[I].WidthMeters>=79.99&&PA[I].WidthMeters<=220.01&&PA[I].ReliefMeters>=PA[I].WidthMeters*.0599&&PA[I].ReliefMeters<=PA[I].WidthMeters*.1401);
        TestEqual(TEXT("Native PCG sizing is deterministic"),PA[I].WidthMeters,PB[I].WidthMeters);
        Ids.Add(PA[I].Id);Sources.Add(PA[I].SourceId);
    }
    TestEqual(TEXT("All resolved placement identities are unique"),Ids.Num(),PA.Num());
    TestTrue(TEXT("Multiple terrain-family stamps are selected"),Sources.Num()>=3);
    auto* Library=UCLStampLibraryAsset::LoadConfigured();auto* Copy=NewObject<UCLStampLibraryAsset>();Copy->Library=Library->Library;
    for(auto& C:Copy->Library.Collections)Algo::Reverse(C.Entries);
    auto Reordered=FCLPlanetCompiler::Compile(R,Error,Copy);
    if(!TestTrue(TEXT("Catalogue ordering does not change a versioned collection"),Reordered.IsValid())){AddError(Error);return false;}
    TArray<FCLResolvedStampPlacement> PR;Reordered->GetStampPlacements(PR);
    for(int32 I=0;I<PA.Num();++I)TestTrue(TEXT("Reordering source rows preserves selection and sizes"),PA[I].Id==PR[I].Id&&PA[I].WidthMeters==PR[I].WidthMeters);
    // One distribution isolates point identity from subsequent layers' reference radii.
    R.NoiseLayers.SetNum(2);auto First=FCLPlanetCompiler::Compile(R,Error);First->GetStampPlacements(PA);
    R.NoiseLayers[1].Distribution.DensityPerSquareKilometer*=1.5;auto More=FCLPlanetCompiler::Compile(R,Error);More->GetStampPlacements(PB);
    TestTrue(TEXT("Density adds points"),PB.Num()>PA.Num());
    for(int32 I=0;I<PA.Num();++I)TestTrue(TEXT("Density preserves existing per-point identities and locations"),PA[I].Id==PB[I].Id&&PA[I].Anchor.Equals(PB[I].Anchor,1.e-12));
    R.NoiseLayers[1].Distribution.DensityPerSquareKilometer/=1.5;R.RadiusMeters*=2;R.NoiseLayers[1].Distribution.DensityPerSquareKilometer/=4;
    auto Resized=FCLPlanetCompiler::Compile(R,Error);if(!TestTrue(TEXT("Resized distribution compiles"),Resized.IsValid())){AddError(Error);return false;}
    Resized->GetStampPlacements(PB);TestEqual(TEXT("Density adjusted for area preserves count"),PA.Num(),PB.Num());
    for(int32 I=0;I<PA.Num();++I)TestTrue(TEXT("Normalized sizes and placements scale with radius"),PA[I].Anchor.Equals(PB[I].Anchor,1.e-12)&&FMath::Abs(PB[I].WidthMeters-PA[I].WidthMeters*2)<.001);
    R.NoiseLayers[1].Distribution.DensityPerSquareKilometer=10000;
    TestFalse(TEXT("Over-budget density fails rather than silently truncating"),FCLPlanetCompiler::Compile(R,Error).IsValid());
    R.NoiseLayers[1].Distribution.DensityPerSquareKilometer=0;
    auto Empty=FCLPlanetCompiler::Compile(R,Error);TestTrue(TEXT("Zero density is an empty distribution"),Empty.IsValid());
    if(Empty){Empty->GetStampPlacements(PB);TestEqual(TEXT("Zero density yields no placements"),PB.Num(),0);}
    auto LegacyRecipe=FCLPlanetCompiler::RollingPlains();
    auto Legacy=FCLPlanetCompiler::Compile(LegacyRecipe,Error);
    auto& ZeroLayer=LegacyRecipe.NoiseLayers.Emplace_GetRef();ZeroLayer.Name=TEXT("Empty stamps");
    ZeroLayer.Operation=ECLTerrainOperation::StampDistribution;ZeroLayer.Distribution.DensityPerSquareKilometer=0;
    auto LegacyWithDistribution=FCLPlanetCompiler::Compile(LegacyRecipe,Error);
    if(TestTrue(TEXT("Legacy noise can precede distribution"),Legacy&&LegacyWithDistribution))
        for(int32 I=0;I<128;++I)TestEqual(TEXT("Empty distribution preserves legacy noise exactly"),
            LegacyWithDistribution->SampleHeightMeters(DistributionDirection(I,128)),Legacy->SampleHeightMeters(DistributionDirection(I,128)));
    AddInfo(FString::Printf(TEXT("PCG distribution: %d stable original placements across %d selected sources"),Ids.Num(),Sources.Num()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLDistributionSnapshotTest,"Catalyst.Landform.StampDistribution.VersioningAndSnapshots",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLDistributionSnapshotTest::RunTest(const FString&)
{
    FString Error,Json;auto* T=DistributionType(Error);if(!TestNotNull(TEXT("Distribution imports"),T))return false;
    auto R=DistributionRecipe(T,Error);auto Plan=FCLPlanetCompiler::Compile(R,Error);if(!TestTrue(TEXT("Snapshot compiles"),Plan.IsValid())){AddError(Error);return false;}
    const double Before=Plan->SampleHeightMeters(FVector(1,2,3));
    auto* Copy=NewObject<UCLStampLibraryAsset>();Copy->Library=UCLStampLibraryAsset::LoadConfigured()->Library;
    Copy->Library.Collections[0].Entries[0].Weight+=1;
    TestFalse(TEXT("Unversioned weight changes are rejected"),FCLPlanetCompiler::Compile(R,Error,Copy).IsValid());
    TestFalse(TEXT("A published collection cannot be rebaked in place"),Copy->BakeSourceDigests(Error));
    int32 NextRevision=Copy->Library.Collections[0].Revision+1;
    for(const auto& Collection:Copy->Library.Collections)
        if(Collection.Id==Copy->Library.Collections[0].Id)NextRevision=FMath::Max(NextRevision,Collection.Revision+1);
    Copy->Library.Collections[0].Revision=NextRevision;
    TestTrue(TEXT("An explicit new revision can be baked"),Copy->BakeSourceDigests(Error));
    TestFalse(TEXT("An old recipe cannot silently switch to the new revision"),FCLPlanetCompiler::Compile(R,Error,Copy).IsValid());
    auto Old=R;Old.SchemaVersion=4;TestFalse(TEXT("Old recipe schema cannot execute distribution operations"),FCLPlanetCompiler::Compile(Old,Error).IsValid());
    R.NoiseLayers[1].Distribution.CollectionId=TEXT("Missing");TestFalse(TEXT("Missing collection is an explicit error"),FCLPlanetCompiler::Compile(R,Error).IsValid());
    R=DistributionRecipe(T,Error);
    auto* Asset=NewObject<UCLPlanetRecipeAsset>();Asset->Recipe=R;
    TestTrue(TEXT("Full recipe exports"),Asset->ExportJsonString(Json,Error));
    TestFalse(TEXT("Recipe contains no client asset paths"),Json.Contains(TEXT("/CatalystLandform/")));
    auto* Imported=NewObject<UCLPlanetRecipeAsset>();TestTrue(TEXT("Distribution recipe reimports"),Imported->ImportRecipeJson(Json,Error));
    auto Reloaded=FCLPlanetCompiler::Compile(Imported->Recipe,Error);
    if(!TestTrue(TEXT("Reimported distribution compiles"),Reloaded.IsValid()))return false;
    for(int32 I=0;I<128;++I)TestEqual(TEXT("Roundtrip preserves the full blended field"),Reloaded->SampleHeightMeters(DistributionDirection(I,128)),Plan->SampleHeightMeters(DistributionDirection(I,128)));
    CollectGarbage(RF_NoFlags);
    const auto Work=Async(EAsyncExecution::ThreadPool,[Plan]{return Plan->SampleHeightMeters(FVector(1,2,3));});
    TestEqual(TEXT("Worker snapshot remains valid after authoring objects are collected"),Work.Get(),Before);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLDistributionParityTest,"Catalyst.Landform.StampDistribution.DesignerSphereParity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLDistributionParityTest::RunTest(const FString&)
{
    FString Error;auto* T=DistributionType(Error);if(!TestNotNull(TEXT("Distribution imports"),T))return false;
    auto* World=GEditor->GetEditorWorldContext().World();auto* Planet=World->SpawnActorDeferred<ACLPlanet>(ACLPlanet::StaticClass(),FTransform::Identity);
    Planet->bEnableNanite=false;Planet->bEnableTessellation=false;Planet->bEnableNavigation=false;Planet->RecipeAsset=NewObject<UCLPlanetRecipeAsset>(Planet);
    Planet->FinishSpawning(FTransform::Identity);
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.DistributionParity"));const auto Surfaces=FVoxelSurfaceTypeTable::Get();
    const FVoxelQuery Query(0,FVoxelLayers::Empty(),*Surfaces,Dependencies);
    double MaxError=0,MaxDisplayError=0;int32 Samples=0;
    for(int32 Case=0;Case<9;++Case)
    {
        auto R=DistributionRecipe(T,Error);
        if(Case>0&&Case<4)
        {
            R.NoiseLayers.SetNum(2);R.NoiseLayers[1].Distribution.CollectionId=TEXT("Impacts");
            R.NoiseLayers[1].Distribution.Blend=Case==1?ECLStampBlend::Max:Case==2?ECLStampBlend::Min:ECLStampBlend::Override;
            R.Seed+=Case*991;
        }
        if(Case==4)
        {
            FString Json;FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/TerrainTypes/Plateaus.json")));
            auto* Plateau=NewObject<UCLTerrainTypeAsset>();
            if(!Plateau->ImportTerrainTypeJson(Json,Error)||!Plateau->TerrainType.ApplyTo(R,Error)){AddError(Error);break;}
            R.NoiseLayers.Insert(T->TerrainType.NoiseLayers[1],1);
            R.NoiseLayers.Add(T->TerrainType.NoiseLayers[3]);
        }
        if(Case==5||Case==6)
        {
            FString Json;FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/TerrainTypes/MountainRidgesStamped.json")));
            auto* Mountains=NewObject<UCLTerrainTypeAsset>();
            if(!Mountains->ImportTerrainTypeJson(Json,Error)){AddError(Error);break;}
            if(Case==6){Mountains->TerrainType.NoiseLayers[0].bEnabled=false;Mountains->TerrainType.NoiseLayers[2].bEnabled=false;}
            if(!Mountains->TerrainType.ApplyTo(R,Error)){AddError(Error);break;}
        }
        if(Case==7||Case==8)
        {
            const FString Example=Case==7?TEXT("Examples/TerrainTypes/MotionForgeMountains.json"):TEXT("Examples/TerrainTypes/MountainStructures.json");
            FString Json;FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/Example));
            auto* Mountains=NewObject<UCLTerrainTypeAsset>();
            if(!Mountains->ImportTerrainTypeJson(Json,Error)||!Mountains->TerrainType.ApplyTo(R,Error)){AddError(Error);break;}
        }
        const auto Plan=FCLPlanetCompiler::Compile(R,Error);if(!Plan){AddError(Error);break;}
        auto BaseRecipe=R;BaseRecipe.NoiseLayers.SetNum(1);
        const auto Base=FCLPlanetCompiler::Compile(BaseRecipe,Error);
        if(!Base){AddError(Error);break;}
        Planet->RecipeAsset->Recipe=R;Planet->RebuildTerrain();const auto Runtime=Planet->GetSamplingRuntime();
        if(!TestTrue(TEXT("Actual spherical distribution runtime initializes"),Runtime.IsValid()))break;
        TArray<FVector> Points;
        for(double Width:{100.,500.,1000.})
        {
            const auto G=FCLSectionGeometry::Build(*Plan,0,Width);Points.Append(G.SourceSurfacePoints);
            if(Case==7&&Width==500)TestEqual(TEXT("Detailed stamp preview resolves a one-metre grid"),G.SurfaceVertexCount,501*501);
            TestTrue(TEXT("Stamp preview vertex budget is bounded"),G.SurfaceVertexCount<=513*513);
            for(int32 I=0;I<G.SurfaceVertexCount;++I)MaxDisplayError=FMath::Max(MaxDisplayError,FMath::Abs(G.Vertices[I].Z/100000-G.SourceSurfacePoints[I].Size()/(100*Plan->RadiusMeters)));
        }
        TArray<FCLResolvedStampPlacement> Placements;Plan->GetStampPlacements(Placements);
        for(const auto& P:Placements)
        {
            for(double Distance:{0.,.25,.48})
            {
                const double Angle=P.WidthMeters*Distance/P.ChartRadiusMeters;
                const FVector D=P.Anchor*FMath::Cos(Angle)+P.TangentX*FMath::Sin(Angle);
                Points.Add(D*(Plan->RadiusMeters+Plan->SampleHeightMeters(D))*100);
            }
        }
        for(int32 I=0;I<1024;++I){const FVector D=DistributionDirection(I,1024);Points.Add(D*(Plan->RadiusMeters+Plan->SampleHeightMeters(D))*100);}
        TArray<double> BaseHeights;Base->SampleHeightsMeters(Points,BaseHeights);
        double MinimumDelta=0,MaximumDelta=0;
        for(int32 I=0;I<Points.Num();++I)
        {
            const double Delta=Points[I].Size()/100-Plan->RadiusMeters-BaseHeights[I];
            MinimumDelta=FMath::Min(MinimumDelta,Delta);MaximumDelta=FMath::Max(MaximumDelta,Delta);
        }
        if(Case==1)TestTrue(TEXT("Native Max raises terrain without carving"),MaximumDelta>1&&MinimumDelta>-.001);
        if(Case==2)TestTrue(TEXT("Native Min carves terrain without raising it"),MinimumDelta<-1&&MaximumDelta<.001);
        if(Case==3)TestTrue(TEXT("Native Override retains crater floors and raised rims"),MinimumDelta<-1&&MaximumDelta>1);
        AddInfo(FString::Printf(TEXT("Distribution blend case %d: delta range %.6f to %.6f m"),Case,MinimumDelta,MaximumDelta));
        FVoxelDoubleVectorBuffer Buffer;Buffer.Allocate(Points.Num());for(int32 I=0;I<Points.Num();++I)Buffer.Set(I,Points[I]);
        const auto Distances=Runtime->SampleDistances(Query,Buffer);
        for(int32 I=0;I<Points.Num();++I)MaxError=FMath::Max(MaxError,FMath::Abs(double(Distances[I])));
        Samples+=Points.Num();
    }
    World->DestroyActor(Planet);
    AddInfo(FString::Printf(TEXT("Distribution parity: %d source points; maximum native spherical residual %.6f cm; normalized display error %.12f"),Samples,MaxError,MaxDisplayError));
    TestTrue(TEXT("Designer/native blended stamp field agrees within 5cm"),MaxError<5);
    TestTrue(TEXT("Flat display retains the same radial heights"),MaxDisplayError<1.e-9);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Interfaces/IPluginManager.h"
#include "CLDeliveryPrototype.h"
#include "Model/CLSpatialCave.h"
#include "Model/CLSphericalPatches.h"
#include "Collision/VoxelCollisionInvoker.h"
#include "HAL/IConsoleManager.h"
#include "UObject/StrongObjectPtr.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLProceduralWorkAudit,"Catalyst.Landform.Caves.ProceduralFamily.WorkVsSurface",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLProceduralWorkAudit::RunTest(const FString&)
{
    FString Json,Error;FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/ProceduralCaves.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> Asset(NewObject<UCLPlanetRecipeAsset>());if(!Asset->ImportRecipeJson(Json,Error)){AddError(Error);return false;}
    const auto P=FCLPlanetCompiler::Compile(Asset->Recipe,Error);if(!P){AddError(Error);return false;}
    const auto& C=*P->Caves;const auto& Spatial=*C.Spatial;
    // Replay the existing geometry's 20 cm query grid, without changing or
    // creating a VPP world. Invoker radius and cell-generator policy are stock.
    constexpr int32 VoxelCm=20,ChunkCm=VoxelCm*32,StepCm=VoxelCm*4;
    const double RadiusCm=GetDefault<UVoxelCollisionInvokerComponent>()->Radius;
    const float Tolerance=IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.CellGenerator.Tolerance"))->GetFloat();
    const bool Signs=IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.CellGenerator.CheckSigns"))->GetBool(),Propagate=IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.CellGenerator.Propagate"))->GetBool();
    const double Threshold=StepCm*UE_HALF_SQRT_3*(1.+Tolerance);
    auto Index=[](int X,int Y,int Z){return X+11*Y+121*Z;};
    struct FSelection{bool Rock=true,Air=true;int64 Fine=0;};
    auto Select=[&](const TArray<double>& F,double LodScale=1.)
    {
        FSelection R;TArray<uint8> Mask;Mask.Init(0,1331);
        for(int I=0;I<1331;++I){R.Rock&=F[I]<0;R.Air&=F[I]>=0;Mask[I]=FMath::Abs(F[I])*100<=Threshold*LodScale;}
        if(Signs)for(int Z=0;Z<11;++Z)for(int Y=0;Y<11;++Y)for(int X=0;X<11;++X)
        {
            const int I=Index(X,Y,Z);if(Mask[I])continue;const bool Negative=F[I]<0;
            for(int A=FMath::Max(0,Z-1);A<=FMath::Min(10,Z+1);++A)for(int B=FMath::Max(0,Y-1);B<=FMath::Min(10,Y+1);++B)for(int D=FMath::Max(0,X-1);D<=FMath::Min(10,X+1);++D)if((F[Index(D,B,A)]<0)!=Negative)Mask[I]=1;
        }
        if(Propagate){const auto Copy=Mask;for(int Z=0;Z<11;++Z)for(int Y=0;Y<11;++Y)for(int X=0;X<11;++X)if(Copy[Index(X,Y,Z)])for(int A=FMath::Max(0,Z-1);A<=FMath::Min(10,Z+1);++A)for(int B=FMath::Max(0,Y-1);B<=FMath::Min(10,Y+1);++B)for(int D=FMath::Max(0,X-1);D<=FMath::Min(10,X+1);++D)Mask[Index(D,B,A)]=1;}
        for(int Z=1;Z<=9;++Z)for(int Y=1;Y<=9;++Y)for(int X=1;X<=9;++X)R.Fine+=Mask[Index(X,Y,Z)]?64:0;
        return R;
    };
    // Inspect guaranteed cave-free chunks around the aggregate envelope. The
    // native cell supports, not coarse sample signs, certify cave absence.
    TArray<FBox> Supports;Spatial.GetCellSupportBounds(Supports);const FBox Whole=Spatial.Bounds();
    TArray<TSharedPtr<FJsonValue>> EnvelopeRows;
    for(int32 LOD:{0,1,2,3})
    {
        const int32 Scale=1<<LOD;TSet<FIntVector> Keys;
        for(int32 K=0;K<8;++K)
        {
            const FVector V(K&1?Whole.Max.X:Whole.Min.X,K&2?Whole.Max.Y:Whole.Min.Y,K&4?Whole.Max.Z:Whole.Min.Z);
            const FVector T=V*100/(ChunkCm*Scale);const FIntVector Key(FMath::FloorToInt(T.X),FMath::FloorToInt(T.Y),FMath::FloorToInt(T.Z));
            for(int32 Z=-1;Z<=1;++Z)for(int32 Y=-1;Y<=1;++Y)for(int32 X=-1;X<=1;++X)Keys.Add(Key+FIntVector(X,Y,Z));
        }
        int32 NoCaveChunks=0,ExtraChunks=0;int64 ActualFine=0,UncutFine=0;double MaxExcess=0;
        for(const auto Key:Keys)
        {
            TArray<FVector> Points;FBox QueryBounds(ForceInit);const FVector Start=FVector(Key)*(ChunkCm*Scale)-FVector(VoxelCm*3.5*Scale);
            for(int32 Z=0;Z<11;++Z)for(int32 Y=0;Y<11;++Y)for(int32 X=0;X<11;++X){const FVector Q=(Start+FVector(X,Y,Z)*(StepCm*Scale))*.01;Points.Add(Q);QueryBounds+=Q;}
            if(Supports.ContainsByPredicate([&](const FBox& B){return B.Intersect(QueryBounds);}))continue;
            ++NoCaveChunks;TArray<double> Current,H,Natural;Spatial.SamplePlanet(*P,Points,Current);P->SampleHeightsMeters(Points,H);P->SampleRadialFields(Points,H,Natural);
            const auto A=Select(Current,Scale),B=Select(Natural,Scale);ActualFine+=A.Fine;UncutFine+=B.Fine;ExtraChunks+=A.Fine>B.Fine;
            for(int32 I=0;I<Current.Num();++I)MaxExcess=FMath::Max(MaxExcess,Current[I]-Natural[I]);
        }
        // Regression for the network-envelope fallback: certified rock must
        // not inherit a near-zero field throughout an empty aggregate box.
        if(LOD<=2)TestEqual(TEXT("Certified cave-free low-LOD envelope adds no fine samples"),ActualFine,UncutFine);
        else TestTrue(TEXT("Certified cave-free coarse envelope excess stays below 0.1 percent"),ActualFine<=UncutFine+FMath::Max<int64>(128,UncutFine/1000));
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("lod"),LOD);Row->SetNumberField(TEXT("certifiedCaveFreeChunks"),NoCaveChunks);Row->SetNumberField(TEXT("chunksWithExtraRefinement"),ExtraChunks);Row->SetNumberField(TEXT("currentFinePointsSelected"),double(ActualFine));Row->SetNumberField(TEXT("uncutFinePointsSelected"),double(UncutFine));Row->SetNumberField(TEXT("maximumCaveFieldExcessMeters"),MaxExcess);EnvelopeRows.Add(MakeShared<FJsonValueObject>(Row));
        AddInfo(FString::Printf(TEXT("CAVE_FREE_ENVELOPE LOD=%d chunks=%d excessChunks=%d currentFine=%lld uncutFine=%lld"),LOD,NoCaveChunks,ExtraChunks,ActualFine,UncutFine));
    }
    TArray<FVector> Sites;TArray<FString> Names;
    Sites.Add(C.Nodes[0].Position*100);Names.Add(TEXT("chamber"));
    Sites.Add(C.Passages[0].Points[C.Passages[0].Points.Num()/2]*100);Names.Add(TEXT("passage"));
    Sites.Add(C.Passages.Last().Points[C.Passages.Last().Points.Num()/2]*100);Names.Add(TEXT("ramp"));
    const FVector D=Sites[0].GetSafeNormal();Sites.Add(D*(P->RadiusMeters+P->SampleHeightMeters(D)+1.6)*100);Names.Add(TEXT("surface"));
    TArray<FVector> Directions;
    for(int32 Face=0;Face<6;++Face)for(int32 Y=0;Y<=64;++Y)for(int32 X=0;X<=64;++X)
        Directions.Add(FCLSphericalPatches::Direction(Face,FVector2D(-1.+2.*X/64.,-1.+2.*Y/64.)));
    for(const auto& Site:Sites)for(int32 I=0;I<4096;++I)Directions.Add((Site*.01+FVector((I%64-32)*.37,(I/64-32)*.41,0)).GetSafeNormal());
    TArray<double> ExactHeights,PreparedHeights;P->SampleHeightsMeters(Directions,ExactHeights);Spatial.SampleSurfaceHeights(*P,Directions,PreparedHeights);
    double SurfaceError=0;for(int32 I=0;I<Directions.Num();++I)SurfaceError=FMath::Max(SurfaceError,FMath::Abs(ExactHeights[I]-PreparedHeights[I]));
    TestTrue(TEXT("Final compiled terrain retains the existing 0.25mm input tolerance"),SurfaceError<=.00025);
    AddInfo(FString::Printf(TEXT("PREPARED_FINAL_SURFACE probes=%d maximumErrorMeters=%.9f"),Directions.Num(),SurfaceError));
    int32 Stable=0,Fallback=0,Mismatches=0;
    auto CheckGeology=[&](const FVector& Q)
    {
        FCLSurfaceWeights Exact,Prepared;P->Rock->ApplySurfaces(Q,Exact);
        if(!Spatial.TryPreparedSurfaces(Q,Prepared)){++Fallback;return;}
        ++Stable;for(int32 K=0;K<P->Surfaces->Ids.Num();++K)Mismatches+=Exact.Values[K]!=Prepared.Values[K];
    };
    // Independent body-space probes cover each level, the external surface,
    // contacts/feather bands, the basement and both sides of the unconformity.
    for(const auto& Site:Sites)for(int32 I=0;I<16384;++I)
        CheckGeology(Site*.01+FVector((I%32-16)*.51,(I/32%32-16)*.47,(I/1024-8)*.43));
    for(const auto& N:C.Nodes)
    {
        const FVector Direction=N.Position.GetSafeNormal();const auto Column=P->Rock->SampleColumn(Direction);
        for(int32 I=0;I<P->Rock->GetBedCount();++I)
        {
            const auto Bed=P->Rock->GetBed(I);const double Contact=Bed.UpperMeters+(I<P->Rock->GetOlderBedCount()?Column.DeformationMeters:0.);
            for(double E:{-.1,-.01,-.0002,-.00002,0.,.00002,.0002,.01,.1})CheckGeology(Direction*(Contact+E));
        }
        for(double Boundary:{Column.RestoredBasementRadiusMeters+Column.DeformationMeters,Column.YoungerFloorRadiusMeters})
            for(double E:{-.001,-.00002,0.,.00002,.001})CheckGeology(Direction*(Boundary+E));
    }
    TestTrue(TEXT("Canonical geology remains available over the full workload"),Stable+Fallback>10000);
    TestTrue(TEXT("Uncertain contacts retain exact fallback"),Fallback>0);
    TestEqual(TEXT("Prepared strata bindings and weights exactly match canonical geology"),Mismatches,0);
    AddInfo(FString::Printf(TEXT("PREPARED_GEOLOGY stable=%d exactFallback=%d mismatches=%d"),Stable,Fallback,Mismatches));
    TArray<TSharedPtr<FJsonValue>> Rows;
    for(int Site=0;Site<Sites.Num();++Site)
    {
        TVoxelArray<FSphere> Invokers;Invokers.Add(FSphere(Sites[Site],RadiusCm));TVoxelSet<FIntVector> Chunks;
        check(FVoxelUtilities::ComputeInvokerChunks(Chunks,MoveTemp(Invokers),FTransform::Identity,ChunkCm,100000));
        int Rock=0,Air=0,Mixed=0,RefineRock=0,SkipRock=0;int64 CaveFine=0,SurfaceFine=0;double CaveSeconds=0,SurfaceSeconds=0,DiscardedRockSeconds=0;int64 HeightPoints=0;
        for(const auto Key:Chunks)
        {
            TArray<FVector> Points;Points.Reserve(1331);const FVector Start=FVector(Key)*ChunkCm-FVector(VoxelCm*3.5);
            for(int Z=0;Z<11;++Z)for(int Y=0;Y<11;++Y)for(int X=0;X<11;++X)Points.Add((Start+FVector(X,Y,Z)*StepCm)*.01);
            TArray<double> Cave,Surface,Heights;double T=FPlatformTime::Seconds();int32 H=0;Spatial.SamplePlanet(*P,Points,Cave,&H);const double CaveTime=FPlatformTime::Seconds()-T;CaveSeconds+=CaveTime;HeightPoints+=H;
            T=FPlatformTime::Seconds();Spatial.SampleSurfaceHeights(*P,Points,Heights);P->SampleRadialFields(Points,Heights,Surface);SurfaceSeconds+=FPlatformTime::Seconds()-T;
            const auto A=Select(Cave),B=Select(Surface);Rock+=A.Rock;Air+=A.Air;Mixed+=!A.Rock&&!A.Air;RefineRock+=A.Rock&&A.Fine;SkipRock+=A.Rock&&!A.Fine;CaveFine+=A.Fine;SurfaceFine+=B.Fine;if(A.Rock&&!A.Fine)DiscardedRockSeconds+=CaveTime;
        }
        // Attribute work is separate from density and was not included in the
        // earlier distance-only telemetry. Time identical canonical samples.
        double SurfaceAttributeSeconds=0,PropertySeconds=0,Checksum=0;
        constexpr int AttributePoints=4096;
        TArray<FVector> AttributePositions;AttributePositions.Reserve(AttributePoints);
        for(int I=0;I<AttributePoints;++I)AttributePositions.Add(Sites[Site]*.01+FVector((I%16-8)*.3,(I/16%16-8)*.3,(I/256-8)*.3));
        double Begin=FPlatformTime::Seconds();
        for(const auto& V:AttributePositions){FCLSurfaceWeights W;P->Rock->ApplySurfaces(V,W);Checksum+=W.Values[0];}
        SurfaceAttributeSeconds=FPlatformTime::Seconds()-Begin;Begin=FPlatformTime::Seconds();
        for(const auto& V:AttributePositions)Checksum+=P->Rock->Properties(V).R;
        PropertySeconds=FPlatformTime::Seconds()-Begin;
        auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("attributePoints"),AttributePoints);R->SetNumberField(TEXT("surfaceAttributeSeconds"),SurfaceAttributeSeconds);R->SetNumberField(TEXT("rockPropertySeconds"),PropertySeconds);R->SetNumberField(TEXT("attributeChecksum"),Checksum);R->SetStringField(TEXT("site"),Names[Site]);R->SetNumberField(TEXT("chunks"),Chunks.Num());R->SetNumberField(TEXT("coarsePoints"),Chunks.Num()*1331);R->SetNumberField(TEXT("allRockChunks"),Rock);R->SetNumberField(TEXT("allAirChunks"),Air);R->SetNumberField(TEXT("mixedChunks"),Mixed);R->SetNumberField(TEXT("uniformRockButRefines"),RefineRock);R->SetNumberField(TEXT("rejectedRockChunks"),SkipRock);R->SetNumberField(TEXT("caveFinePointsSelected"),double(CaveFine));R->SetNumberField(TEXT("uncutPlanetFinePointsSelected"),double(SurfaceFine));R->SetNumberField(TEXT("caveSeconds"),CaveSeconds);R->SetNumberField(TEXT("uncutPlanetSeconds"),SurfaceSeconds);R->SetNumberField(TEXT("caveSecondsSpentOnRejectedRock"),DiscardedRockSeconds);R->SetNumberField(TEXT("terrainHeightPointsStillEvaluated"),double(HeightPoints));Rows.Add(MakeShared<FJsonValueObject>(R));
        AddInfo(FString::Printf(TEXT("FAMILY_WORK %s chunks=%d solid=%d solidRefine=%d caveFine=%lld surfaceFine=%lld cave=%.6f surface=%.6f discardedRock=%.6f"),*Names[Site],Chunks.Num(),Rock,RefineRock,CaveFine,SurfaceFine,CaveSeconds,SurfaceSeconds,DiscardedRockSeconds));
    }
    auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("scope"),TEXT("Stock native coarse-selection policy and invoker radius, replayed on existing 20cm geometry queries; no VPP world/configuration changes. Uncut planet uses the same compiled surface input as caves. This is evaluator/coarse-work evidence, not live streaming timing."));R->SetNumberField(TEXT("finalSurfaceProbes"),Directions.Num());R->SetNumberField(TEXT("finalSurfaceErrorMeters"),SurfaceError);R->SetNumberField(TEXT("preparedSurfaceProbes"),Stable);R->SetNumberField(TEXT("exactSurfaceFallbackProbes"),Fallback);R->SetNumberField(TEXT("surfaceWeightMismatches"),Mismatches);R->SetNumberField(TEXT("stockInvokerRadiusCm"),RadiusCm);R->SetNumberField(TEXT("queryVoxelSizeCm"),VoxelCm);R->SetArrayField(TEXT("sites"),Rows);R->SetArrayField(TEXT("caveFreeEnvelope"),EnvelopeRows);FString Text;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text));FString Path;if(FParse::Value(FCommandLine::Get(),TEXT("CLFamilyWorkReport="),Path))FFileHelper::SaveStringToFile(Text,*Path);
    return !HasAnyErrors();
}
#endif

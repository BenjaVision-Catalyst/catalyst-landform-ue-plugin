#if WITH_DEV_AUTOMATION_TESTS
#include "VoxelQuery.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Interfaces/IPluginManager.h"
#include "CLDeliveryPrototype.h"
#include "Model/CLSpatialCave.h"
#include "VoxelLayers.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "Shape/VoxelSphereShape.h"
#include "UObject/StrongObjectPtr.h"
#include "HAL/IConsoleManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSpatialChunkWork,"Catalyst.Landform.Caves.SpatialPrototype.CoarseWork",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLSpatialChunkWork::RunTest(const FString&)
{
    FString J,E;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/SpatialCave.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> Asset(NewObject<UCLPlanetRecipeAsset>());if(!Asset->ImportRecipeJson(J,E)){AddError(E);return false;}
    FCLPlanetStamp S;S.bUseRecipe=true;S.Recipe=Asset->Recipe;S.Radius=S.Recipe.RadiusMeters*100;S.Behavior=EVoxelStampBehavior::AffectAll;
    const auto Native=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(S),nullptr));if(!Native){AddError(TEXT("Missing runtime"));return false;}
    FCLDeliveryBinding Binding;Binding.Source=Native;Binding.Snapshot=FGuid::NewGuid();Binding.Counters=MakeShared<FCLDeliveryCounters>();
    const auto& P=*Native->RecipePlan;
    TArray<FVector> Sites;TArray<FString> Names;
    const auto& Spatial=*P.Caves->Spatial;
    Sites.Add(Spatial.BodyPoint(FVector(-60,-3,1.6))*100);Names.Add(TEXT("passage"));
    Sites.Add(Spatial.BodyPoint(FVector(14,-5,1.6))*100);Names.Add(TEXT("chamber"));
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.ChunkWork"));FVoxelQuery Q(0,FVoxelLayers::Empty(),*FVoxelSurfaceTypeTable::Get(),Dependencies);
    const float Tolerance=IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.CellGenerator.Tolerance"))->GetFloat();
    const bool CheckSigns=IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.CellGenerator.CheckSigns"))->GetBool(),Propagate=IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.CellGenerator.Propagate"))->GetBool();
    const double Threshold=80.*UE_HALF_SQRT_3*(1.+Tolerance);TArray<TSharedPtr<FJsonValue>> Results;
    auto Index=[](int X,int Y,int Z){return X+11*Y+121*Z;};
    for(int Site=0;Site<Sites.Num();++Site)
    {
        TVoxelArray<FSphere> Invokers;Invokers.Add(FSphere(Sites[Site],2500));TVoxelSet<FIntVector> ChunkSet;
        check(FVoxelUtilities::ComputeInvokerChunks(ChunkSet,MoveTemp(Invokers),FTransform::Identity,640.,100000));
        TVoxelArray<FIntVector> Chunks;for(const auto K:ChunkSet)Chunks.Add(K);Chunks.Sort([](const auto&A,const auto&B){return A.Z!=B.Z?A.Z<B.Z:A.Y!=B.Y?A.Y<B.Y:A.X<B.X;});
        int32 RockChunks=0,AirChunks=0,MixedChunks=0,SkipRock=0,SkipAir=0,RefineRock=0;int64 FinePoints=0;double CoarseTime=0,SkipRockTime=0,AnalyticTime=0,Checksum=0,HeightTime=0,CaveTime=0,VolumeTime=0,SkippedHeightTime=0,SkippedCaveTime=0;TArray<TSharedPtr<FJsonValue>> ChunkRows;
        const double RadiusCm=(P.RadiusMeters+P.SampleHeightMeters(Sites[Site].GetSafeNormal()))*100;
        for(const auto Key:Chunks)
        {
            // Native volume-only cell generator for a full 32-voxel chunk:
            // DataSize=36; sparse bounds=[-1,10)^3, step=4*20cm.
            FVoxelDoubleVectorBuffer XYZ;XYZ.Allocate(1331);const FVector Start=FVector(Key)*640-FVector(70);
            for(int Z=0;Z<11;++Z)for(int Y=0;Y<11;++Y)for(int X=0;X<11;++X)XYZ.Set(Index(X,Y,Z),Start+FVector(X,Y,Z)*80);
            const double Begin=FPlatformTime::Seconds();FVoxelFloatBuffer F;F.Allocate(1331);double HTime=0,CTime=0,VTime=0;
            for(int32 StartIndex=0;StartIndex<1331;StartIndex+=512)
            {
                const int32 N=FMath::Min(512,1331-StartIndex);TArray<FVector> Points;Points.Reserve(N);
                for(int32 I=0;I<N;++I)Points.Add(Native->SamplingFrame.NativeToBodyMeters(XYZ[StartIndex+I]));
                TArray<double> Fields;int32 Needed=0;const double T=FPlatformTime::Seconds();
                Spatial.SamplePlanet(P,Points,Fields,&Needed);CTime+=FPlatformTime::Seconds()-T;
                Binding.Counters->Record(0,Needed,0,0);
                for(int32 I=0;I<N;++I)F.Set(StartIndex+I,Fields[I]*100.);
            }
            Binding.Counters->Record(1331,0,0,0);const double Elapsed=FPlatformTime::Seconds()-Begin;CoarseTime+=Elapsed;HeightTime+=HTime;CaveTime+=CTime;VolumeTime+=VTime;
            bool Rock=true,Air=true;double Min=MAX_dbl,Max=-MAX_dbl,MinAbs=MAX_dbl;TArray<uint8> Mask;Mask.Init(0,1331);
            for(int I=0;I<1331;++I){Rock&=F[I]<0;Air&=F[I]>=0;Min=FMath::Min(Min,double(F[I]));Max=FMath::Max(Max,double(F[I]));MinAbs=FMath::Min(MinAbs,FMath::Abs(double(F[I])));Mask[I]=FMath::Abs(F[I])<=Threshold;}
            if(CheckSigns)for(int Z=0;Z<11;++Z)for(int Y=0;Y<11;++Y)for(int X=0;X<11;++X)
            {
                const int I=Index(X,Y,Z);if(Mask[I])continue;const bool Neg=F[I]<0;
                for(int NZ=FMath::Max(0,Z-1);NZ<=FMath::Min(10,Z+1);++NZ)for(int NY=FMath::Max(0,Y-1);NY<=FMath::Min(10,Y+1);++NY)for(int NX=FMath::Max(0,X-1);NX<=FMath::Min(10,X+1);++NX)if((F[Index(NX,NY,NZ)]<0)!=Neg)Mask[I]=1;
            }
            if(Propagate){const auto Copy=Mask;for(int Z=0;Z<11;++Z)for(int Y=0;Y<11;++Y)for(int X=0;X<11;++X)if(Copy[Index(X,Y,Z)])for(int NZ=FMath::Max(0,Z-1);NZ<=FMath::Min(10,Z+1);++NZ)for(int NY=FMath::Max(0,Y-1);NY<=FMath::Min(10,Y+1);++NY)for(int NX=FMath::Max(0,X-1);NX<=FMath::Min(10,X+1);++NX)Mask[Index(NX,NY,NZ)]=1;}
            int32 Dense=0;for(int Z=1;Z<=9;++Z)for(int Y=1;Y<=9;++Y)for(int X=1;X<=9;++X)Dense+=Mask[Index(X,Y,Z)]?64:0;FinePoints+=Dense;
            RockChunks+=Rock;AirChunks+=Air;MixedChunks+=!Rock&&!Air;SkipRock+=Rock&&!Dense;SkipAir+=Air&&!Dense;RefineRock+=Rock&&Dense;
            if(Rock&&!Dense){SkipRockTime+=Elapsed;SkippedHeightTime+=HTime;SkippedCaveTime+=CTime;}
            FVoxelFloatBuffer Control;Control.Allocate(1331);FVoxelSphereShape Sphere;Sphere.Radius=RadiusCm;const double B=FPlatformTime::Seconds();Sphere.Sample(Control.View(),XYZ);AnalyticTime+=FPlatformTime::Seconds()-B;for(int I=0;I<1331;++I)Checksum+=Control[I]+F[I];
            auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("key"),Key.ToString());C->SetNumberField(TEXT("minFieldCm"),Min);C->SetNumberField(TEXT("maxFieldCm"),Max);C->SetNumberField(TEXT("minAbsFieldCm"),MinAbs);C->SetNumberField(TEXT("finePointsSelected"),Dense);C->SetNumberField(TEXT("coarseSeconds"),Elapsed);ChunkRows.Add(MakeShared<FJsonValueObject>(C));
        }
        auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("site"),Names[Site]);O->SetStringField(TEXT("positionCm"),Sites[Site].ToString());O->SetNumberField(TEXT("bodyOnlyDepthMeters"),RadiusCm*.01-Sites[Site].Length()*.01);O->SetNumberField(TEXT("chunks"),Chunks.Num());O->SetNumberField(TEXT("coarsePoints"),Chunks.Num()*1331);O->SetNumberField(TEXT("allRockAtCoarseProbes"),RockChunks);O->SetNumberField(TEXT("allAirAtCoarseProbes"),AirChunks);O->SetNumberField(TEXT("mixedAtCoarseProbes"),MixedChunks);O->SetNumberField(TEXT("skipRockChunks"),SkipRock);O->SetNumberField(TEXT("skipAirChunks"),SkipAir);O->SetNumberField(TEXT("uniformRockButRefines"),RefineRock);O->SetNumberField(TEXT("finePointsSelected"),double(FinePoints));O->SetNumberField(TEXT("coarseFieldSeconds"),CoarseTime);O->SetNumberField(TEXT("coarseFieldSecondsOnSkippedRock"),SkipRockTime);O->SetNumberField(TEXT("terrainAndRadialSeconds"),HeightTime);O->SetNumberField(TEXT("caveCompositionSeconds"),CaveTime);O->SetNumberField(TEXT("authoredVolumeSeconds"),VolumeTime);O->SetNumberField(TEXT("terrainOnSkippedRockSeconds"),SkippedHeightTime);O->SetNumberField(TEXT("cavesOnSkippedRockSeconds"),SkippedCaveTime);O->SetNumberField(TEXT("nativeAnalyticSphereSeconds"),AnalyticTime);O->SetNumberField(TEXT("checksum"),Checksum);O->SetArrayField(TEXT("chunkRecords"),ChunkRows);Results.Add(MakeShared<FJsonValueObject>(O));
        AddInfo(FString::Printf(TEXT("CHUNK_AUDIT %s chunks=%d rock=%d skipRock=%d mixed=%d fine=%lld coarse=%.3fs discardedRock=%.3fs"),*Names[Site],Chunks.Num(),RockChunks,SkipRock,MixedChunks,FinePoints,CoarseTime,SkipRockTime));
    }
    auto Root=MakeShared<FJsonObject>();Root->SetStringField(TEXT("scope"),TEXT("Replay native volume-only coarse selection at two spatial-cave invoker sites; not a live-streaming timing run. Analytic sphere is a cost control, not equivalent geometry."));Root->SetNumberField(TEXT("coarseThresholdCm"),Threshold);Root->SetBoolField(TEXT("checkSigns"),CheckSigns);Root->SetBoolField(TEXT("propagate"),Propagate);Root->SetNumberField(TEXT("heightPoints"),double(Binding.Counters->HeightPoints.load()));Root->SetArrayField(TEXT("sites"),Results);
    FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));FString Path;FParse::Value(FCommandLine::Get(),TEXT("CLSpatialWorkReport="),Path);
    if(!Path.IsEmpty())FFileHelper::SaveStringToFile(Json,*Path);
    TestEqual(TEXT("Deep invoker probes bypass terrain height"),Binding.Counters->HeightPoints.load(),uint64(0));return !HasAnyErrors();
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Interfaces/IPluginManager.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLSpatialCave.h"
#include "Async/Async.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSpatialCacheTest,"Catalyst.Landform.Caves.SpatialPrototype.SampleCache",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLSpatialCacheTest::RunTest(const FString&)
{
    FString J,E;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/SpatialCave.json")));
    auto* Asset=NewObject<UCLPlanetRecipeAsset>();if(!Asset->ImportRecipeJson(J,E)){AddError(E);return false;}
    const auto Plan=FCLPlanetCompiler::Compile(Asset->Recipe,E);if(!Plan||!Plan->Caves||!Plan->Caves->Spatial){AddError(E);return false;}
    const auto C=Plan->Caves->Spatial;const FVector Center=C->BodyPoint(FVector(15,-5,2));TArray<FVector> Points;
    // Real native Cartesian grid positions, including both sides of the floor.
    for(int32 X=-12;X<12;++X)for(int32 Y=-16;Y<16;++Y)for(int32 Z=-16;Z<16;++Z)
        Points.Add((FVector(FMath::RoundToDouble(Center.X*100/20),FMath::RoundToDouble(Center.Y*100/20),FMath::RoundToDouble(Center.Z*100/20))+FVector(X,Y,Z))*.2);
    TArray<double> Reference,Cold,Warm;double Started=FPlatformTime::Seconds();C->Sample(Points,Reference,false);const double ReferenceSeconds=FPlatformTime::Seconds()-Started;
    C->Sample(Points,Cold);Started=FPlatformTime::Seconds();C->Sample(Points,Warm);const double WarmSeconds=FPlatformTime::Seconds()-Started;double Error=0;
    for(int32 I=0;I<Points.Num();++I)Error=FMath::Max(Error,FMath::Max(FMath::Abs(Reference[I]-Cold[I]),FMath::Abs(Reference[I]-Warm[I])));
    // Reordered, partially novel coordinates exercise mixed hits/misses and
    // cannot be implemented as reuse of the previous packet's result array.
    TArray<FVector> Mixed;for(int32 I=Points.Num()-1;I>=0;--I)Mixed.Add(Points[I]+(I%3==0?FVector(.017,.031,-.029):FVector::ZeroVector));
    TArray<double> MixedReference,MixedCached;C->Sample(Mixed,MixedReference,false);C->Sample(Mixed,MixedCached);
    for(int32 I=0;I<Mixed.Num();++I)Error=FMath::Max(Error,FMath::Abs(MixedReference[I]-MixedCached[I]));
    TArray<TFuture<double>> Workers;for(int32 Worker=0;Worker<8;++Worker)Workers.Add(Async(EAsyncExecution::ThreadPool,[C,Points,Reference]
    {TArray<double> Values;C->Sample(Points,Values);double Worst=0;for(int32 I=0;I<Points.Num();++I)Worst=FMath::Max(Worst,FMath::Abs(Reference[I]-Values[I]));return Worst;}));
    for(auto& Worker:Workers)Error=FMath::Max(Error,Worker.Get());
    // Demand covers the generated walkable floor across the whole composition,
    // including sides and alcoves; the route is not used to construct it.
    const auto Demand=C->FloorCollisionDemandMeters();int32 FloorSites=0;bool Covered=true;
    for(double X=-130;X<=90;X+=2)for(double Y=-60;Y<=60;Y+=2)
    {
        const double F=C->FloorHeight(X,Y);const FVector Foot=C->BodyPoint(FVector(X,Y,F));
        TArray<FVector> Check={C->BodyPoint(FVector(X,Y,F-.35)),C->BodyPoint(FVector(X,Y,F+1))};TArray<double> Air;C->Sample(Check,Air);
        if(Air[0]>=0||Air[1]<=.15)continue;++FloorSites;
        for(double Z:{-.35,1.,2.5})
        {
            const auto Q=C->BodyPoint(FVector(X,Y,F+Z));bool Found=false;
            for(const auto& Region:Demand)if(FVector::DistSquared(Region.Center,Q)<=FMath::Square(Region.W)){Found=true;break;}
            Covered&=Found;
        }
    }
    TestTrue(TEXT("Native demand covers every sampled floor, capsule and head position"),Covered&&FloorSites>1000);
    TestTrue(TEXT("Finite cave demand has a bounded activation size"),Demand.Num()>0&&Demand.Num()<=128);
    uint64 Hits,Misses,Bytes;C->GetSampleCacheStats(Hits,Misses,Bytes);
    TestEqual(TEXT("Cold, warm, reordered and concurrent samples preserve exact fields"),Error,0.);
    TestTrue(TEXT("Repeated native grid samples reuse immutable field values"),Hits>Points.Num());
    TestTrue(TEXT("Sample cache has a fixed eight MiB allocation bound"),Bytes<=8*1024*1024);
    auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("points"),Points.Num());O->SetNumberField(TEXT("floorSites"),FloorSites);O->SetNumberField(TEXT("nativeFloorDemandInvokers"),Demand.Num());O->SetNumberField(TEXT("hits"),Hits);O->SetNumberField(TEXT("misses"),Misses);O->SetNumberField(TEXT("bytes"),Bytes);O->SetNumberField(TEXT("maximumFieldErrorMeters"),Error);O->SetNumberField(TEXT("referenceSeconds"),ReferenceSeconds);O->SetNumberField(TEXT("warmSeconds"),WarmSeconds);
    FString Path;if(FParse::Value(FCommandLine::Get(),TEXT("CLSampleCacheReport="),Path)){FString Json;FJsonSerializer::Serialize(O,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*Path);}
    AddInfo(FString::Printf(TEXT("SPATIAL_CACHE points=%d hits=%llu misses=%llu bytes=%llu maxError=%.9f reference=%.6fs warm=%.6fs"),Points.Num(),Hits,Misses,Bytes,Error,ReferenceSeconds,WarmSeconds));return !HasAnyErrors();
}
#endif

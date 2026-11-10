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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSpatialWorkerTest,"Catalyst.Landform.Caves.SpatialPrototype.WorkerPackets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLSpatialWorkerTest::RunTest(const FString&)
{
    FString J,E;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/SpatialCave.json")));
    auto* Asset=NewObject<UCLPlanetRecipeAsset>();if(!Asset->ImportRecipeJson(J,E)){AddError(E);return false;}
    const auto Plan=FCLPlanetCompiler::Compile(Asset->Recipe,E);if(!Plan||!Plan->Caves||!Plan->Caves->Spatial){AddError(E);return false;}
    const auto C=Plan->Caves->Spatial;const FVector Center=C->BodyPoint(FVector(15,-5,2));TArray<FVector> Points;
    // Real native Cartesian grid positions, including both sides of the floor.
    for(int32 X=-12;X<12;++X)for(int32 Y=-16;Y<16;++Y)for(int32 Z=-16;Z<16;++Z)
        Points.Add((FVector(FMath::RoundToDouble(Center.X*100/20),FMath::RoundToDouble(Center.Y*100/20),FMath::RoundToDouble(Center.Z*100/20))+FVector(X,Y,Z))*.2);
    TArray<double> Reference,Cold,Warm;C->Sample(Points,Reference);C->Sample(Points,Cold);C->Sample(Points,Warm);double Error=0;
    for(int32 I=0;I<Points.Num();++I)Error=FMath::Max(Error,FMath::Max(FMath::Abs(Reference[I]-Cold[I]),FMath::Abs(Reference[I]-Warm[I])));
    // Reordered, partially novel coordinates exercise mixed hits/misses and
    // cannot be implemented as reuse of the previous packet's result array.
    TArray<FVector> Mixed;for(int32 I=Points.Num()-1;I>=0;--I)Mixed.Add(Points[I]+(I%3==0?FVector(.017,.031,-.029):FVector::ZeroVector));
    TArray<double> MixedReference,MixedCached;C->Sample(Mixed,MixedReference);C->Sample(Mixed,MixedCached);
    for(int32 I=0;I<Mixed.Num();++I)Error=FMath::Max(Error,FMath::Abs(MixedReference[I]-MixedCached[I]));
    TArray<TFuture<double>> Workers;for(int32 Worker=0;Worker<8;++Worker)Workers.Add(Async(EAsyncExecution::ThreadPool,[C,Points,Reference]
    {TArray<double> Values;C->Sample(Points,Values);double Worst=0;for(int32 I=0;I<Points.Num();++I)Worst=FMath::Max(Worst,FMath::Abs(Reference[I]-Values[I]));return Worst;}));
    for(auto& Worker:Workers)Error=FMath::Max(Error,Worker.Get());
    TestEqual(TEXT("Repeated, reordered and concurrent immutable packets preserve exact fields"),Error,0.);
    AddInfo(FString::Printf(TEXT("SPATIAL_WORKER_PACKETS points=%d concurrentWorkers=8 maxError=%.12g"),Points.Num(),Error));return !HasAnyErrors();
}
#endif

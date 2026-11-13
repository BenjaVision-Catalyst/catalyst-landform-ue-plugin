#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Model/CLPlanetRecipe.h"
#include "Scene/CLScene.h"
#include "Surface/CLSurfaceCompiler.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "CLPlanetStamp.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRockMaterialBindingsTest,"Catalyst.Landform.Rock.MaterialBindings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRockMaterialBindingsTest::RunTest(const FString&)
{
    FString Error;const auto Library=FCLCompiledSurfaceLibrary::Resolve(Error);
    if(!Library){AddError(Error);return false;}
    for(const FName Id:{FName(TEXT("StrongRock")),FName(TEXT("MixedRock")),FName(TEXT("WeakRock"))})
    {
        const int32 I=Library->Find(Id,1);
        if(!TestTrue(TEXT("Packaged rock artwork resolves independently of old surface revisions"),I!=INDEX_NONE))return false;
        TestTrue(TEXT("Rock beds reuse a rock-face texture, never the soil/scree artwork"),Library->Textures[I].BaseColor==Library->Textures[Library->Find(TEXT("Rock"),2)].BaseColor);
    }
    for(const FName Id:{FName(TEXT("Rock")),FName(TEXT("Scree")),FName(TEXT("Soil"))})TestTrue(TEXT("Authored revision-2 artwork remains installed"),Library->Find(Id,2)!=INDEX_NONE);

    auto Recipe=FCLPlanetCompiler::ContinentalBase();Recipe.UseGuidedGeology();Recipe.SchemaVersion=23;
    Recipe.Caves.bEnabled=false;Recipe.Rock.bEnabled=true;Recipe.Rock.ApplyProgression(.58);
    const auto Before=Recipe;const auto OldPlan=FCLPlanetCompiler::Compile(Recipe,Error);
    if(!OldPlan){AddError(Error);return false;}
    for(int32 Retired=1;Retired<5;++Retired)
    {
        auto Unsupported=Recipe;Unsupported.Rock.AlgorithmVersion=Retired;
        TestFalse(TEXT("Retired rock profiles are rejected instead of aliased or migrated"),FCLPlanetCompiler::Compile(Unsupported,Error).IsValid());
    }
    if(!Recipe.Rock.BindBedrockMaterials(*Library,Error)){AddError(Error);return false;}
    auto Identity=Recipe.Rock;
    for(int32 I=0;I<Identity.Units.Num();++I){Identity.Units[I].SurfaceId=Before.Rock.Units[I].SurfaceId;Identity.Units[I].SurfaceRevision=Before.Rock.Units[I].SurfaceRevision;}
    TestTrue(TEXT("Explicit bindings change only artwork IDs/revisions"),FCLRockLayer::StaticStruct()->CompareScriptStruct(&Identity,&Before.Rock,0));
    const auto Plan=FCLPlanetCompiler::Compile(Recipe,Error);if(!Plan){AddError(Error);return false;}
    TestEqual(TEXT("Rebinding retains every geological contact"),Plan->Rock->GetBedCount(),OldPlan->Rock->GetBedCount());
    for(int32 I=0;I<Plan->Rock->GetBedCount();++I)
    {
        const auto A=Plan->Rock->GetBed(I),B=OldPlan->Rock->GetBed(I);
        TestTrue(TEXT("Material changes leave finite metric bed geometry exact"),A.Unit==B.Unit&&A.LowerMeters==B.LowerMeters&&A.UpperMeters==B.UpperMeters);
    }
    FCLCompiledSurfaceLibrary Missing;Missing.Ids=Library->Ids;Missing.Revisions=Library->Revisions;
    Missing.Ids.Remove(TEXT("WeakRock"));auto Rejected=Before.Rock;
    TestFalse(TEXT("Incomplete material sets refuse before mutation"),Rejected.BindBedrockMaterials(Missing,Error));
    TestTrue(TEXT("Failed material action retains all authored data"),FCLRockLayer::StaticStruct()->CompareScriptStruct(&Rejected,&Before.Rock,0));

    FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Radius=Recipe.RadiusMeters*100;Stamp.Recipe=Recipe;Stamp.CompiledRecipe=Plan;Stamp.Behavior=EVoxelStampBehavior::AffectAll;
    const auto Runtime=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(Stamp),nullptr));
    if(!TestTrue(TEXT("Rebound geology creates a stock VPP stamp"),Runtime.IsValid()))return false;
    constexpr int32 Count=512;FVoxelDoubleVectorBuffer Positions;Positions.Allocate(Count);
    TArray<float> Distances;Distances.Init(MAX_flt,Count);TVoxelArray<FVoxelSurfaceTypeBlend> Types;Types.SetNum(Count);
    TArray<FVector> Points;TSet<FGuid> Seen;
    for(int32 I=0;I<Count;++I)
    {
        const double Z=1-2*(I+.5)/Count,A=I*2.399963229728653;
        const FVector P=FVector(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z)*(170+420*((I*17)%Count)/double(Count));
        Points.Add(P);Positions.Set(I,P*100);const auto Old=OldPlan->Rock->Sample(P),New=Plan->Rock->Sample(P);Seen.Add(New.UnitId);
        TestTrue(TEXT("Appearance keeps unit/stratum IDs and mining properties exact"),Old.UnitId==New.UnitId&&Old.StratumId==New.StratumId&&Old.MiningResistance==New.MiningResistance&&Old.Erodibility==New.Erodibility);
        TestEqual(TEXT("Appearance leaves the accepted terrain unchanged"),Plan->SampleHeightMeters(P),OldPlan->SampleHeightMeters(P));
    }
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.Rock.MaterialBindings"));const auto Table=FVoxelSurfaceTypeTable::Get();
    FVoxelQuery Query(0,FVoxelLayers::Empty(),*Table,Dependencies);
    Runtime->Apply(FVoxelVolumeSparseQuery::Create(Query,MakeVoxelArrayView(Distances),Types,FVoxelMetadataMap(),Positions,true,{}),FVoxelVolumeTransform());
    double Worst=0;
    for(int32 I=0;I<Count;++I)
    {
        FCLSurfaceWeights Expected,Actual;Plan->Rock->ApplySurfaces(Plan->RockPositionMeters(Points[I]),Expected);
        for(const auto& Layer:Types[I].GetLayers())
        {
            const int32 K=Library->NativeTypes.IndexOfByKey(Layer.Type);
            if(!TestTrue(TEXT("Native VPP blend resolves in the shared library"),K!=INDEX_NONE))return false;
            Actual.Values[K]=Layer.Weight.ToFloat();
        }
        for(int32 K=0;K<Library->Ids.Num();++K)Worst=FMath::Max(Worst,FMath::Abs(Expected.Values[K]-Actual.Values[K]));
    }
    TestEqual(TEXT("All three geological units are exercised"),Seen.Num(),3);
    TestTrue(TEXT("Native VPP receives the rebound classification within weight quantization"),Worst<.0001);
    AddInfo(FString::Printf(TEXT("512 native 3D material probes; error %.9g; unchanged beds, identity and mining properties"),Worst));
    return true;
}
#endif

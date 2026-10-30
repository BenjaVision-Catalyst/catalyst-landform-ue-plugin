#if WITH_DEV_AUTOMATION_TESTS
#include "Model/CLTerrainType.h"
#include "CLStrataEditing.h"
#include "CLSectionGeometry.h"
#include "CLPlanet.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "Async/Async.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
#include "ScopedTransaction.h"

namespace
{
bool LoadTerrain(const TCHAR* Name, UCLTerrainTypeAsset* Asset, FString& Error)
{
    FString Json;
    const FString Path = IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir() / TEXT("Examples/TerrainTypes") / (FString(Name) + TEXT(".json"));
    if (!FFileHelper::LoadFileToString(Json, *Path)) { Error = TEXT("Cannot load ") + Path; return false; }
    return Asset->ImportTerrainTypeJson(Json, Error);
}
FVector Direction(int32 I, int32 Count)
{
    const double Z = 1 - 2 * (I + .5) / Count, A = I * 2.399963229728653;
    return FVector(FMath::Sqrt(1 - Z * Z) * FMath::Cos(A), FMath::Sqrt(1 - Z * Z) * FMath::Sin(A), Z);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGuideIntervalTest, "Catalyst.Landform.TerrainTypes.GuideIntervals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLGuideIntervalTest::RunTest(const FString&)
{
    FString Error; auto R = FCLPlanetCompiler::RollingPlains(); R.RadiusMeters = 1000;
    FCLNoiseLayer Layer; Layer.Name = TEXT("Interval");
    Layer.Minimum.Mode = Layer.Maximum.Mode = ECLHeightReferenceMode::IntervalHeight;
    Layer.Minimum.Guide = Layer.Maximum.Guide = TEXT("SurfaceMin");
    Layer.Minimum.UpperGuide = Layer.Maximum.UpperGuide = TEXT("SurfaceMax");
    Layer.Minimum.Fraction = .25; Layer.Maximum.Fraction = .75;
    Layer.Scale = ECLNoiseScale::PlanetRadius; Layer.WavelengthRadiusFraction = .5;
    R.NoiseLayers = {Layer}; auto Plan = FCLPlanetCompiler::Compile(R, Error);
    if (!TestTrue(TEXT("Interval layer compiles"), Plan.IsValid())) return false;
    TestTrue(TEXT("Lower height = lerp(300,600,.25)"), FMath::IsNearlyEqual(Plan->RadiusMeters + Plan->Layers[0].Minimum, 375.0));
    TestTrue(TEXT("Upper height = lerp(300,600,.75)"), FMath::IsNearlyEqual(Plan->RadiusMeters + Plan->Layers[0].Maximum, 525.0));
    TestEqual(TEXT("Planet-relative wavelength resolved once"), Plan->Layers[0].Wavelength, 500.0);
    R.NoiseLayers[0].Minimum.Mode = R.NoiseLayers[0].Maximum.Mode = ECLHeightReferenceMode::IntervalDelta;
    R.NoiseLayers[0].Minimum.Fraction = -.1; R.NoiseLayers[0].Maximum.Fraction = .1;
    Plan = FCLPlanetCompiler::Compile(R, Error);
    TestTrue(TEXT("Signed interval delta uses span, not absolute guide heights"),
        Plan && FMath::IsNearlyEqual(Plan->Layers[0].Minimum, -30.0) && FMath::IsNearlyEqual(Plan->Layers[0].Maximum, 30.0));
    R.NoiseLayers[0].Minimum.Fraction = -1.1;
    TestFalse(TEXT("Out-of-range fraction rejected"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    R.NoiseLayers = {Layer}; R.NoiseLayers[0].Maximum.UpperGuide = TEXT("Missing");
    TestFalse(TEXT("Missing upper guide rejected"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    TestTrue(TEXT("Missing-guide diagnostic identifies name"), Error.Contains(TEXT("Missing")));
    R.NoiseLayers = {Layer}; R.Strata.Stratum[1].NormalizedHeight = .7;
    TestFalse(TEXT("Reversed guide interval rejected"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    R = FCLPlanetCompiler::RollingPlains(); R.NoiseLayers = {Layer};
    TestTrue(TEXT("Upper Stratum can be renamed"), FCLStrataEditing::Rename(R, 2, TEXT("TerrainCeiling"), Error));
    TestEqual(TEXT("Rename updates upper endpoint references"), R.NoiseLayers[0].Maximum.UpperGuide, FName(TEXT("TerrainCeiling")));
    TestTrue(TEXT("Renamed interval still compiles"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    R.SchemaVersion = 2;
    TestFalse(TEXT("Schema 2 cannot silently consume schema 3 features"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTerrainProfileTest, "Catalyst.Landform.TerrainTypes.ProfilesAndSnapshots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLTerrainProfileTest::RunTest(const FString&)
{
    FString Error, Json; TArray<double> Relief;
    for (const TCHAR* Name : {TEXT("Plains"), TEXT("Hills"), TEXT("MountainRidges")})
    {
        auto* Terrain = NewObject<UCLTerrainTypeAsset>();
        if (!TestTrue(TEXT("Source terrain JSON imports"), LoadTerrain(Name, Terrain, Error))) { AddError(Error); return false; }
        TestTrue(TEXT("Terrain type exports through Foundation"), Terrain->ExportJsonString(Json, Error));
        TestFalse(TEXT("Terrain definition owns no Strata"), Json.Contains(TEXT("normalizedHeight")));
        auto* Copy = NewObject<UCLTerrainTypeAsset>();
        TestTrue(TEXT("Terrain definition roundtrips"), Copy->ImportTerrainTypeJson(Json, Error));
        const FGuid Id = Copy->TerrainType.Id;
        auto* Duplicated = DuplicateObject<UCLTerrainTypeAsset>(Copy, GetTransientPackage());
        TestTrue(TEXT("Duplicating a reusable type creates a distinct identity"), Duplicated->TerrainType.Id != Id);
        TestEqual(TEXT("Duplicated type retains generation settings"), Duplicated->TerrainType.NoiseLayers.Num(), Copy->TerrainType.NoiseLayers.Num());
        FString Unknown = Json; Unknown.InsertAt(1, TEXT("\"unknownOperation\":true,"));
        TestFalse(TEXT("Unknown terrain fields rejected"), Copy->ImportTerrainTypeJson(Unknown, Error));
        TestEqual(TEXT("Failed import preserves previous type"), Copy->TerrainType.Id, Id);
        auto R = FCLPlanetCompiler::RollingPlains(); R.Name = TEXT("Keep planet name"); R.RadiusMeters = 1000; R.Seed = 314;
        const FCLStrata Before = R.Strata;
        if (!TestTrue(TEXT("Terrain applies in planet context"), Terrain->TerrainType.ApplyTo(R, Error))) { AddError(Error); return false; }
        TestEqual(TEXT("Planet name retained"), R.Name, FString(TEXT("Keep planet name")));
        TestEqual(TEXT("Planet seed retained"), R.Seed, 314);
        TestEqual(TEXT("Planet radius retained"), R.RadiusMeters, 1000.0);
        TestEqual(TEXT("Global Strata identity retained"), R.Strata.Id, Before.Id);
        for (int32 I = 0; I < Before.Stratum.Num(); ++I)
            TestEqual(TEXT("Global guide values retained"), R.Strata.Stratum[I].NormalizedHeight, Before.Stratum[I].NormalizedHeight);
        const auto A = FCLPlanetCompiler::Compile(R, Error);
        R.RadiusMeters *= 2; const auto B = FCLPlanetCompiler::Compile(R, Error);
        double Low = 1.e9, High = -1.e9;
        for (int32 I = 0; I < 2048; ++I)
        {
            const FVector D = Direction(I, 2048); const double H = A->SampleHeightMeters(D);
            Low = FMath::Min(Low, H); High = FMath::Max(High, H);
            TestTrue(TEXT("Samples remain within compiled bounds"), H >= A->MinimumHeightMeters && H <= A->MaximumHeightMeters);
            TestTrue(TEXT("Scaling radius preserves normalized terrain"), FMath::Abs(H / A->RadiusMeters - B->SampleHeightMeters(D) / B->RadiusMeters) < 1.e-5);
        }
        Relief.Add(High - Low);
        AddInfo(FString::Printf(TEXT("%s sampled relief: %.3f m at 1000m sky radius"), *Terrain->TerrainType.Name, High - Low));
        const auto Snapshot = A; Terrain->TerrainType.NoiseLayers.Reset();
        const auto Work = Async(EAsyncExecution::ThreadPool, [Snapshot] { return Snapshot->SampleHeightMeters(FVector::ZAxisVector); });
        TestEqual(TEXT("Worker snapshot is independent of edited terrain asset"), Work.Get(), A->SampleHeightMeters(FVector::ZAxisVector));
        const auto G = FCLSectionGeometry::Build(*A, .3); double MaximumError = 0;
        for (int32 I = 0; I < G.SurfaceVertexCount; ++I)
        {
            const FVector P = G.SourceSurfacePoints[I] / 100;
            MaximumError = FMath::Max(MaximumError, FMath::Abs(P.Size() - A->RadiusMeters - A->SampleHeightMeters(P)));
        }
        AddInfo(FString::Printf(TEXT("%s section maximum field error: %.3f cm"), Name, MaximumError * 100));
        TestTrue(TEXT("Kilometre-scale section agrees within 5 cm with CPU float noise field"), MaximumError < .05);
        for (const auto& Strata : FCLStrataPresets::Defaults())
        { auto Context = FCLPlanetCompiler::RollingPlains(); Context.Strata = Strata;
          TestTrue(TEXT("Profile supports every built-in Strata including airless"), Copy->TerrainType.ApplyTo(Context, Error)); }
        auto Missing = FCLPlanetCompiler::RollingPlains(); Missing.Strata.Stratum.Reset();
        const int32 OldSchema = Missing.SchemaVersion; const FString OldName = Missing.TerrainTypeName;
        TestFalse(TEXT("Missing contextual guides reject application"), Copy->TerrainType.ApplyTo(Missing, Error));
        TestEqual(TEXT("Failed application preserves schema"), Missing.SchemaVersion, OldSchema);
        TestEqual(TEXT("Failed application preserves applied type"), Missing.TerrainTypeName, OldName);
    }
    TestTrue(TEXT("Hills have more sampled relief than plains"), Relief[1] > Relief[0] * 1.5);
    TestTrue(TEXT("Ridges have more sampled relief than plains"), Relief[2] > Relief[0] * 1.5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTerrainVPPTest, "Catalyst.Landform.TerrainTypes.VPPAndUndo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLTerrainVPPTest::RunTest(const FString&)
{
    FString Error; UWorld* World = GEditor->GetEditorWorldContext().World();
    auto* Planet = World->SpawnActorDeferred<ACLPlanet>(ACLPlanet::StaticClass(), FTransform::Identity);
    Planet->bEnableNanite = false; Planet->bEnableTessellation = false; Planet->bEnableNavigation = false;
    Planet->RecipeAsset = NewObject<UCLPlanetRecipeAsset>(Planet, NAME_None, RF_Transactional);
    Planet->FinishSpawning(FTransform::Identity);
    const FCLStrata Before = Planet->RecipeAsset->Recipe.Strata;
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.TerrainTypeTest"));
    const auto Surfaces = FVoxelSurfaceTypeTable::Get();
    const FVoxelQuery Query(0, FVoxelLayers::Empty(), *Surfaces, Dependencies);
    for (const TCHAR* Name : {TEXT("Plains"), TEXT("Hills"), TEXT("MountainRidges")})
    {
        auto* Terrain = NewObject<UCLTerrainTypeAsset>();
        if (!TestTrue(TEXT("Terrain loads"), LoadTerrain(Name, Terrain, Error))) { AddError(Error); World->DestroyActor(Planet); return false; }
        const auto OldPlan = FCLPlanetCompiler::Compile(Planet->RecipeAsset->Recipe, Error);
        const double OldHeight = OldPlan->SampleHeightMeters(FVector::ZAxisVector);
        {
            FScopedTransaction Tx(NSLOCTEXT("CL", "TerrainApplyTest", "Apply terrain type"));
            TestTrue(TEXT("Terrain applies to live recipe"), Terrain->ApplyToRecipe(Planet->RecipeAsset, Error));
        }
        const auto Plan = FCLPlanetCompiler::Compile(Planet->RecipeAsset->Recipe, Error);
        FVoxelDoubleVectorBuffer Positions; Positions.Allocate(16);
        for (int32 I = 0; I < 16; ++I)
        { const FVector D = Direction(I, 16); Positions.Set(I, D * (Plan->RadiusMeters + Plan->SampleHeightMeters(D)) * 100); }
        const auto Distances = Planet->GetSamplingRuntime()->SampleDistances(Query, Positions);
        for (int32 I = 0; I < 16; ++I) TestTrue(TEXT("VPP and compiled profile share the zero surface"), FMath::Abs(Distances[I]) < 1);
        TestEqual(TEXT("Applying profile leaves global Strata unchanged"), Planet->RecipeAsset->Recipe.Strata.Id, Before.Id);
        GEditor->UndoTransaction();
        const auto Restored = FCLPlanetCompiler::Compile(Planet->RecipeAsset->Recipe, Error);
        TestEqual(TEXT("Undo restores previous planet field"), Restored->SampleHeightMeters(FVector::ZAxisVector), OldHeight);
    }
    World->DestroyActor(Planet); return true;
}
#endif

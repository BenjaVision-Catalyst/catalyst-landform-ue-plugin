#if WITH_DEV_AUTOMATION_TESTS
#include "Model/CLTerrainType.h"
#include "VoxelISPCNodeHelpers.h"
#include <limits>
#include "CLSectionGeometry.h"
#include "CLPlanet.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Async/Async.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"

namespace
{
UCLTerrainTypeAsset* NativeMountain(FString& Error)
{
    FString Json;
    FFileHelper::LoadFileToString(Json, *(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir() /
        TEXT("Examples/TerrainTypes/MountainRidges.json")));
    auto* Asset = NewObject<UCLTerrainTypeAsset>();
    return Asset->ImportTerrainTypeJson(Json, Error) ? Asset : nullptr;
}
FVector SphereDirection(int32 I, int32 Num)
{
    const double Z = 1 - 2 * (I + .5) / Num, A = I * 2.399963229728653;
    return FVector(FMath::Sqrt(1 - Z * Z) * FMath::Cos(A), FMath::Sqrt(1 - Z * Z) * FMath::Sin(A), Z);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLNativeKernelTest, "Catalyst.Landform.NativeGeneration.KernelAndVersioning",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLNativeKernelTest::RunTest(const FString&)
{
    FString Error, Json;
    auto R = FCLPlanetCompiler::RollingPlains();
    const auto Legacy = FCLPlanetCompiler::Compile(R, Error);
    TestTrue(TEXT("Existing recipes retain UE algorithm 1"), Legacy && !Legacy->NativePerlin);
    R.AlgorithmVersion = 2;
    const auto Native = FCLPlanetCompiler::Compile(R, Error);
    if (!TestTrue(TEXT("Native VPP kernel available, no fallback"), Native && Native->NativePerlin)) { AddError(Error); return false; }
    // Independent single-node oracle: known simple recipe, explicit graph-node pin
    // order and a single octave. This catches coordinate/seed/ABI/shape mistakes.
    FCLNoiseLayer L; L.Name = TEXT("Oracle"); L.Seed = 7; L.Octaves = 1;
    L.Shape = ECLNoiseShape::Ridged; L.Blend = ECLNoiseBlend::Override;
    L.WavelengthMeters = 37; L.Minimum.OffsetMeters = -40; L.Maximum.OffsetMeters = -10;
    R.NoiseLayers = {L}; const auto Plan = FCLPlanetCompiler::Compile(R, Error);
    if (!TestTrue(TEXT("Oracle recipe compiles"), Plan.IsValid())) return false;
    const auto Kernel = GVoxelNodeISPCPtrs.FindRef(TEXT("VoxelComputeNode_PerlinNoise3D"));
    bool bDifferent = false; double MaxOracleError = 0;
    for (int32 Count : {0, 1, 31, 512, 513, 1025})
    {
        TArray<FVector> D; for (int32 I = 0; I < Count; ++I) D.Add(SphereDirection(I, Count));
        TArray<double> H; Plan->SampleHeightsMeters(D, H);
        TestEqual(TEXT("Batch output count"), H.Num(), Count);
        for (int32 I = 0; I < Count; ++I)
        {
            const FVector P = D[I].GetSafeNormal() * (100.0 / 37);
            const float X = P.X, Y = P.Y, Z = P.Z, One = 1; float N = 0;
            const int32 Seed = Plan->Layers[0].Seed;
            const ispc::FVoxelInputBuffer Pins[] = {{&X,true},{&Y,true},{&Z,true},{&One,true},{&One,true},{&Seed,true}};
            void* Output[] = {&N}; Kernel(Pins, Output, 1);
            const double Expected = -40 + 30 * FMath::Clamp(1.0 - FMath::Abs(double(N)), 0.0, 1.0);
            MaxOracleError = FMath::Max(MaxOracleError, FMath::Abs(H[I] - Expected));
            TestEqual(TEXT("Scalar and batch generation match"), Plan->SampleHeightMeters(D[I]), H[I]);
            bDifferent |= FMath::Abs(Native->SampleHeightMeters(D[I]) - Legacy->SampleHeightMeters(D[I])) > .01;
        }
    }
    AddInfo(FString::Printf(TEXT("Native single-node oracle maximum error: %.12f metres"), MaxOracleError));
    // VPP coordinates are floats; the independently compiled test's vector
    // normalization can round across a float boundary. Qualify to 0.01 cm.
    TestTrue(TEXT("Batched field equals native graph-node kernel oracle within 0.01 cm"), MaxOracleError < .0001);
    TestTrue(TEXT("Native path is distinct from legacy UE noise"), bDifferent);
    TArray<double> Invalid;
    const TArray<FVector> InvalidDirections = {FVector::ZeroVector, FVector(std::numeric_limits<double>::quiet_NaN(),0,0)};
    Plan->SampleHeightsMeters(InvalidDirections, Invalid);
    TestTrue(TEXT("Invalid direction has same neutral result in both algorithms"), Invalid.Num() == 2 && Invalid[0] == 0 && Invalid[1] == 0);
    const auto Work = Async(EAsyncExecution::ThreadPool, [Plan] { return Plan->SampleHeightMeters(FVector(1,2,3)); });
    TestEqual(TEXT("Immutable plan evaluates on worker"), Work.Get(), Plan->SampleHeightMeters(FVector(1,2,3)));
    auto* Type = NativeMountain(Error);
    if (!TestNotNull(TEXT("Native Mountain ridges source imports"), Type)) { AddError(Error); return false; }
    TestEqual(TEXT("Mountain ridges explicitly selects native algorithm"), Type->TerrainType.AlgorithmVersion, 2);
    TestTrue(TEXT("Native type exports"), Type->ExportJsonString(Json, Error));
    auto* Copy = NewObject<UCLTerrainTypeAsset>();
    TestTrue(TEXT("Native generation choice roundtrips"), Copy->ImportTerrainTypeJson(Json, Error));
    TestEqual(TEXT("Generation choice preserved"), Copy->TerrainType.AlgorithmVersion, 2);
    auto* Recipe = NewObject<UCLPlanetRecipeAsset>();
    TestTrue(TEXT("Applying type transfers generation choice"), Copy->ApplyToRecipe(Recipe, Error));
    TestEqual(TEXT("Planet snapshot pins native algorithm"), Recipe->Recipe.AlgorithmVersion, 2);
    TestTrue(TEXT("Native recipe exports"), Recipe->ExportJsonString(Json, Error));
    auto* Reimport = NewObject<UCLPlanetRecipeAsset>();
    TestTrue(TEXT("Native recipe reimports"), Reimport->ImportRecipeJson(Json, Error));
    TestEqual(TEXT("Recipe evaluator version survives serialization"), Reimport->Recipe.AlgorithmVersion, 2);
    R.AlgorithmVersion = 999;
    TestFalse(TEXT("Unknown generation versions fail closed"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLNativeSectionTest, "Catalyst.Landform.NativeGeneration.DesignerSphereParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLNativeSectionTest::RunTest(const FString&)
{
    FString Error;
    auto* Type = NativeMountain(Error);
    if (!TestNotNull(TEXT("Mountain type loaded"), Type)) return false;
    auto* World = GEditor->GetEditorWorldContext().World();
    auto* Planet = World->SpawnActorDeferred<ACLPlanet>(ACLPlanet::StaticClass(), FTransform(FRotator(17,38,-12), FVector(123456,-45678,9876)));
    Planet->bEnableNanite = false; Planet->bEnableTessellation = false; Planet->bEnableNavigation = false;
    Planet->RecipeAsset = NewObject<UCLPlanetRecipeAsset>(Planet);
    Planet->FinishSpawning(Planet->GetActorTransform());
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.NativeGenerationParity"));
    const auto Surfaces = FVoxelSurfaceTypeTable::Get();
    const FVoxelQuery Query(0, FVoxelLayers::Empty(), *Surfaces, Dependencies);
    double MaxFieldError = 0, MaxPreviewError = 0; int32 Samples = 0;
    for (int32 Case = 0; Case < 4; ++Case)
    {
        auto R = FCLPlanetCompiler::RollingPlains(); R.RadiusMeters = Case == 0 ? 100 : 1000;
        R.Seed = 1729 + Case * 999; R.Strata = FCLStrataPresets::Defaults()[2];
        if (Case == 2) Type->TerrainType.NoiseLayers[1].bEnabled = false;
        if (Case == 3)
        {
            Type->TerrainType.NoiseLayers[1].bEnabled = true;
            Type->TerrainType.NoiseLayers[0].Scale = ECLNoiseScale::Meters;
            Type->TerrainType.NoiseLayers[0].WavelengthMeters = 127;
            Type->TerrainType.NoiseLayers[1].Blend = ECLNoiseBlend::Max;
        }
        if (!Type->TerrainType.ApplyTo(R, Error)) { AddError(Error); break; }
        const auto Plan = FCLPlanetCompiler::Compile(R, Error);
        Planet->RecipeAsset->Recipe = R; Planet->RebuildTerrain();
        const auto Runtime = Planet->GetSamplingRuntime();
        if (!TestTrue(TEXT("Actual spherical VPP runtime initialized"), Runtime && Runtime->RecipePlan && Runtime->RecipePlan->NativePerlin)) break;
        TArray<FVector> Points;
        // All Designer top vertices at both ends and default of the coverage range.
        for (double Width : {100.0, 500.0, 1000.0})
        {
            const auto Section = FCLSectionGeometry::Build(*Plan, 0, Width);
            Points.Append(Section.SourceSurfacePoints);
            for (int32 I = 0; I < Section.SurfaceVertexCount; ++I)
                MaxPreviewError = FMath::Max(MaxPreviewError, FMath::Abs(Section.Vertices[I].Z / 100000 -
                    Section.SourceSurfacePoints[I].Size() / (100 * Plan->RadiusMeters)));
        }
        // Whole globe, poles, both sides of the longitude seam.
        for (int32 I = 0; I < 1024; ++I)
        {
            const FVector D = SphereDirection(I, 1024);
            Points.Add(D * (Plan->RadiusMeters + Plan->SampleHeightMeters(D)) * 100);
        }
        for (FVector D : {FVector::ZAxisVector, -FVector::ZAxisVector, FVector(-1,1.e-9,0), FVector(-1,-1.e-9,0)})
            Points.Add(D.GetSafeNormal() * (Plan->RadiusMeters + Plan->SampleHeightMeters(D)) * 100);
        FVoxelDoubleVectorBuffer Buffer; Buffer.Allocate(Points.Num());
        for (int32 I = 0; I < Points.Num(); ++I) Buffer.Set(I, Points[I]);
        const auto Distances = Runtime->SampleDistances(Query, Buffer);
        for (int32 I = 0; I < Points.Num(); ++I) MaxFieldError = FMath::Max(MaxFieldError, FMath::Abs(double(Distances[I])));
        // The two radial sides of the surface must be solid/empty, not just a
        // coincident zero returned for every query.
        FVoxelDoubleVectorBuffer Sides; Sides.Allocate(2);
        const FVector P = Points[Points.Num()/2], D = P.GetSafeNormal();
        Sides.Set(0, P - D * 100); Sides.Set(1, P + D * 100);
        const auto SD = Runtime->SampleDistances(Query, Sides);
        TestTrue(TEXT("VPP surface has correct inside/outside signs"), SD[0] < -95 && SD[1] > 95);
        Samples += Points.Num();
    }
    World->DestroyActor(Planet);
    AddInfo(FString::Printf(TEXT("Native parity: %d source samples; maximum VPP field residual %.6f cm; normalized flat preview error %.12f"), Samples, MaxFieldError, MaxPreviewError));
    TestTrue(TEXT("Every Designer/whole-globe point lies within 5 cm of the spherical VPP zero surface"), MaxFieldError < 5);
    TestTrue(TEXT("Flat display faithfully unwraps source radial heights"), MaxPreviewError < 1.e-9);
    return true;
}
#endif

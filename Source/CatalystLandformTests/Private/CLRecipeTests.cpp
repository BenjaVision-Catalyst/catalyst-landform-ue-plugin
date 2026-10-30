#include "Modules/ModuleManager.h"
IMPLEMENT_MODULE(FDefaultModuleImpl, CatalystLandformTests)
#if WITH_DEV_AUTOMATION_TESTS
#include "Model/CLPlanetRecipe.h"
#include "CLPlanet.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Async/Async.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
#include "Heightmap/VoxelHeightmap.h"
#include "Heightmap/VoxelHeightmap_Height.h"
#include "ScopedTransaction.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGuideTest, "Catalyst.Landform.Recipes.HeightGuides",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLGuideTest::RunTest(const FString&)
{
    FString Error;
    FCLPlanetRecipe R = FCLPlanetCompiler::RollingPlains();
    const auto A = FCLPlanetCompiler::Compile(R, Error);
    if (!TestTrue(TEXT("Example compiles"), A.IsValid())) return false;
    R.Strata.Stratum[0].NormalizedHeight = .5; // unused ocean reference
    const auto B = FCLPlanetCompiler::Compile(R, Error);
    TestEqual(TEXT("Guide alone creates no geometry"), A->SampleHeightMeters(FVector::ZAxisVector), B->SampleHeightMeters(FVector::ZAxisVector));
    R.Strata.Stratum.RemoveAt(0); R.Strata.Stratum.RemoveAt(2);
    TestTrue(TEXT("Unreferenced guides are optional"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    R.Strata.Stratum.RemoveAt(0);
    TestFalse(TEXT("Required guide must exist"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    TestTrue(TEXT("Diagnostic names missing guide"), Error.Contains(TEXT("SurfaceMin")));
    R.NoiseLayers[0].bEnabled = false;
    TestTrue(TEXT("Disabled layer does not require its guides"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    R = FCLPlanetCompiler::RollingPlains(); R.Strata.Stratum[1].Name = TEXT("oceanheight");
    TestFalse(TEXT("Case-insensitive duplicate guide rejected"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    R = FCLPlanetCompiler::RollingPlains(); R.NoiseLayers[0].WavelengthMeters = 0;
    TestFalse(TEXT("Zero wavelength rejected"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    R = FCLPlanetCompiler::RollingPlains(); R.AlgorithmVersion = 999;
    TestFalse(TEXT("Unknown algorithm does not silently generate another planet"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRecipeSnapshotTest, "Catalyst.Landform.Recipes.SerializationAndSnapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLRecipeSnapshotTest::RunTest(const FString&)
{
    FString Error, Json;
    auto* Asset = NewObject<UCLPlanetRecipeAsset>();
    const auto Before = FCLPlanetCompiler::Compile(Asset->Recipe, Error);
    TestTrue(TEXT("Foundation exports complete payload"), Asset->ExportJsonString(Json, Error));
    auto* Imported = NewObject<UCLPlanetRecipeAsset>();
    TestTrue(TEXT("Recipe imports"), Imported->ImportRecipeJson(Json, Error));
    const auto After = FCLPlanetCompiler::Compile(Imported->Recipe, Error);
    double Low = 1.e9, High = -1.e9;
    for (int32 I = 0; I < 1024; ++I)
    {
        const double Z = 1 - 2 * (I + .5) / 1024;
        const double Angle = I * 2.399963229728653;
        const FVector D(FMath::Sqrt(1 - Z * Z) * FMath::Cos(Angle), FMath::Sqrt(1 - Z * Z) * FMath::Sin(Angle), Z);
        const double H = Before->SampleHeightMeters(D);
        TestEqual(TEXT("JSON roundtrip reproduces noise"), H, After->SampleHeightMeters(D));
        Low = FMath::Min(Low, H); High = FMath::Max(High, H);
        TestTrue(TEXT("Conservative bounds contain evaluated surface"), H >= Before->MinimumHeightMeters && H <= Before->MaximumHeightMeters);
    }
    TestTrue(TEXT("Example has actual variation"), High - Low > 1);
    const double Original = Before->SampleHeightMeters(FVector::ZAxisVector);
    Imported->Recipe.Seed++;
    const auto Changed = FCLPlanetCompiler::Compile(Imported->Recipe, Error);
    TestTrue(TEXT("Seed changes the surface"), FMath::Abs(Changed->SampleHeightMeters(FVector::ZAxisVector) - Original) > .001);
    TestFalse(TEXT("Failed import rejects unsupported version"), Imported->ImportRecipeJson(TEXT("{\"schemaVersion\":99}"), Error));
    TestEqual(TEXT("Failed import preserves last document"), Imported->Recipe.Seed, Asset->Recipe.Seed + 1);
    TestFalse(TEXT("Unknown fields rejected"), Imported->ImportRecipeJson(TEXT("{\"unknownFeature\":42}"), Error));
    FString Unknown = Json; Unknown.InsertAt(1, TEXT("\"unsupportedFeature\":true,"));
    TestFalse(TEXT("Unknown field is rejected even alongside a complete supported payload"), Imported->ImportRecipeJson(Unknown, Error));
    const auto Future = Async(EAsyncExecution::ThreadPool, [Before] { return Before->SampleHeightMeters(FVector::ZAxisVector); });
    TestEqual(TEXT("Immutable plan evaluates on a worker after authoring changes"), Future.Get(), Original);
    const FVector SeamA(-1, 1.e-8, .1), SeamB(-1, -1.e-8, .1);
    TestTrue(TEXT("No longitude seam"), FMath::Abs(Before->SampleHeightMeters(SeamA) - Before->SampleHeightMeters(SeamB)) < .001);
    TestTrue(TEXT("Pole is finite"), FMath::IsFinite(Before->SampleHeightMeters(FVector::ZAxisVector)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRecipeLayersTest, "Catalyst.Landform.Recipes.OrderedLayers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLRecipeLayersTest::RunTest(const FString&)
{
    FCLPlanetRecipe R; FString Error;
    auto Add = [&](FName Name, ECLNoiseBlend Blend, double Height)
    { FCLNoiseLayer L; L.Name = Name; L.Blend = Blend; L.Minimum.OffsetMeters = Height; L.Maximum.OffsetMeters = Height; R.NoiseLayers.Add(L); };
    Add(TEXT("Base"), ECLNoiseBlend::Override, -30); Add(TEXT("Detail"), ECLNoiseBlend::Add, 2);
    Add(TEXT("Carve"), ECLNoiseBlend::Min, -31);
    TestEqual(TEXT("Override, Add, Min compose in order"), FCLPlanetCompiler::Compile(R, Error)->SampleHeightMeters(FVector::ZAxisVector), -31.0);
    R.NoiseLayers.Swap(1, 2);
    TestEqual(TEXT("Reordering changes result"), FCLPlanetCompiler::Compile(R, Error)->SampleHeightMeters(FVector::ZAxisVector), -29.0);
    R.NoiseLayers[0].Minimum.OffsetMeters = -100; R.NoiseLayers[0].Maximum.OffsetMeters = -100;
    TestFalse(TEXT("Stack cannot invert the planet"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRecipeVPPTest, "Catalyst.Landform.Recipes.VPPAndLiveEdits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLRecipeVPPTest::RunTest(const FString&)
{
    UWorld* World = GEditor->GetEditorWorldContext().World();
    const FTransform Pose(FQuat(FVector(1, 2, 3).GetSafeNormal(), .7), FVector(50000, -40000, 60000));
    auto* Planet = World->SpawnActorDeferred<ACLPlanet>(ACLPlanet::StaticClass(), Pose);
    Planet->bEnableNanite = false; Planet->bEnableTessellation = false; Planet->bEnableNavigation = false;
    Planet->RecipeAsset = NewObject<UCLPlanetRecipeAsset>(Planet, NAME_None, RF_Transactional);
    Planet->FinishSpawning(Pose); Planet->RebuildTerrain();
    const FVector D = FVector(1, 2, 3).GetSafeNormal();
    TArray<double> H;
    Planet->SampleRadialHeights({D}, H);
    FString Error; const auto Plan = FCLPlanetCompiler::Compile(Planet->RecipeAsset->Recipe, Error);
    TestTrue(TEXT("VPP sampling equals compiled preview in metres"), FMath::Abs(H[0] - Plan->SampleHeightMeters(D) * 100) < .001);
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.RecipeTest"));
    const auto Surfaces = FVoxelSurfaceTypeTable::Get();
    const FVoxelQuery Query(0, FVoxelLayers::Empty(), *Surfaces, Dependencies);
    FVoxelDoubleVectorBuffer Positions; Positions.Allocate(1);
    Positions.Set(0, D * (Plan->RadiusMeters * 100 + H[0]));
    TestTrue(TEXT("Same surface is zero in VPP volume"), FMath::Abs(Planet->GetSamplingRuntime()->SampleDistances(Query, Positions)[0]) < .001);
    const double Before = H[0];
    Planet->RecipeAsset->Recipe.Strata.Stratum[1].NormalizedHeight += .02;
    Planet->RecipeAsset->Recipe.Strata.Stratum[2].NormalizedHeight += .02;
    Planet->RecipeAsset->OnRecipeChanged.Broadcast();
    Planet->SampleRadialHeights({D}, H);
    TestTrue(TEXT("Guide edits rebuild live VPP terrain"), FMath::Abs(H[0] - Before - 200) < .001);
    {
        FScopedTransaction Transaction(NSLOCTEXT("CL", "GuideEditTest", "Edit Height Guides"));
        Planet->RecipeAsset->Modify();
        Planet->RecipeAsset->Recipe.Strata.Stratum[1].NormalizedHeight += .01;
        Planet->RecipeAsset->Recipe.Strata.Stratum[2].NormalizedHeight += .01;
        Planet->RecipeAsset->PostEditChange();
    }
    TArray<double> Edited; Planet->SampleRadialHeights({D}, Edited);
    TestTrue(TEXT("Editor property change rebuilds terrain"), FMath::Abs(Edited[0] - H[0] - 100) < .001);
    GEditor->UndoTransaction();
    Planet->SampleRadialHeights({D}, Edited);
    TestTrue(TEXT("Editor undo restores compiled terrain"), FMath::Abs(Edited[0] - H[0]) < .001);
    Planet->RecipeAsset->Recipe.NoiseLayers[0].Minimum.Guide = TEXT("Missing");
    Planet->RecipeAsset->OnRecipeChanged.Broadcast();
    TArray<double> Retained; Planet->SampleRadialHeights({D}, Retained);
    TestEqual(TEXT("Invalid recipe retains last terrain"), H[0], Retained[0]);
    TestTrue(TEXT("Invalid recipe has visible status"), Planet->RecipeStatus.Contains(TEXT("Missing")));
    Planet->RecipeAsset->Recipe.NoiseLayers[0].Minimum.Guide = TEXT("SurfaceMin");
    Planet->RebuildTerrain();
    auto* Heightmap = NewObject<UVoxelHeightmap>();
    Heightmap->ScaleXY = 500; Heightmap->Height->ScaleZ = 1000; Heightmap->Height->OffsetZ = 0;
    TVoxelArray<float> Samples; Samples.Init(.1f, 17 * 17); Heightmap->Height->SetHeights(17, 17, Samples);
    auto* Stamp = World->SpawnActor<ACLSphericalHeightmapStamp>();
    Stamp->Planet = Planet; Stamp->Heightmap = Heightmap;
    Stamp->SetActorLocation(Pose.TransformPosition(D * 10000));
    Stamp->BlendMode = EVoxelHeightBlendMode::Override; Stamp->Smoothness = 0; Stamp->RefreshStamp();
    Planet->SampleRadialHeights({D, -D}, Edited);
    TestTrue(TEXT("Native stamp output is clipped at the outer radius"), FMath::Abs(Edited[0]) < .001);
    FVoxelDoubleVectorBuffer Beyond; Beyond.Allocate(1); Beyond.Set(0, D * (Planet->RadiusMeters * 100 + 100));
    TestTrue(TEXT("VPP volume is empty beyond outer radius after a positive native stamp"),
        Planet->GetSamplingRuntime()->SampleDistances(Query, Beyond)[0] > 0);
    const auto Latest = FCLPlanetCompiler::Compile(Planet->RecipeAsset->Recipe, Error);
    TestTrue(TEXT("Outside native footprint the recipe remains intact"), FMath::Abs(Edited[1] - Latest->SampleHeightMeters(-D) * 100) < .001);
    World->DestroyActor(Stamp);
    World->DestroyActor(Planet);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Model/CLPlanetRecipe.h"
#include "CLStrataEditing.h"
#include "CLSectionGeometry.h"
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "ScopedTransaction.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLStrataTest, "Catalyst.Landform.Strata.AuthoringAndResize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLStrataTest::RunTest(const FString&)
{
    FString Error, Json;
    FCLPlanetRecipe R = FCLPlanetCompiler::RollingPlains();
    R.RadiusMeters = 1000;
    auto Plan = FCLPlanetCompiler::Compile(R, Error);
    if (!TestTrue(TEXT("Normalized Strata compiles"), Plan.IsValid())) return false;
    TestTrue(TEXT("SurfaceMin is at 300m from centre"), FMath::IsNearlyEqual(Plan->RadiusMeters + Plan->Guides[1].HeightMeters, 300.0));
    TestTrue(TEXT("SurfaceMax is at 600m from centre"), FMath::IsNearlyEqual(Plan->RadiusMeters + Plan->Guides[2].HeightMeters, 600.0));
    TestTrue(TEXT("Ocean is at 400m from centre"), FMath::IsNearlyEqual(Plan->RadiusMeters + Plan->Guides[0].HeightMeters, 400.0));
    R.RadiusMeters *= 2;
    auto Larger = FCLPlanetCompiler::Compile(R, Error);
    TestEqual(TEXT("Resizing preserves normalized Stratum values"), R.Strata.Stratum[1].NormalizedHeight, .3);
    TestTrue(TEXT("Resolved guide scales with radius"), FMath::IsNearlyEqual(Larger->RadiusMeters + Larger->Guides[1].HeightMeters, 600.0));
    R.Strata.Stratum[0].NormalizedHeight = 0;
    TestTrue(TEXT("Optional centre guide is valid"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    R.Strata.Stratum[0].NormalizedHeight = 1.01;
    TestFalse(TEXT("Stratum cannot exceed outer sky boundary"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    R.Strata.Stratum[0].NormalizedHeight = -.01;
    TestFalse(TEXT("Stratum cannot be below centre"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    R = FCLPlanetCompiler::RollingPlains();
    TestTrue(TEXT("Rename succeeds"), FCLStrataEditing::Rename(R, 1, TEXT("Lowlands"), Error));
    TestEqual(TEXT("Rename preserves generation references"), R.NoiseLayers[0].Minimum.Guide, FName(TEXT("Lowlands")));
    TestTrue(TEXT("Renamed recipe remains valid"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    TestFalse(TEXT("Duplicate names are rejected"), FCLStrataEditing::Rename(R, 1, TEXT("surfaceMAX"), Error));
    TestFalse(TEXT("Empty names are rejected"), FCLStrataEditing::Rename(R, 1, TEXT("  "), Error));
    const int32 Added = FCLStrataEditing::Add(R.Strata, .75);
    TestEqual(TEXT("New Stratum stores the dragged normalized radius"), R.Strata.Stratum[Added].NormalizedHeight, .75);
    TestTrue(TEXT("Deleting an unused Stratum succeeds"), FCLStrataEditing::Remove(R.Strata, Added));
    TestTrue(TEXT("Remaining recipe compiles"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    FCLStrataEditing::Remove(R.Strata, 1);
    TestFalse(TEXT("Deleting referenced Stratum diagnoses dependency"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    TestTrue(TEXT("Dependency diagnostic names deleted Stratum"), Error.Contains(TEXT("Lowlands")));

    auto* Asset = NewObject<UCLPlanetRecipeAsset>(GetTransientPackage(), NAME_None, RF_Transactional);
    auto* Library = NewObject<UCLStrataLibraryAsset>(GetTransientPackage(), NAME_None, RF_Transactional);
    Library->Library.Sets.Add(Asset->Recipe.Strata);
    {
        FScopedTransaction Tx(NSLOCTEXT("CL", "StrataUndoTest", "Move Stratum"));
        Asset->Modify(); Library->Modify();
        Asset->Recipe.Strata.Stratum[0].NormalizedHeight = .5;
        Asset->Recipe.Strata.Stratum[0].NormalizedHeight = .55;
        Library->Library.Sets[0] = Asset->Recipe.Strata;
        Asset->PostEditChange();
    }
    GEditor->UndoTransaction();
    TestEqual(TEXT("One undo restores complete drag on planet"), Asset->Recipe.Strata.Stratum[0].NormalizedHeight, .4);
    TestEqual(TEXT("One undo restores catalog too"), Library->Library.Sets[0].Stratum[0].NormalizedHeight, .4);
    Asset->Recipe.Strata.Stratum[0].NormalizedHeight = .45;
    TestEqual(TEXT("Applied planet snapshot does not mutate another template"), Library->Library.Sets[0].Stratum[0].NormalizedHeight, .4);
    TestTrue(TEXT("Schema 2 exports"), Asset->ExportJsonString(Json, Error));
    TestFalse(TEXT("Export omits obsolete metre guide array"), Json.Contains(TEXT("heightGuides")));
    auto* Copy = NewObject<UCLPlanetRecipeAsset>();
    TestTrue(TEXT("Schema 2 roundtrips"), Copy->ImportRecipeJson(Json, Error));
    TestEqual(TEXT("Strata identity survives JSON"), Copy->Recipe.Strata.Id, Asset->Recipe.Strata.Id);
    TestEqual(TEXT("Normalized value survives JSON"), Copy->Recipe.Strata.Stratum[0].NormalizedHeight, .45);

    const FString V1 = TEXT(R"({"schemaVersion":1,"algorithmVersion":1,"referenceRadiusMeters":100,
        "heightGuides":[{"name":"Surface","heightMeters":7},{"name":"Sky","heightMeters":20}],
        "noiseLayers":[{"name":"Constant","minimum":{"guide":"Surface"},"maximum":{"guide":"Surface"},"wavelengthMeters":50}]})");
    TestTrue(TEXT("Old metre-based JSON migrates explicitly"), Copy->ImportRecipeJson(V1, Error));
    auto Migrated = FCLPlanetCompiler::Compile(Copy->Recipe, Error);
    if (!TestTrue(TEXT("Migrated document compiles"), Migrated.IsValid())) return false;
    TestTrue(TEXT("Migration preserves physical terrain height"), FMath::IsNearlyEqual(Migrated->RadiusMeters + Migrated->SampleHeightMeters(FVector::ZAxisVector), 107.0));
    TestTrue(TEXT("Migration encloses old sky within new radius"), Copy->Recipe.Strata.Stratum[1].NormalizedHeight <= 1);
    TestTrue(TEXT("Migration preserves noise coordinates"), FMath::IsNearlyEqual(Copy->Recipe.NoiseLayers[0].WavelengthMeters, 50 * Copy->Recipe.RadiusMeters / 100));

    FCLPlanetRecipe Cap; FCLNoiseLayer L; L.Name = TEXT("Beyond sky"); L.Minimum.OffsetMeters = 20; L.Maximum.OffsetMeters = 20; Cap.NoiseLayers.Add(L);
    TestEqual(TEXT("Generator cannot render beyond radius"), FCLPlanetCompiler::Compile(Cap, Error)->SampleHeightMeters(FVector::ZAxisVector), 0.0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLStrataPresetTest, "Catalyst.Landform.Strata.Presets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLStrataPresetTest::RunTest(const FString&)
{
    FCLStrataLibrary Library; FCLStrata Edited = FCLStrataPresets::Defaults()[0];
    Edited.Stratum[0].NormalizedHeight = .42; Library.Sets.Add(Edited);
    FCLStrata Collision; Collision.Id = FGuid::NewGuid(); Collision.Name = TEXT("Oceanic"); Library.Sets.Add(Collision);
    TestTrue(TEXT("First catalog upgrade seeds presets"), FCLStrataPresets::Seed(Library));
    TestEqual(TEXT("User-edited built-in is preserved"), Library.Sets[0].Stratum[0].NormalizedHeight, .42);
    TestEqual(TEXT("User-named style is preserved"), Library.Sets[1].Name, FString(TEXT("Oceanic")));
    TestEqual(TEXT("Existing style plus custom style and four new presets"), Library.Sets.Num(), 6);
    TSet<FGuid> Ids; TSet<FString> Names;
    for (const auto& Preset : FCLStrataPresets::Defaults())
    {
        FString Error; TestTrue(TEXT("Preset guides validate"), Preset.Validate(Error));
        TestFalse(TEXT("Preset identities are unique"), Ids.Contains(Preset.Id)); Ids.Add(Preset.Id);
        TestFalse(TEXT("Preset names are unique"), Names.Contains(Preset.Name.ToLower())); Names.Add(Preset.Name.ToLower());
        auto R = FCLPlanetCompiler::RollingPlains(); R.Strata = Preset;
        TestTrue(TEXT("Every preset supports base SurfaceMin/SurfaceMax layers"), FCLPlanetCompiler::Compile(R, Error).IsValid());
        if (Preset.Name == TEXT("Airless rocky"))
            TestFalse(TEXT("Airless preset invents no ocean or atmosphere guides"),
                Preset.Stratum.ContainsByPredicate([](const FCLStratum& G) { return G.Name == TEXT("OceanHeight") || G.Name == TEXT("AtmosphereMax"); }));
    }
    Library.Sets.RemoveAt(2);
    const int32 Remaining = Library.Sets.Num();
    TestFalse(TEXT("Presets are not re-seeded after a user deletion"), FCLStrataPresets::Seed(Library));
    TestEqual(TEXT("Deleted preset stays deleted"), Library.Sets.Num(), Remaining);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLStrataGestureTest, "Catalyst.Landform.Designer.DeferredGesture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLStrataGestureTest::RunTest(const FString&)
{
    auto* Asset = NewObject<UCLPlanetRecipeAsset>(GetTransientPackage(), NAME_None, RF_Transactional);
    auto* Catalog = NewObject<UCLStrataLibraryAsset>(GetTransientPackage(), NAME_None, RF_Transactional);
    Catalog->Library.Sets.Add(Asset->Recipe.Strata);
    FCLStrataSession Session; Session.Asset = Asset; Session.Library.Reset(Catalog);
    int32 GuideFeedback = 0, SceneChanges = 0, MeshBuilds = 0;
    Session.OnPreviewChanged = [&] { ++GuideFeedback; };
    const auto Handle = Asset->OnRecipeChanged.AddLambda([&]
    {
        ++SceneChanges; FString Error; auto Plan = FCLPlanetCompiler::Compile(Asset->Recipe, Error);
        if (Plan) { FCLSectionGeometry::Build(*Plan, 0); ++MeshBuilds; }
    });
    Session.Begin(NSLOCTEXT("CL", "DeferredDragTest", "Move Stratum"));
    const double Start = FPlatformTime::Seconds();
    for (int32 I = 1; I <= 200; ++I) Session.Height(0, .4 + I * .0005);
    const double DragMs = (FPlatformTime::Seconds() - Start) * 1000;
    TestEqual(TEXT("All 200 mouse positions update the guide immediately"), GuideFeedback, 200);
    TestEqual(TEXT("No recipe scene notifications during drag"), SceneChanges, 0);
    TestEqual(TEXT("No section mesh builds during drag"), MeshBuilds, 0);
    Session.End();
    TestEqual(TEXT("Release commits exactly one scene change"), SceneChanges, 1);
    TestEqual(TEXT("Release produces one section build"), MeshBuilds, 1);
    TestEqual(TEXT("Final height is committed"), Asset->Recipe.Strata.Stratum[0].NormalizedHeight, .5);
    GEditor->UndoTransaction();
    TestEqual(TEXT("One undo restores all 200 positions on planet"), Asset->Recipe.Strata.Stratum[0].NormalizedHeight, .4);
    TestEqual(TEXT("One undo restores catalog"), Catalog->Library.Sets[0].Stratum[0].NormalizedHeight, .4);
    const int32 BeforeCancel = SceneChanges;
    Session.Begin(NSLOCTEXT("CL", "CancelDeferredDragTest", "Move Stratum"));
    Session.Height(0, .7); Session.End(true);
    TestEqual(TEXT("Cancel restores guide without a terrain rebuild"), Asset->Recipe.Strata.Stratum[0].NormalizedHeight, .4);
    TestEqual(TEXT("Cancel sends no scene change"), SceneChanges, BeforeCancel);
    Session.Begin(NSLOCTEXT("CL", "NoOpDeferredDragTest", "Move Stratum"));
    Session.Height(0, .4); Session.End();
    TestEqual(TEXT("No-op gesture sends no scene change"), SceneChanges, BeforeCancel);
    Asset->OnRecipeChanged.Remove(Handle);
    AddInfo(FString::Printf(TEXT("200 guide-only updates: %.3f ms; mesh rebuilds while dragging: 0; after release: 1"), DragMs));
    return true;
}
#endif

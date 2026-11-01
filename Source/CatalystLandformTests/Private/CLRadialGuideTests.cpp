#if WITH_DEV_AUTOMATION_TESTS
#include "Model/CLPlanetRecipe.h"
#include "CLRadialGuideEditing.h"
#include "CLSectionGeometry.h"
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "ScopedTransaction.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRadialGuidesTest, "Catalyst.Landform.RadialGuides.AuthoringAndResize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLRadialGuidesTest::RunTest(const FString&)
{
    FString Error, Json;
    FCLPlanetRecipe R = FCLPlanetCompiler::RollingPlains();
    R.RadiusMeters = 1000;
    auto Plan = FCLPlanetCompiler::Compile(R, Error);
    if (!TestTrue(TEXT("Normalized Radial Guides compiles"), Plan.IsValid())) return false;
    TestTrue(TEXT("SurfaceMin is at 300m from centre"), FMath::IsNearlyEqual(Plan->RadiusMeters + Plan->Guides[1].HeightMeters, 300.0));
    TestTrue(TEXT("SurfaceMax is at 600m from centre"), FMath::IsNearlyEqual(Plan->RadiusMeters + Plan->Guides[2].HeightMeters, 600.0));
    TestTrue(TEXT("Ocean is at 400m from centre"), FMath::IsNearlyEqual(Plan->RadiusMeters + Plan->Guides[0].HeightMeters, 400.0));
    R.RadiusMeters *= 2;
    auto Larger = FCLPlanetCompiler::Compile(R, Error);
    TestEqual(TEXT("Resizing preserves normalized Radial Guide values"), R.RadialGuides.Guides[1].NormalizedHeight, .3);
    TestTrue(TEXT("Resolved guide scales with radius"), FMath::IsNearlyEqual(Larger->RadiusMeters + Larger->Guides[1].HeightMeters, 600.0));
    R.RadialGuides.Guides[0].NormalizedHeight = 0;
    TestTrue(TEXT("Optional centre guide is valid"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    R.RadialGuides.Guides[0].NormalizedHeight = 1.01;
    TestFalse(TEXT("Radial Guide cannot exceed outer sky boundary"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    R.RadialGuides.Guides[0].NormalizedHeight = -.01;
    TestFalse(TEXT("Radial Guide cannot be below centre"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    R = FCLPlanetCompiler::RollingPlains();
    TestTrue(TEXT("Rename succeeds"), FCLRadialGuideEditing::Rename(R, 1, TEXT("Lowlands"), Error));
    TestEqual(TEXT("Rename preserves generation references"), R.NoiseLayers[0].Minimum.Guide, FName(TEXT("Lowlands")));
    TestTrue(TEXT("Renamed recipe remains valid"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    TestFalse(TEXT("Duplicate names are rejected"), FCLRadialGuideEditing::Rename(R, 1, TEXT("surfaceMAX"), Error));
    TestFalse(TEXT("Empty names are rejected"), FCLRadialGuideEditing::Rename(R, 1, TEXT("  "), Error));
    const int32 Added = FCLRadialGuideEditing::Add(R.RadialGuides, .75);
    TestEqual(TEXT("New Radial Guide stores the dragged normalized radius"), R.RadialGuides.Guides[Added].NormalizedHeight, .75);
    TestTrue(TEXT("Deleting an unused Radial Guide succeeds"), FCLRadialGuideEditing::Remove(R.RadialGuides, Added));
    TestTrue(TEXT("Remaining recipe compiles"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    FCLRadialGuideEditing::Remove(R.RadialGuides, 1);
    TestFalse(TEXT("Deleting referenced Radial Guide diagnoses dependency"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    TestTrue(TEXT("Dependency diagnostic names deleted Radial Guide"), Error.Contains(TEXT("Lowlands")));

    auto* Asset = NewObject<UCLPlanetRecipeAsset>(GetTransientPackage(), NAME_None, RF_Transactional);
    auto* Library = NewObject<UCLRadialGuideLibraryAsset>(GetTransientPackage(), NAME_None, RF_Transactional);
    Library->Library.Sets.Add(Asset->Recipe.RadialGuides);
    {
        FScopedTransaction Tx(NSLOCTEXT("CL", "Radial GuidesUndoTest", "Move Radial Guide"));
        Asset->Modify(); Library->Modify();
        Asset->Recipe.RadialGuides.Guides[0].NormalizedHeight = .5;
        Asset->Recipe.RadialGuides.Guides[0].NormalizedHeight = .55;
        Library->Library.Sets[0] = Asset->Recipe.RadialGuides;
        Asset->PostEditChange();
    }
    GEditor->UndoTransaction();
    TestEqual(TEXT("One undo restores complete drag on planet"), Asset->Recipe.RadialGuides.Guides[0].NormalizedHeight, .4);
    TestEqual(TEXT("One undo restores catalog too"), Library->Library.Sets[0].Guides[0].NormalizedHeight, .4);
    Asset->Recipe.RadialGuides.Guides[0].NormalizedHeight = .45;
    TestEqual(TEXT("Applied planet snapshot does not mutate another template"), Library->Library.Sets[0].Guides[0].NormalizedHeight, .4);
    TestTrue(TEXT("Schema 2 exports"), Asset->ExportJsonString(Json, Error));
    TestFalse(TEXT("Export omits obsolete metre guide array"), Json.Contains(TEXT("heightGuides")));
    auto* Copy = NewObject<UCLPlanetRecipeAsset>();
    TestTrue(TEXT("Schema 2 roundtrips"), Copy->ImportRecipeJson(Json, Error));
    TestEqual(TEXT("Radial Guides identity survives JSON"), Copy->Recipe.RadialGuides.Id, Asset->Recipe.RadialGuides.Id);
    TestEqual(TEXT("Normalized value survives JSON"), Copy->Recipe.RadialGuides.Guides[0].NormalizedHeight, .45);

    const FString V1 = TEXT(R"({"schemaVersion":1,"algorithmVersion":1,"referenceRadiusMeters":100,
        "heightGuides":[{"name":"Surface","heightMeters":7},{"name":"Sky","heightMeters":20}],
        "noiseLayers":[{"name":"Constant","minimum":{"guide":"Surface"},"maximum":{"guide":"Surface"},"wavelengthMeters":50}]})");
    TestTrue(TEXT("Old metre-based JSON migrates explicitly"), Copy->ImportRecipeJson(V1, Error));
    auto Migrated = FCLPlanetCompiler::Compile(Copy->Recipe, Error);
    if (!TestTrue(TEXT("Migrated document compiles"), Migrated.IsValid())) return false;
    TestTrue(TEXT("Migration preserves physical terrain height"), FMath::IsNearlyEqual(Migrated->RadiusMeters + Migrated->SampleHeightMeters(FVector::ZAxisVector), 107.0));
    TestTrue(TEXT("Migration encloses old sky within new radius"), Copy->Recipe.RadialGuides.Guides[1].NormalizedHeight <= 1);
    TestTrue(TEXT("Migration preserves noise coordinates"), FMath::IsNearlyEqual(Copy->Recipe.NoiseLayers[0].WavelengthMeters, 50 * Copy->Recipe.RadiusMeters / 100));

    FCLPlanetRecipe Cap; FCLNoiseLayer L; L.Name = TEXT("Beyond sky"); L.Minimum.OffsetMeters = 20; L.Maximum.OffsetMeters = 20; Cap.NoiseLayers.Add(L);
    TestEqual(TEXT("Generator cannot render beyond radius"), FCLPlanetCompiler::Compile(Cap, Error)->SampleHeightMeters(FVector::ZAxisVector), 0.0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRadialGuidesPresetTest, "Catalyst.Landform.RadialGuides.Presets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLRadialGuidesPresetTest::RunTest(const FString&)
{
    FCLRadialGuideLibrary Library; FCLRadialGuideSet Edited = FCLRadialGuidePresets::Defaults()[0];
    Edited.Guides[0].NormalizedHeight = .42; Library.Sets.Add(Edited);
    FCLRadialGuideSet Collision; Collision.Id = FGuid::NewGuid(); Collision.Name = TEXT("Oceanic"); Library.Sets.Add(Collision);
    TestTrue(TEXT("First catalog upgrade seeds presets"), FCLRadialGuidePresets::Seed(Library));
    TestEqual(TEXT("User-edited built-in is preserved"), Library.Sets[0].Guides[0].NormalizedHeight, .42);
    TestEqual(TEXT("User-named style is preserved"), Library.Sets[1].Name, FString(TEXT("Oceanic")));
    TestEqual(TEXT("Existing style plus custom style and five new presets"), Library.Sets.Num(), 7);
    TSet<FGuid> Ids; TSet<FString> Names;
    for (const auto& Preset : FCLRadialGuidePresets::Defaults())
    {
        FString Error; TestTrue(TEXT("Preset guides validate"), Preset.Validate(Error));
        TestFalse(TEXT("Preset identities are unique"), Ids.Contains(Preset.Id)); Ids.Add(Preset.Id);
        TestFalse(TEXT("Preset names are unique"), Names.Contains(Preset.Name.ToLower())); Names.Add(Preset.Name.ToLower());
        auto R = FCLPlanetCompiler::RollingPlains(); R.RadialGuides = Preset;
        TestTrue(TEXT("Every preset supports base SurfaceMin/SurfaceMax layers"), FCLPlanetCompiler::Compile(R, Error).IsValid());
        if (Preset.Name == TEXT("Airless rocky"))
            TestFalse(TEXT("Airless preset invents no ocean or atmosphere guides"),
                Preset.Guides.ContainsByPredicate([](const FCLRadialGuide& G) { return G.Name == TEXT("OceanHeight") || G.Name == TEXT("AtmosphereMax"); }));
    }
    Library.Sets.RemoveAt(2);
    const int32 Remaining = Library.Sets.Num();
    TestFalse(TEXT("Presets are not re-seeded after a user deletion"), FCLRadialGuidePresets::Seed(Library));
    TestEqual(TEXT("Deleted preset stays deleted"), Library.Sets.Num(), Remaining);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRadialGuidesGestureTest, "Catalyst.Landform.Designer.DeferredGesture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLRadialGuidesGestureTest::RunTest(const FString&)
{
    auto* Asset = NewObject<UCLPlanetRecipeAsset>(GetTransientPackage(), NAME_None, RF_Transactional);
    auto* Catalog = NewObject<UCLRadialGuideLibraryAsset>(GetTransientPackage(), NAME_None, RF_Transactional);
    Catalog->Library.Sets.Add(Asset->Recipe.RadialGuides);
    FCLRadialGuideSession Session; Session.Asset = Asset; Session.Library.Reset(Catalog);
    int32 GuideFeedback = 0, SceneChanges = 0, MeshBuilds = 0;
    Session.OnPreviewChanged = [&] { ++GuideFeedback; };
    const auto Handle = Asset->OnRecipeChanged.AddLambda([&]
    {
        ++SceneChanges; FString Error; auto Plan = FCLPlanetCompiler::Compile(Asset->Recipe, Error);
        if (Plan) { FCLSectionGeometry::Build(*Plan, 0); ++MeshBuilds; }
    });
    Session.Begin(NSLOCTEXT("CL", "DeferredDragTest", "Move Radial Guide"));
    const double Start = FPlatformTime::Seconds();
    for (int32 I = 1; I <= 200; ++I) Session.Height(0, .4 + I * .0005);
    const double DragMs = (FPlatformTime::Seconds() - Start) * 1000;
    TestEqual(TEXT("All 200 mouse positions update the guide immediately"), GuideFeedback, 200);
    TestEqual(TEXT("No recipe scene notifications during drag"), SceneChanges, 0);
    TestEqual(TEXT("No section mesh builds during drag"), MeshBuilds, 0);
    Session.End();
    TestEqual(TEXT("Release commits exactly one scene change"), SceneChanges, 1);
    TestEqual(TEXT("Release produces one section build"), MeshBuilds, 1);
    TestEqual(TEXT("Final height is committed"), Asset->Recipe.RadialGuides.Guides[0].NormalizedHeight, .5);
    GEditor->UndoTransaction();
    TestEqual(TEXT("One undo restores all 200 positions on planet"), Asset->Recipe.RadialGuides.Guides[0].NormalizedHeight, .4);
    TestEqual(TEXT("One undo restores catalog"), Catalog->Library.Sets[0].Guides[0].NormalizedHeight, .4);
    const int32 BeforeCancel = SceneChanges;
    Session.Begin(NSLOCTEXT("CL", "CancelDeferredDragTest", "Move Radial Guide"));
    Session.Height(0, .7); Session.End(true);
    TestEqual(TEXT("Cancel restores guide without a terrain rebuild"), Asset->Recipe.RadialGuides.Guides[0].NormalizedHeight, .4);
    TestEqual(TEXT("Cancel sends no scene change"), SceneChanges, BeforeCancel);
    Session.Begin(NSLOCTEXT("CL", "NoOpDeferredDragTest", "Move Radial Guide"));
    Session.Height(0, .4); Session.End();
    TestEqual(TEXT("No-op gesture sends no scene change"), SceneChanges, BeforeCancel);
    Asset->OnRecipeChanged.Remove(Handle);
    AddInfo(FString::Printf(TEXT("200 guide-only updates: %.3f ms; mesh rebuilds while dragging: 0; after release: 1"), DragMs));
    return true;
}
#endif

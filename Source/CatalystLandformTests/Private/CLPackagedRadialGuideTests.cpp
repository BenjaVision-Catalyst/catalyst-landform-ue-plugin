#if WITH_DEV_AUTOMATION_TESTS
#include "CLRadialGuideEditing.h"
#include "CLRecipeAssetTools.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPackagedGuidesTest, "Catalyst.Landform.RadialGuides.PackagedExamples",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLPackagedGuidesTest::RunTest(const FString&)
{
    auto* Library = UCLRadialGuideLibraryAsset::LoadExamples();
    if (!TestNotNull(TEXT("Plugin contains the example guide library"), Library)) return false;
    TestTrue(TEXT("Guide library belongs to the plugin"), Library->IsPackaged());
    TestEqual(TEXT("The example sets and authored 100m variant ship"), Library->Library.Sets.Num(), 6);
    TSet<FGuid> Ids;
    for (const auto& Set : Library->Library.Sets)
    {
        FString Error;
        TestTrue(TEXT("Packaged set validates"), Set.Validate(Error));
        TestTrue(TEXT("Each set has a persistent unique ID"), Set.Id.IsValid() && !Ids.Contains(Set.Id)); Ids.Add(Set.Id);
    }
    TestEqual(TEXT("100m OceanHeight exact radius"), Library->Library.Sets[0].Guides[0].NormalizedHeight, 0.8104173117176184);
    TestEqual(TEXT("100m SurfaceMin exact radius"), Library->Library.Sets[0].Guides[1].NormalizedHeight, 0.6816822806729749);
    TestEqual(TEXT("100m SurfaceMax exact radius"), Library->Library.Sets[0].Guides[2].NormalizedHeight, 0.836470820439868);
    TestEqual(TEXT("100m AtmosphereMax exact radius"), Library->Library.Sets[0].Guides[3].NormalizedHeight, 0.9);
    TestEqual(TEXT("100m CavesMin exact radius"), Library->Library.Sets[0].Guides[4].NormalizedHeight, 0.34084114033648744);
    TestEqual(TEXT("100m CavesMax exact radius"), Library->Library.Sets[0].Guides[5].NormalizedHeight, 0.8197414040310707);
    TestEqual(TEXT("1km OceanHeight exact radius"), Library->Library.Sets[1].Guides[0].NormalizedHeight, 0.745);
    TestEqual(TEXT("1km SurfaceMin exact radius"), Library->Library.Sets[1].Guides[1].NormalizedHeight, 0.74);
    TestEqual(TEXT("1km SurfaceMax exact radius"), Library->Library.Sets[1].Guides[2].NormalizedHeight, 0.76);
    TestEqual(TEXT("1km AtmosphereMax exact radius"), Library->Library.Sets[1].Guides[3].NormalizedHeight, 0.9);
    TestEqual(TEXT("1km CavesMin exact radius"), Library->Library.Sets[1].Guides[4].NormalizedHeight, 0.37);
    TestEqual(TEXT("1km CavesMax exact radius"), Library->Library.Sets[1].Guides[5].NormalizedHeight, 0.755);
    TestEqual(TEXT("10km OceanHeight exact radius"), Library->Library.Sets[2].Guides[0].NormalizedHeight, 0.745);
    TestEqual(TEXT("10km SurfaceMin exact radius"), Library->Library.Sets[2].Guides[1].NormalizedHeight, 0.74);
    TestEqual(TEXT("10km SurfaceMax exact radius"), Library->Library.Sets[2].Guides[2].NormalizedHeight, 0.76);
    TestEqual(TEXT("10km AtmosphereMax exact radius"), Library->Library.Sets[2].Guides[3].NormalizedHeight, 0.9);
    TestEqual(TEXT("10km CavesMin exact radius"), Library->Library.Sets[2].Guides[4].NormalizedHeight, 0.37);
    TestEqual(TEXT("10km CavesMax exact radius"), Library->Library.Sets[2].Guides[5].NormalizedHeight, 0.7595000000000001);
    TestEqual(TEXT("Mars - measured MOLA + Jezero SurfaceMin exact radius"), Library->Library.Sets[3].Guides[0].NormalizedHeight, 0.9847585401459854);
    TestEqual(TEXT("Mars - measured MOLA + Jezero SurfaceMax exact radius"), Library->Library.Sets[3].Guides[1].NormalizedHeight, 0.9977497810218978);
    TestEqual(TEXT("Mars - measured MOLA + Jezero ReferenceFigure exact radius"), Library->Library.Sets[3].Guides[2].NormalizedHeight, 0.9915328467153285);
    TestEqual(TEXT("Mars - measured MOLA + Jezero AtmosphereMax exact radius"), Library->Library.Sets[3].Guides[3].NormalizedHeight, 1.0);
    TestEqual(TEXT("Mars - measured MOLA + Jezero CavesMin exact radius"), Library->Library.Sets[3].Guides[4].NormalizedHeight, 0.4923792700729927);
    TestEqual(TEXT("Mars - measured MOLA + Jezero CavesMax exact radius"), Library->Library.Sets[3].Guides[5].NormalizedHeight, 0.9977483211678833);
    TestEqual(TEXT("asteroid OceanHeight exact radius"), Library->Library.Sets[4].Guides[0].NormalizedHeight, 0.745);
    TestEqual(TEXT("asteroid SurfaceMin exact radius"), Library->Library.Sets[4].Guides[1].NormalizedHeight, 0.62);
    TestEqual(TEXT("asteroid SurfaceMax exact radius"), Library->Library.Sets[4].Guides[2].NormalizedHeight, 0.88);
    TestEqual(TEXT("asteroid AtmosphereMax exact radius"), Library->Library.Sets[4].Guides[3].NormalizedHeight, 0.9);
    TestEqual(TEXT("asteroid CavesMin exact radius"), Library->Library.Sets[4].Guides[4].NormalizedHeight, 0.31);
    TestEqual(TEXT("asteroid CavesMax exact radius"), Library->Library.Sets[4].Guides[5].NormalizedHeight, 0.8624);
    TestEqual(TEXT("100m original OceanHeight exact radius"), Library->Library.Sets[5].Guides[0].NormalizedHeight, 0.745);
    TestEqual(TEXT("100m original SurfaceMin exact radius"), Library->Library.Sets[5].Guides[1].NormalizedHeight, 0.74);
    TestEqual(TEXT("100m original SurfaceMax exact radius"), Library->Library.Sets[5].Guides[2].NormalizedHeight, 0.76);
    TestEqual(TEXT("100m original AtmosphereMax exact radius"), Library->Library.Sets[5].Guides[3].NormalizedHeight, 0.9);
    TestEqual(TEXT("100m original CavesMin exact radius"), Library->Library.Sets[5].Guides[4].NormalizedHeight, 0.37);
    TestEqual(TEXT("100m original CavesMax exact radius"), Library->Library.Sets[5].Guides[5].NormalizedHeight, 0.7448);
    const auto Original = Library->Library;
    const bool WasDirty = Library->GetOutermost()->IsDirty();
    UCLPlanetRecipeFactory* Factory = NewObject<UCLPlanetRecipeFactory>();
    TStrongObjectPtr<UCLPlanetRecipeAsset> Planet(CastChecked<UCLPlanetRecipeAsset>(Factory->FactoryCreateNew(
        UCLPlanetRecipeAsset::StaticClass(), GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional, nullptr, GWarn)));
    TestEqual(TEXT("New planet references the packaged guides"), Planet->RadialGuideLibrary.Get(), Library);
    TestEqual(TEXT("New 1km planet uses its example set"), Planet->Recipe.RadialGuides.Name, FString(TEXT("1km guides")));
    FCLRadialGuideSession Session;
    Session.Open(Planet.Get()); Session.Apply(Library->Library.Sets[0].Id);
    TestEqual(TEXT("Selecting a packaged style needs no local catalog"), Session.Library.Get(), Library);
    const auto Before = Planet->Recipe;
    Session.Begin(NSLOCTEXT("CL", "CancelPackagedGuideTest", "Move guide"));
    Session.Height(0, .7);
    TestFalse(TEXT("Editing creates a project copy"), Session.Library->IsPackaged());
    TestTrue(TEXT("Project copy contains every example"), Session.Library->Library.Sets.Num() == 6);
    Session.End(true);
    TestEqual(TEXT("Cancel restores the packaged catalog reference"), Planet->RadialGuideLibrary.Get(), Library);
    TestTrue(TEXT("Cancel preserves all authoring values"), FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Before, &Planet->Recipe, 0));
    TestEqual(TEXT("Cancel restores the menu catalog"), Session.Library.Get(), Library);
    Session.Begin(NSLOCTEXT("CL", "EditPackagedGuideTest", "Move guide"));
    Session.Height(0, .7); Session.End();
    TestFalse(TEXT("Committed edit references a local copy"), Planet->RadialGuideLibrary->IsPackaged());
    TestEqual(TEXT("Edited radius is used by the planet"), Planet->Recipe.RadialGuides.Guides[0].NormalizedHeight, .7);
    GEditor->UndoTransaction();
    TestEqual(TEXT("Undo restores the original catalog reference"), Planet->RadialGuideLibrary.Get(), Library);
    TestTrue(TEXT("Undo restores all guide values"), FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Before, &Planet->Recipe, 0));
    TestTrue(TEXT("Editing leaves the packaged library unchanged"), FCLRadialGuideLibrary::StaticStruct()->CompareScriptStruct(&Original, &Library->Library, 0));
    TestEqual(TEXT("Opening and editing never dirty the packaged library"), Library->GetOutermost()->IsDirty(), WasDirty);
    // No test assets are saved. Release the unused local catalog after Undo.
    Session.Library->GetOutermost()->SetDirtyFlag(false);
    return !HasAnyErrors();
}
#endif

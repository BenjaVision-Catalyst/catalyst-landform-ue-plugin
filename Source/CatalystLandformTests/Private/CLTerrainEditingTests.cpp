#if WITH_DEV_AUTOMATION_TESTS
#include "CLTerrainEditing.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "Interfaces/IPluginManager.h"
#include "UObject/StrongObjectPtr.h"
#include "Editor.h"

namespace
{
bool MountainSource(UCLTerrainTypeAsset* Asset, FString& Error)
{
    FString Json;
    const auto Path = IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir() / TEXT("Examples/TerrainTypes/MountainRidges.json");
    return FFileHelper::LoadFileToString(Json, *Path) && Asset->ImportTerrainTypeJson(Json, Error);
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTerrainLayerAuthoringTest, "Catalyst.Landform.TerrainTypes.LayerAuthoring",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLTerrainLayerAuthoringTest::RunTest(const FString&)
{
    TStrongObjectPtr<UCLTerrainTypeAsset> Asset(NewObject<UCLTerrainTypeAsset>(GetTransientPackage(), NAME_None, RF_Transactional));
    FString Error;
    if (!TestTrue(TEXT("Mountain ridges loads"), MountainSource(Asset.Get(), Error))) return false;
    auto Context = FCLPlanetCompiler::RollingPlains(); Context.RadiusMeters = 1000; Context.Seed = 921;
    FCLTerrainEditing Edit; Edit.Open(Asset.Get(), &Context);
    const auto Handle = Asset->OnTerrainChanged.AddLambda([&] { Edit.Reconcile(); });
    ON_SCOPE_EXIT { Asset->OnTerrainChanged.Remove(Handle); };
    const double Original = Asset->TerrainType.NoiseLayers[0].Persistence;
    auto Layer = Asset->TerrainType.NoiseLayers[0]; Layer.Persistence = .72;
    TestTrue(TEXT("Selected layer can be edited"), Edit.SetLayer(Layer));
    TestEqual(TEXT("Edited settings belong to the terrain asset"), Asset->TerrainType.NoiseLayers[0].Persistence, .72);
    GEditor->UndoTransaction();
    TestEqual(TEXT("One undo restores the finished edit"), Asset->TerrainType.NoiseLayers[0].Persistence, Original);
    GEditor->RedoTransaction();
    TestEqual(TEXT("Redo restores the edit"), Asset->TerrainType.NoiseLayers[0].Persistence, .72);
    Layer.Persistence = 2;
    TestFalse(TEXT("Invalid draft is rejected"), Edit.SetLayer(Layer));
    TestEqual(TEXT("Rejected draft leaves the asset intact"), Asset->TerrainType.NoiseLayers[0].Persistence, .72);
    TestTrue(TEXT("Returning to the current value accepts the corrected draft"), Edit.SetLayer(Asset->TerrainType.NoiseLayers[0]));
    TestTrue(TEXT("Corrected draft clears its validation error"), Edit.Error.IsEmpty());
    TestTrue(TEXT("Add layer"), Edit.Add());
    const FName AddedName = Asset->TerrainType.NoiseLayers[2].Name;
    TestTrue(TEXT("Duplicate selected layer"), Edit.Duplicate());
    TestTrue(TEXT("Duplicate gets a distinct name"), Asset->TerrainType.NoiseLayers[3].Name != AddedName);
    Edit.SetSolo(Edit.Selected); const FName SoloName = Asset->TerrainType.NoiseLayers[Edit.Solo].Name;
    TestTrue(TEXT("Move selected layer up"), Edit.Move(-1));
    TestEqual(TEXT("Solo follows the moved layer"), Asset->TerrainType.NoiseLayers[Edit.Solo].Name, SoloName);
    GEditor->UndoTransaction();
    TestEqual(TEXT("Undo ordering retains solo identity"), Asset->TerrainType.NoiseLayers[Edit.Solo].Name, SoloName);
    Edit.Selected = 3;
    TestTrue(TEXT("Remove selected layer"), Edit.Remove());
    TestEqual(TEXT("Removal clears solo on that layer"), Edit.Solo, INDEX_NONE);
    GEditor->UndoTransaction();
    TestEqual(TEXT("Undo restores removed layer"), Asset->TerrainType.NoiseLayers.Num(), 4);
    TestTrue(TEXT("Enable switch persists"), Edit.Enable(0, false));
    TestFalse(TEXT("Layer disabled in source"), Asset->TerrainType.NoiseLayers[0].bEnabled);
    GEditor->UndoTransaction();
    TestTrue(TEXT("Undo restores enabled state"), Asset->TerrainType.NoiseLayers[0].bEnabled);
    Layer = Asset->TerrainType.NoiseLayers[0]; Layer.Name = Asset->TerrainType.NoiseLayers[1].Name; Edit.Selected = 0;
    TestFalse(TEXT("Duplicate layer names rejected atomically"), Edit.SetLayer(Layer));
    TestEqual(TEXT("Context radius untouched"), Context.RadiusMeters, 1000.0);
    TestEqual(TEXT("Context seed untouched"), Context.Seed, 921);
    TestEqual(TEXT("Context guide references untouched"), Context.Strata.Id, Edit.Context.Strata.Id);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTerrainAuthoringSnapshotTest, "Catalyst.Landform.TerrainTypes.AuthoringSnapshots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLTerrainAuthoringSnapshotTest::RunTest(const FString&)
{
    TStrongObjectPtr<UCLTerrainTypeAsset> Asset(NewObject<UCLTerrainTypeAsset>(GetTransientPackage(), NAME_None, RF_Transactional));
    FString Error, Before, After;
    if (!TestTrue(TEXT("Mountain ridges loads"), MountainSource(Asset.Get(), Error))) return false;
    FCLTerrainEditing Edit; Edit.Open(Asset.Get());
    const auto Full = Edit.CompilePreview(Error);
    Asset->ExportJsonString(Before, Error);
    const bool Dirty = Asset->GetOutermost()->IsDirty();
    Edit.SetSolo(1); const auto Solo = Edit.CompilePreview(Error);
    TestTrue(TEXT("Detail solo has a valid isolated preview"), Solo && Solo->Layers.Num() == 1);
    TestTrue(TEXT("Detail solo stays below the sky in planet context"), Solo && Solo->BaseHeightMeters < -1);
    Asset->ExportJsonString(After, Error);
    TestEqual(TEXT("Solo does not alter export"), After, Before);
    TestEqual(TEXT("Solo does not dirty the source package"), Asset->GetOutermost()->IsDirty(), Dirty);
    Edit.SetSolo(INDEX_NONE); const auto Again = Edit.CompilePreview(Error);
    TestTrue(TEXT("Full stack restored"), Again && Again->Layers.Num() == Full->Layers.Num());
    TStrongObjectPtr<UCLTerrainTypeAsset> Reloaded(NewObject<UCLTerrainTypeAsset>());
    TestTrue(TEXT("Authoring JSON reloads"), Reloaded->ImportTerrainTypeJson(After, Error));
    FCLTerrainEditing Reopened; Reopened.Open(Reloaded.Get(), &Edit.Context);
    TestEqual(TEXT("Solo is not restored from a saved definition"), Reopened.Solo, INDEX_NONE);
    const auto Recompiled = Reopened.CompilePreview(Error);
    for (int32 I = 0; I < 128; ++I)
    {
        const FVector Direction = FVector(FMath::Sin(I * .73), FMath::Cos(I * .41), FMath::Sin(I * .19)).GetSafeNormal();
        TestEqual(TEXT("Reopened definition reproduces terrain"), Recompiled->SampleHeightMeters(Direction), Full->SampleHeightMeters(Direction));
    }
    TStrongObjectPtr<UCLPlanetRecipeAsset> Planet(NewObject<UCLPlanetRecipeAsset>(GetTransientPackage(), NAME_None, RF_Transactional));
    Planet->Recipe = FCLPlanetCompiler::RollingPlains(); Planet->Recipe.RadiusMeters = 2500; Planet->Recipe.Seed = 182;
    const auto Strata = Planet->Recipe.Strata;
    Edit.SetSolo(1);
    TestTrue(TEXT("Explicit apply succeeds"), Edit.ApplyToPlanet(Planet.Get()));
    TestEqual(TEXT("Apply ignores solo and uses all authored layers"), Planet->Recipe.NoiseLayers.Num(), Asset->TerrainType.NoiseLayers.Num());
    TestEqual(TEXT("Apply preserves planet radius"), Planet->Recipe.RadiusMeters, 2500.0);
    TestEqual(TEXT("Apply preserves planet seed"), Planet->Recipe.Seed, 182);
    TestEqual(TEXT("Apply preserves global Strata identity"), Planet->Recipe.Strata.Id, Strata.Id);
    GEditor->UndoTransaction();
    TestEqual(TEXT("Applying terrain is undoable"), Planet->Recipe.TerrainTypeName, FString());
    Edit.Context.Strata.Stratum.Reset();
    TestFalse(TEXT("Missing guides produce a preview diagnostic"), Edit.CompilePreview(Error).IsValid());
    TestTrue(TEXT("Diagnostic identifies the missing guide"), Error.Contains(TEXT("Guide")));
    Asset->ExportJsonString(After, Error);
    TestEqual(TEXT("Invalid preview context cannot mutate terrain settings"), After, Before);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Model/CLPlanetRecipe.h"
#include "CLRadialGuideEditing.h"
#include "Misc/AutomationTest.h"
#include "Editor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGuideReferenceTest,"Catalyst.Landform.RadialGuides.SharedReference",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGuideReferenceTest::RunTest(const FString&)
{
    TStrongObjectPtr<UCLRadialGuideLibraryAsset> Library(NewObject<UCLRadialGuideLibraryAsset>(GetTransientPackage(),NAME_None,RF_Transactional));
    Library->Library.Sets=FCLRadialGuidePresets::Defaults();
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>(GetTransientPackage(),NAME_None,RF_Transactional)),B(NewObject<UCLPlanetRecipeAsset>(GetTransientPackage(),NAME_None,RF_Transactional));
    A->Recipe=FCLPlanetCompiler::BasePlanet();B->Recipe=FCLPlanetCompiler::BasePlanet();FString Error;
    TestTrue(TEXT("First planet references a real catalog entry"),A->SetRadialGuideSet(Library.Get(),Library->Library.Sets[0].Id,Error));
    TestTrue(TEXT("Second planet shares the same set"),B->SetRadialGuideSet(Library.Get(),A->RadialGuideSetId,Error));
    FCLRadialGuideSession S;S.Asset=A.Get();S.Library.Reset(Library.Get());int32 AChanges=0,BChanges=0;
    A->OnRecipeChanged.AddLambda([&]{++AChanges;});B->OnRecipeChanged.AddLambda([&]{++BChanges;});
    S.Begin(FText::FromString(TEXT("Resize shared guide")));for(int32 I=1;I<=200;++I)S.Height(0,.4+I*.0005);
    TestEqual(TEXT("No planet rebuilds during 200 drag updates"),AChanges+BChanges,0);
    TestEqual(TEXT("Other planet retains committed values during draft"),B->Recipe.RadialGuides.Guides[0].NormalizedHeight,.4);
    S.End();TestEqual(TEXT("One notification per referencing planet on commit"),AChanges+BChanges,2);
    TestEqual(TEXT("Shared values propagate"),B->Recipe.RadialGuides.Guides[0].NormalizedHeight,.5);
    GEditor->UndoTransaction();TestEqual(TEXT("Undo restores both references"),B->Recipe.RadialGuides.Guides[0].NormalizedHeight,.4);
    TestEqual(TEXT("Undo restores selected reference"),A->RadialGuideSetId,Library->Library.Sets[0].Id);
    GEditor->RedoTransaction();TestEqual(TEXT("Redo restores shared radius"),B->Recipe.RadialGuides.Guides[0].NormalizedHeight,.5);
    const int32 BeforeIndependent=BChanges;const auto SharedId=B->RadialGuideSetId;S.NewSet();TestTrue(TEXT("Duplicate gets a new referenced identity"),A->RadialGuideSetId!=SharedId);
    S.Begin(FText::FromString(TEXT("Edit independent set")));S.Height(0,.62);S.End();
    TestEqual(TEXT("Independent variant does not edit the shared starter"),B->Recipe.RadialGuides.Guides[0].NormalizedHeight,.5);
    TestEqual(TEXT("Unchanged sets do not rebuild other planets"),BChanges,BeforeIndependent);
    S.Begin(FText::FromString(TEXT("Cancel shared guide")));S.Height(0,.73);S.End(true);
    TestEqual(TEXT("Cancel restores variant values"),A->Recipe.RadialGuides.Guides[0].NormalizedHeight,.62);
    FString Json;TestTrue(TEXT("Referenced planet exports resolved recipe"),A->ExportJsonString(Json,Error));
    TestFalse(TEXT("Server recipe contains no Unreal catalog path"),Json.Contains(TEXT("radialGuideLibrary")));
    TestTrue(TEXT("Import consumes resolved recipe"),B->ImportRecipeJson(Json,Error));
    TestNull(TEXT("Import clears old authoring reference"),B->RadialGuideLibrary.Get());
    TestEqual(TEXT("Imported guide identity survives"),B->Recipe.RadialGuides.Id,A->RadialGuideSetId);
    Library->Library.Sets.RemoveAll([&](const auto& V){return V.Id==A->RadialGuideSetId;});Library->PostEditChange();
    TestFalse(TEXT("Missing set is diagnosed instead of silently changing style"),A->SynchronizeRadialGuides(Error));
    TStrongObjectPtr<UCLPlanetRecipeAsset> Legacy(NewObject<UCLPlanetRecipeAsset>(GetTransientPackage(),NAME_None,RF_Transactional));Legacy->Recipe=FCLPlanetCompiler::BasePlanet();
    FCLRadialGuideSession Old;Old.Asset=Legacy.Get();Old.Library.Reset(Library.Get());const auto OriginalId=Legacy->Recipe.RadialGuides.Id;const auto OriginalSet=*Library->FindSet(OriginalId);
    Old.Add(.8);TestTrue(TEXT("A conflicting legacy snapshot gets an independent set"),Legacy->RadialGuideSetId!=OriginalId);
    TestTrue(TEXT("Legacy edits preserve the catalog's existing values"),FCLRadialGuideSet::StaticStruct()->CompareScriptStruct(&OriginalSet,Library->FindSet(OriginalId),0));
    TStrongObjectPtr<UCLPlanetRecipeAsset> SameName(NewObject<UCLPlanetRecipeAsset>(GetTransientPackage(),NAME_None,RF_Transactional));SameName->Recipe=FCLPlanetCompiler::BasePlanet();
    FCLRadialGuideSession OtherOld;OtherOld.Asset=SameName.Get();OtherOld.Library.Reset(Library.Get());OtherOld.Add(.7);
    TestTrue(TEXT("Independent legacy forks keep catalog names unique"),SameName->Recipe.RadialGuides.Name!=Legacy->Recipe.RadialGuides.Name);
    GEditor->UndoTransaction();GEditor->UndoTransaction();TestNull(TEXT("Undo restores legacy detached ownership"),Legacy->RadialGuideLibrary.Get());
    A->OnRecipeChanged.Clear();B->OnRecipeChanged.Clear();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGuideReferenceRenameTest,"Catalyst.Landform.RadialGuides.SharedRename",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGuideReferenceRenameTest::RunTest(const FString&)
{
    TStrongObjectPtr<UCLRadialGuideLibraryAsset> Library(NewObject<UCLRadialGuideLibraryAsset>(GetTransientPackage(),NAME_None,RF_Transactional));Library->Library.Sets=FCLRadialGuidePresets::Defaults();
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>(GetTransientPackage(),NAME_None,RF_Transactional)),B(NewObject<UCLPlanetRecipeAsset>(GetTransientPackage(),NAME_None,RF_Transactional));
    FString Error;A->SetRadialGuideSet(Library.Get(),Library->Library.Sets[0].Id,Error);B->SetRadialGuideSet(Library.Get(),Library->Library.Sets[0].Id,Error);
    FCLRadialGuideSession S;S.Asset=A.Get();S.Library.Reset(Library.Get());S.Begin(FText::FromString(TEXT("Rename shared input")));
    FCLRadialGuideEditing::Rename(A->Recipe,1,TEXT("Lowlands"),Error);S.Changed();S.End();
    TestEqual(TEXT("Other planet bindings follow the renamed shared input"),B->Recipe.NoiseLayers[0].Minimum.Guide,FName(TEXT("Lowlands")));
    GEditor->UndoTransaction();TestEqual(TEXT("Undo restores binding on the other planet"),B->Recipe.NoiseLayers[0].Minimum.Guide,FName(TEXT("SurfaceMin")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGuideCatalogUpgradeTest,"Catalyst.Landform.RadialGuides.StarterUpgrade",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGuideCatalogUpgradeTest::RunTest(const FString&)
{
    FCLRadialGuideLibrary L;L.Sets=FCLRadialGuidePresets::Defaults();L.PresetRevision=1;L.Sets.SetNum(5);L.Sets.RemoveAt(1);L.Sets[0].Guides[0].NormalizedHeight=.44;
    FCLRadialGuidePresets::Seed(L);TestEqual(TEXT("Only the new radial layout is seeded"),L.Sets.Num(),5);
    TestEqual(TEXT("Existing edits preserved"),L.Sets[0].Guides[0].NormalizedHeight,.44);
    TestFalse(TEXT("Deleted older starter stays deleted"),L.Sets.ContainsByPredicate([](const auto& V){return V.Name==TEXT("Oceanic");}));
    TestFalse(TEXT("Upgrade is idempotent"),FCLRadialGuidePresets::Seed(L));return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "CLTerrainEditing.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "Interfaces/IPluginManager.h"
#include "UObject/StrongObjectPtr.h"
#include "Editor.h"
#include "ScopedTransaction.h"

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
    { const FScopedTransaction Tx(NSLOCTEXT("CL", "TestAssembleTerrain", "Assemble terrain onto planet"));
      TestTrue(TEXT("Recipe assembly applies the definition outside the Designer session"), Asset->ApplyToRecipe(Planet.Get(), Error)); }
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLStampIsolationTest, "Catalyst.Landform.TerrainTypes.StampIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLStampIsolationTest::RunTest(const FString&)
{
    TStrongObjectPtr<UCLTerrainTypeAsset> Asset(NewObject<UCLTerrainTypeAsset>());
    FString Error, Json, Before, After;
    const auto Path = IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir() / TEXT("Examples/TerrainTypes/MountainRidgesStamped.json");
    if (!FFileHelper::LoadFileToString(Json, *Path) || !Asset->ImportTerrainTypeJson(Json, Error)) { AddError(Error); return false; }
    const auto Original = Asset->TerrainType;
    auto Context = FCLPlanetCompiler::RollingPlains(); Context.RadiusMeters = 1000;
    FCLTerrainEditing Edit; Edit.Open(Asset.Get(), &Context);
    Asset->ExportJsonString(Before, Error); const bool Dirty = Asset->GetOutermost()->IsDirty();
    Edit.SetSolo(1);
    const auto Solo = Edit.CompilePreview(Error);
    if (!TestTrue(TEXT("Stamp Solo compiles an isolated flat input"), Solo && Solo->Layers.Num() == 1)) { AddError(Error); return false; }
    TestTrue(TEXT("Solo base is halfway between SurfaceMin and SurfaceMax"), FMath::IsNearlyEqual(Solo->BaseHeightMeters, -550., 1.e-6));
    Asset->ExportJsonString(After, Error);
    TestEqual(TEXT("Solo preserves the authored definition"), After, Before);
    TestEqual(TEXT("Solo does not change package dirtiness"), Asset->GetOutermost()->IsDirty(), Dirty);
    TestEqual(TEXT("Preview does not alter context base"), Edit.Context.BaseSurfaceNormalized, 1.);

    // Reproduce the user's enable switches, rather than relying on Solo mode.
    Edit.SetSolo(INDEX_NONE);
    Asset->TerrainType.NoiseLayers[0].bEnabled = false;
    Asset->TerrainType.NoiseLayers[2].bEnabled = false;
    Asset->ExportJsonString(Before, Error);
    const auto Stamps = Edit.CompilePreview(Error);
    if (!TestTrue(TEXT("Stamps alone compile"), Stamps.IsValid())) { AddError(Error); return false; }
    TestTrue(TEXT("Disabled upstream noise uses the same midpoint as Solo"), FMath::IsNearlyEqual(Stamps->BaseHeightMeters, Solo->BaseHeightMeters, 1.e-9));
    TArray<FCLResolvedStampPlacement> Placements; Stamps->GetStampPlacements(Placements);
    TestTrue(TEXT("Flat surface still produces deterministic PCG placements"), !Placements.IsEmpty());
    double Relief = 0; int32 FlatSamples = 0;
    for (const auto& P : Placements) Relief = FMath::Max(Relief, Stamps->SampleHeightMeters(P.Anchor) - Stamps->BaseHeightMeters);
    TestTrue(TEXT("Raised mountain stamps remain visible below sky clipping"), Relief > 5 && Stamps->BaseHeightMeters + Relief < 0);
    for (int32 I = 0; I < 512; ++I)
    {
        const double Z = 1 - 2 * (I + .5) / 512, A = I * 2.399963229728653;
        const FVector D(FMath::Sqrt(1-Z*Z)*FMath::Cos(A), FMath::Sqrt(1-Z*Z)*FMath::Sin(A), Z);
        int32 Count; double Weight; Stamps->GetStampCoverage(D, Count, Weight);
        if (!Count) { ++FlatSamples; TestEqual(TEXT("Unstamped input is exactly flat"), Stamps->SampleHeightMeters(D), Stamps->BaseHeightMeters); }
    }
    TestTrue(TEXT("Flat areas between stamps are sampled"), FlatSamples > 0);
    TStrongObjectPtr<UCLPlanetRecipeAsset> Planet(NewObject<UCLPlanetRecipeAsset>(GetTransientPackage(), NAME_None, RF_Transactional)); Planet->Recipe = Context;
    { const FScopedTransaction Tx(NSLOCTEXT("CL", "TestAssembleStamps", "Assemble stamps onto planet"));
      if (!TestTrue(TEXT("Recipe assembly applies a stamp-only definition"), Asset->ApplyToRecipe(Planet.Get(), Error))) { AddError(Error); return false; } }
    TestTrue(TEXT("Applied recipe stores the resolved midpoint"), FMath::IsNearlyEqual(Planet->Recipe.BaseSurfaceNormalized, .45, 1.e-9));
    const auto Applied = FCLPlanetCompiler::Compile(Planet->Recipe, Error);
    for (const auto& P : Placements) TestEqual(TEXT("Planet and Designer share the stamp-only field"), Applied->SampleHeightMeters(P.Anchor), Stamps->SampleHeightMeters(P.Anchor));
    Asset->ExportJsonString(After, Error); TestEqual(TEXT("Compile/apply preserve authored enable switches and settings"), After, Before);

    auto Explicit = Context; Explicit.BaseSurfaceNormalized = .52;
    TestTrue(TEXT("Explicit terrain base is accepted"), Asset->TerrainType.ApplyTo(Explicit, Error));
    TestEqual(TEXT("An explicit non-default base is preserved"), Explicit.BaseSurfaceNormalized, .52);
    auto Resized = Context; Resized.RadiusMeters = 2000;
    TestTrue(TEXT("Stamp-only stack scales with planet radius"), Asset->TerrainType.ApplyTo(Resized, Error));
    const auto Large = FCLPlanetCompiler::Compile(Resized, Error);
    TestTrue(TEXT("Midpoint height scales with radius"), FMath::IsNearlyEqual(Large->BaseHeightMeters, Stamps->BaseHeightMeters * 2, 1.e-6));
    Asset->TerrainType.NoiseLayers[1].bEnabled = false;
    const auto Empty = Edit.CompilePreview(Error);
    TestTrue(TEXT("All-disabled stack is a flat landscape at the guide midpoint"), Empty && Empty->Layers.IsEmpty() && FMath::IsNearlyEqual(Empty->BaseHeightMeters, -550., 1.e-6));
    Asset->TerrainType = Original; Edit.SetSolo(INDEX_NONE);
    const auto Full = Edit.CompilePreview(Error);
    auto Reference = Context; Original.ApplyTo(Reference, Error); const auto Expected = FCLPlanetCompiler::Compile(Reference, Error);
    TestEqual(TEXT("Leaving isolation restores the full authored stack"), Full->SampleHeightMeters(FVector::ZAxisVector), Expected->SampleHeightMeters(FVector::ZAxisVector));
    AddInfo(FString::Printf(TEXT("Stamp isolation: %d placements; %.6f m visible relief; %d flat samples"), Placements.Num(), Relief, FlatSamples));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLMixedLayerIsolationTest, "Catalyst.Landform.TerrainTypes.MixedLayerIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLMixedLayerIsolationTest::RunTest(const FString&)
{
    FString Json, Error;
    const auto Path=IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/TerrainTypes/MountainStructures.json");
    TStrongObjectPtr<UCLTerrainTypeAsset> Asset(NewObject<UCLTerrainTypeAsset>(GetTransientPackage(),NAME_None,RF_Transactional));
    if(!FFileHelper::LoadFileToString(Json,*Path)||!Asset->ImportTerrainTypeJson(Json,Error)){AddError(Error);return false;}
    auto Context=FCLPlanetCompiler::RollingPlains();Context.RadiusMeters=1000;
    FCLTerrainEditing Edit;Edit.Open(Asset.Get(),&Context);
    if(!TestTrue(TEXT("Reproduce adding default noise after stamps"),Edit.Add()))return false;
    const auto Full=Edit.CompilePreview(Error);
    if(!TestTrue(TEXT("Both layers compile"),Full.IsValid())){AddError(Error);return false;}
    TestEqual(TEXT("Both operations remain active"),Full->Layers.Num(),2);
    const auto GoodLayer=Asset->TerrainType.NoiseLayers[1];auto Reversed=GoodLayer;
    Reversed.Minimum.Fraction=.5;Reversed.Maximum.Fraction=.02;
    TestFalse(TEXT("Reversed guide-relative noise bounds are rejected at edit time"),Edit.SetLayer(Reversed));
    TestTrue(TEXT("Rejected edit identifies the noise bounds"),Edit.Error.Contains(TEXT("minimum must not exceed maximum")));
    TestEqual(TEXT("Rejected bounds preserve the authored minimum"),Asset->TerrainType.NoiseLayers[1].Minimum.Fraction,GoodLayer.Minimum.Fraction);
    TestTrue(TEXT("Returning to the saved layer clears its rejected-edit diagnostic"),Edit.SetLayer(GoodLayer));
    Edit.Enable(1,false);const auto Stamps=Edit.CompilePreview(Error);
    Edit.Enable(0,false);Edit.Enable(1,true);const auto Noise=Edit.CompilePreview(Error);
    if(!TestTrue(TEXT("Either enable switch works independently"),Stamps&&Noise))return false;
    TestTrue(TEXT("Noise-only and stamp-only views have the same neutral guide base"),
        FMath::IsNearlyEqual(Stamps->BaseHeightMeters,-550.,1.e-6)&&FMath::IsNearlyEqual(Noise->BaseHeightMeters,Stamps->BaseHeightMeters,1.e-9));
    TestEqual(TEXT("Disabled stamps create no placements"),Noise->GetDistributedStampCount(),0);
    double MinNoise=1.e30,MaxNoise=-1.e30,MaxSumError=0;
    for(int32 I=0;I<256;++I)
    {
        const double Z=1-2*(I+.5)/256,A=I*2.399963229728653;
        const FVector D(FMath::Sqrt(1-Z*Z)*FMath::Cos(A),FMath::Sqrt(1-Z*Z)*FMath::Sin(A),Z);
        const double H=Noise->SampleHeightMeters(D)-Noise->BaseHeightMeters;
        MinNoise=FMath::Min(MinNoise,H);MaxNoise=FMath::Max(MaxNoise,H);
        MaxSumError=FMath::Max(MaxSumError,FMath::Abs(Full->SampleHeightMeters(D)-Stamps->SampleHeightMeters(D)-H));
    }
    TestTrue(TEXT("Default additive noise contains visible signed relief below the sky"),MinNoise<-.01&&MaxNoise>.01&&Noise->MaximumHeightMeters<0);
    TestTrue(TEXT("Combined field contains both the exact stamp and noise contributions"),MaxSumError<1.e-6);
    Edit.SetSolo(1);const auto Solo=Edit.CompilePreview(Error);
    TestTrue(TEXT("Noise Solo uses the same base and field as noise-only toggles"),Solo&&FMath::IsNearlyEqual(Solo->SampleHeightMeters(FVector::ZAxisVector),Noise->SampleHeightMeters(FVector::ZAxisVector),1.e-8));
    for(const auto Blend:{ECLNoiseBlend::Max,ECLNoiseBlend::Min,ECLNoiseBlend::Override})
    {
        Asset->TerrainType.NoiseLayers[1].Blend=Blend;
        const auto Source=Edit.CompilePreview(Error);
        TestTrue(TEXT("Noise Solo displays source relief regardless of comparison blend"),Source&&FMath::IsNearlyEqual(Source->SampleHeightMeters(FVector::ZAxisVector),Noise->SampleHeightMeters(FVector::ZAxisVector),1.e-8));
        TestEqual(TEXT("Solo leaves the saved comparison blend unchanged"),Asset->TerrainType.NoiseLayers[1].Blend,Blend);
    }
    Asset->TerrainType.NoiseLayers[1].Blend=ECLNoiseBlend::Add;
    Edit.Enable(1,false);TestEqual(TEXT("Disabling a soloed layer clears Solo"),Edit.Solo,INDEX_NONE);
    Edit.SetSolo(1);TestEqual(TEXT("A disabled layer cannot be soloed"),Edit.Solo,INDEX_NONE);
    const auto Empty=Edit.CompilePreview(Error);
    TestTrue(TEXT("All-off state is a flat guide-midpoint landscape"),Empty&&Empty->Layers.IsEmpty()&&FMath::IsNearlyEqual(Empty->BaseHeightMeters,-550.,1.e-6));
    Edit.Enable(0,true);Edit.Enable(1,true);Edit.Context.RadiusMeters=10000;
    TestFalse(TEXT("Oversized full stack reports its real placement budget"),Edit.CompilePreview(Error).IsValid());
    TestTrue(TEXT("Budget diagnostic includes requested count and actionable density"),Error.Contains(TEXT("needs"))&&Error.Contains(TEXT("256"))&&Error.Contains(TEXT("/km2")));
    FString Before,After;Asset->ExportJsonString(Before,Error);
    Edit.SetSolo(1);const auto LargeSolo=Edit.CompilePreview(Error);
    TestTrue(TEXT("Noise Solo never compiles over-budget upstream stamps"),LargeSolo&&LargeSolo->Layers.Num()==1&&LargeSolo->GetDistributedStampCount()==0);
    Asset->ExportJsonString(After,Error);TestEqual(TEXT("Solo does not rewrite source layers"),After,Before);
    Edit.SetSolo(INDEX_NONE);Edit.Enable(0,false);
    TestTrue(TEXT("Muting over-budget stamps allows the remaining noise to compile"),Edit.CompilePreview(Error).IsValid());
    // A terrace preview must discard a failing downstream distribution before applying.
    auto Terrace=Asset->TerrainType.NoiseLayers[1];Terrace.Name=TEXT("Terrace preview");Terrace.Operation=ECLTerrainOperation::Terrace;
    const auto Stamp=Asset->TerrainType.NoiseLayers[0];
    Asset->TerrainType.NoiseLayers={Asset->TerrainType.NoiseLayers[1],Terrace,Stamp};Asset->TerrainType.NoiseLayers[2].bEnabled=true;
    Edit.SetSolo(1);const auto Shaped=Edit.CompilePreview(Error);
    TestTrue(TEXT("Terrace Solo omits over-budget downstream stamps before compilation"),Shaped&&Shaped->Layers.Num()==2);
    AddInfo(FString::Printf(TEXT("Mixed layers: %d stamp placements; signed noise %.6f to %.6f m; additive parity error %.12f m"),Stamps->GetDistributedStampCount(),MinNoise,MaxNoise,MaxSumError));
    return true;
}
#endif

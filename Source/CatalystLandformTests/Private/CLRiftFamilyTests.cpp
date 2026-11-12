#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Model/CLTerrainType.h"
#include "Model/CLRegionGeneration.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Model/CLCompileProgress.h"
#include "UObject/StrongObjectPtr.h"
namespace
{
bool FamilySource(UCLTerrainTypeAsset& A,FString& E)
{FString J;return FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/TerrainTypes/ContinentalRiftFamily.json")))&&A.ImportTerrainTypeJson(J,E);}
TArray<FVector> FamilyDirections()
{TArray<FVector> D;for(int32 I=0;I<1024;++I){double Z=1-2*(I+.5)/1024,A=I*2.399963229728653;D.Add(FVector(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z));}return D;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRiftFallbackTest,"Catalyst.Landform.Rift.Family.PlacementRelaxation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRiftFallbackTest::RunTest(const FString&)
{
    FString E;TStrongObjectPtr<UCLTerrainTypeAsset> Source(NewObject<UCLTerrainTypeAsset>());if(!FamilySource(*Source,E)){AddError(E);return false;}
    const auto Definition=Source->TerrainType.PlacementSuitability.Province;FCLProvinceGeology Missing;
    TestEqual(TEXT("Strict metadata still records missing geology"),Definition.Match(Missing),ECLProvinceMatch::Unknown);
    TestEqual(TEXT("Explicit final fallback permits a selected local expression"),Definition.Match(Missing,nullptr,3),ECLProvinceMatch::Eligible);
    TestEqual(TEXT("Relaxation never fabricates evidence"),Missing.Known,uint64(0));
    auto Bad=Definition;Bad.Relaxations[1].RelaxedRequirements.Add(ECLGeologicalEvidence::VolcanicHistory);TestFalse(TEXT("Fallback cannot relax unrelated evidence"),Bad.Validate(E));
    Bad=Definition;Bad.Relaxations[2].RelaxedRequirements.Reset();TestFalse(TEXT("Later fallback cannot reinstate requirements"),Bad.Validate(E));
    auto R=FCLPlanetCompiler::RollingPlains();R.AlgorithmVersion=2;R.Coordinates=ECLTerrainCoordinates::SphericalSurface;R.RadiusMeters=2000;R.RegionGeneration.RegionCount=24;
    TStrongObjectPtr<UCLPlanetRecipeAsset> Asset(NewObject<UCLPlanetRecipeAsset>());Asset->Recipe=R;
    if(!Asset->AddPaletteSource(Source.Get(),E)){AddError(E);return false;}
    const auto P=FCLPlanetCompiler::Compile(Asset->Recipe,E);if(!P){AddError(E);return false;}
    int32 Assigned=0;for(const auto& C:P->GeneratedRegions->Cells)if(P->GeneratedRegions->Palette[C.PaletteIndex].Id==Source->TerrainType.Id)
    {++Assigned;TestEqual(TEXT("Unknown geology uses the recorded final tier"),C.ProvinceRelaxationTier,3);TestTrue(TEXT("Existing inspector explains the relaxed placement"),C.Reason.Contains(TEXT("Selected local rift expression")));}
    TestEqual(TEXT("A single selected Rift fills every cell"),Assigned,P->GeneratedRegions->Cells.Num());
    auto Again=FCLPlanetCompiler::Compile(Asset->Recipe,E);if(!Again){AddError(E);return false;}
    for(int32 I=0;I<P->GeneratedRegions->Cells.Num();++I)TestEqual(TEXT("Fallback placement is deterministic"),P->GeneratedRegions->Cells[I].PaletteIndex,Again->GeneratedRegions->Cells[I].PaletteIndex);
    FString J;TStrongObjectPtr<UCLPlanetRecipeAsset> Copy(NewObject<UCLPlanetRecipeAsset>());TestTrue(TEXT("Relaxation metadata exports"),Asset->ExportJsonString(J,E));TestTrue(TEXT("Relaxation metadata imports"),Copy->ImportRecipeJson(J,E));
    TestTrue(TEXT("Recipe snapshot preserves complete noise and placement metadata"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Asset->Recipe,&Copy->Recipe,0));
    AddInfo(FString::Printf(TEXT("RIFT_FAMILY_FALLBACK assigned=%d cells=%d tier=3"),Assigned,P->GeneratedRegions->Cells.Num()));return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRiftFamilyShapeTest,"Catalyst.Landform.Rift.Family.CharacterNoiseAndRock",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRiftFamilyShapeTest::RunTest(const FString&)
{
    FString E,J;TStrongObjectPtr<UCLTerrainTypeAsset> Source(NewObject<UCLTerrainTypeAsset>());if(!FamilySource(*Source,E)){AddError(E);return false;}
    auto Bad=Source->TerrainType;Bad.NoiseLayers[0].Blend=ECLNoiseBlend::Override;TestFalse(TEXT("Local noise cannot replace underlying geology"),Bad.Validate(E));
    auto S=Source->TerrainType.Rift;const auto D=FamilyDirections();double Last=-1;
    for(double C:{0.,.5,1.})
    {
        S.Character=C;auto F=FCLCompiledRiftLandform::Compile(S,1729,1500,220,nullptr,nullptr,E);if(!F){AddError(E);return false;}
        double Peak=0;for(auto V:D){const double H=F->SampleMeters(V);TestTrue(TEXT("Local fallback is finite and physically bounded"),FMath::IsFinite(H)&&FMath::Abs(H)<=F->MaximumDisplacementMeters());Peak=FMath::Max(Peak,FMath::Abs(H));}
        TestTrue(TEXT("Each Character position supplies real local fault-block relief"),Peak>.01);
        if(Last>=0)TestTrue(TEXT("Weathering noise reduces toward sharper young scarps"),F->SurfaceDetailStrength()<Last);Last=F->SurfaceDetailStrength();
        auto Large=FCLCompiledRiftLandform::Compile(S,1729,6000,220,nullptr,nullptr,E);TestEqual(TEXT("Local wavelength stays in metres across planet sizes"),F->SpacingMeters(),Large->SpacingMeters());
    }
    S.AlgorithmVersion=1;auto Frozen=FCLCompiledRiftLandform::Compile(S,1729,1500,220,nullptr,nullptr,E);for(auto V:D)TestEqual(TEXT("Frozen profile 1 remains neutral without geological context"),Frozen->SampleMeters(V),0.);
    TStrongObjectPtr<UCLPlanetRecipeAsset> Asset(NewObject<UCLPlanetRecipeAsset>());
    FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/ContinentalRiftProvince.json")));
    if(!Asset->ImportRecipeJson(J,E)||!Asset->AddPaletteSource(Source.Get(),E)){AddError(E);return false;}
    const auto P=FCLPlanetCompiler::Compile(Asset->Recipe,E);auto R=Asset->Recipe;R.RegionGeneration=FCLRegionGenerationSettings();const auto B=FCLPlanetCompiler::Compile(R,E);if(!P||!B){AddError(E);return false;}
    TestEqual(TEXT("Native child retains the authored surface noise stack"),P->SourceRecipe->RegionGeneration.Palette[0].NoiseLayers.Num(),3);
    auto Bare=Asset->Recipe;Bare.RegionGeneration.Palette[0].NoiseLayers.Reset();const auto WithoutNoise=FCLPlanetCompiler::Compile(Bare,E);if(!WithoutNoise){AddError(E);return false;}
    double SurfaceDifference=0;for(auto V:D)SurfaceDifference+=FMath::Abs(P->SampleHeightMeters(V)-WithoutNoise->SampleHeightMeters(V));
    TestTrue(TEXT("Authored noise changes the actual composed planet surface"),SurfaceDifference>1);
    auto Flat=Bare;Flat.RegionGeneration.Palette[0].Rift.MinimumThrowMeters=Flat.RegionGeneration.Palette[0].Rift.MaximumThrowMeters=0;
    const auto WithoutFaults=FCLPlanetCompiler::Compile(Flat,E);if(!WithoutFaults){AddError(E);return false;}
    double FaultDifference=0;for(auto V:D)FaultDifference+=FMath::Abs(WithoutNoise->SampleHeightMeters(V)-WithoutFaults->SampleHeightMeters(V));
    TestTrue(TEXT("Fault blocks change actual terrain as well as restored Rock"),FaultDifference>1);


    for(const auto& Cell:P->GeneratedRegions->Cells)if(P->GeneratedRegions->Palette[Cell.PaletteIndex].Id==Source->TerrainType.Id)TestEqual(TEXT("Suitable geology uses strict placement before relaxing"),Cell.ProvinceRelaxationTier,0);
    double Residual=0;int32 Probes=0;
    for(auto V:D)
    {
        const double Delta=P->GeneratedRegions->ProvinceDeformationMeters(V);if(FMath::Abs(Delta)<.01)continue;++Probes;
        const double Radius=Asset->Recipe.RadiusMeters*.65;const auto A=B->Rock->Sample(V*Radius),C=P->Rock->Sample(V*(Radius+Delta));
        TestEqual(TEXT("Local deformation preserves canonical beds"),A.StratumId,C.StratumId);
        Residual=FMath::Max(Residual,FMath::Abs(B->Rock->RestoredRadiusMeters(V*Radius)-P->Rock->RestoredRadiusMeters(V*(Radius+Delta))));
    }
    TestTrue(TEXT("Noise family has an actual shared geological deformation"),Probes>0&&Residual<1.e-7);
    AddInfo(FString::Printf(TEXT("RIFT_FAMILY_ROCK probes=%d restorationMetres=%.12g noiseLayers=3"),Probes,Residual));return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRiftSavedSoloTest,"Catalyst.Landform.Rift.Family.SoloSavedProfile",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRiftSavedSoloTest::RunTest(const FString&)
{
    FString J,E;TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());
    FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/ContinentalRiftProvince.json")));
    if(!A->ImportRecipeJson(J,E)){AddError(E);return false;}const auto Id=A->Recipe.RegionGeneration.Palette[0].Id;
    TestEqual(TEXT("Saved Rift remains profile 1"),A->Recipe.RegionGeneration.Palette[0].Rift.AlgorithmVersion,1);
    auto Prepared=FCLPlanetCompiler::PrepareBackground(A->Recipe,E,nullptr,INDEX_NONE,nullptr,Id);const auto Solo=Prepared?FCLPlanetCompiler::CompleteBackground(*Prepared,MakeShared<FCLCompileProgress>(),E):nullptr;
    auto Flat=A->Recipe;Flat.RegionGeneration.Palette[0].Rift.MinimumThrowMeters=Flat.RegionGeneration.Palette[0].Rift.MaximumThrowMeters=0;
    auto FlatInput=FCLPlanetCompiler::PrepareBackground(Flat,E,nullptr,INDEX_NONE,nullptr,Id);const auto NoThrow=FlatInput?FCLPlanetCompiler::CompleteBackground(*FlatInput,MakeShared<FCLCompileProgress>(),E):nullptr;
    if(!Solo||!NoThrow){AddError(E);return false;}double Delta=0;for(auto D:FamilyDirections())Delta+=FMath::Abs(Solo->SampleHeightMeters(D)-NoThrow->SampleHeightMeters(D));
    TestTrue(TEXT("Solo delivers saved-profile fault relief to the actual spherical surface"),Delta>1);
    TestEqual(TEXT("Solo keeps the saved profile pinned"),A->Recipe.RegionGeneration.Palette[0].Rift.AlgorithmVersion,1);return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRiftCuratedTest,"Catalyst.Landform.Rift.Family.CuratedLandscape",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRiftCuratedTest::RunTest(const FString&)
{
    FString E,J;TStrongObjectPtr<UCLTerrainTypeAsset> S(NewObject<UCLTerrainTypeAsset>());if(!FamilySource(*S,E)){AddError(E);return false;}
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());
    auto* Review=LoadObject<UCLPlanetRecipeAsset>(nullptr,TEXT("/Game/LandformDesigner/Provinces/DA_ContinentalRiftFamilyReview"));if(!Review){AddError(TEXT("Current review is required"));return false;}A->Recipe=Review->Recipe;A->Recipe.Caves.bEnabled=false;A->Recipe.Watershed.bEnabled=false;
    if(!FCLPlanetCompiler::WithAuthoringValidation([&]{return A->AddPaletteSource(S.Get(),E);})){AddError(E);return false;}
    auto R=A->Recipe;R.RegionGeneration=FCLRegionGenerationSettings();const auto Base=FCLPlanetCompiler::Compile(R,E);if(!Base){AddError(E);return false;}const auto D=FamilyDirections();double Worst=0,SoloWorst=0;
    for(int32 I=0;I<=10;++I)
    {
        A->Recipe.RegionGeneration.Palette[0].Rift.ApplyCuratedCharacter(I/10.);const auto P=FCLPlanetCompiler::Compile(A->Recipe,E);if(!P){AddError(E);return false;}
        double Peak=0;for(auto V:D)Peak=FMath::Max(Peak,FMath::Abs(P->SampleHeightMeters(V)-Base->SampleHeightMeters(V)));
        TestTrue(TEXT("Every slider position adds bounded metre relief without re-fitting the landscape"),Peak>0.01&&Peak<48);Worst=FMath::Max(Worst,Peak);
        auto Input=FCLPlanetCompiler::PrepareBackground(A->Recipe,E,nullptr,INDEX_NONE,nullptr,S->TerrainType.Id);const auto Solo=Input?FCLPlanetCompiler::CompleteBackground(*Input,MakeShared<FCLCompileProgress>(),E):nullptr;if(!Solo){AddError(E);return false;}
        double SoloPeak=0;for(auto V:D)SoloPeak=FMath::Max(SoloPeak,FMath::Abs(Solo->SampleHeightMeters(V)-Base->SampleHeightMeters(V)));
        TestTrue(TEXT("Whole-planet Solo also preserves local metre scale throughout the slider"),SoloPeak>.01&&SoloPeak<48);SoloWorst=FMath::Max(SoloWorst,SoloPeak);
    }
    auto& F=A->Recipe.RegionGeneration.Palette[0].Rift;F.MaximumThrowMeters=160;TestTrue(TEXT("Deliberate extreme physical ranges remain valid"),F.Validate(E));F.ApplyCuratedCharacter(1);TestEqual(TEXT("Returning to the knob restores the curated range"),F.MaximumThrowMeters,42.);
    AddInfo(FString::Printf(TEXT("CURATED_RIFT wholeSliderSamples=11 maximumLandscapeDeltaMeters=%.6f maximumSoloDeltaMeters=%.6f"),Worst,SoloWorst));return !HasAnyErrors();
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Model/CLTerrainType.h"
#include "Model/CLRegionGeneration.h"
#include "Model/CLCompileProgress.h"
#include "Scene/CLScene.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLVolcanicArcTest,"Catalyst.Landform.VolcanicArc.FamilyAndAssembly",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLVolcanicArcTest::RunTest(const FString&)
{
    FString J,Error;const auto Root=IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir();
    TStrongObjectPtr<UCLTerrainTypeAsset> Source(NewObject<UCLTerrainTypeAsset>());
    FFileHelper::LoadFileToString(J,*(Root/TEXT("Examples/TerrainTypes/Provinces/VolcanicArc.json")));
    if(!Source->ImportTerrainTypeJson(J,Error)){AddError(Error);return false;}
    const auto Original=Source->TerrainType;auto Bad=Original;Bad.VolcanicArc.CraterRadiusMeters=Bad.VolcanicArc.BaseRadiusMeters;
    TestFalse(TEXT("Crater cannot exceed its edifice"),Bad.Validate(Error));Bad=Original;Bad.NoiseLayers[0].Blend=ECLNoiseBlend::Override;TestFalse(TEXT("Arc retains the geological base and materials"),Bad.Validate(Error));
    auto V=Original.VolcanicArc;V.ApplyProgression(.33);const auto Cone=V;V.ApplyProgression(.67);const auto Complex=V;V.ApplyProgression(1);
    TestTrue(TEXT("Curves have independent peaks and falls"),Cone.SecondaryHeight<Complex.SecondaryHeight&&Cone.IncisionMeters<Original.VolcanicArc.IncisionMeters&&Complex.LavaShoulderMeters>V.LavaShoulderMeters);
    TArray<FCLGeneratedRegionCell> Cells;auto& Cell=Cells.AddDefaulted_GetRef();Cell.Id=FGuid(1,2,3,4);Cell.Centre=FVector::ZAxisVector;
    TArray<FVector> Points;for(int32 X=-22;X<=22;++X)for(int32 Y=-22;Y<=22;++Y)Points.Add(FVector(X*.018,Y*.018,1).GetSafeNormal());
    TArray<FVector> Sites;double Last=0;
    for(double C:{0.,.17,.34,.50,.67,.84,1.})
    {
        V=Original.VolcanicArc;V.ApplyProgression(C);
        const auto A=FCLCompiledVolcanicArcLandform::Compile(V,1729,1500,Cells,nullptr,Error),B=FCLCompiledVolcanicArcLandform::Compile(V,1729,1500,Cells,nullptr,Error);
        ++V.Seed;const auto Reseed=FCLCompiledVolcanicArcLandform::Compile(V,1729,1500,Cells,nullptr,Error);if(!A||!B||!Reseed){AddError(Error);return false;}
        if(Sites.IsEmpty())for(const auto& Centre:A->GetCentres())Sites.Add(Centre.Direction);
        for(int32 I=0;I<Sites.Num();++I)TestTrue(TEXT("Character retains seeded vent locations"),Sites[I]==A->GetCentres()[I].Direction);
        for(const auto& Centre:A->GetCentres())
        {
            const double Axis=A->SampleMeters(Centre.Direction);
            for(const FVector Tangent:{Centre.Along,-Centre.Along,Centre.Across,-Centre.Across})
                TestTrue(TEXT("Crater floors remain continuous through their vent axes"),FMath::Abs(A->SampleMeters((Centre.Direction+Tangent*1.e-8).GetSafeNormal())-Axis)<.001);
        }
        double Peak=0,Total=0,Difference=0;
        for(auto P:Points){const double H=A->SampleMeters(P);TestEqual(TEXT("Deterministic physical field"),H,B->SampleMeters(P));TestTrue(TEXT("Finite conservative bounds"),FMath::IsFinite(H)&&H>=0&&H<=A->MaximumDisplacementMeters());Peak=FMath::Max(Peak,H);Total+=H;Difference+=FMath::Abs(H-Reseed->SampleMeters(P));}
        TestTrue(TEXT("Visible relief at every sampled Character"),Peak>10);TestTrue(TEXT("Independent reseed changes anatomy"),Difference>100);if(C>0)TestTrue(TEXT("Range changes physical terrain"),FMath::Abs(Total-Last)>10);Last=Total;
    }
    V=Original.VolcanicArc;V.ApplyProgression(.42);V.HeightMeters=180;auto Manual=FCLCompiledVolcanicArcLandform::Compile(V,1729,1500,Cells,nullptr,Error);V.ApplyProgression(.42);auto Reset=FCLCompiledVolcanicArcLandform::Compile(V,1729,1500,Cells,nullptr,Error);
    double Delta=0;for(auto P:Points)Delta+=FMath::Abs(Manual->SampleMeters(P)-Reset->SampleMeters(P));TestTrue(TEXT("Generation honors disclosure overrides"),Delta>100);
    V.ApplyProgression(.67);auto Small=FCLCompiledVolcanicArcLandform::Compile(V,1729,30,Cells,nullptr,Error);TestTrue(TEXT("Small planets adapt physical size"),Small&&FMath::IsFinite(Small->SampleMeters(FVector::ZAxisVector)));
    TStrongObjectPtr<UCLPlanetRecipeAsset> Asset(NewObject<UCLPlanetRecipeAsset>());
    FFileHelper::LoadFileToString(J,*(Root/TEXT("Examples/ContinentalRiftProvince.json")));if(!Asset->ImportRecipeJson(J,Error)){AddError(Error);return false;}
    for(auto& P:Asset->Recipe.RegionGeneration.Palette)P.bEnabled=false;
    if(!FCLPlanetCompiler::WithAuthoringValidation([&]{return Asset->AddPaletteSource(Source.Get(),Error);})){AddError(Error);return false;}
    Asset->Recipe.Caves.bEnabled=false;Asset->Recipe.Watershed.bEnabled=false;
    const auto Arc=FCLPlanetCompiler::Compile(Asset->Recipe,Error);auto BaseRecipe=Asset->Recipe;BaseRecipe.RegionGeneration=FCLRegionGenerationSettings();const auto Base=FCLPlanetCompiler::Compile(BaseRecipe,Error);if(!Arc||!Base){AddError(Error);return false;}
    TestTrue(TEXT("Arc reaches assembled surface and rock"),Arc->ProvinceSurface&&Arc->GeneratedRegions->bHasProvinceDeformation);
    int32 Probes=0;for(const auto& C:Arc->GeneratedRegions->Cells)
    {
        if(Arc->GeneratedRegions->Palette[C.PaletteIndex].Id!=Original.Id)continue;
        const auto D=C.Centre;const double H=Arc->GeneratedRegions->ProvinceDeformationMeters(D);if(H<.1)continue;++Probes;
        TestTrue(TEXT("Actual surface receives metre relief"),FMath::Abs(Arc->SampleHeightMeters(D)-Base->SampleHeightMeters(D)-H)<2.);
        TestTrue(TEXT("Body-space query matches direction query"),FMath::Abs(Arc->SampleHeightMeters(D*1300)-Arc->SampleHeightMeters(D))<1.e-7);
        if(Arc->Rock&&Base->Rock){const auto A=Base->Rock->Sample(D*1100),B=Arc->Rock->Sample(D*(1100+H));TestEqual(TEXT("Canonical unit survives arc deformation"),A.UnitId,B.UnitId);TestEqual(TEXT("Canonical stratum survives arc deformation"),A.StratumId,B.StratumId);}
    }
    TestTrue(TEXT("Normal generation places the selected arc"),Probes>0);
    TStrongObjectPtr<UCLPlanetRecipeAsset> Copy(NewObject<UCLPlanetRecipeAsset>());TestTrue(TEXT("Arc exports"),Asset->ExportJsonString(J,Error));TestTrue(TEXT("Arc imports"),Copy->ImportRecipeJson(J,Error));TestTrue(TEXT("Physical settings round-trip exactly"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Asset->Recipe,&Copy->Recipe,0));
    FCLProvinceGeology Unknown;const auto& Definition=Original.PlacementSuitability.Province;
    TestEqual(TEXT("Missing history stays unknown"),Definition.Match(Unknown),ECLProvinceMatch::Unknown);
    TestEqual(TEXT("Recorded final gameplay tier permits selected arc"),Definition.Match(Unknown,nullptr,2),ECLProvinceMatch::Eligible);
    Unknown.Set(ECLGeologicalEvidence::SubductionHistory,true);Unknown.Set(ECLGeologicalEvidence::VolcanicHistory,true);Unknown.Set(ECLGeologicalEvidence::Compression,false);
    TestEqual(TEXT("An inherited arc does not require active compression"),Definition.Match(Unknown),ECLProvinceMatch::Eligible);
    TestTrue(TEXT("Source remains unchanged"),FCLTerrainType::StaticStruct()->CompareScriptStruct(&Original,&Source->TerrainType,0));return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLVolcanicArcFallbackTest,"Catalyst.Landform.VolcanicArc.UnknownGeologyAndEnabledCaves",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLVolcanicArcFallbackTest::RunTest(const FString&)
{
    FString J,Error;const auto Root=IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir();
    TStrongObjectPtr<UCLTerrainTypeAsset> Source(NewObject<UCLTerrainTypeAsset>());
    FFileHelper::LoadFileToString(J,*(Root/TEXT("Examples/TerrainTypes/Provinces/VolcanicArc.json")));if(!Source->ImportTerrainTypeJson(J,Error)){AddError(Error);return false;}
    TStrongObjectPtr<UCLPlanetRecipeAsset> Asset(NewObject<UCLPlanetRecipeAsset>());
    Asset->Recipe=FCLPlanetCompiler::RollingPlains();Asset->Recipe.AlgorithmVersion=2;Asset->Recipe.Coordinates=ECLTerrainCoordinates::SphericalSurface;Asset->Recipe.RadiusMeters=1000;
    if(!FCLPlanetCompiler::WithAuthoringValidation([&]{return Asset->AddPaletteSource(Source.Get(),Error);})){AddError(Error);return false;}
    auto P=FCLPlanetCompiler::Compile(Asset->Recipe,Error);if(!P){AddError(Error);return false;}
    int32 Assigned=0;for(const auto& C:P->GeneratedRegions->Cells)if(P->GeneratedRegions->Palette[C.PaletteIndex].Id==Source->TerrainType.Id){++Assigned;TestEqual(TEXT("Unknown volcanic history uses recorded local fallback"),C.ProvinceRelaxationTier,2);}
    TestEqual(TEXT("Relaxed placement retains its authored regional budget"),Assigned,2);
    // A real assembled review planet retains enabled caves, independently of
    // volcanic history and the new local surface anatomy.
    FFileHelper::LoadFileToString(J,*(Root/TEXT("Examples/ContinentalRiftProvince.json")));if(!Asset->ImportRecipeJson(J,Error)){AddError(Error);return false;}
    for(auto& Entry:Asset->Recipe.RegionGeneration.Palette)Entry.bEnabled=false;
    if(!FCLPlanetCompiler::WithAuthoringValidation([&]{return Asset->AddPaletteSource(Source.Get(),Error);})){AddError(Error);return false;}
    Asset->Recipe.Caves.bEnabled=true;Asset->Recipe.Caves.AlgorithmVersion=9;P=FCLPlanetCompiler::Compile(Asset->Recipe,Error);
    if(!P||!P->Caves||P->Caves->Nodes.IsEmpty()){AddError(Error.IsEmpty()?TEXT("Enabled caves must exist on the volcanic planet"):Error);return false;}
    const auto Scene=FCLScene::FromPlanet(P,Error);if(!Scene){AddError(Error);return false;}
    TArray<FVector> Points={P->Caves->Nodes[0].Position};TArray<FCLSceneShapeSample> Shape;
    TestTrue(TEXT("Arc caves reach the shared scene"),Scene->SampleShape({Points},Shape,Error));TestTrue(TEXT("A generated cave contains underground air"),Shape.Num()==1&&Shape[0].FieldMeters>0);
    return !HasAnyErrors();
}
#endif

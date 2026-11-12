#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Model/CLTerrainType.h"
#include "Model/CLRegionGeneration.h"
#include "Model/CLCompileProgress.h"
#include "Scene/CLScene.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "UObject/StrongObjectPtr.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTransformFamilyTest,"Catalyst.Landform.Transform.Family.CurvesDeterminismAndRock",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLTransformFamilyTest::RunTest(const FString&)
{
    FString J,E;const auto Root=IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir();
    TStrongObjectPtr<UCLTerrainTypeAsset> S(NewObject<UCLTerrainTypeAsset>());FFileHelper::LoadFileToString(J,*(Root/TEXT("Examples/TerrainTypes/Provinces/TransformBelt.json")));if(!S->ImportTerrainTypeJson(J,E)){AddError(E);return false;}
    const auto Original=S->TerrainType;auto Bad=Original;Bad.Transform.ValleyWidthMeters=0;TestFalse(TEXT("Invalid physical width rejected"),Bad.Validate(E));Bad=Original;Bad.NoiseLayers[0].Blend=ECLNoiseBlend::Override;TestFalse(TEXT("Replacement relief rejected"),Bad.Validate(E));
    auto Knob=Original.Transform;Knob.ApplyProgression(.3);const auto Offset=Knob;Knob.ApplyProgression(.65);const auto Basin=Knob;Knob.ApplyProgression(1);const auto Ridge=Knob;
    TestTrue(TEXT("Curves vary independently rather than monotonically scaling noise"),Offset.LateralOffsetMeters>Basin.LateralOffsetMeters&&Offset.RidgeHeightMeters>Basin.RidgeHeightMeters&&Basin.BasinDepthMeters>Ridge.BasinDepthMeters);
    TArray<FCLGeneratedRegionCell> Cells;auto& Cell=Cells.AddDefaulted_GetRef();Cell.Id=FGuid(1,2,3,4);Cell.Centre=FVector::ZAxisVector;
    TArray<FVector> D;for(int32 I=-20;I<=20;++I)for(int32 K=-15;K<=15;++K)D.Add(FVector(I*.008,K*.008,1).GetSafeNormal());
    double Last=0;
    for(double C:{0.,.15,.3,.475,.65,.825,1.})
    {
        Knob=Original.Transform;Knob.ApplyProgression(C);auto F=FCLCompiledTransformLandform::Compile(Knob,1729,1500,Cells,nullptr,E),Again=FCLCompiledTransformLandform::Compile(Knob,1729,1500,Cells,nullptr,E);Knob.Seed++;auto Reseed=FCLCompiledTransformLandform::Compile(Knob,1729,1500,Cells,nullptr,E);if(!F||!Again||!Reseed){AddError(E);return false;}
        double Peak=0,Difference=0,Total=0;for(auto V:D){auto A=F->Sample(V),B=Again->Sample(V);TestEqual(TEXT("Same seed gives identical physical terrain"),A.Relief,B.Relief);TestTrue(TEXT("Lateral mapping is deterministic"),A.TangentOffset==B.TangentOffset);TestTrue(TEXT("Relief finite and conservatively bounded"),FMath::IsFinite(A.Relief)&&FMath::Abs(A.Relief)<=F->MaximumDisplacementMeters());Peak=FMath::Max(Peak,FMath::Abs(A.Relief));Difference+=FMath::Abs(A.Relief-Reseed->Sample(V).Relief);Total+=A.Relief;}
        TestTrue(TEXT("Character supplies terrain throughout its range"),Peak>1);TestTrue(TEXT("Reseed changes the landscape"),Difference>10);if(C>0)TestTrue(TEXT("Sampled Character positions are distinct"),FMath::Abs(Total-Last)>1);Last=Total;
    }
    Knob=Original.Transform;Knob.ApplyProgression(.37);Knob.ValleyDepthMeters=67;Knob.RidgeHeightMeters=29;
    auto Manual=FCLCompiledTransformLandform::Compile(Knob,1729,1500,Cells,nullptr,E);auto Curated=Knob;Curated.ApplyProgression(Knob.Character);auto Reset=FCLCompiledTransformLandform::Compile(Curated,1729,1500,Cells,nullptr,E);if(!Manual||!Reset){AddError(E);return false;}
    double ManualDifference=0;for(auto V:D)ManualDifference+=FMath::Abs(Manual->Sample(V).Relief-Reset->Sample(V).Relief);TestTrue(TEXT("Compilation honors manual values at the stored Character"),ManualDifference>10);
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());FFileHelper::LoadFileToString(J,*(Root/TEXT("Examples/ContinentalRiftProvince.json")));if(!A->ImportRecipeJson(J,E)){AddError(E);return false;}for(auto& Entry:A->Recipe.RegionGeneration.Palette)Entry.bEnabled=false;if(!FCLPlanetCompiler::WithAuthoringValidation([&]{return A->AddPaletteSource(S.Get(),E);})){AddError(E);return false;}
    // Normal planet assembly, not Solo: geological placement and rock deformation.
    A->Recipe.Caves.bEnabled=false;A->Recipe.Watershed.bEnabled=false;auto P=FCLPlanetCompiler::Compile(A->Recipe,E);auto R=A->Recipe;R.RegionGeneration=FCLRegionGenerationSettings();auto B=FCLPlanetCompiler::Compile(R,E);if(!P||!B){AddError(E);return false;}
    TestTrue(TEXT("Transform delivers directional mapping to the real planet"),P->ProvinceSurface&&P->GeneratedRegions->bHasDirectionalDeformation);int32 Probes=0;double Peak=0;
    for(const auto& C:P->GeneratedRegions->Cells)
    {
        const FVector V=C.Centre;TestTrue(TEXT("VPP body-space and unit-direction queries share the same sideways warp"),P->GeneratedRegions->ProvinceReferenceDirection(V*1300).Equals(P->GeneratedRegions->ProvinceReferenceDirection(V),1.e-12));TestTrue(TEXT("Native point queries deliver the same height as generation queries"),FMath::Abs(P->SampleHeightMeters(V*1300)-P->SampleHeightMeters(V))<1.e-7);const double Delta=P->GeneratedRegions->ProvinceDeformationMeters(V);const FVector M=P->GeneratedRegions->ProvinceReferenceDirection(V);Peak=FMath::Max(Peak,FMath::Abs(P->SampleHeightMeters(V)-B->SampleHeightMeters(V)));
        if(FMath::Abs(Delta)<.01)continue;++Probes;const double Radius=A->Recipe.RadiusMeters*.65;
        const auto Base=B->Rock->Sample(M*Radius),Moved=P->Rock->Sample(V*(Radius+Delta));TestEqual(TEXT("Horizontal and vertical deformation preserve the canonical stratum"),Base.StratumId,Moved.StratumId);TestEqual(TEXT("Canonical unit identity is retained"),Base.UnitId,Moved.UnitId);TestTrue(TEXT("Restored coordinates share the surface mapping"),FMath::Abs(B->Rock->RestoredRadiusMeters(M*Radius)-P->Rock->RestoredRadiusMeters(V*(Radius+Delta)))<1.e-7);
    }
    TestTrue(TEXT("Normal assembled planet has Transform relief and mapped rock probes"),Probes>0&&Peak>.1);
    TStrongObjectPtr<UCLPlanetRecipeAsset> Copy(NewObject<UCLPlanetRecipeAsset>());TestTrue(TEXT("Physical settings export"),A->ExportJsonString(J,E));TestTrue(TEXT("Physical settings import"),Copy->ImportRecipeJson(J,E));TestTrue(TEXT("All authored physical values round-trip"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&A->Recipe,&Copy->Recipe,0));
    auto Unknown=FCLPlanetCompiler::RollingPlains();Unknown.AlgorithmVersion=2;Unknown.Coordinates=ECLTerrainCoordinates::SphericalSurface;Unknown.RadiusMeters=2000;A->Recipe=Unknown;if(!FCLPlanetCompiler::WithAuthoringValidation([&]{return A->AddPaletteSource(S.Get(),E);})){AddError(E);return false;}P=FCLPlanetCompiler::Compile(A->Recipe,E);if(!P){AddError(E);return false;}
    int32 Assigned=0;for(const auto& C:P->GeneratedRegions->Cells)if(P->GeneratedRegions->Palette[C.PaletteIndex].Id==S->TerrainType.Id){++Assigned;TestEqual(TEXT("Missing shear follows recorded fallback"),C.ProvinceRelaxationTier,2);}
    TestEqual(TEXT("A single selected Transform fills every cell"),Assigned,P->GeneratedRegions->Cells.Num());TestTrue(TEXT("Source settings are unchanged"),FCLTerrainType::StaticStruct()->CompareScriptStruct(&Original,&S->TerrainType,0));return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTransformCaveTest,"Catalyst.Landform.Transform.Family.EnabledCaves",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLTransformCaveTest::RunTest(const FString&)
{
    FString J,E;TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());
    FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/ContinentalRiftProvince.json")));
    if(!A->ImportRecipeJson(J,E)){AddError(E);return false;}
    TStrongObjectPtr<UCLTerrainTypeAsset> Source(NewObject<UCLTerrainTypeAsset>());FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/TerrainTypes/Provinces/TransformBelt.json")));if(!Source->ImportTerrainTypeJson(J,E)){AddError(E);return false;}
    for(auto& Entry:A->Recipe.RegionGeneration.Palette)Entry.bEnabled=false;if(!FCLPlanetCompiler::WithAuthoringValidation([&]{return A->AddPaletteSource(Source.Get(),E);})){AddError(E);return false;}A->Recipe.Caves.bEnabled=true;A->Recipe.Caves.AlgorithmVersion=9;
    const auto P=FCLPlanetCompiler::Compile(A->Recipe,E);if(!P||!P->Caves||P->Caves->Nodes.IsEmpty()){AddError(E.IsEmpty()?TEXT("Transform planet must generate enabled caves"):E);return false;}
    const auto Scene=FCLScene::FromPlanet(P,E);if(!Scene){AddError(E);return false;}TArray<FVector> Points={P->Caves->Nodes[0].Position};TArray<FCLSceneShapeSample> Field;
    TestTrue(TEXT("Transform caves are delivered through the shared scene"),Scene->SampleShape({Points},Field,E));TestTrue(TEXT("Generated cave contains real underground air"),Field.Num()==1&&Field[0].FieldMeters>0);return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTransformWeightsTest,"Catalyst.Landform.Transform.Family.UniformWeights",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLTransformWeightsTest::RunTest(const FString&)
{
    FCLCompiledRegionGeneration G;G.Band=.3;G.Palette.SetNum(2);G.Cells.SetNum(2);
    for(int32 I=0;I<2;++I){auto& C=G.Cells[I];C.Centre=FVector(I*.2,0,1).GetSafeNormal();C.Frame=FQuat::Identity;C.Stretch=1;C.PaletteIndex=0;}
    for(int32 I=0;I<32;++I){const FVector D=FVector(I*.02-.3,.1,1).GetSafeNormal();TArray<double> CW,PW;G.CellWeights(D,CW);G.PaletteWeights(D,PW);TestTrue(TEXT("Uniform palette shortcut equals the sum of cell weights"),FMath::Abs(PW[0]-CW[0]-CW[1])<1.e-12&&PW[1]==0);}
    G.Cells[1].PaletteIndex=1;TArray<double> CW,PW;G.CellWeights(FVector::ZAxisVector,CW);G.PaletteWeights(FVector::ZAxisVector,PW);TestTrue(TEXT("Mixed provinces retain the original partition"),CW==PW);return !HasAnyErrors();
}

#endif

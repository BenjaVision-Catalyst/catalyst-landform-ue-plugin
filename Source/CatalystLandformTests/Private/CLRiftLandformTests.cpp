#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>
#include "Model/CLTerrainType.h"
#include "Model/CLRegionGeneration.h"
#include "Model/CLCompileProgress.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Tasks/Task.h"
namespace
{
FCLPlanetRecipe RiftRecipe(FString& Error)
{
    FString J;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/ContinentalRiftProvince.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());if(!A->ImportRecipeJson(J,Error))return {};return A->Recipe;
}
TArray<FVector> Points(int32 N=4096)
{TArray<FVector> D;for(int32 I=0;I<N;++I){double Z=1-2*(I+.5)/N,A=I*2.399963229728653;D.Add(FVector(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z));}return D;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRiftGeology,"Catalyst.Landform.Rift.GeologyAndRestoration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRiftGeology::RunTest(const FString&)
{
    FString E;auto R=RiftRecipe(E);auto P=FCLPlanetCompiler::Compile(R,E);if(!P){AddError(E);return false;}
    auto Base=R;Base.RegionGeneration=FCLRegionGenerationSettings();auto B=FCLPlanetCompiler::Compile(Base,E);if(!B){AddError(E);return false;}
    int32 Active=0,Outside=0,Probes=0;double Max=0,RestError=0;
    for(auto D:Points())
    {
        const double V=P->GeneratedRegions->ProvinceDeformationMeters(D);Max=FMath::Max(Max,FMath::Abs(V));
        const auto G=FCLProvinceGeology::Sample(*B,D);const auto Match=R.RegionGeneration.Palette[0].Suitability.Province.Match(G);
        if(Match!=ECLProvinceMatch::Eligible){++Outside;TestTrue(TEXT("Physical relief is zero outside actual local host/extension evidence"),FMath::Abs(V)<1.e-10);}
        if(FMath::Abs(V)>.01)
        {
            ++Active;const double Q=R.RadiusMeters*.65;
            const FVector Original=D*Q,Moved=D*(Q+V);
            const auto A=B->Rock->Sample(Original),C=P->Rock->Sample(Moved);
            RestError=FMath::Max(RestError,FMath::Abs(B->Rock->RestoredRadiusMeters(Original)-P->Rock->RestoredRadiusMeters(Moved)));
            TestEqual(TEXT("Restored fault-block strata retain the same unit"),A.UnitId,C.UnitId);
            TestEqual(TEXT("Restored fault-block strata retain the same bed"),A.StratumId,C.StratumId);++Probes;
        }
        const double H=P->SampleHeightMeters(D);TestTrue(TEXT("Final relief remains inside the configured surface range"),H>=P->MinimumHeightMeters-1.e-5&&H<=P->MaximumHeightMeters+1.e-5);
    }
    TestTrue(TEXT("The reference develops real metre-scale displaced blocks"),Active>10&&Max>1);TestTrue(TEXT("Unsuitable local geology remains unchanged"),Outside>100);TestTrue(TEXT("One physical displacement restores the rock coordinates"),RestError<1.e-7);
    auto None=R;None.Continents.bEnabled=false;None.Tectonics.bEnabled=false;auto N=FCLPlanetCompiler::Compile(None,E);if(!N){AddError(E);return false;}
    for(auto D:Points(64))TestEqual(TEXT("No geology cannot produce random fault blocks"),N->GeneratedRegions->ProvinceDeformationMeters(D),0.);
    AddInfo(FString::Printf(TEXT("%d active physical samples, %d outside hosts, %d strata probes; max throw %.9g m; restoration %.12g m"),Active,Outside,Probes,Max,RestError));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRiftCharacter,"Catalyst.Landform.Rift.CharacterAndScale",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRiftCharacter::RunTest(const FString&)
{
    FString E;auto R=RiftRecipe(E);auto P=FCLPlanetCompiler::Compile(R,E);if(!P){AddError(E);return false;}auto D=Points();TArray<double> Last;
    double MinSpacing=MAX_dbl,MaxSpacing=0;
    for(double C:{0.,.25,.5,.75,1.})
    {
        auto S=R.RegionGeneration.Palette[0].Rift;S.Character=C;
        auto F=FCLCompiledRiftLandform::Compile(S,R.Seed,1500,220,P->Tectonics,P->Continents,E);if(!F){AddError(E);return false;}
        TArray<double> Current;double Change=0,Maximum=0;
        for(int32 I=0;I<D.Num();++I){double V=F->SampleMeters(D[I]);Current.Add(V);Maximum=FMath::Max(Maximum,FMath::Abs(V));if(Last.Num())Change+=FMath::Abs(V-Last[I]);}
        TestTrue(TEXT("Every Character position develops a non-neutral fault field"),Maximum>.02);
        if(Last.Num())TestTrue(TEXT("Character changes structure across the range"),Change>.1);
        Last=Current;MinSpacing=FMath::Min(MinSpacing,F->SpacingMeters());MaxSpacing=FMath::Max(MaxSpacing,F->SpacingMeters());
        for(double Radius:{1500.,3000.,6000.})
        {auto Q=FCLCompiledRiftLandform::Compile(S,R.Seed,Radius,220,P->Tectonics,P->Continents,E);TestEqual(TEXT("Physical fault spacing does not stretch with planet radius"),Q->SpacingMeters(),F->SpacingMeters());}
    }
    TestTrue(TEXT("Character varies fault abundance as well as throw"),MaxSpacing>=MinSpacing*4.9);
    auto Bad=R.RegionGeneration.Palette[0].Rift;Bad.MinimumSpacingMeters=-1;TestFalse(TEXT("Invalid physical ranges are rejected"),Bad.Validate(E));
    Bad=R.RegionGeneration.Palette[0].Rift;Bad.Character=std::numeric_limits<double>::quiet_NaN();TestFalse(TEXT("Non-finite Character is rejected"),Bad.Validate(E));
    AddInfo(FString::Printf(TEXT("5 Character positions; physical spacing %.3f to %.3f m"),MaxSpacing,MinSpacing));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRiftBackground,"Catalyst.Landform.Rift.BackgroundAndRecipe",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRiftBackground::RunTest(const FString&)
{
    FString E;auto R=RiftRecipe(E);auto Sync=FCLPlanetCompiler::Compile(R,E);if(!Sync){AddError(E);return false;}
    auto Prepared=FCLPlanetCompiler::PrepareBackground(R,E);if(!Prepared){AddError(E);return false;}
    auto Control=MakeShared<FCLCompileProgress>();struct FResult{TSharedPtr<const FCLCompiledPlanetRecipe> Plan;FString Error;};
    auto Job=UE::Tasks::Launch(UE_SOURCE_LOCATION,[Prepared,Control]{FString Error;auto P=FCLPlanetCompiler::CompleteBackground(*Prepared,Control,Error);return FResult{P,Error};});Job.Wait();auto Result=Job.GetResult();
    if(!Result.Plan){AddError(Result.Error);return false;}double Worst=0;for(auto D:Points(256))Worst=FMath::Max(Worst,FMath::Abs(Sync->SampleHeightMeters(D)-Result.Plan->SampleHeightMeters(D)));
    TestTrue(TEXT("Native captured inputs and worker completion retain the same field"),Worst<1.e-7);
    auto Changed=R;Changed.RegionGeneration.Palette[0].Rift.Character=.95;auto Reused=FCLPlanetCompiler::PrepareBackground(Changed,E,nullptr,INDEX_NONE,Prepared.Get());if(!Reused){AddError(E);return false;}
    TestTrue(TEXT("A Character edit reuses native inputs instead of rebuilding geology"),FCLCompileProgress::ReusedNativeInputs(*Reused)>0);
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>()),B(NewObject<UCLPlanetRecipeAsset>());A->Recipe=R;FString Json;TestTrue(TEXT("Server recipe exports physical province settings"),A->ExportJsonString(Json,E));TestTrue(TEXT("Server recipe imports without editor source links"),B->ImportRecipeJson(Json,E));TestTrue(TEXT("The physical settings roundtrip exactly"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&R,&B->Recipe,0));
    AddInfo(FString::Printf(TEXT("Background/synchronous residual %.12g m; %d reused native inputs"),Worst,FCLCompileProgress::ReusedNativeInputs(*Reused)));return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRiftUpstreamHosts,"Catalyst.Landform.Rift.UpstreamHostsAndSeed",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRiftUpstreamHosts::RunTest(const FString&)
{
    FString E;auto R=RiftRecipe(E);R.SchemaVersion=23;
    for(auto& U:R.Rock.Units){U.Lithology=ECLRockLithology::Carbonate;U.DissolutionPotential=.8;}
    auto* Source=LoadObject<UCLTerrainTypeAsset>(nullptr,TEXT("/CatalystLandform/TerrainTypes/Provinces/DA_Province_CarbonatePlatform"));if(!TestNotNull(TEXT("Native carbonate definition"),Source))return false;
    FCLLandformPaletteEntry C;C.Id=Source->TerrainType.Id;C.Name=Source->TerrainType.Name;C.Suitability=Source->TerrainType.PlacementSuitability;R.RegionGeneration.Palette.Add(C);
    auto P=FCLPlanetCompiler::Compile(R,E);if(!P){AddError(E);return false;}
    int32 Carbonate=0,Rift=0;for(auto& Cell:P->GeneratedRegions->Cells){auto T=P->GeneratedRegions->Palette[Cell.PaletteIndex].Suitability.Province.Type;Carbonate+=T==ECLProvinceType::CarbonatePlatform;Rift+=T==ECLProvinceType::ContinentalRift;}
    TestTrue(TEXT("Real Rock evidence is available before province selection"),Carbonate>0);TestTrue(TEXT("Physical rifts coexist with typed hosts and other included provinces"),Rift>0);
    auto Changed=R;++Changed.RegionGeneration.Palette[0].Rift.Seed;auto Q=FCLPlanetCompiler::Compile(Changed,E);if(!Q){AddError(E);return false;}
    for(int32 I=0;I<P->GeneratedRegions->Cells.Num();++I)TestEqual(TEXT("Local reseeding does not move the underlying province assignment"),P->GeneratedRegions->Cells[I].PaletteIndex,Q->GeneratedRegions->Cells[I].PaletteIndex);
    double Difference=0,Seam=0;for(auto D:Points())Difference+=FMath::Abs(P->GeneratedRegions->ProvinceDeformationMeters(D)-Q->GeneratedRegions->ProvinceDeformationMeters(D));
    TestTrue(TEXT("Local reseeding changes the fault-block field"),Difference>1);
    for(int32 I=0;I<64;++I)
    {const double Z=-.9+1.8*I/63.;auto A=FVector(1,1+1.e-8,Z).GetSafeNormal(),B=FVector(1+1.e-8,1,Z).GetSafeNormal();Seam=FMath::Max(Seam,FMath::Abs(P->GeneratedRegions->ProvinceDeformationMeters(A)-P->GeneratedRegions->ProvinceDeformationMeters(B)));}
    TestTrue(TEXT("The physical field is continuous across cube-face chart ties"),Seam<.01);
    AddInfo(FString::Printf(TEXT("%d carbonate regions, %d physical rift regions; reseed difference %.6f m; chart-tie residual %.12g m"),Carbonate,Rift,Difference,Seam));return true;
}
#endif

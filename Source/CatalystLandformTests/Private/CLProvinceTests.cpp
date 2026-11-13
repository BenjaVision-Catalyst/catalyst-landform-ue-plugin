#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Model/CLProvinceDefinition.h"
#include "Model/CLTerrainType.h"
#include "Model/CLRegionGeneration.h"
#include "Model/CLCompileProgress.h"
#include "Algo/Reverse.h"
#include "Surface/CLSurfaceCompiler.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
namespace
{
TArray<FCLTerrainType> ProvinceDefinitions(FString& Error)
{
    const FString Root=IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir();FString Json;TSharedPtr<FJsonObject> Catalog;
    if(!FFileHelper::LoadFileToString(Json,*(Root/TEXT("Examples/ProvinceLandforms.json")))||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Catalog))return {};
    TArray<FCLTerrainType> Result;
    for(const auto& Item:Catalog->GetArrayField(TEXT("profiles")))
    {
        if(!FFileHelper::LoadFileToString(Json,*(Root/Item->AsObject()->GetStringField(TEXT("source")))))return {};
        TStrongObjectPtr<UCLTerrainTypeAsset> Source(NewObject<UCLTerrainTypeAsset>());if(!Source->ImportTerrainTypeJson(Json,Error))return {};Result.Add(Source->TerrainType);
    }
    return Result;
}
FCLLandformPaletteEntry ProvinceEntry(const FCLTerrainType& T)
{FCLLandformPaletteEntry P;P.Id=T.Id;P.Name=T.Name;P.AlgorithmVersion=T.AlgorithmVersion;P.Suitability=T.PlacementSuitability;P.Rift=T.Rift;P.Transform=T.Transform;P.VolcanicArc=T.VolcanicArc;P.NoiseLayers=T.NoiseLayers;return P;}
FCLPlanetRecipe ProvinceBase(FString& Error)
{
    FString Json;FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/WatershedContinental.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());if(!A->ImportRecipeJson(Json,Error))return {};return A->Recipe;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLProvinceBalanceTest,"Catalyst.Landform.Provinces.BalancedCoverage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLProvinceBalanceTest::RunTest(const FString&)
{
    FString Error,Json;const auto Types=ProvinceDefinitions(Error);if(Types.Num()!=17){AddError(Error);return false;}
    TStrongObjectPtr<UCLPlanetRecipeAsset> Asset(NewObject<UCLPlanetRecipeAsset>());
    FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/ContinentalRiftProvince.json")));
    if(!Asset->ImportRecipeJson(Json,Error)){AddError(Error);return false;}
    Asset->Recipe.Caves.bEnabled=false;Asset->Recipe.Watershed.bEnabled=false;Asset->Recipe.RegionGeneration.RegionCount=24;
    FGuid RiftId;for(auto& P:Asset->Recipe.RegionGeneration.Palette){P.bEnabled=P.Suitability.Province.Type==ECLProvinceType::ContinentalRift;if(P.bEnabled)RiftId=P.Id;}
    const auto Single=FCLPlanetCompiler::Compile(Asset->Recipe,Error);if(!Single){AddError(Error);return false;}
    for(const auto& C:Single->GeneratedRegions->Cells)TestEqual(TEXT("Selecting Rift alone fills every cell"),Single->GeneratedRegions->Palette[C.PaletteIndex].Id,RiftId);
    TStrongObjectPtr<UCLTerrainTypeAsset> Arc(NewObject<UCLTerrainTypeAsset>());Arc->TerrainType=*Types.FindByPredicate([](const auto& T){return T.VolcanicArc.IsActive();});
    if(!FCLPlanetCompiler::WithAuthoringValidation([&]{return Asset->AddPaletteSource(Arc.Get(),Error);})){AddError(Error);return false;}
    auto CheckCounts=[&](const FCLCompiledRegionGeneration& G,int32 Sources)
    {
        TMap<FGuid,int32> Counts;for(const auto& C:G.Cells)++Counts.FindOrAdd(G.Palette[C.PaletteIndex].Id);
        TestEqual(TEXT("Every selected source has an actual assigned cell"),Counts.Num(),Sources);
        for(const auto& Pair:Counts)TestTrue(TEXT("Included sources share cells equally within one cell"),Pair.Value>=G.Cells.Num()/Sources&&Pair.Value<=(G.Cells.Num()+Sources-1)/Sources);
    };
    TSharedPtr<const FCLCompiledPlanetRecipe> Pair;
    for(int32 Count:{24,25,128})
    {
        auto R=Asset->Recipe;R.RegionGeneration.RegionCount=Count;
        Pair=FCLPlanetCompiler::Compile(R,Error);if(!Pair){AddError(Error);return false;}CheckCounts(*Pair->GeneratedRegions,2);
        auto Reordered=R;Algo::Reverse(Reordered.RegionGeneration.Palette);const auto Replay=FCLPlanetCompiler::Compile(Reordered,Error);if(!Replay){AddError(Error);return false;}
        for(int32 I=0;I<Count;++I){const auto& A=*Pair->GeneratedRegions;const auto& B=*Replay->GeneratedRegions;TestEqual(TEXT("Balanced placement is deterministic and independent of source order"),A.Palette[A.Cells[I].PaletteIndex].Id,B.Palette[B.Cells[I].PaletteIndex].Id);}
        AddInfo(FString::Printf(TEXT("Rift + Volcanic arc: %d cells, each receives %d or %d"),Count,Count/2,(Count+1)/2));
    }
    auto OtherSeed=Asset->Recipe;++OtherSeed.RegionGeneration.Seed;const auto Seeded=FCLPlanetCompiler::Compile(OtherSeed,Error);if(!Seeded){AddError(Error);return false;}CheckCounts(*Seeded->GeneratedRegions,2);
    auto Three=Asset->Recipe;Three.RegionGeneration.Palette.Add(ProvinceEntry(*Types.FindByPredicate([](const auto& T){return T.Transform.IsActive();})));const auto Third=FCLPlanetCompiler::Compile(Three,Error);if(!Third){AddError(Error);return false;}CheckCounts(*Third->GeneratedRegions,3);
    auto Pinned=Asset->Recipe;const auto Baseline=FCLPlanetCompiler::Compile(Pinned,Error);if(!Baseline){AddError(Error);return false;}
    for(int32 I=0;I<20;++I){FCLRegionAssignmentOverride O;O.RegionId=Baseline->GeneratedRegions->Cells[I].Id;O.LandformId=Arc->TerrainType.Id;Pinned.RegionGeneration.Overrides.Add(O);}
    const auto Locked=FCLPlanetCompiler::Compile(Pinned,Error);if(!Locked){AddError(Error);return false;}
    int32 RiftCount=0;for(int32 I=0;I<Locked->GeneratedRegions->Cells.Num();++I){const auto& G=*Locked->GeneratedRegions;const auto Id=G.Palette[G.Cells[I].PaletteIndex].Id;RiftCount+=Id==RiftId;if(I<20)TestTrue(TEXT("Manual assignments remain locked"),Id==Arc->TerrainType.Id&&G.Cells[I].bOverride);}
    TestEqual(TEXT("Manual excess leaves the other included source represented"),RiftCount,4);
    for(int32 I=20;I<24;++I){FCLRegionAssignmentOverride O;O.RegionId=Baseline->GeneratedRegions->Cells[I].Id;O.LandformId=Arc->TerrainType.Id;Pinned.RegionGeneration.Overrides.Add(O);}
    TestFalse(TEXT("Contradictory overrides cannot silently orphan an included source"),FCLPlanetCompiler::Compile(Pinned,Error).IsValid());
    auto Many=ProvinceBase(Error);Many.Caves.bEnabled=false;Many.RegionGeneration.bEnabled=true;Many.RegionGeneration.AlgorithmVersion=3;Many.RegionGeneration.RegionCount=8;
    for(auto T:Types){T.Rift=FCLRiftLandform();T.Transform=FCLTransformLandform();T.VolcanicArc=FCLVolcanicArcLandform();T.NoiseLayers.Reset();T.PlacementSuitability.Province.bPlaceholder=true;Many.RegionGeneration.Palette.Add(ProvinceEntry(T));}
    const auto Expanded=FCLPlanetCompiler::Compile(Many,Error);if(!Expanded){AddError(Error);return false;}TestEqual(TEXT("Enough cells are generated for every selected source"),Expanded->GeneratedRegions->Cells.Num(),Types.Num());CheckCounts(*Expanded->GeneratedRegions,Types.Num());
    TestEqual(TEXT("The authored cell-count setting is unchanged"),Many.RegionGeneration.RegionCount,8);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLProvinceDefinitionsTest,"Catalyst.Landform.Provinces.DefinitionsAndEvidence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLProvinceDefinitionsTest::RunTest(const FString&)
{
    FString Error;const auto Types=ProvinceDefinitions(Error);if(!TestEqual(TEXT("All proposed province Landforms have portable definitions"),Types.Num(),17)){AddError(Error);return false;}
    TSet<FGuid> Ids;TSet<uint8> Kinds;
    for(const auto& T:Types)
    {
        TestTrue(TEXT("Province definitions use the existing Landform type and native algorithm"),T.SchemaVersion==(T.VolcanicArc.IsActive()?14:T.Transform.IsActive()?13:T.Rift.IsActive()?12:9)&&T.AlgorithmVersion==2&&T.Coordinates==ECLTerrainCoordinates::SphericalSurface);
        TestTrue(TEXT("Every placeholder keeps base relief and material"),T.PlacementSuitability.Province.bPlaceholder==!(T.Rift.IsActive()||T.Transform.IsActive()||T.VolcanicArc.IsActive())&&(!T.PlacementSuitability.Province.bPlaceholder||T.NoiseLayers.IsEmpty())&&!T.SurfaceStack.IsActive());
        TestFalse(TEXT("Landform identity is unique"),Ids.Contains(T.Id));Ids.Add(T.Id);Kinds.Add(uint8(T.PlacementSuitability.Province.Type));
        FCLProvinceGeology Context;TestEqual(TEXT("Unknown geology cannot place a province"),T.PlacementSuitability.Province.Match(Context),ECLProvinceMatch::Unknown);
        for(auto E:T.PlacementSuitability.Province.Requirements)Context.Set(E,true);
        TestEqual(TEXT("Complete positive geological evidence permits a definition"),T.PlacementSuitability.Province.Match(Context),ECLProvinceMatch::Eligible);
        Context.Set(T.PlacementSuitability.Province.Requirements[0],false);TestEqual(TEXT("Contradictory base geology rejects a definition"),T.PlacementSuitability.Province.Match(Context),ECLProvinceMatch::Incompatible);
        auto Invalid=T;Invalid.NoiseLayers.AddDefaulted();TestFalse(TEXT("Placeholder cannot quietly acquire relief"),Invalid.Validate(Error));
    }
    TestEqual(TEXT("All 17 province types are represented"),Kinds.Num(),17);
    auto R=FCLPlanetCompiler::BasePlanet(),Before=R;TestFalse(TEXT("Province cannot replace global terrain"),Types[0].ApplyTo(R,Error));TestTrue(TEXT("Rejected application is atomic"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Before,&R,0));
    auto Invalid=Types[0].PlacementSuitability.Province;const auto Duplicate=Invalid.Requirements[0];Invalid.Requirements.Add(Duplicate);TestFalse(TEXT("Duplicate geological prerequisites are rejected"),Invalid.Validate(Error));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLProvincePlacementTest,"Catalyst.Landform.Provinces.BaseGeologyAndNeutrality",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLProvincePlacementTest::RunTest(const FString&)
{
    FString Error;auto Types=ProvinceDefinitions(Error);if(Types.Num()!=17){AddError(Error);return false;}for(auto& T:Types)if(T.Rift.IsActive()||T.Transform.IsActive()||T.VolcanicArc.IsActive()){T.Rift=FCLRiftLandform();T.Transform=FCLTransformLandform();T.VolcanicArc=FCLVolcanicArcLandform();T.NoiseLayers.Reset();T.SchemaVersion=9;T.PlacementSuitability.Province.bPlaceholder=true;T.PlacementSuitability.Province.AlgorithmVersion=1;T.PlacementSuitability.Province.Relaxations.Reset();T.PlacementSuitability.Province.PlacementPreferences.Reset();}auto R=ProvinceBase(Error);
    const auto Before=FCLPlanetCompiler::Compile(R,Error);if(!Before){AddError(Error);return false;}
    R.RegionGeneration.bEnabled=true;R.RegionGeneration.AlgorithmVersion=3;R.RegionGeneration.RegionCount=24;for(const auto& T:Types)R.RegionGeneration.Palette.Add(ProvinceEntry(T));
    const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}
    int32 Assigned=0,Retained=0;for(const auto& C:P->GeneratedRegions->Cells)
    {
        const auto Type=P->GeneratedRegions->Palette[C.PaletteIndex].Suitability.Province.Type;
        TestTrue(TEXT("Every cell belongs to an included province"),Type!=ECLProvinceType::None);++Assigned;
        if(C.ProvinceRelaxationTier==0){TestTrue(TEXT("Strict candidates retain priority wherever geological context fits"),Type==ECLProvinceType::OceanicCrust||Type==ECLProvinceType::ContinentalRift||Type==ECLProvinceType::TransformBelt);TestTrue(TEXT("Positive geological area coverage is recorded"),C.Reason.Contains(TEXT("positive area samples")));}
        else {++Retained;TestTrue(TEXT("Relaxed placement records the coverage fallback"),C.Reason.Contains(TEXT("fallback")));}
    }
    TestTrue(TEXT("Real base geology supplies supported structural assignments"),Assigned>0);
    auto Restricted=R;for(auto& Entry:Restricted.RegionGeneration.Palette)Entry.bEnabled=Entry.Suitability.Province.Type==ECLProvinceType::OceanicCrust;
    const auto SelectedOnly=FCLPlanetCompiler::Compile(Restricted,Error);if(!SelectedOnly){AddError(Error);return false;}
    for(const auto& C:SelectedOnly->GeneratedRegions->Cells){const auto& Entry=SelectedOnly->GeneratedRegions->Palette[C.PaletteIndex];TestTrue(TEXT("Placement uses only included Landforms or the unchanged base"),Entry.Id.IsValid()&&Entry.Suitability.Province.Type==ECLProvinceType::OceanicCrust);}
    double Worst=0;for(int32 I=0;I<256;++I)
    {
        const double Z=1-2*(I+.5)/256,A=I*2.399963229728653;const FVector D(FMath::Sqrt(1-Z*Z)*FMath::Cos(A),FMath::Sqrt(1-Z*Z)*FMath::Sin(A),Z);
        const double H=Before->SampleHeightMeters(D);Worst=FMath::Max(Worst,FMath::Abs(H-P->SampleHeightMeters(D)));
        const auto WB=Before->SurfaceWeightsAt(D,H,25),WP=P->SurfaceWeightsAt(D,H,25);for(int32 S=0;S<FCLCompiledSurfaceLibrary::MaxSurfaces;++S)TestEqual(TEXT("Province placeholders preserve native surface weights"),WB.Values[S],WP.Values[S]);
        const FVector Point=D*(R.RadiusMeters+H-4);TestEqual(TEXT("Province placement does not replace the shared rock units"),Before->Rock->Sample(Point).UnitId,P->Rock->Sample(Point).UnitId);
        const auto G=FCLProvinceGeology::Sample(*Before,D);TestFalse(TEXT("Generic rock resistance never becomes carbonate evidence"),(G.Known&(uint64(1)<<uint8(ECLGeologicalEvidence::CarbonateSequence)))!=0);
    }
    TestTrue(TEXT("Neutral placeholders leave the final native surface unchanged"),Worst<1.e-7);
    const auto* Carbonate=Types.FindByPredicate([](const auto& T){return T.PlacementSuitability.Province.Type==ECLProvinceType::CarbonatePlatform;});
    auto Only=R;Only.RegionGeneration.Palette={ProvinceEntry(*Carbonate)};const auto Single=FCLPlanetCompiler::Compile(Only,Error);if(!Single){AddError(Error);return false;}
    for(const auto& C:Single->GeneratedRegions->Cells)TestTrue(TEXT("One selected province fills every cell without rewriting its geological metadata"),Single->GeneratedRegions->Palette[C.PaletteIndex].Id==Carbonate->Id&&C.ProvinceRelaxationTier>0);
    FCLRegionAssignmentOverride Override;Override.RegionId=Single->GeneratedRegions->Cells[0].Id;Override.LandformId=Carbonate->Id;Only.RegionGeneration.Overrides.Add(Override);TestTrue(TEXT("Editorial assignment uses recorded relaxation when geology is missing"),FCLPlanetCompiler::Compile(Only,Error).IsValid());
    Only.RegionGeneration.Overrides[0].LandformId=FGuid();TestFalse(TEXT("An override cannot assign a cell to the internal neutral palette entry"),FCLPlanetCompiler::Compile(Only,Error).IsValid());
    auto Legacy=R;Legacy.RegionGeneration.AlgorithmVersion=2;TestFalse(TEXT("Province snapshots cannot enter the legacy unconditional selector"),FCLPlanetCompiler::Compile(Legacy,Error).IsValid());
    auto Empty=R;Empty.Continents.bEnabled=false;Empty.Tectonics.bEnabled=false;Empty.NoiseLayers.Reset();Empty.BaseStructure.Reset();Empty.TerrainRegions.Reset();Empty.ElevationDistribution.bEnabled=false;Empty.Watershed.bEnabled=false;
    const auto NoEvidence=FCLPlanetCompiler::Compile(Empty,Error);if(!NoEvidence){AddError(Error);return false;}for(const auto& C:NoEvidence->GeneratedRegions->Cells)TestTrue(TEXT("Missing geology still assigns a selected province and records relaxation"),NoEvidence->GeneratedRegions->Palette[C.PaletteIndex].Id.IsValid()&&C.ProvinceRelaxationTier>0);
    const auto Replay=FCLPlanetCompiler::Compile(Empty,Error);if(!Replay){AddError(Error);return false;}
    for(int32 I=0;I<NoEvidence->GeneratedRegions->Cells.Num();++I){TestEqual(TEXT("Relaxed coverage is deterministic"),NoEvidence->GeneratedRegions->Cells[I].PaletteIndex,Replay->GeneratedRegions->Cells[I].PaletteIndex);TestEqual(TEXT("Relaxation levels are deterministic"),NoEvidence->GeneratedRegions->Cells[I].ProvinceRelaxationTier,Replay->GeneratedRegions->Cells[I].ProvinceRelaxationTier);}
    TStrongObjectPtr<UCLPlanetRecipeAsset> Asset(NewObject<UCLPlanetRecipeAsset>()),Copy(NewObject<UCLPlanetRecipeAsset>());Asset->Recipe=R;FString Json;TestTrue(TEXT("Province snapshots export"),Asset->ExportJsonString(Json,Error));TestTrue(TEXT("Server recipe import retains geological placement"),Copy->ImportRecipeJson(Json,Error));TestTrue(TEXT("Definitions and prerequisites roundtrip without live source links"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&R,&Copy->Recipe,0));
    TStrongObjectPtr<UCLTerrainTypeAsset> Source(NewObject<UCLTerrainTypeAsset>());Source->TerrainType=*Carbonate;Asset->Recipe=ProvinceBase(Error);TestTrue(TEXT("Choosing a province source opts into geological placement"),Asset->AddPaletteSource(Source.Get(),Error));TestEqual(TEXT("Geological source link uses mode 3"),Asset->Recipe.RegionGeneration.AlgorithmVersion,3);
    AddInfo(FString::Printf(TEXT("%d selected assignments, %d relaxed regions; max final-height change %.12g m"),Assigned,Retained,Worst));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLProvinceAssetsTest,"Catalyst.Landform.Provinces.PackagedLibrary",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLProvinceAssetsTest::RunTest(const FString&)
{
    FString Error;const auto Types=ProvinceDefinitions(Error);if(Types.Num()!=17){AddError(Error);return false;}
    FString Json;TSharedPtr<FJsonObject> Catalog;FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/ProvinceLandforms.json")));FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Catalog);
    for(int32 I=0;I<Types.Num();++I)
    {
        const auto Path=Catalog->GetArrayField(TEXT("profiles"))[I]->AsObject()->GetStringField(TEXT("asset"));const auto* A=LoadObject<UCLTerrainTypeAsset>(nullptr,*Path);
        if(!TestNotNull(TEXT("Province has a real Content Browser Landform asset"),A))continue;
        TestTrue(TEXT("Saved native asset equals its portable definition"),FCLTerrainType::StaticStruct()->CompareScriptStruct(&A->TerrainType,&Types[I],0));TestTrue(TEXT("Native saved province validates"),A->TerrainType.Validate(Error));
    }
    return true;
}
#endif

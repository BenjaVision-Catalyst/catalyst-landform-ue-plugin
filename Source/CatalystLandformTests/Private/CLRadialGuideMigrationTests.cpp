#if WITH_DEV_AUTOMATION_TESTS
#include "Model/CLPlanetRecipe.h"
#include "CLRadialGuideEditing.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGuideMigrationTest,"Catalyst.Landform.RadialGuides.LegacyAndReferences",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGuideMigrationTest::RunTest(const FString&)
{
    auto* Asset=NewObject<UCLPlanetRecipeAsset>();const auto Original=Asset->Recipe;
    auto Object=FJsonObjectConverter::UStructToJsonObject(Original);Object->SetNumberField(TEXT("schemaVersion"),8);
    auto Set=Object->GetObjectField(TEXT("radialGuides"));Set->SetArrayField(TEXT("stratum"),Set->GetArrayField(TEXT("guides")));Set->RemoveField(TEXT("guides"));
    Object->SetObjectField(TEXT("strata"),Set);Object->RemoveField(TEXT("radialGuides"));
    FString Json,Error;FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Json));
    if(!TestTrue(TEXT("Legacy recipe and nested guide names migrate"),Asset->ImportRecipeJson(Json,Error))){AddError(Error);return false;}
    TestEqual(TEXT("Canonical recipe schema"),Asset->Recipe.SchemaVersion,9);
    TestTrue(TEXT("Guide IDs, names, normalized heights and colors survive"),FCLRadialGuideSet::StaticStruct()->CompareScriptStruct(&Original.RadialGuides,&Asset->Recipe.RadialGuides,0));
    const auto A=FCLPlanetCompiler::Compile(Original,Error),B=FCLPlanetCompiler::Compile(Asset->Recipe,Error);
    for(int32 I=0;I<100;++I)
    {
        const FVector D(FMath::Sin(I*.37),FMath::Cos(I*.53),FMath::Sin(I*.71));
        TestEqual(TEXT("Terminology migration preserves terrain"),A->SampleHeightMeters(D),B->SampleHeightMeters(D));
    }
    TestTrue(TEXT("Canonical export succeeds"),Asset->ExportJsonString(Json,Error));
    TestTrue(TEXT("Export uses radialGuides"),Json.Contains(TEXT("\"radialGuides\"")));
    TestFalse(TEXT("Export omits legacy names"),Json.Contains(TEXT("\"strata\""))||Json.Contains(TEXT("\"stratum\"")));
    Object->SetObjectField(TEXT("radialGuides"),Set);FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Json));
    const auto Before=Asset->Recipe;
    TestFalse(TEXT("Ambiguous legacy/canonical aliases fail"),Asset->ImportRecipeJson(Json,Error));
    TestTrue(TEXT("Refused input leaves existing asset intact"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Before,&Asset->Recipe,0));
    Object->RemoveField(TEXT("radialGuides"));Object->SetNumberField(TEXT("schemaVersion"),9);FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Json));
    TestFalse(TEXT("Schema 9 requires canonical field names"),Asset->ImportRecipeJson(Json,Error));

    auto R=Original;FCLTerrainRegion Region;Region.NoiseLayers=R.NoiseLayers;R.TerrainRegions.Add(Region);
    for(auto* Layers:{&R.NoiseLayers,&R.TerrainRegions[0].NoiseLayers})
        for(auto& L:*Layers)
            for(auto* Ref:{&L.Minimum,&L.Maximum,&L.Terrace.StepHeight,&L.Terrace.Origin,&L.Distribution.MinimumRelief,&L.Distribution.MaximumRelief})
            {Ref->Guide=TEXT("SurfaceMin");Ref->UpperGuide=TEXT("SurfaceMax");}
    TestTrue(TEXT("Guide rename succeeds"),FCLRadialGuideEditing::Rename(R,1,TEXT("Lowlands"),Error));
    for(auto* Layers:{&R.NoiseLayers,&R.TerrainRegions[0].NoiseLayers})
        for(auto& L:*Layers)
            for(auto* Ref:{&L.Minimum,&L.Maximum,&L.Terrace.StepHeight,&L.Terrace.Origin,&L.Distribution.MinimumRelief,&L.Distribution.MaximumRelief})
            {TestEqual(TEXT("All base/region noise, terrace and stamp references follow the name"),Ref->Guide,FName(TEXT("Lowlands")));TestEqual(TEXT("Unrelated guide is preserved"),Ref->UpperGuide,FName(TEXT("SurfaceMax")));}
    return true;
}
#endif

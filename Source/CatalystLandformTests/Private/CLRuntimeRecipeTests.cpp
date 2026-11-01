#if WITH_DEV_AUTOMATION_TESTS
#include "CLPlanet.h"
#include "Surface/CLSurfaceLibrary.h"
#include "MegaMaterial/VoxelMegaMaterial.h"
#include "Model/CLRegionGeneration.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "JsonObjectConverter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRuntimeRecipeApplyTest,"Catalyst.Landform.RuntimeRecipe.AtomicApplication",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRuntimeRecipeApplyTest::RunTest(const FString&)
{
    FString Json,Error;
    FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/TerrainSurfaces.json")));
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("CLRuntimeRecipeTest"),nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Planet=World->SpawnActor<ACLPlanet>();Planet->bCreateRuntimeOnBeginPlay=false;
    if(!TestTrue(TEXT("Portable JSON applies without an authored planet recipe asset"),Planet->ApplyRecipeJson(Json,Error)))AddError(Error);
    if(Planet->GetSamplingRuntime()&&Planet->RecipeAsset)
    {
        const auto Before=Planet->GetSamplingRuntime();const auto* Input=Planet->RecipeAsset.Get();const auto Material=Planet->MegaMaterial;
        TestTrue(TEXT("Runtime presentation uses the configured client material"),Material==GetDefault<UCLSurfaceSettings>()->PlanetMaterial.LoadSynchronous());
        TestTrue(TEXT("Portable input owns guide values without editor catalog/source links"),!Input->RadialGuideLibrary&&Input->PaletteSources.IsEmpty()&&Input->LandformSources.IsEmpty());
        const FVector Direction=FVector(1,2,3).GetSafeNormal();const double OldHeight=Before->RecipePlan->SampleHeightMeters(Direction);
        auto Invalid=Input->Recipe;Invalid.SurfaceStack.DefaultSurfaceId=TEXT("UnavailableSurface");FString Bad;FJsonObjectConverter::UStructToJsonObjectString(Invalid,Bad);
        TestFalse(TEXT("Missing client content rejects replacement"),Planet->ApplyRecipeJson(Bad,Error));TestTrue(TEXT("Rejection has an actionable reason"),Error.Contains(TEXT("surface")));
        TestTrue(TEXT("Rejected input leaves the exact accepted runtime, recipe and material intact"),Before==Planet->GetSamplingRuntime()&&Input==Planet->RecipeAsset.Get()&&Material==Planet->MegaMaterial&&OldHeight==Planet->GetSamplingRuntime()->RecipePlan->SampleHeightMeters(Direction));
        auto Changed=Input->Recipe;++Changed.Seed;FJsonObjectConverter::UStructToJsonObjectString(Changed,Bad);
        TestTrue(TEXT("A second server-shaped recipe applies without editor assets"),Planet->ApplyRecipeJson(Bad,Error));
        TestTrue(TEXT("Accepted replacement retires the previous actor snapshot"),Before!=Planet->GetSamplingRuntime()&&Planet->RecipeAsset.Get()!=Input&&Planet->RecipeAsset->Recipe.Seed==Changed.Seed);
        TestTrue(TEXT("Recipe edit changes the generated field"),FMath::Abs(OldHeight-Planet->GetSamplingRuntime()->RecipePlan->SampleHeightMeters(Direction))>.0001);
    }
    Planet->DestroyRuntime();Planet->Destroy();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif

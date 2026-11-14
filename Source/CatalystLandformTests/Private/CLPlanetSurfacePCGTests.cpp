#include "Misc/AutomationTest.h"
#include "PCG/CLPlanetSurfacePCG.h"
#include "CLPlanet.h"
#include "CLPlanetStamp.h"
#include "Model/CLPlanetRecipe.h"
#include "PCGComponent.h"
#include "Components/BoxComponent.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGContext.h"
#include "Data/PCGPointData.h"
#include "HAL/PlatformProcess.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPlanetSurfaceTilesTest,"Catalyst.Landform.PCG.PlanetaryTilesAndRadialFrame",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPlanetSurfaceTilesTest::RunTest(const FString&)
{
    FString Json,Error;FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/TerrainSurfaces.json")));
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* W=UWorld::CreateWorld(EWorldType::Game,false,TEXT("CLSurfaceTilesTest"),nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    auto* Planet=W->SpawnActor<ACLPlanet>();Planet->bCreateRuntimeOnBeginPlay=false;
    if(!Planet->ApplyRecipeJson(Json,Error)){AddError(Error);W->DestroyWorld(false);GEngine->DestroyWorldContext(W);return false;}
    const FTransform Pose(FQuat(FVector(1,2,3).GetSafeNormal(),1.17),FVector(9000,-4567,12000));Planet->SetActorTransform(Pose);
    auto* Owner=W->SpawnActor<AActor>();auto* Component=NewObject<UPCGComponent>(Owner);Owner->AddInstanceComponent(Component);
    TStrongObjectPtr<UPCGGraph> Graph(NewObject<UPCGGraph>());UCLPlanetSurfacePCGSettings* Settings=nullptr;auto* Node=Graph->AddNodeOfType(Settings);Settings->PointsPerSquaredMeter=.003;Settings->Seed=71;Component->SetGraph(Graph.Get());
    const double R=Planet->GetSamplingRuntime()->RecipePlan->RadiusMeters;double WorstResidual=0;
    auto Run=[&](int32 Face,int32 X,int32 Y,int32 N)
    {
        UCLPlanetSurfacePCGSettings::ConfigureRegion(Owner,Planet,Face,X,Y,N);
        FPCGDataCollection Input;auto E=Settings->GetElement();
        TUniquePtr<FPCGContext> C(E->Initialize(FPCGInitializeElementParams(&Input,TWeakInterfacePtr<IPCGGraphExecutionSource>(Component),Node)));if(!TestNotNull(TEXT("Configured PCG execution context"),C.Get()))return TArray<FString>();C->AsyncState.NumAvailableTasks=1;
        int32 Steps=0;while(!E->Execute(C.Get())&&++Steps<10000){FPlatformProcess::Sleep(.001f);}
        TestTrue(TEXT("Surface node completes within bounded batches"),Steps<10000);
        TArray<FString> Keys;
        for(const auto& Tag:C->OutputData.TaggedData)
        {
            const auto* D=Cast<UPCGPointData>(Tag.Data);if(!D)continue;
            TestTrue(TEXT("Each published batch is bounded"),D->GetPoints().Num()<=65536);
            for(const auto& P:D->GetPoints())
            {
                const FVector B=Pose.InverseTransformPosition(P.Transform.GetLocation())*.01,Dir=B.GetSafeNormal();
                WorstResidual=FMath::Max(WorstResidual,FMath::Abs(B.Length()-R-Planet->GetSamplingRuntime()->RecipePlan->SampleHeightMeters(Dir)));
                Keys.Add(FString::Printf(TEXT("%.9f,%.9f,%.9f,%d"),Dir.X,Dir.Y,Dir.Z,P.Seed));
            }
        }
        Keys.Sort();return Keys;
    };
    Planet->RebuildTerrain();const FString InitialKey=Planet->GetSurfaceGenerationKey();
    Planet->RebuildTerrain();TestEqual(TEXT("Identical accepted inputs keep their surface identity"),Planet->GetSurfaceGenerationKey(),InitialKey);
    auto* Region=Owner->FindComponentByClass<UCLPlanetSurfaceRegion>();
    if(!Region)Region=UCLPlanetSurfacePCGSettings::ConfigureRegion(Owner,Planet,0,0,0,4);
    FString Status;TestFalse(TEXT("Old products without an accepted identity block walking"),CLPlanetSurfaceRegionsReady(Planet,Status));
    Component->bGenerated=true;Region->GeneratedSurfaceKey=InitialKey;TestTrue(TEXT("Current region permits walking"),CLPlanetSurfaceRegionsReady(Planet,Status));
    Planet->SetActorLocation(Pose.GetLocation()+FVector(1,0,0));Planet->RebuildTerrain();
    TestNotEqual(TEXT("Frame change invalidates accepted scenery"),Planet->GetSurfaceGenerationKey(),InitialKey);
    TestFalse(TEXT("Stale region blocks walking"),CLPlanetSurfaceRegionsReady(Planet,Status));
    Planet->SetActorTransform(Pose);Planet->RebuildTerrain();
    int32 Count=0;
    for(int32 Face=0;Face<6;++Face)
    {
        auto Whole=Run(Face,0,0,1);TArray<FString> Tiles;
        for(int32 X=0;X<4;++X)for(int32 Y=0;Y<4;++Y)Tiles.Append(Run(Face,X,Y,4));Tiles.Sort();
        TestTrue(TEXT("Tiling creates neither omissions nor duplicate candidates"),Whole==Tiles);Count+=Whole.Num();
    }
    TestTrue(TEXT("All six planetary faces produce surface samples"),Count>100);
    TestTrue(TEXT("Roots agree with independently resampled native surface within one millimetre"),WorstResidual<.001);
    AddInfo(FString::Printf(TEXT("%d unique planetary candidates; worst native scalar resampling residual %.12g metres"),Count,WorstResidual));
    Planet->DestroyRuntime();Owner->Destroy();Planet->Destroy();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);return !HasAnyErrors();
}

namespace
{
class FCLWaitForSurfaceRefresh final : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    UWorld* World;
    TWeakObjectPtr<ACLPlanet> Planet;
    TWeakObjectPtr<UCLPlanetSurfaceRegion> Region;
    double Started=FPlatformTime::Seconds();
    FString FirstKey;
    int32 Phase=0;
    void Cleanup()
    {
        if(Planet.IsValid())Planet->DestroyRuntime();
        World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
public:
    FCLWaitForSurfaceRefresh(FAutomationTestBase* T,UWorld* W,ACLPlanet* P,UCLPlanetSurfaceRegion* R):Test(T),World(W),Planet(P),Region(R){}
    bool Update() override
    {
        World->Tick(LEVELTICK_All,.033f);
        if(FPlatformTime::Seconds()-Started>60)
        {Test->AddError(TEXT("Automatic native region regeneration timed out"));Cleanup();return true;}
        FString Status;
        if(!CLPlanetSurfaceRegionsReady(Planet.Get(),Status))return false;
        if(Phase++==0)
        {
            FirstKey=Region->GeneratedSurfaceKey;
            Planet->RecipeAsset->Recipe.RadiusMeters+=10;
            Planet->RebuildTerrain();
            Test->TestNotEqual(TEXT("Accepted shape edit changes the surface identity"),Planet->GetSurfaceGenerationKey(),FirstKey);
            Test->TestFalse(TEXT("Walking blocks stale scenery immediately after editing"),CLPlanetSurfaceRegionsReady(Planet.Get(),Status));
            return false;
        }
        Test->TestNotEqual(TEXT("The native queue regenerated after a live shape edit"),Region->GeneratedSurfaceKey,FirstKey);
        Test->TestEqual(TEXT("Completed products carry the new accepted identity"),Region->GeneratedSurfaceKey,Planet->GetSurfaceGenerationKey());
        Test->AddInfo(TEXT("Native editor PCG generated, accepted a radius edit, and automatically regenerated without manual Generate."));
        Cleanup();return true;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPlanetSurfaceRefreshTest,"Catalyst.Landform.PCG.AutomaticAcceptedSurfaceRefresh",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPlanetSurfaceRefreshTest::RunTest(const FString&)
{
    FString Json,Error;FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/TerrainSurfaces.json")));
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* W=UWorld::CreateWorld(EWorldType::Editor,false,TEXT("CLAutomaticSurfaceTest"),nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Editor).SetCurrentWorld(W);
    auto* P=W->SpawnActor<ACLPlanet>();
    if(!P->ApplyRecipeJson(Json,Error)){AddError(Error);W->DestroyWorld(false);GEngine->DestroyWorldContext(W);return false;}
    P->CreateRuntime();
    auto* Owner=W->SpawnActor<AActor>();auto* Root=NewObject<UBoxComponent>(Owner);Root->SetBoxExtent(FVector(20000));Owner->SetRootComponent(Root);Owner->AddInstanceComponent(Root);Root->RegisterComponent();
    auto* Component=NewObject<UPCGComponent>(Owner);Owner->AddInstanceComponent(Component);Component->RegisterComponent();
    auto* Graph=NewObject<UPCGGraph>(Owner);UCLPlanetSurfacePCGSettings* Settings=nullptr;auto* Node=Graph->AddNodeOfType(Settings);Settings->PointsPerSquaredMeter=.001;
    Graph->AddEdge(Node,PCGPinConstants::DefaultOutputLabel,Graph->GetOutputNode(),PCGPinConstants::DefaultOutputLabel);Component->SetGraph(Graph);
    auto* Region=UCLPlanetSurfacePCGSettings::ConfigureRegion(Owner,P,0,0,0,4);
    ADD_LATENT_AUTOMATION_COMMAND(FCLWaitForSurfaceRefresh(this,W,P,Region));return true;
}

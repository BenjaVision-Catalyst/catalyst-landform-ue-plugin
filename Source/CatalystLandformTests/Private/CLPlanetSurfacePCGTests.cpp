#include "Misc/AutomationTest.h"
#include "PCG/CLPlanetSurfacePCG.h"
#include "CLPlanet.h"
#include "CLPlanetStamp.h"
#include "Model/CLPlanetRecipe.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGContext.h"
#include "Data/PCGPointData.h"
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
        int32 Steps=0;while(!E->Execute(C.Get())&&++Steps<10000){}
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

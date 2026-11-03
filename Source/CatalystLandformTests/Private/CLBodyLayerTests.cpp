#if WITH_DEV_AUTOMATION_TESTS
#include "CLPlanet.h"
#include "VoxelLayer.h"
#include "VoxelLayerStack.h"
#include "VoxelStampComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLBodyLayerTest,"Catalyst.Landform.NativeGeneration.BodyLayerRouting",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLBodyLayerTest::RunTest(const FString&)
{
    auto V=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* W=UWorld::CreateWorld(EWorldType::Game,false,TEXT("CLBodyLayerTest"),nullptr,true,ERHIFeatureLevel::Num,&V);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    auto* A=W->SpawnActor<ACLPlanet>();auto* B=W->SpawnActor<ACLPlanet>();
    TestTrue(TEXT("Existing planets retain native default layer"),A->TerrainStamp->GetStamp().AsChecked<FCLPlanetStamp>().Layer==UVoxelVolumeLayer::Default());
    auto* LA=NewObject<UVoxelVolumeLayer>(A);auto* LB=NewObject<UVoxelVolumeLayer>(B);
    auto* SA=NewObject<UVoxelLayerStack>(A);SA->HeightLayers.Reset();SA->VolumeLayers={LA};auto* SB=NewObject<UVoxelLayerStack>(B);SB->HeightLayers.Reset();SB->VolumeLayers={LB};
    A->TerrainLayer=LA;A->LayerStack=SA;B->TerrainLayer=LB;B->LayerStack=SB;A->RebuildTerrain();B->RebuildTerrain();
    TestTrue(TEXT("Each native stamp enters its selected VPP layer"),A->TerrainStamp->GetStamp().AsChecked<FCLPlanetStamp>().Layer==LA&&B->TerrainStamp->GetStamp().AsChecked<FCLPlanetStamp>().Layer==LB&&LA!=LB);
    TestTrue(TEXT("Each native world selects only its own body layer"),SA->VolumeLayers.Num()==1&&SA->VolumeLayers[0]==LA&&SB->VolumeLayers.Num()==1&&SB->VolumeLayers[0]==LB);
    A->TerrainLayer=nullptr;A->LayerStack=UVoxelLayerStack::Default();A->RebuildTerrain();TestTrue(TEXT("Null layer restores legacy default semantics"),A->TerrainStamp->GetStamp().AsChecked<FCLPlanetStamp>().Layer==UVoxelVolumeLayer::Default());
    A->Destroy();B->Destroy();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);return true;
}
#endif

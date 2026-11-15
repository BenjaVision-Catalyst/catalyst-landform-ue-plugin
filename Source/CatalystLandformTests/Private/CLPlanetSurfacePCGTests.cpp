#include "Misc/AutomationTest.h"
#include "PCG/CLPlanetSurfacePCG.h"
#include "CLPlanet.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGContext.h"
#include "Data/PCGPointData.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLNativePlanetContextTest,"Catalyst.Landform.PCG.NativePlanetContextAndRadialFrame",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLNativePlanetContextTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto* W=UWorld::CreateWorld(EWorldType::Game,false,TEXT("CLNativePCGContextTest"),nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    auto* Planet=W->SpawnActor<ACLPlanet>();Planet->bCreateRuntimeOnBeginPlay=false;Planet->SetActorLocation(FVector(9000,-4567,12000));
    auto* Owner=W->SpawnActor<AActor>();auto* Region=UCLPlanetPCGLibrary::BindSource(Owner,Planet);TestNotNull(TEXT("Native execution source binds the explicit Planet"),Region);
    auto* Component=NewObject<UPCGComponent>(Owner);Owner->AddInstanceComponent(Component);Component->GenerationTrigger=EPCGComponentGenerationTrigger::GenerateAtRuntime;
    TestTrue(TEXT("Generation remains owned by the native runtime system"),Component->IsManagedByRuntimeGenSystem());
    TStrongObjectPtr<UPCGGraph> Graph(NewObject<UPCGGraph>());UCLPlanetRadialOrientationPCGSettings* Settings=nullptr;auto* Node=Graph->AddNodeOfType(Settings);Component->SetGraph(Graph.Get());
    auto* Input=NewObject<UPCGPointData>();auto& P=Input->GetMutablePoints().AddDefaulted_GetRef();const FVector Up=FVector(1,2,3).GetSafeNormal();P.Transform.SetLocation(Planet->GetActorLocation()+Up*50000);P.Seed=1729;
    FPCGDataCollection In;In.TaggedData.AddDefaulted_GetRef().Data=Input;In.TaggedData[0].Pin=PCGPinConstants::DefaultInputLabel;
    auto E=Settings->GetElement();TUniquePtr<FPCGContext> C(E->Initialize(FPCGInitializeElementParams(&In,TWeakInterfacePtr<IPCGGraphExecutionSource>(Component),Node)));C->AsyncState.NumAvailableTasks=1;
    int32 Steps=0;bool Complete=false;while(!(Complete=E->Execute(C.Get()))&&++Steps<100){}
    TestTrue(TEXT("Context annotation completes through native PCG execution phases"),Complete);
    if(TestEqual(TEXT("Native point data remains one batch"),C->OutputData.TaggedData.Num(),1))
    {
        const auto* Out=Cast<UPCGPointData>(C->OutputData.TaggedData[0].Data);
        if(TestNotNull(TEXT("PCG output remains native point data"),Out)&&TestEqual(TEXT("No points generated or removed by orientation"),Out->GetPoints().Num(),1))
        {const auto& Q=Out->GetPoints()[0];TestTrue(TEXT("Native world-space root remains unchanged"),Q.Transform.GetLocation().Equals(P.Transform.GetLocation(),1.e-8));TestTrue(TEXT("Up follows the explicit Planet centre"),Q.Transform.GetRotation().GetAxisZ().Equals(Up,1.e-8));TestEqual(TEXT("Native deterministic seed remains unchanged"),Q.Seed,P.Seed);}
    }
    Owner->Destroy();Planet->Destroy();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);return !HasAnyErrors();
}

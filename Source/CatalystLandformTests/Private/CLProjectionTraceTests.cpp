#include "Misc/AutomationTest.h"
#include "Presentation/CLProjectionTrace.h"
#include "Presentation/CLProjectionRay.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Actor.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLProjectionTraceTest,"Catalyst.Landform.Presentation.CurvedCollisionTrace",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLProjectionTraceTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("CLTraceTest"),nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    for(double C:{0.,.5,1.})
    {
        FCLPlanetProjectionSettings S;S.ReferenceRadiusCm=1000;S.Curvature=C;S.MinAltitudeCm=-400;S.MaxAltitudeCm=400;
        FCLPlanetProjection P;FCLPlanetProjection::TryCreate(S,P);
        const FVector Origin(0,0,1050);FCLProjectionRay Ray;
        if(!FCLProjectionRay::TryCreate(P,Origin,FVector::XAxisVector,600,Ray)){AddError(TEXT("Test ray rejected"));break;}
        auto Box=[&](double T,double Offset)
        {
            FVector Position;Ray.TryGetCanonicalPoint(T,Position);Position+=Offset*Position.GetSafeNormal();
            auto* Actor=World->SpawnActor<AActor>();auto* Shape=NewObject<UBoxComponent>(Actor);Actor->SetRootComponent(Shape);
            Shape->SetBoxExtent(FVector(1,1,1));Shape->SetWorldLocation(Position);Shape->SetCollisionProfileName(TEXT("BlockAllDynamic"));Shape->RegisterComponent();return Actor;
        };
        auto* Near=Box(300,0);auto* Far=Box(450,0);auto* Side=Box(200,10);
        FCollisionQueryParams Params(SCENE_QUERY_STAT(CLTraceTest),false);FCLProjectionTraceSettings Settings;Settings.MaxDistanceCm=600;
        auto Hit=FCLProjectionTrace::Trace(*World,P,Origin,FVector::XAxisVector,ECC_Visibility,Params,Settings);
        TestTrue(TEXT("Curved trace hits nearest on-ray box, ignores nearby off-ray box"),Hit.Status==ECLProjectionTraceStatus::Hit&&Hit.CanonicalHit.GetActor()==Near);
        TestTrue(TEXT("Hit point lies within presented ray tolerance"),Hit.RayErrorCm<=.5);
        TestTrue(TEXT("Canonical collision identity retained"),Hit.CanonicalHit.GetComponent()==Near->GetRootComponent());
        TestTrue(TEXT("Projected hit normal normalized"),Hit.PresentedNormal.IsNormalized());
        TestTrue(TEXT("Bounded query count"),Hit.WorldQueries<=64);
        if(C==0)
        {
            FVector A,B;Ray.TryGetCanonicalPoint(0,A);Ray.TryGetCanonicalPoint(600,B);FHitResult Straight;
            TestFalse(TEXT("Endpoint-only canonical chord misses thin curved target"),World->LineTraceSingleByChannel(Straight,A,B,ECC_Visibility,Params));
            Settings.MaxWorldQueries=1;auto Budget=FCLProjectionTrace::Trace(*World,P,Origin,FVector::XAxisVector,ECC_Visibility,Params,Settings);
            TestTrue(TEXT("Insufficient budget is explicit, never a clear miss"),Budget.Status==ECLProjectionTraceStatus::BudgetExceeded&&Budget.WorldQueries==1);
            Settings.MaxWorldQueries=64;Settings.MaxDepth=0;auto Depth=FCLProjectionTrace::Trace(*World,P,Origin,FVector::XAxisVector,ECC_Visibility,Params,Settings);
            TestTrue(TEXT("Unresolved envelope is explicit"),Depth.Status==ECLProjectionTraceStatus::UnresolvedContact);Settings.MaxDepth=12;
        }
        if(C==1)TestEqual(TEXT("Natural identity uses one native line trace"),Hit.WorldQueries,1);
        for(int32 Flag=0;Flag<3;++Flag)
        {
            auto Unsafe=Params;if(Flag==0)Unsafe.bFindInitialOverlaps=false;if(Flag==1)Unsafe.bIgnoreBlocks=true;if(Flag==2)Unsafe.bSkipNarrowPhase=true;
            const auto Refused=FCLProjectionTrace::Trace(*World,P,Origin,FVector::XAxisVector,ECC_Visibility,Unsafe,Settings);
            TestTrue(TEXT("Unsafe broad-phase/overlap flags refused before querying"),Refused.Status==ECLProjectionTraceStatus::InvalidRequest&&Refused.WorldQueries==0);
        }
        auto Miss=FCLProjectionTrace::Trace(*World,P,Origin,-FVector::YAxisVector,ECC_Visibility,Params,Settings);
        TestTrue(TEXT("Clear curve returns a certified collision miss"),Miss.Status==ECLProjectionTraceStatus::Miss);
        Near->Destroy();Far->Destroy();Side->Destroy();
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}

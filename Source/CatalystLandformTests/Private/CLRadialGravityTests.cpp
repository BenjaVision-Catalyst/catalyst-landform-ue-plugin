#if WITH_DEV_AUTOMATION_TESTS
#include "CLRadialGravityMovement.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRadialGravityOriginTest,
    "Catalyst.Landform.Gravity.LiveOriginFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCLRadialGravityOriginTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("CLGravityOriginTest"),nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    const FVector Center(1300,-700,200);
    for(const FVector Direction:{FVector::XAxisVector,FVector::ZAxisVector,FVector(1,2,-3).GetSafeNormal()})
    {
        auto* Character=World->SpawnActor<ACharacter>();
        Character->SetActorLocation(Center+Direction*100000);
        auto* Movement=NewObject<UCLRadialGravityMovement>(Character);
        Character->AddInstanceComponent(Movement);Movement->RegisterComponent();
        Movement->SetUpdatedComponent(Character->GetCapsuleComponent());Movement->PlanetCenter=Center;
        Movement->UpdateLocalGravity();
        Movement->Velocity=Character->GetActorForwardVector()*800;
        const FQuat Rotation=Character->GetActorQuat();const FVector Velocity=Movement->Velocity;
        for(const FIntVector Origin:{FIntVector(50000,-30000,10000),FIntVector::ZeroValue})
        {
            TestTrue(TEXT("Native world accepts origin translation"),World->SetNewWorldOrigin(Origin));
            const FVector ExpectedCenter=Center-FVector(Origin);
            TestTrue(TEXT("Gravity centre translates before any owner tick"),Movement->PlanetCenter.Equals(ExpectedCenter,1.e-8));
            // Stock movement ticks before its owner. No actor refresh is allowed here.
            Movement->UpdateLocalGravity();
            TestTrue(TEXT("First movement update retains radial gravity"),FVector::DotProduct(-Movement->GetGravityDirection(),Direction)>.999999999);
            TestTrue(TEXT("First movement update preserves heading"),Character->GetActorQuat().Equals(Rotation,1.e-8));
            TestTrue(TEXT("Translation preserves velocity"),Movement->Velocity.Equals(Velocity,1.e-8));
        }
        Character->Destroy();
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif

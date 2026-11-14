#include "CLPlanetWalker.h"
#include "CLPlanet.h"
#include "CLRadialGravityMovement.h"
#include "CLSurfaceInspection.h"
#include "Scene/CLScene.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Collision/VoxelCollisionInvoker.h"
#include "Collision/VoxelCollisionComponent.h"
#include "Collision/VoxelStaticMeshCollisionComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

ACLPlanetWalker::ACLPlanetWalker(const FObjectInitializer& Initializer)
    : Super(Initializer.SetDefaultSubobjectClass<UCLRadialGravityMovement>(ACharacter::CharacterMovementComponentName))
{
    PrimaryActorTick.bCanEverTick=true;
    GetCapsuleComponent()->InitCapsuleSize(34,88);
    bUseControllerRotationPitch=bUseControllerRotationYaw=bUseControllerRotationRoll=false;
    auto* Movement=RadialMovement();
    Movement->bOrientRotationToMovement=false;Movement->bUseControllerDesiredRotation=false;
    Movement->MaxWalkSpeed=2000;Movement->JumpZVelocity=350;Movement->AirControl=.2;
    Movement->SetWalkableFloorAngle(38);Movement->MaxStepHeight=45;
    Camera=CreateDefaultSubobject<UCameraComponent>(TEXT("InspectionCamera"));
    Camera->SetupAttachment(GetCapsuleComponent());Camera->SetRelativeLocation(FVector(0,0,64));
    Camera->bUsePawnControlRotation=false;
    Camera->PostProcessSettings.bOverride_AutoExposureBias=true;
    Camera->PostProcessSettings.AutoExposureBias=-.75;
    CollisionInvoker=CreateDefaultSubobject<UVoxelCollisionInvokerComponent>(TEXT("InspectionCollision"));
    CollisionInvoker->SetupAttachment(GetCapsuleComponent());CollisionInvoker->Radius=2500;
    CollisionInvoker->bWaitForVoxelWorld=true;
    FootCollisionInvoker=CreateDefaultSubobject<UVoxelCollisionInvokerComponent>(TEXT("InspectionFootCollision"));
    FootCollisionInvoker->SetupAttachment(GetCapsuleComponent());
    FootCollisionInvoker->SetRelativeLocation(FVector(0,0,-118));
    FootCollisionInvoker->Radius=100;FootCollisionInvoker->bWaitForVoxelWorld=true;
}

UCLRadialGravityMovement* ACLPlanetWalker::RadialMovement() const
{return CastChecked<UCLRadialGravityMovement>(GetCharacterMovement());}

bool ACLPlanetWalker::BeginInspection(ACLPlanet* Target,FVector Direction,bool HasPoint,FVector Point,FString& Error)
{
    Error.Reset();
    if(Started||!IsValid(Target)||Target->GetWorld()!=GetWorld()||Target->SamplingWindow.bEnabled||Target->DisplayRadiusMeters>0||
        !Target->GetActorScale3D().Equals(FVector::OneVector,.0001)||!Target->GetSamplingRuntime()||
        !Target->GetSamplingRuntime()->RecipePlan||Direction.ContainsNaN()||Direction.IsNearlyZero())
    {Error=TEXT("Walking requires one unscaled native planet in this world with accepted terrain.");return false;}
    const auto& Runtime=*Target->GetSamplingRuntime();FCLSurfaceLanding Landing;
    const bool Found=HasPoint&&Runtime.Scene?FCLSurfaceInspection::FindLandingPoint(*Runtime.Scene,Point,Landing):
        FCLSurfaceInspection::FindLanding(*Runtime.RecipePlan,Direction,Landing);
    if(!Found){Error=TEXT("No safe footing near the selected point. Choose another location.");return false;}
    Planet=Target;LandingAdjustmentMeters=Landing.AdjustmentMeters;PresentationRadiusCm=Landing.PositionCm.Length();
    const FVector Up=Target->GetActorQuat().RotateVector(Landing.Direction);
    FVector Heading=FVector::VectorPlaneProject(Target->GetActorForwardVector(),Up).GetSafeNormal();
    if(Heading.IsNearlyZero())Heading=FVector::CrossProduct(Up,FVector::YAxisVector).GetSafeNormal();
    HoldPosition=Target->GetActorTransform().TransformPosition(Landing.PositionCm)+Up*300;
    auto* Movement=RadialMovement();Movement->StopMovementImmediately();Movement->DisableMovement();
    Movement->SetComponentTickEnabled(false);Movement->PlanetCenter=Target->BodyCenterWorld();Movement->CollisionWorld=Target;
    SetActorLocationAndRotation(HoldPosition,FRotationMatrix::MakeFromXZ(Heading,Up).ToQuat(),false,nullptr,ETeleportType::TeleportPhysics);
    if(auto* PC=GetWorld()->GetFirstPlayerController();PC && PC->PlayerCameraManager)
        PC->PlayerCameraManager->AddNewCameraModifier(UCLInspectionPresentationCamera::StaticClass());
    Started=FPlatformTime::Seconds();Status=TEXT("Loading terrain collision…");return true;
}

bool ACLPlanetWalker::IsPlanetFloor(const FHitResult& Hit) const
{
    const auto* C=Hit.GetComponent();return C&&C->GetOwner()==Planet&&
        (C->IsA<UVoxelCollisionComponent>()||C->IsA<UVoxelStaticMeshCollisionComponent>());
}
void ACLPlanetWalker::Fail(const FString& Error)
{
    bFailed=true;bReady=false;bPresented=false;Status=Error;
    RadialMovement()->StopMovementImmediately();RadialMovement()->DisableMovement();
    RadialMovement()->SetComponentTickEnabled(false);
    UE_LOG(LogTemp,Warning,TEXT("LANDFORM_WALK_FAILED: %s"),*Error);
}

void ACLPlanetWalker::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);if(!Started||bFailed||!IsValid(Planet))return;
    auto* Movement=RadialMovement();const double Now=FPlatformTime::Seconds();
    if(!Released)
    {
        if(!GetActorLocation().Equals(HoldPosition,.01)){Fail(TEXT("Character moved before collision was ready."));return;}
        if(Now-Started>120){Fail(TEXT("Terrain collision did not become ready. Press Esc and choose another point."));return;}
        if(!Planet->IsVoxelWorldReady()||Planet->IsProcessingNewState())return;
        const FVector Up=(HoldPosition-Planet->BodyCenterWorld()).GetSafeNormal();
        FCollisionQueryParams Params(SCENE_QUERY_STAT(LandformInspectionLanding),false);Params.AddIgnoredActor(this);
        FHitResult Hit;
        if(!GetWorld()->LineTraceSingleByChannel(Hit,HoldPosition+Up*500,HoldPosition-Up*2000,ECC_Visibility,Params)||!IsPlanetFloor(Hit))return;
        const FVector Local=Planet->GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);
        const auto& Runtime=Planet->GetSamplingRuntime();
        if(!Runtime||!Runtime->Scene)return;
        const FVector Point=Local*.01;TArray<FCLSceneShapeSample> Samples;FString Error;
        if(!Runtime->Scene->SampleShape({MakeArrayView(&Point,1)},Samples,Error)||Samples.IsEmpty()||Samples[0].State!=ECLSceneSampleState::Known)return;
        InitialFloorErrorCm=FMath::Abs(Samples[0].FieldMeters)*100;
        if(InitialFloorErrorCm>Planet->VoxelSize)return;
        Movement->UpdateLocalGravity();
        if(!Movement->IsWalkable(Hit)){Fail(TEXT("Native terrain is too steep here. Press Esc and choose another point."));return;}
        const auto* Capsule=GetCapsuleComponent();
        if(GetWorld()->OverlapBlockingTestByChannel(HoldPosition,GetActorQuat(),ECC_Pawn,
            FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()),Params))
        {Fail(TEXT("There is no clearance above this surface. Press Esc and choose another point."));return;}
        Movement->SetMovementMode(MOVE_Falling);Movement->SetComponentTickEnabled(true);Released=Now;
        Status=TEXT("Landing…");return;
    }
    if(!bReady)
    {
        if(Now-Released>8){Fail(TEXT("Could not establish native ground contact. Press Esc to return."));return;}
        if(!Movement->IsMovingOnGround()||!IsPlanetFloor(Movement->CurrentFloor.HitResult)){Grounded=0;return;}
        if(!Grounded)Grounded=Now;
        if(Now-Grounded<.25)return;
        bReady=true;Status=TEXT("WASD · Mouse look · Space jump · Esc return");
        UE_LOG(LogTemp,Display,TEXT("LANDFORM_WALK_READY: %s | adjustment %.2f m | floor error %.2f cm"),*Planet->GetPathName(),LandingAdjustmentMeters,InitialFloorErrorCm);
    }
    Camera->SetRelativeRotation(FRotator(Pitch,0,0));
    AddMovementInput(GetActorForwardVector(),float(bForward)-float(bBackward));
    AddMovementInput(GetActorRightVector(),float(bRight)-float(bLeft));
}
void ACLPlanetWalker::LookYaw(float Value){if(bReady)AddActorLocalRotation(FRotator(0,Value*.15,0));}
void ACLPlanetWalker::LookPitch(float Value){if(bReady)Pitch=FMath::Clamp(Pitch+Value*.15f,-85.f,85.f);}
void ACLPlanetWalker::StartJump(){if(bReady)Jump();}
void ACLPlanetWalker::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindKey(EKeys::W,IE_Pressed,this,&ACLPlanetWalker::ForwardPressed);Input->BindKey(EKeys::W,IE_Released,this,&ACLPlanetWalker::ForwardReleased);
    Input->BindKey(EKeys::S,IE_Pressed,this,&ACLPlanetWalker::BackwardPressed);Input->BindKey(EKeys::S,IE_Released,this,&ACLPlanetWalker::BackwardReleased);
    Input->BindKey(EKeys::D,IE_Pressed,this,&ACLPlanetWalker::RightPressed);Input->BindKey(EKeys::D,IE_Released,this,&ACLPlanetWalker::RightReleased);
    Input->BindKey(EKeys::A,IE_Pressed,this,&ACLPlanetWalker::LeftPressed);Input->BindKey(EKeys::A,IE_Released,this,&ACLPlanetWalker::LeftReleased);
    Input->BindAxisKey(EKeys::MouseX,this,&ACLPlanetWalker::LookYaw);Input->BindAxisKey(EKeys::MouseY,this,&ACLPlanetWalker::LookPitch);
    Input->BindKey(EKeys::SpaceBar,IE_Pressed,this,&ACLPlanetWalker::StartJump);
    Input->BindKey(EKeys::SpaceBar,IE_Released,this,&ACLPlanetWalker::StopJumping);
}
ACLPlanetInspectionGameMode::ACLPlanetInspectionGameMode(){DefaultPawnClass=nullptr;HUDClass=ACLPlanetInspectionHUD::StaticClass();}
void ACLPlanetInspectionHUD::DrawHUD()
{
    Super::DrawHUD();
    if(const auto* Pawn=Cast<ACLPlanetWalker>(GetOwningPawn()))DrawText(Pawn->Status,FLinearColor::White,24,24,nullptr,1.2f);
}

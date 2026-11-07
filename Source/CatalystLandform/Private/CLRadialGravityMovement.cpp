#include "CLRadialGravityMovement.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "VoxelWorld.h"

void UCLRadialGravityMovement::UpdateLocalGravity()
{
    if (!UpdatedComponent) return;
    const FVector Up = (UpdatedComponent->GetComponentLocation() - PlanetCenter).GetSafeNormal();
    if (Up.IsNearlyZero()) return;
    SetGravityDirection(-Up);
    const FQuat Transport = FQuat::FindBetweenNormals(UpdatedComponent->GetUpVector(), Up);
    const FQuat Rotation = (Transport * UpdatedComponent->GetComponentQuat()).GetNormalized();
    FHitResult Hit;
    MoveUpdatedComponent(FVector::ZeroVector, Rotation, true, &Hit);
}

void UCLRadialGravityMovement::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    if(!CanMoveOverNativeCollision(DeltaTime))
    {
        ++CollisionWaitFrames;CollisionWaitSeconds+=DeltaTime;
        StopMovementImmediately();ConsumeInputVector();return;
    }
    UpdateLocalGravity();
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    UpdateLocalGravity();
}

void UCLRadialGravityMovement::ApplyWorldOffset(const FVector& InOffset, bool bWorldShift)
{
    Super::ApplyWorldOffset(InOffset, bWorldShift);
    // Movement ticks before its owner. Translate this world-space cache now,
    // before the first movement step can use the previous origin's centre.
    if (bWorldShift) PlanetCenter += InOffset;
}

bool UCLRadialGravityMovement::CanMoveOverNativeCollision(float DeltaTime) const
{
    if(!CollisionWorld||CollisionWorld->IsActorBeingDestroyed()||!CharacterOwner||!UpdatedComponent||
       !IsMovingOnGround()||!CollisionWorld->IsProcessingNewState())return true;
    const FVector Input=GetPendingInputVector();const FVector Motion=Input.IsNearlyZero()?Velocity:Input;
    if(Motion.IsNearlyZero())return true;
    const FVector Here=UpdatedComponent->GetComponentLocation(),Up=(Here-PlanetCenter).GetSafeNormal();
    const FVector Ahead=FVector::VectorPlaneProject(Motion,Up).GetSafeNormal();if(Ahead.IsNearlyZero())return true;
    const double Half=CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CLRadialCollisionAdmission),false);Params.AddIgnoredActor(CharacterOwner);
    auto HasWalkingFloor=[&](const FVector& Center,FHitResult& Hit)
    {
        const FVector LocalUp=(Center-PlanetCenter).GetSafeNormal();
        return GetWorld()->LineTraceSingleByChannel(Hit,Center+LocalUp*MaxStepHeight,
            Center-LocalUp*(Half+MaxStepHeight*2.),ECC_Visibility,Params)&&IsWalkable(Hit);
    };
    FHitResult Current;if(!HasWalkingFloor(Here,Current)||Current.GetActor()!=CollisionWorld)return true;
    const double LookAhead=FMath::Clamp(GetMaxSpeed()*double(DeltaTime)+CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius()*2.,150.,300.);
    FHitResult Next;
    // A native "computed chunk" may still await publication to physics. Keep
    // the last genuinely supported walking pose until the next native floor
    // exists. No replacement floor, gravity change or position correction.
    return HasWalkingFloor(Here+Ahead*LookAhead,Next);
}

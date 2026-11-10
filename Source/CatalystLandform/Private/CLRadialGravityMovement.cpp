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
    if (bWorldShift) {PlanetCenter += InOffset;CollisionWaitProbe += InOffset;}
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
    auto HasPhysicsSurface=[&](const FVector& Center,FHitResult& Hit)
    {
        const FVector LocalUp=(Center-PlanetCenter).GetSafeNormal();
        return GetWorld()->LineTraceSingleByChannel(Hit,Center+LocalUp*MaxStepHeight,
            Center-LocalUp*(Half+MaxStepHeight*2.),ECC_Visibility,Params);
    };
    FHitResult Current;if(!HasPhysicsSurface(Here,Current)||!IsWalkable(Current)||Current.GetActor()!=CollisionWorld)return true;
    // A blocking surface is generated physics even when it is too steep to
    // walk. Native CharacterMovement owns slope, wall and step resolution;
    // only a missing surface can be a publication gap.
    // Check the capsule's possible movement in this frame, not a distant
    // point beyond a wall, corner or intended turn. Resolve blocking geometry
    // with the same native slide projection used by CharacterMovement.
    const double FrameTime=FMath::Max(0.,double(DeltaTime));
    const FVector Momentum=FVector::VectorPlaneProject(Velocity,Up)*FrameTime;
    const FVector Desired=Ahead*(FMath::Max(double(GetMaxSpeed()),Velocity.Length())*FrameTime);
    const auto* Capsule=CharacterOwner->GetCapsuleComponent();
    auto HasSupportedStep=[&](const FVector& Delta)
    {
        if(Delta.IsNearlyZero())return true;
        FVector Destination=Here+Delta;FHitResult Blocking,Next;
        if(HasPhysicsSurface(Destination,Next))return true;
        if(GetWorld()->SweepSingleByChannel(Blocking,Here,Destination,UpdatedComponent->GetComponentQuat(),ECC_Visibility,
            FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Half),Params))
        {
            // Capsule perching can still touch the old chunk after its centre
            // leaves published support. Do not accept that as a safe new step.
            if(Blocking.bStartPenetrating){CollisionWaitProbe=Destination;return false;}
            const FVector Slide=ComputeSlideVector(Delta,1.-Blocking.Time,Blocking.Normal,Blocking);
            Destination=Here+Delta*FMath::Max(0.,double(Blocking.Time)-.001)+Slide;
            if(FVector::DistSquared(Here,Destination)<1.)return true; // Ordinary wall contact.
        }
        if(HasPhysicsSurface(Destination,Next))return true;
        CollisionWaitProbe=Destination;return false;
    };
    // Turning input does not immediately cancel existing momentum. Both ends
    // must retain real centre support; native movement owns sliding and steps.
    return HasSupportedStep(Desired)&&(Momentum.Equals(Desired,.01)||HasSupportedStep(Momentum));
}

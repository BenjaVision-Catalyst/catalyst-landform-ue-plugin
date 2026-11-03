#include "CLRadialGravityMovement.h"

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

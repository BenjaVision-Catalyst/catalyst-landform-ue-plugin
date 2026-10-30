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

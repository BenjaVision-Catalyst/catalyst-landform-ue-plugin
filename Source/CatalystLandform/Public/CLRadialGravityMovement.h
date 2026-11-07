#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "CLRadialGravityMovement.generated.h"

class AVoxelWorld;

/** One planet's local gravity. Heading is parallel transported as up changes. */
UCLASS(ClassGroup=(Landform), meta=(BlueprintSpawnableComponent))
class CATALYSTLANDFORM_API UCLRadialGravityMovement : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Landform|Gravity")
    FVector PlanetCenter = FVector::ZeroVector;

    /** Bind explicitly when walking streamed native voxel terrain. */
    UPROPERTY(Transient)
    TObjectPtr<AVoxelWorld> CollisionWorld;

    int32 GetCollisionWaitFrames() const {return CollisionWaitFrames;}
    double GetCollisionWaitSeconds() const {return CollisionWaitSeconds;}

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    virtual void ApplyWorldOffset(const FVector& InOffset, bool bWorldShift) override;

    void UpdateLocalGravity();

private:
    bool CanMoveOverNativeCollision(float DeltaTime) const;
    int32 CollisionWaitFrames=0;
    double CollisionWaitSeconds=0;
};

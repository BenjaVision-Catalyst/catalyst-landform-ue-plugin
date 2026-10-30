#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "CLRadialGravityMovement.generated.h"

/** One planet's local gravity. Heading is parallel transported as up changes. */
UCLASS(ClassGroup=(Landform), meta=(BlueprintSpawnableComponent))
class CATALYSTLANDFORM_API UCLRadialGravityMovement : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Landform|Gravity")
    FVector PlanetCenter = FVector::ZeroVector;

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    void UpdateLocalGravity();
};

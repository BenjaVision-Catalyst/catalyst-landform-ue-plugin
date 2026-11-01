#pragma once
#include "CoreMinimal.h"
#include "Model/CLHeightReference.h"
#include "CLPlanetStructure.generated.h"

/** An editorial broad elevation feature, owned by the planet's base structure. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLPlanetFeature
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FGuid Id;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FString Name=TEXT("Broad uplift");
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(DisplayName="Longitude / latitude",Units="deg")) FVector2D LocationDegrees=FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="deg")) double HeadingDegrees=0;
    /** Angular half-widths in the fixed local east/north frame; positive and below a hemisphere. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(DisplayName="Angular half extents",Units="deg")) FVector2D HalfExtentDegrees=FVector2D(45,25);
    /** Signed guide-relative delta: positive uplift, negative basin. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FCLHeightReference Relief;
    FCLPlanetFeature();
};

/** Immutable resolved broad feature. No per-query randomness or mutable authoring objects. */
struct CATALYSTLANDFORM_API FCLCompiledPlanetFeature
{
    FGuid Id;
    FQuat Orientation=FQuat::Identity;
    FVector2D HalfExtentRadians;
    double ReliefMeters=0;
    static FQuat Frame(const FVector2D& LocationDegrees,double HeadingDegrees);
    double Weight(const FVector& Direction) const;
};

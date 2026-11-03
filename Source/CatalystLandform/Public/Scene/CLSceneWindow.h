#pragma once

#include "CoreMinimal.h"
#include "Scene/CLBodyFrame.h"
#include "CLSceneWindow.generated.h"

/** Transient native coverage of a canonical scene. Not a planet recipe or a solid clip. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLSceneWindow
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m")) FVector AnchorMeters=FVector::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FQuat BodyFromLocal=FQuat::Identity;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m")) FVector HalfExtentMeters=FVector(32,32,24);
    FCLBodyFrame Frame() const
    {
        // UE's SIMD GetNormalized is intentionally approximate. Cancel its
        // tiny scale error before deriving a planet-sized body-centre vector.
        FQuat Q=BodyFromLocal;const double N=Q.SizeSquared();
        if(FMath::IsFinite(N)&&N>0)Q*=1./FMath::Sqrt(N);
        return {AnchorMeters,Q};
    }
    bool Validate(FString& Error) const;
    FBox BoundsCm() const {return FBox(-HalfExtentMeters*100,HalfExtentMeters*100);}
};

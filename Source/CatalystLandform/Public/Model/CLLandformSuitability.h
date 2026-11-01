#pragma once
#include "CoreMinimal.h"
#include "CLLandformSuitability.generated.h"

/** Optional placement preferences; no planet guide values, radius or placements. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLLandformSuitability
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bUsePreferences=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="bUsePreferences",ClampMin="0.01",ClampMax="100")) double SelectionWeight=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="bUsePreferences",ClampMin="-1",ClampMax="1")) double UpliftAffinity=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="bUsePreferences",ClampMin="-1",ClampMax="1")) double BasinAffinity=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="bUsePreferences",ClampMin="-1",ClampMax="1")) double StructuralEdgeAffinity=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="bUsePreferences")) FName LowerElevationGuide=TEXT("SurfaceMin");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="bUsePreferences")) FName UpperElevationGuide=TEXT("SurfaceMax");
    /** Relative to the named guide interval, resolved only when a planet is compiled. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="bUsePreferences")) FVector2D PreferredElevation=FVector2D(0,1);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="bUsePreferences",Units="deg")) FVector2D PreferredSlopeDegrees=FVector2D(0,90);
    /** Approximate region diameter / outer planet radius; independent of planet size. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="bUsePreferences")) FVector2D PreferredScale=FVector2D(.01,4);
    bool Validate(FString& Error) const;
};

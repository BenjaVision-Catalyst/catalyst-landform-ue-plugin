#pragma once
#include "CoreMinimal.h"
#include "Curves/RichCurve.h"
#include "CLSurfaceDistribution.generated.h"

/** Area fractions on the neutral spherical surface, not mesh area or voxel count. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLSurfaceDistributionSettings
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 AlgorithmVersion=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Character=.5;
    /** Area occupying the lowest/highest EndBandHeightPercent of the guide interval. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.2",ClampMax="20",Units="%")) double LowEndAreaPercent=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.2",ClampMax="20",Units="%")) double HighEndAreaPercent=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="2",ClampMax="20",Units="%")) double EndBandHeightPercent=10;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="21",ClampMax="79",Units="%")) double TypicalHeightPercent=50;
    /** Interquartile spread toward the low/high end bands, independently of tail area. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="5",ClampMax="95",Units="%")) double SpreadPercent=35;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="8192",ClampMax="262144")) int32 CalibrationSamples=65536;
    void ApplyProgression(double Position);
    bool Validate(FString& Error) const;
    /** X is area below; Y is normalized elevation. Endpoints always remain 0/1. */
    TArray<FVector2D> Quantiles() const;
    static FRichCurve InterpolateMonotone(TConstArrayView<FVector2D> Points);
    FRichCurve TargetCurve()const{return InterpolateMonotone(Quantiles());}
};
struct FCLPlanetRecipe;
/** Authoring operations rewrite existing native noise/stamp settings; no second generator. */
struct CATALYSTLANDFORM_API FCLPhysicalDetailScale
{
    static double MaximumMeters(const FCLPlanetRecipe& Recipe);
    static bool UsesMeters(const FCLPlanetRecipe& Recipe);
    static bool SetMaximumMeters(FCLPlanetRecipe& Recipe,double Maximum,FString& Error);
    static void AddNativeDetail(FCLPlanetRecipe& Recipe);
};

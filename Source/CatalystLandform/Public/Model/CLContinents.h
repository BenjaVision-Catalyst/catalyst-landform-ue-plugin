#pragma once
#include "CoreMinimal.h"
#include "CLContinents.generated.h"

/** Planet-owned crust foundation, independently composed with tectonic deformation. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLContinentalLayer
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Strength=.7;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Seed=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName LowerGuide=TEXT("SurfaceMin");
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName UpperGuide=TEXT("SurfaceMax");
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FRotator Orientation=FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bOverrideExtent=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="bOverrideExtent",ClampMin="0.6",ClampMax="1.4")) double Extent=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bOverrideRelief=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="bOverrideRelief",ClampMin="0",ClampMax="1")) double Relief=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bCoastalMargin=false;
};
/** The same resolved mapping feeds compilation and the Advanced inspector. */
class FCLCompiledTectonics;
struct CATALYSTLANDFORM_API FCLContinentalControls
{
    double Extent=1,Relief=0;
    static FCLContinentalControls Resolve(const FCLContinentalLayer& Layer);
};
class CATALYSTLANDFORM_API FCLCompiledContinents
{
public:
    double MaximumDeltaMeters=0;
    double Continentalness(const FVector& UnitDirection) const;
    double SampleDeltaMeters(const FVector& UnitDirection) const;
    static TSharedPtr<const FCLCompiledContinents> Compile(const FCLContinentalLayer& Layer,int32 PlanetSeed,double SpanMeters,FString& Error,bool Calibrated=false,double DatumOffsetMeters=0);
private:
    TSharedPtr<const FCLCompiledTectonics> LegacyCrust;
    TArray<FVector> Centres;
    FVector NoiseOffset;
    FQuat Frame;
    double Extent=1;
    double Relief=1,SpanMeters=0,DatumOffsetMeters=0;
};

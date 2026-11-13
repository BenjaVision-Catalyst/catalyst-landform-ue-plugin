#pragma once
#include "CoreMinimal.h"
#include "CLRiftLandform.generated.h"
class FCLCompiledTectonics;
class FCLCompiledContinents;
class FCLNativeNoiseKernel;
struct FCLNoiseLayer;
/** Physical ranges curated by Character; zero version preserves existing neutral provinces. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLRiftLandform
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 AlgorithmVersion=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Character=.5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Seed=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="2",Units="m",ForceUnits="m")) double MinimumSpacingMeters=100;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="2",Units="m",ForceUnits="m")) double MaximumSpacingMeters=500;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="m",ForceUnits="m")) double MinimumThrowMeters=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="m",ForceUnits="m")) double MaximumThrowMeters=80;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.1",Units="m",ForceUnits="m")) double MinimumScarpWidthMeters=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.1",Units="m",ForceUnits="m")) double MaximumScarpWidthMeters=15;
    /** Return to the curated physical family; manual ranges remain editable until the next adoption. */
    void ApplyCuratedCharacter(double Value);
    bool IsActive()const{return AlgorithmVersion!=0;}
    bool Validate(FString& Error)const;
    bool ValidateNoise(TConstArrayView<FCLNoiseLayer>,FString& Error)const;
};
/** Immutable secondary fault-block field inside the existing warped extensional belts.
 * Surface relief and restored rock coordinates consume exactly this metre displacement. */
class CATALYSTLANDFORM_API FCLCompiledRiftLandform
{
public:
    static TSharedPtr<const FCLCompiledRiftLandform> Compile(const FCLRiftLandform&,int32 PlanetSeed,double ReferenceRadius,double GuideSpan,TSharedPtr<const FCLCompiledTectonics>,TSharedPtr<const FCLCompiledContinents>,FString& Error);
    double SampleMeters(const FVector& Direction)const;
    double MaximumDisplacementMeters()const{return Throw;}
    double SpacingMeters()const{return Spacing;}
    double ScarpWidthMeters()const{return Width;}
    double SurfaceDetailStrength()const{return FMath::Lerp(.8,.55,Character);}
private:
    double StructuralMeters(const FVector& Direction,double* Confidence=nullptr)const;
    TSharedPtr<const FCLCompiledTectonics> Tectonics;
    TSharedPtr<const FCLCompiledContinents> Crust;
    double Radius=1,Spacing=1,Throw=0,Width=1,Character=.5;
    uint32 Seed=0;

    FVector FallbackX,FallbackY;
    TSharedPtr<const FCLNativeNoiseKernel> Noise;
};

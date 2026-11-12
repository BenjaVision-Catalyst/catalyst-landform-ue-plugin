#pragma once
#include "CoreMinimal.h"
#include "CLTransformLandform.generated.h"
class FCLCompiledTectonics;
struct FCLGeneratedRegionCell;
/** Current physical settings. Character applies the curated progression; widths are half widths. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLTransformLandform
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 AlgorithmVersion=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Character=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Seed=73;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="20",Units="m",ForceUnits="m")) double FeatureLengthMeters=650;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="m",ForceUnits="m")) double ValleyDepthMeters=12;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",Units="m",ForceUnits="m")) double ValleyWidthMeters=75;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="m",ForceUnits="m")) double RidgeHeightMeters=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",Units="m",ForceUnits="m")) double RidgeWidthMeters=55;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="m",ForceUnits="m")) double LateralOffsetMeters=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="m",ForceUnits="m")) double BasinDepthMeters=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",Units="m",ForceUnits="m")) double BasinWidthMeters=65;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double SecondaryStrandWeight=.12;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="m",ForceUnits="m")) double DissectionMeters=1;
    void ApplyProgression(double Position);
    bool IsActive()const{return AlgorithmVersion!=0;}
    bool Validate(FString& Error)const;
};
/** Immutable finite shear corridors. Character changes anatomy without reseeding layout. */
class CATALYSTLANDFORM_API FCLCompiledTransformLandform
{
public:
    struct FCorridor{FVector Centre,Along,Across;double HalfLength=1,Phase=0,Sign=1,Variation=1;bool bPlateGuided=false;};
    struct FSample{FVector TangentOffset=FVector::ZeroVector;double Relief=0;};
    static TSharedPtr<const FCLCompiledTransformLandform> Compile(const FCLTransformLandform&,int32 PlanetSeed,double ReferenceRadius,TConstArrayView<FCLGeneratedRegionCell>,TSharedPtr<const FCLCompiledTectonics>,FString& Error);
    FSample Sample(const FVector&)const;
    double MaximumDisplacementMeters()const;
    const TArray<FCorridor>& GetCorridors()const{return Corridors;}
    double ReferenceRadius()const{return Radius;}
private:
    FCLTransformLandform Settings;
    double Radius=1,Scale=1;
    TArray<FCorridor> Corridors;
};

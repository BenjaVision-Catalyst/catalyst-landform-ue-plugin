#pragma once
#include "CoreMinimal.h"
#include "CLTransformLandform.generated.h"
class FCLCompiledTectonics;
struct FCLGeneratedRegionCell;
/** An authored landscape, not a global intensity preset. Widths are half widths. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLTransformExample
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FString Name;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) double Position=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="m")) double ValleyDepthMeters=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",Units="m")) double ValleyWidthMeters=80;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="m")) double RidgeHeightMeters=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",Units="m")) double RidgeWidthMeters=50;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="m")) double LateralOffsetMeters=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="m")) double BasinDepthMeters=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",Units="m")) double BasinWidthMeters=70;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double SecondaryStrandWeight=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="m")) double DissectionMeters=0;
};
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLTransformLandform
{
    GENERATED_BODY()
    FCLTransformLandform();
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 AlgorithmVersion=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Character=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Seed=73;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="20",Units="m")) double FeatureLengthMeters=650;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,EditFixedSize) TArray<FCLTransformExample> Examples;
    bool IsActive()const{return AlgorithmVersion!=0;}
    bool Validate(FString& Error)const;
    FCLTransformExample Interpolate()const;
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
    FCLTransformExample Form;
    double Radius=1,Scale=1;
    TArray<FCorridor> Corridors;
};

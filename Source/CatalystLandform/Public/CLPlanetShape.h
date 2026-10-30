#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Shape/VoxelShape.h"
#include "CLPlanetShape.generated.h"

/** An ordinary rectangular height stamp. Samples and dimensions use centimetres. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLFlatStampGrid
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stamp")
    FIntPoint Resolution = FIntPoint(65, 65);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stamp", meta=(Units="cm"))
    FVector2D Size = FVector2D(6000, 4000);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stamp")
    TArray<float> Heights;

    bool IsValid() const;
    double Sample(const FVector2D& Coordinates) const;
    double MaxAbsoluteHeight() const;
    static FCLFlatStampGrid MakeProofRidge();
};

UCLASS(BlueprintType)
class CATALYSTLANDFORM_API UCLFlatStampAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    UCLFlatStampAsset();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stamp")
    FCLFlatStampGrid Grid;
};

/** VPP volume shape with a geodesic chart sampling the original flat data.
 * The zero set is the terrain; the radial field is not an exact Euclidean SDF.
 */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLPlanetShape : public FVoxelShape
{
    GENERATED_BODY()
    GENERATED_VIRTUAL_STRUCT_BODY()

    UPROPERTY(EditAnywhere, Category="Planet", meta=(Units="cm", ClampMin="100"))
    double Radius = 10000;

    UPROPERTY(EditAnywhere, Category="Stamp")
    FVector AnchorDirection = FVector::XAxisVector;

    UPROPERTY(EditAnywhere, Category="Stamp")
    FVector ChartXAxis = FVector::ZAxisVector;

    UPROPERTY(EditAnywhere, Category="Stamp")
    FCLFlatStampGrid FlatStamp;

    bool ToChart(const FVector& Direction, FVector2D& Coordinates) const;
    FVector FromChart(const FVector2D& Coordinates) const;
    double HeightAt(const FVector& Direction) const;
    double DistanceAt(const FVector& Position) const;

    virtual FVoxelBox GetLocalBounds() const override;
    virtual void Sample(TVoxelArrayView<float> OutDistances,
        const FVoxelDoubleVectorBuffer& Positions) const override;
};

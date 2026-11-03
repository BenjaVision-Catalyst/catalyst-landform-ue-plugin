#pragma once
#include "CoreMinimal.h"
#include "CLVolumes.generated.h"

struct FCLCompiledSurfaceLibrary;
struct FCLSurfaceWeights;

UENUM(BlueprintType)
enum class ECLSolidPrimitive : uint8 { Sphere, Capsule, Box };
UENUM(BlueprintType)
enum class ECLSolidComposition : uint8 { Union, Subtract };
UENUM(BlueprintType)
enum class ECLVolumeHeightPolicy : uint8 { BaseTerrainOnly };

/** Ordered body-space solid operation. Dimensions and translations are metres. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLSolidOperation
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FGuid Id;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName Name;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 AlgorithmVersion=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) ECLSolidPrimitive Primitive=ECLSolidPrimitive::Sphere;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) ECLSolidComposition Composition=ECLSolidComposition::Union;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m")) FVector CenterMeters=FVector::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FRotator Rotation=FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector Scale=FVector::OneVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="0.01")) double RadiusMeters=5;
    /** Capsule's straight half segment, along local Z. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="0")) double HalfLengthMeters=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m")) FVector HalfExtentsMeters=FVector(5);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="0")) double SmoothnessMeters=0;
    /** Optional material of added rock or a freshly cut contact. None retains geology. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName SurfaceId;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 SurfaceRevision=2;
};

/** Radial rock/soil unit evaluated at XYZ, including underground interfaces. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLGeologicalUnit
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName Name;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName SurfaceId=TEXT("Rock");
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 SurfaceRevision=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector2D RadiusInterval=FVector2D(0,1);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="0")) double FeatherMeters=1;
};

USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLVolumeSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 AlgorithmVersion=1;
    /** Exact SurfaceMin/Max fitting finishes before any volume operations. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite) ECLVolumeHeightPolicy HeightPolicy=ECLVolumeHeightPolicy::BaseTerrainOnly;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(TitleProperty="Name")) TArray<FCLSolidOperation> Operations;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(TitleProperty="Name")) TArray<FCLGeologicalUnit> GeologicalUnits;
};

/** Worker-owned values and BVH. No live recipe, asset or UObject access. */
class CATALYSTLANDFORM_API FCLCompiledVolumes final
{
public:
    struct FOperation
    {
        FCLSolidOperation Source;
        FTransform3d Transform;
        FBox3d Bounds;
        double MinimumScale=1;
        int32 SurfaceIndex=INDEX_NONE;
        double Field(const FVector3d& Position) const;
    };
    struct FUnit {double Low=0,High=0,Feather=0;int32 SurfaceIndex=INDEX_NONE;};
    struct FNode {FBox3d Bounds;int32 Left=INDEX_NONE,Right=INDEX_NONE;uint64 Mask=0;};
    static TSharedPtr<const FCLCompiledVolumes> Compile(const FCLVolumeSettings&,double OuterRadius,
        const FCLCompiledSurfaceLibrary* Surfaces,FString& Error);
    TArray<FOperation> Operations;
    TArray<FUnit> Units;
    TArray<FNode> Index;
    /** Bounds contain the zero set plus smoothing and an air halo. Field pruning additionally requires a conservative distance bound. */
    uint64 Candidates(const FVector3d& Position) const;
    /** At most 64 operations, in authored order; no sorting by transient identity. */
    double Compose(const FVector3d& Position,double BaseField,FCLSurfaceWeights* Weights=nullptr) const;
private:
    int32 BuildIndex(TArray<int32> Items);
};

#pragma once
#include "CoreMinimal.h"
#include "Model/CLHeightReference.h"
#include "CLPlanetStructure.generated.h"

UENUM(BlueprintType)
enum class ECLPlanetFeatureProfile : uint8 { Oval, Heightmap };

/** Planet-owned placement of a reusable Landform, optionally relative to broad structure. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLLandformPlacement
{
    GENERATED_BODY()
    /** Invalid means planet frame; otherwise location/heading are local to this feature. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FGuid StructureFeatureId;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(DisplayName="Longitude / latitude",Units="deg")) FVector2D LocationDegrees=FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="deg")) double HeadingDegrees=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bFollowStructureExtent=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.01",ClampMax="4")) double CoverageScale=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(DisplayName="Angular half extents",Units="deg")) FVector2D HalfExtentDegrees=FVector2D(35,25);
    /** Fraction of each half extent occupied by a smooth quintic transition. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.01",ClampMax="1")) double TransitionFraction=.4;
    /** Source relief relative to the planet's neutral ground, before structure is added once. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Strength=1;
};

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
    UPROPERTY(EditAnywhere,BlueprintReadWrite) ECLPlanetFeatureProfile Profile=ECLPlanetFeatureProfile::Oval;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="Profile == ECLPlanetFeatureProfile::Heightmap",EditConditionHides)) FName CollectionId;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="Profile == ECLPlanetFeatureProfile::Heightmap",EditConditionHides)) int32 CollectionRevision=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="Profile == ECLPlanetFeatureProfile::Heightmap",EditConditionHides)) FName SourceId;
    FCLPlanetFeature();
};

class FCLCompiledStampDistribution;

/** Immutable resolved broad feature. No per-query randomness or mutable authoring objects. */
struct CATALYSTLANDFORM_API FCLCompiledPlanetFeature
{
    FGuid Id;
    FQuat Orientation=FQuat::Identity;
    FVector2D HalfExtentRadians;
    double ReliefMeters=0;
    TSharedPtr<const FCLCompiledStampDistribution> Heightmap;
    static FQuat Frame(const FVector2D& LocationDegrees,double HeadingDegrees);
    double Weight(const FVector& Direction) const;
};

/** Resolved source frame and compact influence; shared by rendering and collision. */
struct CATALYSTLANDFORM_API FCLCompiledLandformPlacement
{
    FQuat Orientation=FQuat::Identity;
    FVector2D HalfExtentRadians;
    double TransitionFraction=.4,Strength=1;
    double Weight(const FVector& Direction) const;
    static bool Resolve(const FCLLandformPlacement& Placement,const TArray<FCLPlanetFeature>& Structure,FCLCompiledLandformPlacement& Result,FString& Error);
};

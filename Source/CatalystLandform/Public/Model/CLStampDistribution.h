#pragma once
#include "CoreMinimal.h"
#include "Model/CLHeightReference.h"
#include "CLStampDistribution.generated.h"

UENUM(BlueprintType)
enum class ECLStampBlend : uint8 { Collection, Max, Min, Override };
UENUM(BlueprintType)
enum class ECLStampSizeMode : uint8 { Absolute, PlanetRadius };
UENUM(BlueprintType)
enum class ECLStampReliefMode : uint8 { FootprintRatio, GuideInterval };

/** Serializable parameters; client assets are resolved by collection ID/revision. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLStampDistributionSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName CollectionId = TEXT("Mountains");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1")) int32 CollectionRevision = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Seed = 0;
    /** Points per square kilometre in the selected coordinate domain. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="10000")) double DensityPerSquareKilometer = 24;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) ECLStampSizeMode SizeMode = ECLStampSizeMode::PlanetRadius;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Minimum size", Units="m", EditCondition="SizeMode == ECLStampSizeMode::Absolute", EditConditionHides)) double MinimumSizeMeters = 100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Maximum size", Units="m", EditCondition="SizeMode == ECLStampSizeMode::Absolute", EditConditionHides)) double MaximumSizeMeters = 220;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="SizeMode == ECLStampSizeMode::PlanetRadius", EditConditionHides)) double MinimumSizeRadiusFraction = .1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="SizeMode == ECLStampSizeMode::PlanetRadius", EditConditionHides)) double MaximumSizeRadiusFraction = .22;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) ECLStampReliefMode ReliefMode = ECLStampReliefMode::FootprintRatio;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="ReliefMode == ECLStampReliefMode::GuideInterval", EditConditionHides)) FCLHeightReference MinimumRelief;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="ReliefMode == ECLStampReliefMode::GuideInterval", EditConditionHides)) FCLHeightReference MaximumRelief;
    /** Total source relief as a fraction of selected footprint width. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.001", ClampMax="1", EditCondition="ReliefMode == ECLStampReliefMode::FootprintRatio", EditConditionHides)) double MinimumReliefRatio = .06;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.001", ClampMax="1", EditCondition="ReliefMode == ECLStampReliefMode::FootprintRatio", EditConditionHides)) double MaximumReliefRatio = .14;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) ECLStampBlend Blend = ECLStampBlend::Collection;
    /** Smooth min/max distance relative to the outer planet radius. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="0.05")) double BlendSmoothnessRadiusFraction = .001;
    /** Smooth fade over this fraction of each footprint's width. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.01", ClampMax="0.5")) double EdgeFalloffFraction = .12;
    FCLStampDistributionSettings()
    {
        MinimumRelief.Mode=MaximumRelief.Mode=ECLHeightReferenceMode::IntervalDelta;
        MinimumRelief.Guide=MaximumRelief.Guide=TEXT("SurfaceMin");
        MinimumRelief.UpperGuide=MaximumRelief.UpperGuide=TEXT("SurfaceMax");
        MinimumRelief.Fraction=.08; MaximumRelief.Fraction=.18;
    }
    bool Validate(FString& Error) const;
};

/** Frozen PCG output. Local records use metric XY positions/headings; Anchor is a derived globe marker. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLResolvedStampPlacement
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FGuid Id;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bLocalCoordinates = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FName SourceId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector2D PositionMeters = FVector2D::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector Anchor = FVector::ZAxisVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector TangentX = FVector::XAxisVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) double WidthMeters = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) double ReliefMeters = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) double ChartRadiusMeters = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) ECLStampBlend Blend = ECLStampBlend::Max;
};

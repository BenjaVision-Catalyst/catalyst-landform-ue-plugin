#pragma once

#include "CoreMinimal.h"
#include "Model/CFModelAsset.h"
#include "Model/CLStrata.h"
#include "Model/CLStampDistribution.h"
#include "CLPlanetRecipe.generated.h"

/** Resolved metre heights for compiler output and legacy schema migration. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLHeightGuide
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Height", Units="m")) double HeightMeters = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FLinearColor Color = FLinearColor(.2, .65, 1);
};

UENUM(BlueprintType)
enum class ECLTerrainCoordinates : uint8 { Sphere, LocalPatch, SphericalSurface };

/** Assembly placement, owned by a planet. Local terrain itself has no spherical dimensions. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLTerrainPatch
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Anchor = FVector::ZAxisVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector TangentX = FVector::XAxisVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Patch half width", Units="m")) double HalfWidthMeters = 750;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Patch transition", Units="m")) double TransitionMeters = 100;
};

UENUM(BlueprintType)
enum class ECLNoiseShape : uint8 { Smooth, Ridged };
UENUM(BlueprintType)
enum class ECLNoiseBlend : uint8 { Override, Add, Max, Min };
UENUM(BlueprintType)
enum class ECLNoiseScale : uint8 { Meters UMETA(DisplayName="Absolute distance"), PlanetRadius };

UENUM(BlueprintType)
enum class ECLTerrainOperation : uint8 { Noise, Terrace, StampDistribution };

/** Pointwise shaping of the preceding terrain. Heights are resolved through planet guides. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLTerraceSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FCLHeightReference StepHeight;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FCLHeightReference Origin;
    /** Fraction of each step used for its smooth transition; zero gives hard cliffs. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="1")) double Softness = .28;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="1")) double Strength = 1;
    FCLTerraceSettings()
    {
        StepHeight.Mode = ECLHeightReferenceMode::IntervalDelta;
        StepHeight.Guide = TEXT("SurfaceMin"); StepHeight.UpperGuide = TEXT("SurfaceMax"); StepHeight.Fraction = .12;
        Origin.Guide = TEXT("SurfaceMin");
    }
};

/** Ordered generation operation. Algorithm 1 uses UE Perlin; 2 uses VPP Perlin 3D. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLNoiseLayer
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="Operation == ECLTerrainOperation::StampDistribution", EditConditionHides)) FCLStampDistributionSettings Distribution;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) ECLTerrainOperation Operation = ECLTerrainOperation::Noise;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="Operation == ECLTerrainOperation::Terrace", EditConditionHides)) FCLTerraceSettings Terrace;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="Operation == ECLTerrainOperation::Noise", EditConditionHides)) int32 Seed = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="Operation == ECLTerrainOperation::Noise", EditConditionHides)) ECLNoiseShape Shape = ECLNoiseShape::Smooth;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="Operation == ECLTerrainOperation::Noise", EditConditionHides)) ECLNoiseBlend Blend = ECLNoiseBlend::Override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="Operation == ECLTerrainOperation::Noise", EditConditionHides)) FCLHeightReference Minimum;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="Operation == ECLTerrainOperation::Noise", EditConditionHides)) FCLHeightReference Maximum;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="Operation == ECLTerrainOperation::Noise", EditConditionHides)) ECLNoiseScale Scale = ECLNoiseScale::Meters;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Wavelength", Units="m", ClampMin="0.1", EditCondition="Operation == ECLTerrainOperation::Noise && Scale == ECLNoiseScale::Meters", EditConditionHides)) double WavelengthMeters = 50;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.000001", EditCondition="Operation == ECLTerrainOperation::Noise && Scale == ECLNoiseScale::PlanetRadius", EditConditionHides)) double WavelengthRadiusFraction = .5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="Operation == ECLTerrainOperation::Noise", EditConditionHides, ClampMin="1", ClampMax="8")) int32 Octaves = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="Operation == ECLTerrainOperation::Noise", EditConditionHides, ClampMin="0", ClampMax="1")) double Persistence = .5;
};

UENUM(BlueprintType)
enum class ECLTerrainRegionShape : uint8 { Band, Disc, HalfPlane };

/** Planet-owned influence in the assembly chart, in metres. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLTerrainRegionMask
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) ECLTerrainRegionShape Shape = ECLTerrainRegionShape::Band;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Centre", Units="m")) FVector2D CenterMeters = FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Heading", Units="deg")) double HeadingDegrees = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Half width / radius", Units="m")) double HalfWidthMeters = 150;
    /** Full transition width, centred on the nominal boundary. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Transition width", Units="m")) double TransitionMeters = 200;
    bool Validate(FString& Error) const;
    double Weight(const FVector2D& Meters) const;
};

/** Great-circle influences, independent of any patch's local frame. Distances use the neutral radius. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLSphericalRegionMask
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) ECLTerrainRegionShape Shape = ECLTerrainRegionShape::Band;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Axis = FVector::YAxisVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Center = FVector::ZAxisVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Offset", Units="m")) double OffsetMeters = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Half width / radius", Units="m")) double HalfWidthMeters = 90;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Transition width", Units="m")) double TransitionMeters = 100;
    bool Validate(double Radius,FString& Error) const;
    double Weight(const FVector& UnitDirection,double Radius) const;
};

/** Applied terrain snapshot. Guides and projection are supplied only by the parent planet. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLTerrainRegion
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGuid Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name = TEXT("Terrain region");
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Priority = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SeedOffset = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bSphericalMask = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="!bSphericalMask", EditConditionHides)) FCLTerrainRegionMask Mask;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bSphericalMask", EditConditionHides)) FCLSphericalRegionMask SphericalMask;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FGuid TerrainTypeId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString TerrainTypeName;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 AlgorithmVersion = 2;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TArray<FCLNoiseLayer> NoiseLayers;
};

USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLPlanetRecipe
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SchemaVersion = 6;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 AlgorithmVersion = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name = TEXT("Rolling plains");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Radius", Units="m", ClampMin="10")) double RadiusMeters = 100;
    /** Initial solid sphere before ordered operations; fraction of outer radius. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.001", ClampMax="1")) double BaseSurfaceNormalized = 1;
    UPROPERTY() double ReferenceRadiusMeters_DEPRECATED = 100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Seed = 1729;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) ECLTerrainCoordinates Coordinates = ECLTerrainCoordinates::Sphere;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="Coordinates == ECLTerrainCoordinates::LocalPatch", EditConditionHides)) FCLTerrainPatch TerrainPatch;
    /** Applied planet-global snapshot, independent of its authoring catalog. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FCLStrata Strata;
    /** Read old binary recipes once, then migrate to normalized Strata. */
    UPROPERTY() TArray<FCLHeightGuide> HeightGuides_DEPRECATED;
    void MigrateLegacyGuides();
    /** Applied source identity; NoiseLayers remains the authoritative self-contained snapshot. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, AdvancedDisplay) FGuid TerrainTypeId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString TerrainTypeName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Terrain layers")) TArray<FCLNoiseLayer> NoiseLayers;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FCLTerrainRegion> TerrainRegions;
};

class FCLNativeNoiseKernel;
class FCLCompiledStampDistribution;
class UCLStampLibraryAsset;

/** Worker-safe output. Opaque source owners retain lifetimes without reading live authoring state. */
struct CATALYSTLANDFORM_API FCLCompiledPlanetRecipe
{
    struct FLayer
    {
        TSharedPtr<const FCLCompiledStampDistribution> Distribution;
        ECLTerrainOperation Operation = ECLTerrainOperation::Noise;
        double StepHeight = 1, Origin = 0, Softness = 0, Strength = 1;
        double TerraceHeight(double Height) const;
        FVector Offset;
        ECLNoiseShape Shape;
        ECLNoiseBlend Blend;
        double Minimum, Maximum, Wavelength, Persistence;
        int32 Octaves;
        int32 Seed = 0;
    };
    /** Captured native kernel only: no graph assets or UObject access on workers. */
    TSharedPtr<const FCLNativeNoiseKernel> NativePerlin;
    double RadiusMeters = 100;
    ECLTerrainCoordinates Coordinates = ECLTerrainCoordinates::Sphere;
    FCLTerrainPatch TerrainPatch;
    /** -1 is the six-patch router; -2 is the bounded whole-field reference oracle. */
    int32 SurfacePatch = INDEX_NONE;
    TArray<TSharedPtr<const FCLCompiledPlanetRecipe>> SurfacePatches;
    FBox2D EvaluationRegion;
    FVector TerrainDirection(const FVector2D& Meters) const;
    bool TerrainPosition(const FVector& Direction, FVector2D& Meters) const;
    /** Direct metric field, independent of assembly curvature and its boundary transition. */
    void SampleLocalHeightsMeters(TConstArrayView<FVector> Positions, TArray<double>& Heights) const;
    double BaseHeightMeters = 0;
    double MinimumHeightMeters = 0, MaximumHeightMeters = 0;
    TArray<FCLHeightGuide> Guides;
    TArray<FLayer> Layers;
    struct FRegion
    {
        FGuid Id;
        FCLTerrainRegionMask Mask;
        FCLSphericalRegionMask SphericalMask;
        TSharedPtr<const FCLCompiledPlanetRecipe> Field;
    };
    /** Sorted by explicit priority then persistent ID. No UObject state on workers. */
    TArray<FRegion> Regions;
    int32 GetDistributedStampCount() const;
    /** Diagnostic coverage of distributed stamp footprints and their fade weights. */
    void GetStampCoverage(const FVector& Direction,int32& Count,double& MinimumWeight) const;
    void GetStampPlacements(TArray<FCLResolvedStampPlacement>& Placements) const;
    double SampleHeightMeters(const FVector& LocalDirection) const;
    /** Batched native generation, with bounded scratch memory; results are metre offsets. */
    void SampleHeightsMeters(TConstArrayView<FVector> LocalDirections, TArray<double>& Heights) const;
private:
    void BlendRegionHeights(TConstArrayView<FVector> Positions,TArray<double>& Heights,bool bLocal) const;
    void EvaluateHeightsMeters(TConstArrayView<FVector> Positions, TArray<double>& Heights, bool bLocal) const;
};

class CATALYSTLANDFORM_API FCLPlanetCompiler
{
public:
    /** Returns no plan on error; callers may retain their last successful plan. */
    static TSharedPtr<const FCLCompiledPlanetRecipe> Compile(const FCLPlanetRecipe& Recipe, FString& Error, UCLStampLibraryAsset* StampLibrary = nullptr, const FBox2D* LocalRegion = nullptr, int32 SurfacePatch = INDEX_NONE);
    static FCLPlanetRecipe RollingPlains();
    /** Validate operation syntax without requiring a planet's guide values. */
    static bool ValidateNoiseLayers(const TArray<FCLNoiseLayer>& Layers, FString& Error);
};

UCLASS(BlueprintType)
class CATALYSTLANDFORM_API UCLPlanetRecipeAsset : public UCFModelAsset
{
    GENERATED_BODY()
public:
    UCLPlanetRecipeAsset();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Landform|Recipe") FCLPlanetRecipe Recipe;
    virtual FString GetSummaryText() const override;
    virtual void Serialize(FArchive& Ar) override;
    virtual void PostLoad() override;
    /** Transactional local import; Foundation's legacy importer mutates before validating. */
    UFUNCTION(BlueprintCallable, Category="Landform|Recipe") bool ImportRecipeJson(const FString& Json, FString& Error);
#if WITH_EDITOR
    FSimpleMulticastDelegate OnRecipeChanged;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
    virtual void PostEditUndo() override;
#endif
protected:
    virtual FString GetPluginNameForPaths() const override { return TEXT("CatalystLandform"); }
    virtual int32 GetSchemaVersion() const override { return Recipe.SchemaVersion; }
    virtual UScriptStruct* GetPayloadScriptStruct() const override { return FCLPlanetRecipe::StaticStruct(); }
    virtual void* GetPayloadMemory() override { return &Recipe; }
    virtual const void* GetPayloadMemory() const override { return &Recipe; }
    virtual bool ValidatePayload(FString& Error) const override;
    virtual void CollectValidationMessages(TArray<FCFValidationMessage>& Messages) const override;
};

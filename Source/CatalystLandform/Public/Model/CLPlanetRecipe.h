#pragma once

#include "CoreMinimal.h"
#include "Model/CFModelAsset.h"
#include "Model/CLStrata.h"
#include "CLPlanetRecipe.generated.h"

/** Resolved metre heights for compiler output and legacy schema migration. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLHeightGuide
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="m")) double HeightMeters = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FLinearColor Color = FLinearColor(.2, .65, 1);
};

/** An absolute metre offset, or a named Stratum plus offset, relative to the outer radius. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLHeightReference
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Guide;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="m")) double OffsetMeters = 0;
};

UENUM(BlueprintType)
enum class ECLNoiseShape : uint8 { Smooth, Ridged };
UENUM(BlueprintType)
enum class ECLNoiseBlend : uint8 { Override, Add, Max, Min };

/** Ordered generation operation. Algorithm v1 uses UE's 3D Perlin evaluator. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLNoiseLayer
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Seed = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) ECLNoiseShape Shape = ECLNoiseShape::Smooth;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) ECLNoiseBlend Blend = ECLNoiseBlend::Override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FCLHeightReference Minimum;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FCLHeightReference Maximum;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="m", ClampMin="0.1")) double WavelengthMeters = 50;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1", ClampMax="8")) int32 Octaves = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="1")) double Persistence = .5;
};

USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLPlanetRecipe
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SchemaVersion = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 AlgorithmVersion = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name = TEXT("Rolling plains");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="m", ClampMin="10")) double RadiusMeters = 100;
    /** Initial solid sphere before ordered operations; fraction of outer radius. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.001", ClampMax="1")) double BaseSurfaceNormalized = 1;
    UPROPERTY() double ReferenceRadiusMeters_DEPRECATED = 100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Seed = 1729;
    /** Applied planet-global snapshot, independent of its authoring catalog. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FCLStrata Strata;
    /** Read old binary recipes once, then migrate to normalized Strata. */
    UPROPERTY() TArray<FCLHeightGuide> HeightGuides_DEPRECATED;
    void MigrateLegacyGuides();
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FCLNoiseLayer> NoiseLayers;
};

/** Plain-data, worker-safe compiler output. No live asset or service references. */
struct CATALYSTLANDFORM_API FCLCompiledPlanetRecipe
{
    struct FNoise
    {
        FVector Offset;
        ECLNoiseShape Shape;
        ECLNoiseBlend Blend;
        double Minimum, Maximum, Wavelength, Persistence;
        int32 Octaves;
    };
    double RadiusMeters = 100;
    double BaseHeightMeters = 0;
    double MinimumHeightMeters = 0, MaximumHeightMeters = 0;
    TArray<FCLHeightGuide> Guides;
    TArray<FNoise> Layers;
    double SampleHeightMeters(const FVector& LocalDirection) const;
};

class CATALYSTLANDFORM_API FCLPlanetCompiler
{
public:
    /** Returns no plan on error; callers may retain their last successful plan. */
    static TSharedPtr<const FCLCompiledPlanetRecipe> Compile(const FCLPlanetRecipe& Recipe, FString& Error);
    static FCLPlanetRecipe RollingPlains();
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
    virtual int32 GetSchemaVersion() const override { return 2; }
    virtual UScriptStruct* GetPayloadScriptStruct() const override { return FCLPlanetRecipe::StaticStruct(); }
    virtual void* GetPayloadMemory() override { return &Recipe; }
    virtual const void* GetPayloadMemory() const override { return &Recipe; }
    virtual bool ValidatePayload(FString& Error) const override;
    virtual void CollectValidationMessages(TArray<FCFValidationMessage>& Messages) const override;
};

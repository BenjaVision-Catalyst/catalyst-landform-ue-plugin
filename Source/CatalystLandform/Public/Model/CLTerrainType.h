#pragma once
#include "CoreMinimal.h"
#include "Model/CLPlanetRecipe.h"
#include "CLTerrainType.generated.h"

/** Reusable generation settings. Planet Strata, radius and seed are supplied by context. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLTerrainType
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SchemaVersion = 3;
    /** 1 preserves the original CPU field; 2 is the pinned native VPP kernel. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay) int32 AlgorithmVersion = 1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, AdvancedDisplay) FGuid Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name = TEXT("Untitled terrain type");
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Description;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FCLNoiseLayer> NoiseLayers;
    bool Validate(FString& Error) const;
    /** Apply a validated snapshot atomically; never changes the planet's Strata/radius/seed. */
    bool ApplyTo(FCLPlanetRecipe& Recipe, FString& Error) const;
};

UCLASS(BlueprintType)
class CATALYSTLANDFORM_API UCLTerrainTypeAsset : public UCFModelAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Landform") FCLTerrainType TerrainType;
    virtual FString GetSummaryText() const override;
    virtual void PostLoad() override;
    virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
    UFUNCTION(BlueprintCallable, Category="Landform") bool ApplyToRecipe(UCLPlanetRecipeAsset* Asset, FString& Error) const;
    UFUNCTION(BlueprintCallable, Category="Landform") bool ImportTerrainTypeJson(const FString& Json, FString& Error);
#if WITH_EDITOR
    FSimpleMulticastDelegate OnTerrainChanged;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
    virtual void PostEditUndo() override;
#endif
protected:
    virtual int32 GetSchemaVersion() const override { return TerrainType.SchemaVersion; }
    virtual FString GetPluginNameForPaths() const override { return TEXT("CatalystLandform/TerrainTypes"); }
    virtual UScriptStruct* GetPayloadScriptStruct() const override { return FCLTerrainType::StaticStruct(); }
    virtual void* GetPayloadMemory() override { return &TerrainType; }
    virtual const void* GetPayloadMemory() const override { return &TerrainType; }
    virtual bool ValidatePayload(FString& Error) const override;
    virtual void CollectValidationMessages(TArray<FCFValidationMessage>& Messages) const override;
};

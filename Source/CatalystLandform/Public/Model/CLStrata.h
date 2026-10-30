#pragma once
#include "CoreMinimal.h"
#include "Model/CFModelAsset.h"
#include "CLStrata.generated.h"

/** A planet-wide reference height, expressed as a fraction of the outer sky radius. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLStratum
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Name;
    /** Centre = 0; outer sky boundary = 1; all generated terrain stays inside it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double NormalizedHeight = .5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FLinearColor Color = FLinearColor(.2, .65, 1);
};

/** A reusable style. A planet receives its own applied copy at creation/selection. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLStrata
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FGuid Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name = TEXT("Untitled strata");
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FCLStratum> Stratum;
    bool Validate(FString& Error) const;
};

USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLStrataLibrary
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FCLStrata> Sets;
    /** Seed each built-in catalog revision once; deleted styles stay deleted. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 PresetRevision = 0;
};

/** Project authoring catalog; applied planet recipes remain self-contained. */
UCLASS(BlueprintType)
class CATALYSTLANDFORM_API UCLStrataLibraryAsset : public UCFModelAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Landform") FCLStrataLibrary Library;
    virtual FString GetSummaryText() const override;
protected:
    virtual FString GetPluginNameForPaths() const override { return TEXT("CatalystLandform/Strata"); }
    virtual UScriptStruct* GetPayloadScriptStruct() const override { return FCLStrataLibrary::StaticStruct(); }
    virtual void* GetPayloadMemory() override { return &Library; }
    virtual const void* GetPayloadMemory() const override { return &Library; }
    virtual bool ValidatePayload(FString& Error) const override;
};

/** Reference layouts only: choosing a style does not generate water or atmosphere. */
struct CATALYSTLANDFORM_API FCLStrataPresets
{
    static TArray<FCLStrata> Defaults();
    /** Add missing built-ins once, preserving edited styles and name collisions. */
    static bool Seed(FCLStrataLibrary& Library);
};

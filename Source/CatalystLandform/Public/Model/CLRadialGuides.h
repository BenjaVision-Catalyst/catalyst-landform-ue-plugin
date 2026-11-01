#pragma once
#include "CoreMinimal.h"
#include "Model/CFModelAsset.h"
#include "CLRadialGuides.generated.h"

/** A planet-wide reference height, expressed as a fraction of the outer sky radius. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLRadialGuide
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Name;
    /** Centre = 0; outer sky boundary = 1; all generated terrain stays inside it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double NormalizedHeight = .5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FLinearColor Color = FLinearColor(.2, .65, 1);
};

/** A reusable style, referenced by a planet through its catalog and persistent ID. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLRadialGuideSet
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FGuid Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name = TEXT("Untitled radial guide set");
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FCLRadialGuide> Guides;
    bool Validate(FString& Error) const;
};

USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLRadialGuideLibrary
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FCLRadialGuideSet> Sets;
    /** Seed each built-in catalog revision once; deleted styles stay deleted. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 PresetRevision = 0;
};

/** Project authoring catalog; exported planet recipes retain resolved guide values. */
UCLASS(BlueprintType)
class CATALYSTLANDFORM_API UCLRadialGuideLibraryAsset : public UCFModelAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Landform") FCLRadialGuideLibrary Library;
    const FCLRadialGuideSet* FindSet(FGuid Id) const;
    UFUNCTION(BlueprintCallable, Category="Landform|Radial Guides") bool SeedStarterSets();
#if WITH_EDITOR
    FSimpleMulticastDelegate OnGuidesChanged;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
    virtual void PostEditUndo() override;
#endif
    virtual FString GetSummaryText() const override;
protected:
    virtual FString GetPluginNameForPaths() const override { return TEXT("CatalystLandform/RadialGuides"); }
    virtual UScriptStruct* GetPayloadScriptStruct() const override { return FCLRadialGuideLibrary::StaticStruct(); }
    virtual void* GetPayloadMemory() override { return &Library; }
    virtual const void* GetPayloadMemory() const override { return &Library; }
    virtual bool ValidatePayload(FString& Error) const override;
};

/** Reference layouts only: choosing a style does not generate water or atmosphere. */
struct CATALYSTLANDFORM_API FCLRadialGuidePresets
{
    static TArray<FCLRadialGuideSet> Defaults();
    /** Add missing built-ins once, preserving edited styles and name collisions. */
    static bool Seed(FCLRadialGuideLibrary& Library);
};

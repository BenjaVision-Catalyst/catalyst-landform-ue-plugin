#pragma once
#include "CoreMinimal.h"
#include "Model/CFModelAsset.h"
#include "Model/CLStampDistribution.h"
#include "Engine/DeveloperSettings.h"
#include "CLStampLibrary.generated.h"

class UVoxelHeightmap;
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLStampLibraryEntry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.001")) double Weight = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UVoxelHeightmap> Heightmap;
    /** Source's encoded full vertical span, used to normalize the selected relief. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Reference relief", Units="m")) double ReferenceReliefMeters = 10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) ECLStampBlend Blend = ECLStampBlend::Max;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString SourceDigest;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Provenance;
};
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLStampCollection
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Revision = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(MultiLine=true)) FString Description;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FCLStampLibraryEntry> Entries;
    UPROPERTY(VisibleAnywhere) int32 BakedRevision = 0;
    UPROPERTY(VisibleAnywhere) FString DefinitionDigest;
};
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLStampLibrary
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SchemaVersion = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FCLStampCollection> Collections;
    bool Validate(FString& Error) const;
};

/** Client-side asset manifest, separate from the backend-shaped terrain recipe. */
UCLASS(BlueprintType)
class CATALYSTLANDFORM_API UCLStampLibraryAsset : public UCFModelAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Landform|Stamps") FCLStampLibrary Library;
    UFUNCTION(BlueprintCallable, Category="Landform|Stamps") bool ImportLibraryJson(const FString& Json, FString& Error);
    /** Capture source digests while authoring a new collection revision. */
    UFUNCTION(BlueprintCallable, Category="Landform|Stamps") bool BakeSourceDigests(FString& Error);
    static UCLStampLibraryAsset* LoadConfigured();
    static FString SourceDigest(UVoxelHeightmap* Heightmap);
    static FString CollectionDigest(const FCLStampCollection& Collection);
    virtual FString GetSummaryText() const override;
protected:
    virtual FString GetPluginNameForPaths() const override { return TEXT("CatalystLandform/Stamps"); }
    virtual UScriptStruct* GetPayloadScriptStruct() const override { return FCLStampLibrary::StaticStruct(); }
    virtual void* GetPayloadMemory() override { return &Library; }
    virtual const void* GetPayloadMemory() const override { return &Library; }
    virtual bool ValidatePayload(FString& Error) const override { return Library.Validate(Error); }
};
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Catalyst Landform"))
class CATALYSTLANDFORM_API UCLLandformSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere, Category="Stamps") TSoftObjectPtr<UCLStampLibraryAsset> StampLibrary =
        TSoftObjectPtr<UCLStampLibraryAsset>(FSoftObjectPath(TEXT("/CatalystLandform/Stamps/DA_StampLibrary.DA_StampLibrary")));
};

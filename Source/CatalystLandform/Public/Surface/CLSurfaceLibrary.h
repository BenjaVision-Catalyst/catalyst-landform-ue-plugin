#pragma once
#include "CoreMinimal.h"
#include "Model/CFModelAsset.h"
#include "Engine/DeveloperSettings.h"
#include "Surface/VoxelSurfaceType.h"
#include "UObject/StrongObjectPtr.h"
#include "CLSurfaceLibrary.generated.h"
class UVoxelSurfaceTypeAsset;
class UVoxelMegaMaterial;
class UTexture2D;

/** Client artwork only. Portable recipes continue to carry surface IDs/revisions. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLSurfaceTextures
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UTexture2D> BaseColor;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UTexture2D> Normal;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UTexture2D> Roughness;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="0.01")) double TileMeters=2;
    bool IsActive() const { return BaseColor||Normal||Roughness; }
};

/** Client appearance parameters shared by native surface materials and the Designer shader. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLSurfaceAppearance
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FLinearColor Color=FLinearColor(.2,.18,.15);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Roughness=.85;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.01",Units="m")) double GrainMeters=.12;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double GrainStrength=.25;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double NormalStrength=.15;
};
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLSurfaceLibraryEntry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName Id;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Revision=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FCLSurfaceAppearance Appearance;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FCLSurfaceTextures Textures;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UVoxelSurfaceTypeAsset> SurfaceType;
};
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLSurfaceLibrary
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 SchemaVersion=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FCLSurfaceLibraryEntry> Entries;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UMaterialInterface> PreviewMaterial;
};
UCLASS(BlueprintType)
class CATALYSTLANDFORM_API UCLSurfaceLibraryAsset : public UCFModelAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Landform|Surfaces") FCLSurfaceLibrary Library;
    bool ValidateLibrary(FString& Error) const;
#if WITH_EDITOR
    /** Copy shared appearance parameters into the native material instances. */
    UFUNCTION(CallInEditor,BlueprintCallable,Category="Landform|Surfaces") bool BakeAppearance(FString& Error);
#endif
    static UCLSurfaceLibraryAsset* LoadConfigured();
    virtual FString GetSummaryText() const override {return FString::Printf(TEXT("%d client terrain surfaces"),Library.Entries.Num());}
protected:
    virtual FString GetPluginNameForPaths() const override {return TEXT("CatalystLandform/Surfaces");}
    virtual UScriptStruct* GetPayloadScriptStruct() const override {return FCLSurfaceLibrary::StaticStruct();}
    virtual void* GetPayloadMemory() override {return &Library;}
    virtual const void* GetPayloadMemory() const override {return &Library;}
    virtual bool ValidatePayload(FString& Error) const override {return ValidateLibrary(Error);}
};
UCLASS(Config=Game,DefaultConfig,meta=(DisplayName="Catalyst Landform Surfaces"))
class CATALYSTLANDFORM_API UCLSurfaceSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UCLSurfaceSettings();
    UPROPERTY(Config,EditAnywhere,Category="Surfaces") TSoftObjectPtr<UCLSurfaceLibraryAsset> SurfaceLibrary;
    /** Client-owned renderer binding for recipe surface IDs; never supplied as a server asset path. */
    UPROPERTY(Config,EditAnywhere,Category="Surfaces") TSoftObjectPtr<UVoxelMegaMaterial> PlanetMaterial;
};
struct CATALYSTLANDFORM_API FCLCompiledSurfaceLibrary
{
    static constexpr int32 MaxSurfaces=8;
    TStrongObjectPtr<UCLSurfaceLibraryAsset> Owner;
    TArray<FName> Ids;
    TArray<int32> Revisions;
    TArray<FCLSurfaceAppearance> Appearances;
    TArray<FCLSurfaceTextures> Textures;
    TArray<TStrongObjectPtr<UTexture2D>> RetainedTextures;
    TArray<FVoxelSurfaceType> NativeTypes;
    static TSharedPtr<const FCLCompiledSurfaceLibrary> Resolve(FString& Error);
    int32 Find(FName Id,int32 Revision) const;
};

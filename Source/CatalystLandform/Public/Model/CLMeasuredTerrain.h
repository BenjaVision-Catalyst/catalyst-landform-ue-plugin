#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CLMeasuredTerrain.generated.h"
class UVoxelHeightmap;
class UTexture2D;
class UCLSurfaceLibraryAsset;
class UVoxelMegaMaterial;

/** Portable resource identity, never an asset path or mutable importer configuration. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLMeasuredTerrainReference
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Revision = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Digest;
    bool IsActive() const
    {
        return !Id.IsNone();
    }
};

/** Planetocentric/east-positive grid of physical radial offsets stored by native VPP.
 * Original pixel centres and native two-pixel halos are explicit. Regional NoData
 * uses Coverage; missing samples retain the declared global context, never air. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLMeasuredRaster
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UVoxelHeightmap> Heightmap;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UVoxelHeightmap> Coverage;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Width = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Height = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double WestCenterDegrees = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double NorthCenterDegrees = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double LongitudeStepDegrees = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double LatitudeStepDegrees = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double RadiusOffsetMeters = 3396000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double FeatherMeters = 100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bGlobal = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double NorthPoleOffsetMeters = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double SouthPoleOffsetMeters = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString PayloadDigest;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString CoverageDigest;
};

USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLMeasuredTerrainEntry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Revision = 1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString DefinitionDigest;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString BodyId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString FrameId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Datum;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (MultiLine = true)) FString Provenance;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double MinimumRadiusMeters = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double MaximumRadiusMeters = 0;
    /** Client native VPP artwork binding; excluded from the portable scientific digest. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UCLSurfaceLibraryAsset> SurfaceLibrary;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UVoxelMegaMaterial> Material;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FCLMeasuredRaster Global;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FCLMeasuredRaster> Regions;
};

UCLASS(BlueprintType)
class CATALYSTLANDFORM_API UCLMeasuredTerrainLibrary : public UDataAsset
{
    GENERATED_BODY()
  public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landform|Measured terrain")
    TArray<FCLMeasuredTerrainEntry> Entries;
    UFUNCTION(BlueprintCallable, Category = "Landform|Measured terrain") bool BakeDigests(FString &Error);
    static FString DefinitionDigest(const FCLMeasuredTerrainEntry &Entry);
};
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catalyst Landform Measured Terrain"))
class CATALYSTLANDFORM_API UCLMeasuredTerrainSettings : public UDeveloperSettings
{
    GENERATED_BODY()
  public:
    UPROPERTY(Config, EditAnywhere, Category = "Resources")
    TSoftObjectPtr<UCLMeasuredTerrainLibrary> Library = TSoftObjectPtr<UCLMeasuredTerrainLibrary>(
        FSoftObjectPath(TEXT("/CatalystLandform/Measured/DA_MeasuredTerrain.DA_MeasuredTerrain")));
};
UCLASS()
class CATALYSTLANDFORM_API UCLMeasuredTerrainTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
  public:
    /** Author-owned assets only. Input is explicit little-endian float32, never guessed from extension. */
    UFUNCTION(BlueprintCallable, Category = "Landform|Measured terrain")
    static bool ImportNativeHeights(UVoxelHeightmap *Target, UTexture2D *SourceTexture, const FString &Path, int32 Width, int32 Height,
                                    FString &Error);
};

/** Immutable worker boundary. Native storage/interpolation remain behind this owner. */
class CATALYSTLANDFORM_API FCLCompiledMeasuredTerrain
{
  public:
    static TSharedPtr<const FCLCompiledMeasuredTerrain> Compile(const FCLMeasuredTerrainReference &Ref,
                                                                double OuterRadius, FString &Error,
                                                                UCLMeasuredTerrainLibrary *Library = nullptr);
    static FVector NativeDirection(double PlanetocentricLatitudeDegrees, double EastLongitudeDegrees);
    static FVector2D SourceCoordinates(const FVector &NativeDirection);
    void SampleHeights(TConstArrayView<FVector> Directions, TArray<double> &Heights) const;
    UCLSurfaceLibraryAsset *GetSurfaceLibrary() const;
    UVoxelMegaMaterial *GetMaterial() const;
    double MinimumHeightMeters = 0, MaximumHeightMeters = 0;
    int64 ResidentBytes = 0;

  private:
    struct FNative;
    TSharedPtr<const FNative> Native;
    double OuterRadiusMeters = 0;
};

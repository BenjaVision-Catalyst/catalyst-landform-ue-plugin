#pragma once

#include "CoreMinimal.h"
#include "VoxelWorld.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CLPlanetStamp.h"
#include "CLPlanet.generated.h"

class UVoxelStampComponent;
class UArrowComponent;
class UTexture2D;
class USplineComponent;

UCLASS()
class CATALYSTLANDFORM_API UCLHeightmapLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Editor authoring helper for VPP source settings that Python cannot access. */
    UFUNCTION(BlueprintCallable, Category="Landform|Heightmap")
    static bool ConfigureTextureSource(UVoxelHeightmap* Heightmap, UTexture2D* Texture,
        float ScaleXY, float ScaleZ, float OffsetZ);

    UFUNCTION(BlueprintCallable, Category="Landform|Heightmap")
    static FIntPoint GetSourceResolution(UVoxelHeightmap* Heightmap);
};

UCLASS(BlueprintType)
class CATALYSTLANDFORM_API ACLPlanet : public AVoxelWorld
{
    GENERATED_BODY()
public:
    ACLPlanet();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet", meta=(Units="m", ClampMin="10"))
    double RadiusMeters = 100;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Planet")
    TObjectPtr<UVoxelStampComponent> TerrainStamp;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet|Recipe")
    TObjectPtr<UCLPlanetRecipeAsset> RecipeAsset;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Planet|Recipe")
    FString RecipeStatus;

    UFUNCTION(CallInEditor, BlueprintCallable, Category="Planet")
    virtual void RebuildTerrain();

    UFUNCTION(BlueprintCallable, Category="Planet")
    bool SampleRadialHeights(const TArray<FVector>& LocalDirections, TArray<double>& Heights) const;

    UFUNCTION(BlueprintCallable, Category="Planet")
    int32 GetNativeStampCount() const;

    uint32 GetFootprintMask(const FVector& LocalDirection) const;
    const TSharedPtr<const FCLPlanetStampRuntime>& GetSamplingRuntime() const { return SamplingRuntime; }
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostRegisterAllComponents() override;
    virtual void BeginPlay() override;

private:
    bool bRebuilding = false;
    TSharedPtr<const FCLPlanetStampRuntime> SamplingRuntime;
#if WITH_EDITOR
    TWeakObjectPtr<UCLPlanetRecipeAsset> BoundRecipe;
    FDelegateHandle RecipeChangedHandle;
#endif
};

/** Move on the sphere, rotate about local Z, scale XY footprint/Z relief. */
UCLASS(BlueprintType)
class CATALYSTLANDFORM_API ACLSphericalHeightmapStamp : public AActor
{
    GENERATED_BODY()
public:
    ACLSphericalHeightmapStamp();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spherical Stamp")
    TObjectPtr<ACLPlanet> Planet;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spherical Stamp")
    TObjectPtr<UVoxelHeightmap> Heightmap;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spherical Stamp")
    EVoxelHeightBlendMode BlendMode = EVoxelHeightBlendMode::Max;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spherical Stamp")
    int32 Priority = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spherical Stamp", meta=(Units="cm", ClampMin="0"))
    float Smoothness = 150;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spherical Stamp", meta=(Units="m"))
    double HeightOffsetMeters = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spherical Stamp", meta=(Units="m", ClampMin="0.01"))
    double EdgeFalloffMeters = 8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spherical Stamp")
    bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview")
    FColor FootprintColor = FColor(255, 180, 60);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview")
    bool bShowFootprint = true;
    UPROPERTY(Transient, BlueprintReadWrite, Category="Preview")
    bool bShowFootprintInGame = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Spherical Stamp")
    FString ValidationMessage;
    UPROPERTY(VisibleAnywhere, Category="Spherical Stamp")
    FGuid StampId;

    UFUNCTION(CallInEditor, BlueprintCallable, Category="Spherical Stamp")
    void RefreshStamp();

    void SnapToSurface();
    void UpdateFootprint();
    bool BuildPlacement(FCLSphericalHeightmapPlacement& Placement);
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostRegisterAllComponents() override;
    virtual void Destroyed() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool ShouldTickIfViewportsOnly() const override { return true; }
#if WITH_EDITOR
    virtual void PostEditMove(bool bFinished) override;
    virtual void PostEditUndo() override;
    virtual void PostDuplicate(EDuplicateMode::Type Mode) override;
#endif

private:
    UPROPERTY() TObjectPtr<UArrowComponent> Arrow;
    TArray<FVector> FootprintPoints;
#if WITH_EDITORONLY_DATA
    UPROPERTY(Transient) TObjectPtr<USplineComponent> EditorFootprint;
#endif
#if WITH_EDITOR
    TWeakObjectPtr<UVoxelHeightmap> BoundHeightmap;
    FDelegateHandle HeightmapChangedHandle;
    void BindHeightmap();
#endif
};

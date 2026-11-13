#pragma once
#include "CoreMinimal.h"
#include "Model/CLRockLayer.h"
#include "Surface/CLSurfaceCompiler.h"
#include "CLMaterialLayers.generated.h"

struct FCLPlanetRecipe;
struct FCLCompiledPlanetRecipe;

/** A finite physical deposit inside its landform footprint, anchored to the
 * generated terrain before excavation. It reclassifies existing solid; no
 * extra terrain height or independent voxel geometry is generated. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLLandformDeposit
{
    GENERATED_BODY()
    FCLLandformDeposit();
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FCLRockUnit Unit;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ToolTip="Higher material priority applies last. Array order has no effect.")) int32 Priority=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="0",ClampMax="10000")) double TopDepthMeters=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="0.05",ClampMax="10000")) double ThicknessMeters=3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="0",ClampMax="5")) double ContactFeatherMeters=.5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double ThicknessVariation=.25;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Coverage=.7;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="1",ClampMax="10000000")) double PatchSizeMeters=60;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="deg",ClampMin="0",ClampMax="90")) double MaximumSlopeDegrees=90;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Seed=0;
    bool Validate(FString& Error)const;
    static bool ValidateList(TConstArrayView<FCLLandformDeposit> Deposits,FString& Error);
};

/** Shared composition result. Physical identity is chosen from contributions,
 * never inferred from a visible material or a process-local rendering index. */
struct CATALYSTLANDFORM_API FCLMaterialSample
{
    ECLSceneSampleState State=ECLSceneSampleState::NoContribution;
    FCLSurfaceWeights Surfaces;
    FCLRockSample Material;
    FCLRockSample Substrate;
    FLinearColor Properties=FLinearColor(0,0,0,0);
    double DominantContribution=1;
    void Overlay(const FCLSurfaceWeights& Weights,const FCLRockSample& Physical,double Alpha);
    /** Appearance-only cover can reuse the same compositor without replacing
     * physical identity/properties. Ecosystem classification stays external. */
    void OverlayAppearance(const FCLSurfaceWeights& Weights,double Alpha);
};

/** Immutable resolved material program. No UObjects, world lookups or dependency
 * on Ecosystem. All spatial inputs are canonical body coordinates in metres. */
class CATALYSTLANDFORM_API FCLCompiledMaterialLayers
{
public:
    static bool Compile(const FCLPlanetRecipe&,const FCLCompiledPlanetRecipe&,TSharedPtr<const FCLCompiledMaterialLayers>& Out,FString& Error);
    bool HasDeposits()const{return !Layers.IsEmpty();}
    void Apply(const FCLCompiledPlanetRecipe&,TConstArrayView<FVector3d> Positions,TArrayView<FCLMaterialSample> Samples)const;
private:
    struct FLayer
    {
        FCLLandformDeposit Definition;
        FGuid SourceId,UnitId,StratumId;
        int32 PaletteIndex=INDEX_NONE,SurfaceIndex=INDEX_NONE,SourcePriority=0;
        TFunction<double(const FVector&)> Footprint;
        FVector Offset;
    };
    TArray<FLayer> Layers;
    double MaximumDepthMeters=0;
};

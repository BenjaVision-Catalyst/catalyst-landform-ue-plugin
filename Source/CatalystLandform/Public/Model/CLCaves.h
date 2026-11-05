#pragma once
#include "CoreMinimal.h"
#include "Scene/CLVolumes.h"
#include "CLCaves.generated.h"
struct FCLCompiledPlanetRecipe;

/** Structurally controlled underground spaces; dissolution and lava history have separate prerequisites. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLCaveSettings
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 AlgorithmVersion=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Character=.5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Seed=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="10")) double NetworksPerSquareKilometer=.4;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="1",ClampMax="500")) double LengthMeters=87.5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="0.1",ClampMax="10")) double RadiusMeters=2.15;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="1",ClampMax="100")) double MinimumCoverMeters=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="1",ClampMax="500")) double DepthMeters=26;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Branching=.575;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bSurfaceEntrances=true;
    void ApplyProgression(double Value);
    bool Validate(FString& Error)const;
};

/** Placement data and native cutters share one immutable result. No preview-only cave geometry. */
class FCLNativeNoiseKernel;
class FCLCaveVolumeData;

/** A passage's correlated sections and curve are geological construction data, not a mesh. */
struct CATALYSTLANDFORM_API FCLCavePassage
{
    FGuid Network;
    int32 From=INDEX_NONE,To=INDEX_NONE;
    TArray<FVector> Points;
    /** Half width / half height at each curve control point, in metres. */
    TArray<FVector2D> Sections;
    bool Entrance=false;
};
struct CATALYSTLANDFORM_API FCLCaveChamber
{
    FGuid Network;
    int32 Node=INDEX_NONE;
    FVector Center,HalfExtents;
    FQuat Rotation;
};

/** Native VPP spline sampling, shape kernels, smooth blends and spatial index. */
class CATALYSTLANDFORM_API FCLCompiledCaveVolume
{
public:
    static TSharedPtr<const FCLCompiledCaveVolume> Compile(TConstArrayView<FCLCavePassage>,TConstArrayView<FCLCaveChamber>,TSharedPtr<const FCLNativeNoiseKernel>,int32 Seed,FString& Error);
    double Compose(const FVector3d& P,double Field,FCLSurfaceWeights* Weights=nullptr)const;
    void ComposeBatch(TConstArrayView<FVector3d>,TArrayView<double>)const;
    TSharedPtr<const FCLCompiledCaveVolume> Select(FGuid Network)const;
    FBox Bounds(FGuid Network=FGuid())const;
    double MinimumHalfWidth(FGuid Network=FGuid())const;
private:
    TSharedPtr<const FCLCaveVolumeData> Data;
    FGuid SelectedNetwork;
};

class CATALYSTLANDFORM_API FCLCompiledCaves
{
public:
    /** Conservative control-polygon envelopes for diagnostics; native splines define the volume. */
    struct FSegment {FGuid Network;FVector A,B;double Radius=0;FVector Scale=FVector::OneVector;bool Entrance=false;};
    TArray<FSegment> Segments;
    struct FNode {FGuid Network;FVector Position;FVector2D Section;bool Chamber=false,Portal=false;};
    TArray<FNode> Nodes;
    TArray<FCLCavePassage> Passages;
    TArray<FCLCaveChamber> Chambers;
    TSharedPtr<const FCLCompiledCaveVolume> Cutters;
    int32 Candidates=0,EligibleCandidates=0,Networks=0,Entrances=0;
    FString Readiness;
    static TSharedPtr<const FCLCompiledCaves> Compile(const FCLCaveSettings&,const FCLCompiledPlanetRecipe&,int32 PlanetSeed,FString& Error);
};

#pragma once
#include "CoreMinimal.h"
#include "Scene/CLVolumes.h"
#include "CLCaves.generated.h"
struct FCLCompiledPlanetRecipe;

/** First cave family: structurally controlled fissures, not dissolution or lava history. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLCaveSettings
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 AlgorithmVersion=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Character=.5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Seed=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="10")) double NetworksPerSquareKilometer=.4;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="1",ClampMax="500")) double LengthMeters=55;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="0.1",ClampMax="10")) double RadiusMeters=.9;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="1",ClampMax="100")) double MinimumCoverMeters=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="1",ClampMax="500")) double DepthMeters=25;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Branching=.35;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bSurfaceEntrances=true;
    void ApplyProgression(double Value);
    bool Validate(FString& Error)const;
};

/** Placement data and native cutters share one immutable result. No preview-only cave geometry. */
class CATALYSTLANDFORM_API FCLCompiledCaves
{
public:
    struct FSegment {FGuid Network;FVector A,B;double Radius=0;FVector Scale=FVector::OneVector;bool Entrance=false;};
    TArray<FSegment> Segments;
    TSharedPtr<const FCLCompiledVolumes> Cutters;
    int32 Candidates=0,EligibleCandidates=0,Networks=0,Entrances=0;
    FString Readiness;
    static TSharedPtr<const FCLCompiledCaves> Compile(const FCLCaveSettings&,const FCLCompiledPlanetRecipe&,int32 PlanetSeed,FString& Error);
};

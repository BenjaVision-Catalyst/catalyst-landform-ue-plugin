#pragma once
#include "CoreMinimal.h"
class FCLCompiledRock;
class FCLNativeNoiseKernel;
class FCLSpatialCaveData;
struct FCLCompiledPlanetRecipe;
class FCLCompiledCaves;

/** Opt-in morphology study: spatial bedding/fracture volumes, never swept sections.
 * Immutable worker data. Native VPP retains all meshing, LOD, collision and edits. */
class CATALYSTLANDFORM_API FCLSpatialCave
{
public:
    static TSharedPtr<const FCLCompiledCaves> BuildPrototype(const FCLCompiledPlanetRecipe&,int32 Seed,FString& Error);
    void Sample(TConstArrayView<FVector>,TArray<double>& Air,bool bUseCache=true)const;
    /** Bounded memoization of this immutable base field, before live native edits. */
    void GetSampleCacheStats(uint64& Hits,uint64& Misses,uint64& Bytes)const;
    /** Cave-first evaluation; only request terrain where its proven radial bound can win. */
    void SamplePlanet(const FCLCompiledPlanetRecipe&,TConstArrayView<FVector>,TArray<double>& Fields,int32* HeightPoints=nullptr)const;
    /** Native collision demand covering this finite composition's complete floor,
     * in body metres. Activation owns these requests; VPP owns their collision. */
    TArray<FSphere> FloorCollisionDemandMeters()const;
    FBox Bounds()const;
    FVector BodyPoint(const FVector& BeddingPosition)const;
    FVector BeddingPoint(const FVector& BodyPosition)const;
    double FloorHeight(double X,double Y)const;
private:
    TSharedPtr<const FCLSpatialCaveData> Data;
};

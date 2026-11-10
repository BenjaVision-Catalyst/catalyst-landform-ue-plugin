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
    void Sample(TConstArrayView<FVector>,TArray<double>& Air,bool bCompiledInputs=true)const;
    /** Cave-first evaluation; only request terrain where its proven radial bound can win. */
    void SamplePlanet(const FCLCompiledPlanetRecipe&,TConstArrayView<FVector>,TArray<double>& Fields,int32* HeightPoints=nullptr)const;
    void GetBeddingCompilationStats(int32& Nodes,int32& ExactLeaves,uint64& Bytes,double& Error)const;
    void GetSurfaceCompilationStats(int32& Nodes,int32& ExactLeaves,uint64& Bytes,uint64& Interpolated,uint64& Exact)const;
    void SampleSurfaceHeights(const FCLCompiledPlanetRecipe&,TConstArrayView<FVector>,TArray<double>&)const;
    void GetEvaluationSeconds(TArray<double>& Seconds)const;
    FBox Bounds()const;
    FVector BodyPoint(const FVector& BeddingPosition)const;
    FVector BeddingPoint(const FVector& BodyPosition,bool bCompiled=true)const;
    double FloorHeight(double X,double Y)const;
private:
    TSharedPtr<const FCLSpatialCaveData> Data;
};

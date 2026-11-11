#pragma once
struct FCLSurfaceWeights;
#include "CoreMinimal.h"
class FCLCompiledRock;
class FCLNativeNoiseKernel;
class FCLSpatialCaveData;
struct FCLCompiledPlanetRecipe;
class FCLCompiledCaves;
struct FCLCaveSettings;

/** Opt-in morphology study: spatial bedding/fracture volumes, never swept sections.
 * Immutable worker data. Native VPP retains all meshing, LOD, collision and edits. */
class CATALYSTLANDFORM_API FCLSpatialCave
{
public:
    static TSharedPtr<const FCLCompiledCaves> BuildPrototype(const FCLCompiledPlanetRecipe&,int32 Seed,FString& Error);
    static TSharedPtr<const FCLCompiledCaves> BuildFamily(const FCLCompiledPlanetRecipe&,const FCLCaveSettings&,int32 Seed,FString& Error);
    bool IsProceduralFamily()const;
    bool TryPreparedSurfaces(const FVector&,FCLSurfaceWeights&)const;
    void GetPreparedGeologyStats(uint64&,uint64&)const;
    void GetFamilyStats(int32& Levels,int32& Cells,int32& Ramps)const;
    void SampleSelected(TConstArrayView<FVector>,TArray<double>&,const TSet<FGuid>&)const;
    void Sample(TConstArrayView<FVector>,TArray<double>& Air,bool bCompiledInputs=true)const;
    /** Cave-first evaluation; only request terrain where its proven radial bound can win. */
    void SamplePlanet(const FCLCompiledPlanetRecipe&,TConstArrayView<FVector>,TArray<double>& Fields,int32* HeightPoints=nullptr)const;
    void GetBeddingCompilationStats(int32& Nodes,int32& ExactLeaves,uint64& Bytes,double& Error)const;
    void GetSurfaceCompilationStats(int32& Nodes,int32& ExactLeaves,uint64& Bytes,uint64& Interpolated,uint64& Exact)const;
    void SampleSurfaceHeights(const FCLCompiledPlanetRecipe&,TConstArrayView<FVector>,TArray<double>&)const;
    void GetEvaluationSeconds(TArray<double>& Seconds)const;
    FBox Bounds(FGuid Network=FGuid())const;
    double MinimumHalfWidth(FGuid Network=FGuid())const;
    void GetCellSupportBounds(TArray<FBox>& Out)const;
    FVector BodyPoint(const FVector& BeddingPosition)const;
    FVector BeddingPoint(const FVector& BodyPosition,bool bCompiled=true)const;
    double FloorHeight(double X,double Y)const;
private:
    friend class FCLSpatialCaveFamily;
    static TSharedPtr<const FCLCompiledCaves> BuildNetworkAt(const FCLCompiledPlanetRecipe&,const FCLCaveSettings&,int32,const FVector&,double,FString&,bool FollowBedding=true,int32 RequestedLevels=0);
    static TSharedPtr<const FCLCompiledCaves> CompileComposition(const FCLCompiledPlanetRecipe&,TSharedPtr<FCLSpatialCaveData>,int32,FString&,bool);
    TSharedPtr<const FCLSpatialCaveData> Data;
};

#pragma once
#include "Model/CLPlanetRecipe.h"
#include "CLPlanetStamp.h"
#include "UObject/StrongObjectPtr.h"
class UCLStampLibraryAsset;
class FVoxelSurfaceTypeTable;

/** Immutable runtime adapter; native sources and their data are frozen on the game thread. */
class FCLCompiledStampDistribution
{
public:
    TArray<FCLResolvedStampPlacement> Placements;
    double MinimumDeltaMeters = 0, MaximumDeltaMeters = 0;
    void Coverage(const FVector& Direction,int32& Count,double& MinimumWeight) const;
    void Apply(TConstArrayView<FVector> Directions, TArrayView<double> Heights) const;
private:
    bool bLocal = false;
    TArray<TStrongObjectPtr<UObject>> Assets;
    TSharedPtr<const FCLPlanetStampRuntime> Runtime;
    TSharedPtr<const FVoxelSurfaceTypeTable> Surfaces;
    friend class FCLStampDistributionCompiler;
};
class FCLStampDistributionCompiler
{
public:
    static TSharedPtr<const FCLCompiledStampDistribution> Compile(const FCLStampDistributionSettings& Settings,
        int32 Seed, const FCLCompiledPlanetRecipe& Input, UCLStampLibraryAsset* Library, FString& Error,
        double MinimumRelief = 0, double MaximumRelief = 0);
};

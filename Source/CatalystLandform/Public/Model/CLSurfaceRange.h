#pragma once
#include "CoreMinimal.h"
struct FCLCompiledPlanetRecipe;
struct FCLPlanetRecipe;
class FCLCompiledSurfaceDistribution;
/** One planet-global remapping, applied after every surface operation and blend. */
struct CATALYSTLANDFORM_API FCLSurfaceRange
{
    double SourceMin=0,SourceMax=0,TargetMin=0,TargetMax=0;
    FVector MinimumDirection,MaximumDirection;
    TSharedPtr<const FCLCompiledSurfaceDistribution> Distribution;
    double Map(double Height) const;
    double Map(double Height,const FVector& Direction) const;
    void MapHeights(TConstArrayView<FVector> Directions,TArray<double>& Heights) const;
    static TSharedPtr<const FCLSurfaceRange> Compile(const FCLCompiledPlanetRecipe& Surface,double Low,double High,FString& Error,const FCLPlanetRecipe* DistributionRecipe=nullptr);
};

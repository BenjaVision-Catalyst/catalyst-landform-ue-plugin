#pragma once
#include "CoreMinimal.h"
struct FCLCompiledPlanetRecipe;
/** One planet-global remapping, applied after every surface operation and blend. */
struct CATALYSTLANDFORM_API FCLSurfaceRange
{
    double SourceMin=0,SourceMax=0,TargetMin=0,TargetMax=0;
    FVector MinimumDirection,MaximumDirection;
    double Map(double Height) const;
    static TSharedPtr<const FCLSurfaceRange> Compile(const FCLCompiledPlanetRecipe& Surface,double Low,double High,FString& Error);
};

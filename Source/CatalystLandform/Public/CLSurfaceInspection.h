#pragma once
#include "CoreMinimal.h"
struct FCLCompiledPlanetRecipe;
/** A recipe-space location. Distances use UE centimetres; directions are planet-local. */
struct CATALYSTLANDFORM_API FCLSurfaceLanding
{
    FVector Direction=FVector::ZeroVector,PositionCm=FVector::ZeroVector,Normal=FVector::ZeroVector;
    double AdjustmentMeters=0;
};
struct CATALYSTLANDFORM_API FCLSurfaceInspection
{
    /** Bounded nearest-ring search (0, 2, 5, 10, 20 m). Collision remains the host's responsibility. */
    static bool FindLanding(const FCLCompiledPlanetRecipe& Plan,const FVector& Requested,FCLSurfaceLanding& Out);
};

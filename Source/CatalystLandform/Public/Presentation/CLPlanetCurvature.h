#pragma once
#include "CoreMinimal.h"
struct FCLCompiledPlanetRecipe;

/** Reversible view policy in canonical metres: SurfaceMax -> ground, outer Sky -> natural sphere. */
struct CATALYSTLANDFORM_API FCLPlanetCurvature
{
    static bool Evaluate(const FCLCompiledPlanetRecipe& Plan,double ViewRadiusMeters,double GroundCurvature,
        double& Curvature,double& SurfaceMaxMeters,double& SkyMeters,FString& Error);
};

#pragma once
#include "CoreMinimal.h"
#include "Model/CLPlanetRecipe.h"
#include "Scene/CLSceneWindow.h"

/** Synthetic scale qualification. Surface radius varies; metre geometry does not. */
struct CATALYSTLANDFORM_API FCLScaleFixture
{
    double SurfaceRadiusMeters=100;
    FCLPlanetRecipe Recipe;
    FCLBodyFrame FeatureFrame;
    FCLSceneWindow Window;
    static FCLScaleFixture Make(double SurfaceRadiusMeters,int32 Site=0);
    static double Radius(int32 Index);
    static FString RadiusName(int32 Index);
    static FVector SiteDirection(int32 Site);
    FVector Footing(int32 Index) const;
};

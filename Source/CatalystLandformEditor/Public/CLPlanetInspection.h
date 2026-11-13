#pragma once
#include "CoreMinimal.h"
class UCLPlanetRecipeAsset;
/** Immutable editor-to-host handoff. The host owns streaming, collision and movement. */
struct CATALYSTLANDFORMEDITOR_API FCLPlanetInspectionRequest
{
    FGuid Id=FGuid::NewGuid();
    FString RecipeJson;
    TWeakObjectPtr<UCLPlanetRecipeAsset> SourceAsset;
    FVector Direction=FVector::ZeroVector;
    TOptional<FVector> SurfacePointMeters;
    TFunction<void(FGuid,FVector,FString)> OnReturned;
    TFunction<void(FGuid,FVector)> OnPositionReturned;
};
DECLARE_DELEGATE_RetVal_TwoParams(bool,FCLLaunchPlanetInspection,FCLPlanetInspectionRequest,FString&);
/** One explicitly registered walking host per editor. No runtime dependency on editor UI. */
struct CATALYSTLANDFORMEDITOR_API FCLPlanetInspection
{
    static FCLLaunchPlanetInspection Launch;
    static void Startup();
    static void Shutdown();
};

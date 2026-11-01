#pragma once
#include "CoreMinimal.h"
/** Immutable editor-to-host handoff. The host owns streaming, collision and movement. */
struct CATALYSTLANDFORMEDITOR_API FCLPlanetInspectionRequest
{
    FGuid Id=FGuid::NewGuid();
    FString RecipeJson;
    FVector Direction=FVector::ZeroVector;
    TFunction<void(FGuid,FVector,FString)> OnReturned;
};
DECLARE_DELEGATE_RetVal_TwoParams(bool,FCLLaunchPlanetInspection,FCLPlanetInspectionRequest,FString&);
/** One explicitly registered walking host per editor. No runtime dependency on editor UI. */
struct CATALYSTLANDFORMEDITOR_API FCLPlanetInspection
{
    static FCLLaunchPlanetInspection Launch;
};

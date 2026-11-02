#pragma once
#include "CoreMinimal.h"
#include "Model/CLPlanetRecipe.h"

/** Display-only measurements. Recipe radii are metres; guide heights are radius fractions. */
struct CATALYSTLANDFORMEDITOR_API FCLRadialGuideMeasurements
{
    struct FSpan { int32 Low, High; double Meters; bool bTotal; };
    static FText Meters(double Value);
    static FText Height(const FCLPlanetRecipe& Recipe, int32 Index);
    static TArray<FSpan> SurfaceSpans(const FCLPlanetRecipe& Recipe);
};

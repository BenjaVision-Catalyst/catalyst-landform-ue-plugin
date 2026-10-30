#pragma once
#include "CoreMinimal.h"
#include "Model/CLPlanetRecipe.h"

/** Clean Cartesian cut through the upper planet, from its centre plane to its surface. */
struct CATALYSTLANDFORMEDITOR_API FCLSectionGeometry
{
    TArray<FVector> Vertices; // cm, planet centre at origin
    TArray<int32> Triangles; // UE clockwise outward faces
    TArray<FLinearColor> Colors;
    int32 SurfaceVertexCount = 0;
    double HalfWidthMeters = 0;

    static double HalfWidth(const FCLCompiledPlanetRecipe& Plan);
    /** Preview only: retain radial height while displaying one third of its sag. */
    static FVector DisplaySurfacePoint(const FVector& SourcePoint);
    static FVector SurfacePoint(const FCLCompiledPlanetRecipe& Plan, double X, double Y, double Heading);
    static FCLSectionGeometry Build(const FCLCompiledPlanetRecipe& Plan, double Heading);
};

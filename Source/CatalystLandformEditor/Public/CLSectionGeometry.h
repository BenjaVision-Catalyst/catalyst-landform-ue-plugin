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
    /** Reference tick position, including the schematic continuation to the core. */
    static FVector StratumPoint(const FCLCompiledPlanetRecipe& Plan, const FVector2D& Edge, double Normalized);
    /** Inverse used by direct dragging; input Y is a fraction of viewport height. */
    static double StratumAtScreenY(const FCLCompiledPlanetRecipe& Plan, const FVector2D& Edge, const FMatrix& ViewProjection, double Y);
    static FVector SurfacePoint(const FCLCompiledPlanetRecipe& Plan, double X, double Y, double Heading);
    static FCLSectionGeometry Build(const FCLCompiledPlanetRecipe& Plan, double Heading);
};

#pragma once
#include "CoreMinimal.h"
#include "Model/CLPlanetRecipe.h"

/** Unwrapped surface-distance square with a schematic core-to-sky cutaway. */
struct CATALYSTLANDFORMEDITOR_API FCLSectionGeometry
{
    TArray<FVector> Vertices; // display cm; core plane at zero
    TArray<FVector> SourceSurfacePoints; // actual planet-space cm, before display unwrapping
    TArray<int32> Triangles; // UE clockwise outward faces
    TArray<FLinearColor> Colors;
    int32 SurfaceVertexCount = 0;
    double HalfWidthMeters = 0;

    /** Twice the previous footprint, within the 100–1000m viewing range. */
    static double DefaultWidth(const FCLCompiledPlanetRecipe& Plan);
    /** Great-circle distances are measured on the sphere through the patch centre. */
    static double ReferenceSurfaceRadius(const FCLCompiledPlanetRecipe& Plan);
    static FVector SourceSurfacePoint(const FCLCompiledPlanetRecipe& Plan, double X, double Y, double Heading);
    /** Display position for a radial reference; one-third sag, continued to the core. */
    static FVector StratumPoint(const FCLCompiledPlanetRecipe& Plan, const FVector2D& Edge, double Normalized);
    /** Inverse used by direct dragging; input Y is a fraction of viewport height. */
    static double StratumAtScreenY(const FCLCompiledPlanetRecipe& Plan, const FVector2D& Edge, const FMatrix& ViewProjection, double Y);
    static FVector SurfacePoint(const FCLCompiledPlanetRecipe& Plan, double X, double Y, double Heading);
    static FCLSectionGeometry Build(const FCLCompiledPlanetRecipe& Plan, double Heading, double WidthMeters = 0);
};

#pragma once
#include "CoreMinimal.h"
#include "Model/CLPlanetRecipe.h"

/** Unwrapped surface-distance square with a schematic core-to-sky cutaway. */
struct CATALYSTLANDFORMEDITOR_API FCLSectionGeometry
{
    // Fixed presentation units: physical area changes sampling, never the frame.
    static constexpr double DisplaySize = 1000;
    TArray<FVector> Vertices; // display units * 100; core plane at zero
    TArray<FVector> SourceSurfacePoints; // actual planet-space cm, before display unwrapping
    TArray<int32> Triangles; // UE clockwise outward faces
    TArray<FLinearColor> Colors;
    int32 SurfaceVertexCount = 0;
    double HalfWidthMeters = 0;

    /** Initial 500m square viewing area for every planet context. */
    static double DefaultWidth(const FCLCompiledPlanetRecipe& Plan);
    /** Great-circle distances are measured on the sphere through the patch centre. */
    static double ReferenceSurfaceRadius(const FCLCompiledPlanetRecipe& Plan);
    static FVector SourceSurfacePoint(const FCLCompiledPlanetRecipe& Plan, double X, double Y, double Heading);
    /** Fixed display position for a normalized radial reference, independent of sampling area. */
    static FVector StratumPoint(const FVector2D& Edge, double Normalized);
    /** Inverse used by direct dragging; input Y is a fraction of viewport height. */
    static double StratumAtScreenY(const FVector2D& Edge, const FMatrix& ViewProjection, double Y);
    static FVector SurfacePoint(const FCLCompiledPlanetRecipe& Plan, double X, double Y, double Heading, double WidthMeters = 0);
    static FCLSectionGeometry Build(const FCLCompiledPlanetRecipe& Plan, double Heading, double WidthMeters = 0);
};

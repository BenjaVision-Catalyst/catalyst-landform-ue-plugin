#pragma once
#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Math/InterpCurve.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLCompileProgress.h"

/** Water surface and optional drainage paths from an accepted watershed, in body metres. */
struct CATALYSTLANDFORM_API FCLWatershedPresentationGeometry
{
    struct FBody
    {
        double HeightMeters=0;
        bool Ocean=false;
        int32 Cells=0,Triangles=0;
        FVector InteriorDirection=FVector::ZeroVector;
        double MaximumDepthMeters=0;
    };
    struct FShore {FVector A,B;int32 Body=0;};
    struct FPath {FInterpCurveVector Curve;TArray<int32> Cells;bool Channel=false;};
    TSharedPtr<const FCLCompiledWatershed> Source;
    UE::Geometry::FDynamicMesh3 Water;
    TArray<FBody> Bodies;
    TArray<FShore> Shores;
    TArray<FPath> Paths;
    int32 RoutingEdges=0;
    double Seconds=0;
    /** Uses the accepted analysis plus native height samples, inside the existing evaluation task. */
    static TSharedPtr<const FCLWatershedPresentationGeometry> Build(const FCLCompiledPlanetRecipe&,
        FCLCompileProgress* Control=nullptr, bool bIncludeDrainagePaths=true, bool bOverlapRenderedShore=false);
    /** Find water basins on an accepted spherical source without conditioning its terrain. */
    static TSharedPtr<const FCLWatershedPresentationGeometry> BuildUnconditionedSurface(
        const FCLCompiledPlanetRecipe&, FCLCompileProgress* Control, bool bOverlapRenderedShore=false);

private:
    static TSharedPtr<const FCLWatershedPresentationGeometry> BuildAnalyzed(
        const FCLCompiledPlanetRecipe&, const TSharedPtr<const FCLCompiledWatershed>&,
        FCLCompileProgress*, bool bIncludeDrainagePaths, bool bOverlapRenderedShore);
};

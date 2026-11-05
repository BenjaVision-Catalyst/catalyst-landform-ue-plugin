#pragma once
#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Math/InterpCurve.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLCompileProgress.h"

/** Derived editor diagnostics, in body metres. No terrain producer or water simulation. */
struct FCLWatershedDebugGeometry
{
    struct FBody {double HeightMeters=0;bool Ocean=false;int32 Cells=0,Triangles=0;};
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
    static TSharedPtr<const FCLWatershedDebugGeometry> Build(const FCLCompiledPlanetRecipe&,
        FCLCompileProgress* Control=nullptr);
};

#if WITH_DEV_AUTOMATION_TESTS
/** Bounded analytic terrain fixture, not a library asset or alternate generation path. */
FCLPlanetRecipe CLWatershedDebugRecipe(FString& Error);
#endif

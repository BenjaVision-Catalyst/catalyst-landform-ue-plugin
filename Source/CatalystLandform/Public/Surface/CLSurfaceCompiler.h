#pragma once
#include "CoreMinimal.h"
#include "Model/CLSurfaceLayers.h"
#include "Surface/CLSurfaceLibrary.h"
struct FCLHeightGuide;
struct FCLCompiledPlanetRecipe;
struct CATALYSTLANDFORM_API FCLSurfaceWeights
{
    double Values[FCLCompiledSurfaceLibrary::MaxSurfaces]={};
    void Lerp(const FCLSurfaceWeights& Other,double Alpha);
};
struct CATALYSTLANDFORM_API FCLCompiledSurfaceStack
{
    struct FLayer {int32 Index=0;FVector2D Slope;double Feather=5,Low=-MAX_dbl,High=MAX_dbl,HeightFeather=1,Strength=1,Noise=0,Wavelength=80;FVector Offset;};
    int32 DefaultIndex=INDEX_NONE;
    TArray<FLayer> Layers;
    FCLSurfaceWeights Evaluate(const FVector& PositionMeters,double HeightMeters,double SlopeDegrees) const;
    static bool Compile(const FCLSurfaceStack& Stack,const FCLCompiledSurfaceLibrary& Library,TConstArrayView<FCLHeightGuide> Guides,double Radius,int32 Seed,FCLCompiledSurfaceStack& Out,FString& Error);
};

/** Canonical radial slope stencil shared by native sampling and the globe mesh. */
CATALYSTLANDFORM_API void CLSurfaceProbe(const FVector& Direction,TArray<FVector>& Samples);
CATALYSTLANDFORM_API FVector CLSurfaceNormal(TConstArrayView<FVector> Samples,TConstArrayView<double> Heights,double Radius,int32 Start);

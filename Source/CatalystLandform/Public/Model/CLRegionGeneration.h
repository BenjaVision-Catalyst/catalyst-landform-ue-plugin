#pragma once
#include "CoreMinimal.h"
#include "Model/CLPlanetRecipe.h"

struct CATALYSTLANDFORM_API FCLGeneratedRegionCell
{
    FGuid Id;
    FVector Centre;
    FQuat Frame;
    double Stretch=1;
    FVector StructureSignature;
    double Uplift=0,Basin=0,StructuralEdge=0,HeightMeters=0,SlopeDegrees=0,Scale=0;
    int32 PaletteIndex=INDEX_NONE;
    bool bOverride=false;
    double Score=0;
    FString Reason;
};
struct CATALYSTLANDFORM_API FCLGeneratedRegionBoundary
{
    FVector A,B;
    int32 CellA=0,CellB=0;
};
/** Plain immutable partition shared by all six patches, globe, cutaway and VPP queries. */
class CATALYSTLANDFORM_API FCLCompiledRegionGeneration
{
public:
    TArray<FCLGeneratedRegionCell> Cells;
    TArray<FCLLandformPaletteEntry> Palette;
    TArray<TSharedPtr<const FCLCompiledPlanetRecipe>> Fields;
    TArray<FCLGeneratedRegionBoundary> Boundaries;
    TArray<FCLCompiledPlanetFeature> Structure;
    double Guidance=0,Band=0,Strength=1,NeutralHeight=0;
    FVector Signature(const FVector& Direction) const;
    double Cost(int32 Cell,const FVector& Direction,const FVector& Signature) const;
    int32 Owner(const FVector& Direction) const;
    void CellWeights(const FVector& Direction,TArray<double>& Weights) const;
    void PaletteWeights(const FVector& Direction,TArray<double>& Weights) const;
    void Blend(TConstArrayView<FVector> Directions,TArray<double>& Heights) const;
    static bool ValidateSettings(const FCLRegionGenerationSettings& Settings,FString& Error);
    static TSharedPtr<const FCLCompiledRegionGeneration> Compile(const FCLPlanetRecipe& Recipe,const FCLCompiledPlanetRecipe& Base,UCLStampLibraryAsset* Library,FString& Error);
private:
    void Weights(const FVector& Direction,double* CellWeights,double* PaletteWeights) const;
};

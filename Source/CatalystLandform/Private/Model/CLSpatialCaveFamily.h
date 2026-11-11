#pragma once
#include "CoreMinimal.h"
class FCLSpatialCave;
class FCLCompiledRock;
class FCLCompiledCaves;
struct FCLCaveSettings;
struct FCLCompiledPlanetRecipe;
struct FCLSurfaceWeights;

/** Placement of the approved profile-8 anatomy. No alternate morphology or ring layout. */
class FCLSpatialCaveFamily
{
public:
    static TSharedPtr<const FCLSpatialCaveFamily> Build(const FCLCompiledPlanetRecipe&,const FCLCaveSettings&,int32,FCLCompiledCaves&,FString&);
    void Sample(TConstArrayView<FVector>,TArray<double>&,bool Compiled=true,const TSet<FGuid>* Selection=nullptr)const;
    FBox Bounds(FGuid Network=FGuid())const;
    void CellSupportBounds(TArray<FBox>&)const;
    bool TrySurfaces(const FVector&,FCLSurfaceWeights&)const;
    void GeologyStats(uint64& Prepared,uint64& Exact)const{Prepared=Exact=0;}
    FVector BodyPoint(FVector,int32 Network=0)const;
    FVector BeddingPoint(FVector,int32 Network=0,bool Compiled=true)const;
    double FloorHeight(double,double,int32 Level=0,int32 Network=0)const;
    void Stats(int32&,int32&,int32&,int32&,int32&,uint64&,double&)const;
private:
    struct FNetwork {FGuid Id;TSharedPtr<const FCLSpatialCave> Cave;FBox Bounds;};
    TArray<FNetwork> Networks;
    FBox AllBounds=FBox(ForceInit);
};

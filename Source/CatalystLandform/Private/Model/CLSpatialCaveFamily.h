#pragma once
#include "CoreMinimal.h"
#include "Model/CLDirectionalScalarField.h"
#include "Polygon2.h"
#include <atomic>
class FVoxelAABBTree;
class FCLCompiledRock;
class FCLNativeNoiseKernel;
class FCLCompiledCaves;
struct FCLCaveSettings;
struct FCLCompiledPlanetRecipe;
struct FCLSurfaceWeights;

/** Immutable spatial dissolution cells. Route metadata never sweeps a volume. */
class FCLSpatialCaveFamily
{
public:
    static TSharedPtr<const FCLSpatialCaveFamily> Build(const FCLCompiledPlanetRecipe&,const FCLCaveSettings&,int32,FCLCompiledCaves&,FString&);
    void Sample(TConstArrayView<FVector>,TArray<double>&,bool Compiled=true,const TSet<FGuid>* Selection=nullptr)const;
    FBox Bounds(FGuid Network=FGuid())const;
    void CellSupportBounds(TArray<FBox>& Out)const;
    bool TrySurfaces(const FVector&,FCLSurfaceWeights&)const;
    void GeologyStats(uint64& Prepared,uint64& Exact)const{Prepared=PreparedSurfaces.load();Exact=ExactSurfaces.load();}
    FVector BodyPoint(FVector,int32 Network=0)const;
    FVector BeddingPoint(FVector,int32 Network=0,bool Compiled=true)const;
    double FloorHeight(double,double,int32 Level=0,int32 Network=0)const;
    void Stats(int32& Levels,int32& Cells,int32& Ramps,int32& Nodes,int32& Exact,uint64& Bytes,double& Error)const;
private:
    struct FCell
    {
        UE::Geometry::FPolygon2d Polygon;
        TArray<double> X,Y;
        FBox Bounds,BodyBounds;
        FVector2D A,B,Center;
        double FloorA=0,FloorB=0,Roof=0,Scale=1,Phase=0;
        bool Chamber=false;
    };
    struct FNetwork
    {
        FGuid Id;
        FVector Origin,X,Y,Up;
        double Datum=0,Spacing=0,Phase=0;
        int32 Levels=1;
        FBox BodyBounds;
        TArray<FCell> Cells;
        TArray<double> Contacts;
        TArray<double> Params,VertexX,VertexY;TArray<float> Edges,EdgeGrids;TArray<uint32> EdgeMasks;TArray<int32> EdgeTiles;TArray<int32> Starts,Counts;
        FCLDirectionalScalarField Bedding,Basement;
        FBox2D PlanarBounds;FIntPoint PlanarSize=FIntPoint::ZeroValue;FVector2D PlanarStep;
        TArray<double> PlanarDistanceLowerBounds;
        TArray<float> BodyBoxes,SupportGrid;TArray<int32> SupportSize;TArray<uint64> SupportMasks;
        double PlanarSupportPadding=0;
        TSharedPtr<const FVoxelAABBTree> Index;
    };
    TArray<FNetwork> Networks;
    TArray<FIntPoint> Features;
    TSharedPtr<const FVoxelAABBTree> Index;
    TSharedPtr<const FCLCompiledRock> Rock;
    TSharedPtr<const FCLNativeNoiseKernel> Noise;
    mutable std::atomic<uint64> PreparedSurfaces{0},ExactSurfaces{0};
    int32 Seed=0;
    FBox AllBounds=FBox(ForceInit);
    double RollingFloor(const FNetwork&,double,double)const;
    double CellFloor(const FNetwork&,const FCell&,double,double)const;
    void SampleCell(const FNetwork&,const FCell&,TConstArrayView<FVector>,TConstArrayView<float>,TConstArrayView<float>,TConstArrayView<float>,TArray<double>&,bool)const;
};

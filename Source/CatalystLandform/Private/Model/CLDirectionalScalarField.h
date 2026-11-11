#pragma once
#include "CoreMinimal.h"

/** Immutable local chart for a slowly varying directional input. This contains
 * scalar recipe data, never voxel samples, meshes, collision or live edits. */
class FCLDirectionalScalarField
{
public:
    void Build(const FBox2D& Bounds,TFunction<double(const FVector2D&)> Exact,TFunction<void(TConstArrayView<FVector2D>,TArray<double>&)> Batch={},double ToleranceMeters=.000025);
    void SetMaximumDepth(int32 Depth){MaximumDepth=FMath::Clamp(Depth,1,12);}
    void BuildLookup();
    void BuildPacked();
    const TArray<double>& PackedNodes()const{return Packed;}
    const TArray<int32>& PackedLinks()const{return Links;}
    const TArray<int32>& PackedLookup()const{return LeafLookup;}
    double Sample(const FVector2D& P,bool* bUsedExact=nullptr)const;
    bool TrySample(const FVector2D& P,double& Value)const;
    bool Contains(const FVector2D& P)const{return Nodes[0].Bounds.IsInside(P);}
    int32 NumNodes()const{return Nodes.Num();}
    int32 NumExactLeaves()const;
    int64 GetAllocatedSize()const{return Nodes.GetAllocatedSize()+LeafLookup.GetAllocatedSize()+Packed.GetAllocatedSize()+Links.GetAllocatedSize();}
    double MaximumValidationError()const{return MaxError;}
private:
    struct FNode
    {
        FBox2D Bounds;
        double Coefficients[16]{};
        int32 Children=INDEX_NONE;
        bool bExact=false;
        double Sample(const FVector2D& P)const;
    };
    void BuildNode(int32 Index,int32 Depth);
    TArray<double> Packed;TArray<int32> Links;
    TArray<FNode> Nodes;
    TArray<int32> LeafLookup;
    int32 MaximumDepth=10;
    TFunction<double(const FVector2D&)> Exact;
    TFunction<void(TConstArrayView<FVector2D>,TArray<double>&)> Batch;
    double MaxError=0,Tolerance=.000025;
};

#include "Scene/CLVolumes.h"
#include "Surface/CLSurfaceCompiler.h"
#include "Shape/VoxelSphereShape.h"
#include "Shape/VoxelCubeShape.h"
#include "VoxelBlendModeUtilities.h"
#include "VoxelAABBTree.h"

// Native buffers use metres here; the outer VPP stamp adapter owns cm conversion.
class FCLNativeVolumeIndex
{
public:
    TSharedPtr<const FVoxelAABBTree> Tree;
};

void FCLCompiledVolumes::FOperation::SampleFields(TConstArrayView<FVector3d> Positions,TArray<double>& Fields) const
{
    FVoxelDoubleVectorBuffer Local;Local.Allocate(Positions.Num());
    for(int32 I=0;I<Positions.Num();++I)
    {
        FVector3d P=Transform.InverseTransformPosition(Positions[I]);
        // VPP 2.0p8 has sphere/cube/plane shapes, but no capsule shape. Keep only
        // the segment-coordinate specialisation; VPP evaluates the sphere field.
        if(Source.Primitive==ECLSolidPrimitive::Capsule)P.Z-=FMath::Clamp(P.Z,-Source.HalfLengthMeters,Source.HalfLengthMeters);
        Local.Set(I,P);
    }
    FVoxelFloatBuffer Native;Native.Allocate(Positions.Num());
    if(Source.Primitive==ECLSolidPrimitive::Box)
    {FVoxelCubeShape Shape;Shape.Size=Source.HalfExtentsMeters*2;Shape.Sample(Native.View(),Local);}
    else
    {FVoxelSphereShape Shape;Shape.Radius=Source.RadiusMeters;Shape.Sample(Native.View(),Local);}
    Fields.SetNumUninitialized(Positions.Num());
    for(int32 I=0;I<Positions.Num();++I)Fields[I]=double(Native[I])*MinimumScale;
}

double FCLCompiledVolumes::FOperation::Field(const FVector3d& Position) const
{
    TArray<double> Fields;SampleFields(MakeArrayView(&Position,1),Fields);return Fields[0];
}

namespace
{
double NativeBlend(const FCLCompiledVolumes::FOperation& O,double A,double B,float* Alpha)
{
    const auto Mode=O.Source.Composition==ECLSolidComposition::Subtract?EVoxelVolumeBlendMode::Subtractive:EVoxelVolumeBlendMode::Additive;
    return FVoxelBlendModeUtilities::ComputeDistance(FVoxelVolumeTransform(),float(A),float(B),Mode,true,float(O.Source.SmoothnessMeters),Alpha);
}
bool CanSkip(const FCLCompiledVolumes::FOperation& O,const FVector3d& P,double A,bool InBounds)
{
    if(InBounds)return false;
    const double Lower=FMath::Sqrt(O.Bounds.ComputeSquaredDistanceToPoint(P))*O.MinimumScale/O.Source.Scale.GetMax();
    const double Threshold=(O.Source.Composition==ECLSolidComposition::Subtract?-A:A)+O.Source.SmoothnessMeters;
    return Lower>=Threshold;
}
}

TSharedPtr<const FCLCompiledVolumes> FCLCompiledVolumes::Compile(const FCLVolumeSettings& S,double Radius,
    const FCLCompiledSurfaceLibrary* Surfaces,FString& Error)
{
    Error.Reset();auto Fail=[&](const TCHAR* Text)->TSharedPtr<const FCLCompiledVolumes>{Error=Text;return nullptr;};
    if(S.AlgorithmVersion!=1||S.HeightPolicy!=ECLVolumeHeightPolicy::BaseTerrainOnly||S.Operations.Num()>64||S.GeologicalUnits.Num()>16||!FMath::IsFinite(Radius)||Radius<=0)
        return Fail(TEXT("Volume profile requires algorithm 1, base-terrain-only height policy, at most 64 operations and 16 geological units"));
    auto Out=MakeShared<FCLCompiledVolumes>();TSet<FGuid> Ids;
    for(const auto& V:S.Operations)
    {
        if(!V.Id.IsValid()||Ids.Contains(V.Id)||V.Name.IsNone()||V.AlgorithmVersion!=1||uint8(V.Primitive)>uint8(ECLSolidPrimitive::Box)||uint8(V.Composition)>uint8(ECLSolidComposition::Subtract)||
           V.CenterMeters.ContainsNaN()||V.Rotation.ContainsNaN()||V.Scale.ContainsNaN()||V.Scale.GetMin()<.01||V.Scale.GetMax()>100||
           !FMath::IsFinite(V.RadiusMeters)||V.RadiusMeters<.01||!FMath::IsFinite(V.HalfLengthMeters)||V.HalfLengthMeters<0||
           V.HalfExtentsMeters.ContainsNaN()||V.HalfExtentsMeters.GetMin()<.01||!FMath::IsFinite(V.SmoothnessMeters)||V.SmoothnessMeters<0||V.SmoothnessMeters>Radius*.1)
            return Fail(TEXT("Invalid volume operation identity, version, primitive, transform or dimensions"));
        Ids.Add(V.Id);if(!V.bEnabled)continue;
        FOperation O;O.Source=V;O.Transform=FTransform3d(V.Rotation.Quaternion(),V.CenterMeters,V.Scale);O.MinimumScale=V.Scale.GetMin();
        FVector3d Extent=V.Primitive==ECLSolidPrimitive::Box?V.HalfExtentsMeters:FVector3d(V.RadiusMeters);
        if(V.Primitive==ECLSolidPrimitive::Capsule)Extent.Z+=V.HalfLengthMeters;
        const FBox3d Solid=FBox3d(-Extent,Extent).TransformBy(O.Transform.ToMatrixWithScale());
        const double Expansion=V.SmoothnessMeters*V.Scale.GetMax()/O.MinimumScale;
        O.Bounds=Solid.ExpandBy(Expansion+1.);
        // Certify additions against the sky without deforming them. A cutter
        // may extend beyond the planet: subtraction cannot create exterior rock.
        const FBox3d Supported=Solid.ExpandBy(Expansion);
        for(int32 I=0;I<8;++I)
        {
            const FVector3d Corner(I&1?Supported.Max.X:Supported.Min.X,I&2?Supported.Max.Y:Supported.Min.Y,I&4?Supported.Max.Z:Supported.Min.Z);
            if(!FMath::IsFinite(Corner.SizeSquared())||Corner.Length()>Radius*10||
               (V.Composition==ECLSolidComposition::Union&&Corner.Length()>Radius))
                return Fail(TEXT("Solid additions must fit inside the sky; all operation support must fit the bounded ten-radius domain"));
        }
        if(!V.SurfaceId.IsNone())
        {O.SurfaceIndex=Surfaces?Surfaces->Find(V.SurfaceId,V.SurfaceRevision):INDEX_NONE;if(O.SurfaceIndex==INDEX_NONE)return Fail(TEXT("Volume contact surface is missing from the client library"));}
        Out->Operations.Add(MoveTemp(O));
    }
    for(const auto& U:S.GeologicalUnits)
    {
        if(U.Name.IsNone()||U.RadiusInterval.ContainsNaN()||U.RadiusInterval.X<0||U.RadiusInterval.Y>1||U.RadiusInterval.X>=U.RadiusInterval.Y||!FMath::IsFinite(U.FeatherMeters)||U.FeatherMeters<0)
            return Fail(TEXT("Geological units require ordered normalized radial intervals and finite feather widths"));
        const int32 Material=Surfaces?Surfaces->Find(U.SurfaceId,U.SurfaceRevision):INDEX_NONE;
        if(Material==INDEX_NONE)return Fail(TEXT("Geological unit surface is missing from the client library"));
        Out->Units.Add({U.RadiusInterval.X*Radius,U.RadiusInterval.Y*Radius,U.FeatherMeters,Material});
    }
    if(!Out->Operations.IsEmpty())
    {
        FVoxelAABBTree::FElementArray Elements;Elements.Reserve(Out->Operations.Num());
        for(int32 I=0;I<Out->Operations.Num();++I)Elements.Add(FVoxelBox(Out->Operations[I].Bounds),I);
        auto Native=MakeShared<FCLNativeVolumeIndex>();Native->Tree=FVoxelAABBTree::Create(MoveTemp(Elements));Out->Index=Native;
    }
    return Out;
}

int32 FCLCompiledVolumes::IndexedOperationCount() const {return Index?Index->Tree->Num():0;}
uint64 FCLCompiledVolumes::Candidates(const FVector3d& P) const
{
    if(!Index)return 0;uint64 Mask=0;
    Index->Tree->Intersects(FVoxelFastBox(FVoxelBox(P,P)),[&](int32 I){Mask|=uint64(1)<<I;return false;});
    return Mask;
}
double FCLCompiledVolumes::Compose(const FVector3d& P,double A,FCLSurfaceWeights* Weights) const
{
    const uint64 Mask=Candidates(P);
    for(int32 OpIndex=0;OpIndex<Operations.Num();++OpIndex)
    {
        const auto& O=Operations[OpIndex];if(CanSkip(O,P,A,(Mask&(uint64(1)<<OpIndex))!=0))continue;
        float Alpha=0;A=NativeBlend(O,A,O.Field(P),&Alpha);
        if(Weights&&O.SurfaceIndex!=INDEX_NONE)
        {for(double& V:Weights->Values)V*=1-Alpha;Weights->Values[O.SurfaceIndex]+=Alpha;}
    }
    return A;
}
void FCLCompiledVolumes::ComposeBatch(TConstArrayView<FVector3d> Positions,TArrayView<double> Fields) const
{
    check(Positions.Num()==Fields.Num());
    TArray<uint64> Masks;Masks.Reserve(Positions.Num());for(const auto& P:Positions)Masks.Add(Candidates(P));
    for(int32 OpIndex=0;OpIndex<Operations.Num();++OpIndex)
    {
        const auto& O=Operations[OpIndex];TArray<FVector3d> Needed;TArray<int32> Indices;
        for(int32 I=0;I<Positions.Num();++I)if(!CanSkip(O,Positions[I],Fields[I],(Masks[I]&(uint64(1)<<OpIndex))!=0)){Needed.Add(Positions[I]);Indices.Add(I);}
        if(Needed.IsEmpty())continue;
        TArray<double> Values;O.SampleFields(Needed,Values);
        for(int32 I=0;I<Indices.Num();++I){const int32 J=Indices[I];Fields[J]=NativeBlend(O,Fields[J],Values[I],nullptr);}
    }
}

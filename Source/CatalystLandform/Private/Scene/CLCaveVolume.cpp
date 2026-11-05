#include "Model/CLCaves.h"
#include "CLNativeNoiseKernel.h"
#include "Spline/VoxelSplineStamp.h"
#include "Spline/VoxelSplineMetadata.h"
#include "Components/SplineComponent.h"
#include "Shape/VoxelSphereShape.h"
#include "VoxelBlendModeUtilities.h"
#include "VoxelAABBTree.h"

// Pinned VPP 2.0p8 exported kernel, exactly the ABI called by its public
// GetClosestSplineKey3D node. Reuse native cubic nearest-point evaluation;
// do not copy the spline-distance algorithm or include private generated headers.
namespace ispc
{
extern "C" void VoxelSplineStamp_FindNearest3D(float*,const float*,const float*,const float*,int32,const FSegment*,int32);
}
class FCLCaveVolumeData
{
public:
    struct FPath
    {
        FGuid Network;FVector Origin,Across;int32 NoiseSeed=0;TSharedPtr<FSplineCurves> Curve;
        TVoxelArray<FVoxelSplineSegment> Segments;
        TSharedPtr<TArray<FVector2D>> Sections;
        FBox Bounds;double MinimumWidth=0,MaximumWidth=0,Incision=0;
    };
    struct FChamber {FCLCaveChamber Source;FBox Bounds;int32 NoiseSeed=0;};
    TArray<FPath> Paths;
    TArray<FChamber> Chambers;
    TSharedPtr<const FCLNativeNoiseKernel> Noise;
    TSharedPtr<const FVoxelAABBTree> Index;
    int32 Seed=0;
};
namespace
{
double Blend(double A,double B,double Smoothness)
{
    return FVoxelBlendModeUtilities::ComputeDistance(FVoxelVolumeTransform(),float(A),float(B),EVoxelVolumeBlendMode::Additive,true,float(Smoothness),nullptr);
}
}
TSharedPtr<const FCLCompiledCaveVolume> FCLCompiledCaveVolume::Compile(TConstArrayView<FCLCavePassage> Passages,TConstArrayView<FCLCaveChamber> Chambers,TSharedPtr<const FCLNativeNoiseKernel> Noise,int32 Seed,FString& Error)
{
    auto D=MakeShared<FCLCaveVolumeData>();D->Noise=Noise;D->Seed=Seed;
    FVoxelAABBTree::FElementArray Elements;TMap<FGuid,int32> PathCounts,ChamberCounts;
    for(const auto& P:Passages)
    {
        if(P.Points.Num()<2||P.Points.Num()!=P.Sections.Num()||!FMath::IsFinite(P.Incision)||P.Incision<0||P.Incision>2) {Error=TEXT("Cave passage requires matching curve controls and sections");return nullptr;}
        FCLCaveVolumeData::FPath C;C.Curve=MakeShared<FSplineCurves>();C.Network=P.Network;C.Origin=P.Points[0];C.Sections=MakeShared<TArray<FVector2D>>();*C.Sections=P.Sections;C.MinimumWidth=MAX_dbl;C.MaximumWidth=0;C.Incision=P.Incision;
        C.NoiseSeed=Seed^GetTypeHash(P.Network)^PathCounts.FindOrAdd(P.Network)++;
        C.Across=FVector::CrossProduct(C.Origin.GetSafeNormal(),(P.Points.Last()-P.Points[0]).GetSafeNormal()).GetSafeNormal();
        if(C.Across.IsNearlyZero())C.Across=FVector::CrossProduct(FVector::XAxisVector,(P.Points.Last()-P.Points[0]).GetSafeNormal()).GetSafeNormal();
        for(int32 I=0;I<P.Points.Num();++I)
        {
            const auto S=P.Sections[I];if(S.ContainsNaN()||FMath::Min(S.X,S.Y)<.1){Error=TEXT("Cave sections must have finite positive physical dimensions");return nullptr;}
            C.MinimumWidth=FMath::Min(C.MinimumWidth,FMath::Min(S.X,S.Y));C.MaximumWidth=FMath::Max(C.MaximumWidth,FMath::Max(S.X,S.Y*(1+C.Incision)));
            if(C.Incision>0)C.MinimumWidth=FMath::Min(C.MinimumWidth,FMath::Min(S.X*.38,S.Y*(1+C.Incision)*.5));
            // Explicit limited tangents avoid overshoot at joint changes. The
            // same UE curve is consumed by VPP's native spline kernel.
            FVector Tangent=I==0?P.Points[1]-P.Points[0]:I==P.Points.Num()-1?P.Points[I]-P.Points[I-1]:(P.Points[I+1]-P.Points[I-1])*.35;
            if(I>0&&I<P.Points.Num()-1)Tangent=Tangent.GetClampedToMaxSize(.5*FMath::Min(FVector::Distance(P.Points[I],P.Points[I-1]),FVector::Distance(P.Points[I+1],P.Points[I])));
            C.Curve->Position.Points.Emplace(float(I),P.Points[I]-C.Origin,Tangent,Tangent,CIM_CurveUser);
            C.Curve->Rotation.Points.Emplace(float(I),FQuat::Identity,FQuat::Identity,FQuat::Identity,CIM_Linear);
            C.Curve->Scale.Points.Emplace(float(I),FVector(S.X,S.Y,1),FVector::ZeroVector,FVector::ZeroVector,CIM_Linear);
        }
        // The native nearest-key kernel's fixed Newton tolerances can jump
        // between incomplete solutions on long, tightly bent cubic intervals.
        // Reparameterize the same UE Hermite curve into shorter exact cubics;
        // derivative scaling preserves geometry and avoids another distance solver.
        const auto Original=C.Curve;C.Curve=MakeShared<FSplineCurves>();C.Sections=MakeShared<TArray<FVector2D>>();
        for(int32 I=0;I<P.Points.Num()-1;++I)
        {
            const double Extent=FVector::Distance(P.Points[I],P.Points[I+1])+Original->Position.Points[I].LeaveTangent.Length()+Original->Position.Points[I+1].ArriveTangent.Length();
            const int32 Steps=FMath::Max(1,FMath::CeilToInt(Extent/4.));
            for(int32 K=0;K<=Steps;++K)
            {
                const double Key=I+double(K)/Steps;const FVector Tangent=Original->Position.EvalDerivative(Key)/Steps;
                if(I>0&&K==0){C.Curve->Position.Points.Last().LeaveTangent=Tangent;continue;}
                const float At=C.Curve->Position.Points.Num();const auto Section=FMath::Lerp(P.Sections[I],P.Sections[I+1],double(K)/Steps);
                C.Curve->Position.Points.Emplace(At,Original->Position.Eval(Key),Tangent,Tangent,CIM_CurveUser);
                C.Curve->Rotation.Points.Emplace(At,FQuat::Identity,FQuat::Identity,FQuat::Identity,CIM_Linear);
                C.Curve->Scale.Points.Emplace(At,FVector(Section.X,Section.Y,1),FVector::ZeroVector,FVector::ZeroVector,CIM_Linear);C.Sections->Add(Section);
            }
        }
        const auto Segments=FVoxelSplineSegment::Create(*C.Curve,FVoxelSplineMetadataRuntime());
        if(D->Paths.Num()+Segments.Num()>65536){Error=TEXT("Cave construction exceeds 65536 native cubic intervals; no partial field was published");return nullptr;}
        for(const auto& Segment:Segments)
        {
            // A nearest centreline chosen across an entire winding spline does
            // not minimize a varying anisotropic section. Lower each native
            // cubic interval separately and native-union its actual volume.
            // This matches VPP's per-segment bounds granularity and removes
            // closest-key switching discontinuities at folded passage corners.
            auto Part=C;Part.Segments.Add(Segment);
            const auto Chord=(Segment.PositionB.OutVal-Segment.PositionA.OutVal).GetSafeNormal();
            const auto Radial=(C.Origin+(Segment.PositionA.OutVal+Segment.PositionB.OutVal)*.5).GetSafeNormal();
            Part.Across=FVector::CrossProduct(Radial,Chord).GetSafeNormal();
            if(Part.Across.IsNearlyZero()){FVector Other;Chord.FindBestAxisVectors(Part.Across,Other);}
            Part.Bounds=FBox(FVector(Segment.Bounds.Min)+C.Origin,FVector(Segment.Bounds.Max)+C.Origin).ExpandBy(C.MaximumWidth*1.5+1);
            D->Paths.Add(MoveTemp(Part));Elements.Add(FVoxelBox(D->Paths.Last().Bounds),D->Paths.Num()-1);
        }
    }
    for(const auto& C:Chambers)
    {
        auto& V=D->Chambers.AddDefaulted_GetRef();V.Source=C;V.NoiseSeed=Seed^GetTypeHash(C.Network)^0x4348414D^ChamberCounts.FindOrAdd(C.Network)++;
        V.Bounds=FBox(-C.HalfExtents,C.HalfExtents).TransformBy(FTransform(C.Rotation,C.Center).ToMatrixWithScale()).ExpandBy(C.HalfExtents.GetMin()*.6+1);
        Elements.Add(FVoxelBox(V.Bounds),D->Paths.Num()+D->Chambers.Num()-1);
    }
    if(Elements.Num()>0)D->Index=FVoxelAABBTree::Create(MoveTemp(Elements));
    auto Out=MakeShared<FCLCompiledCaveVolume>();Out->Data=D;return Out;
}
TSharedPtr<const FCLCompiledCaveVolume> FCLCompiledCaveVolume::Select(FGuid Network)const
{
    auto Out=MakeShared<FCLCompiledCaveVolume>();Out->Data=Data;Out->SelectedNetwork=Network;return Out;
}
FBox FCLCompiledCaveVolume::Bounds(FGuid Network)const
{
    if(!Network.IsValid())Network=SelectedNetwork;FBox B(ForceInit);
    for(const auto& P:Data->Paths)if(!Network.IsValid()||P.Network==Network)B+=P.Bounds;
    for(const auto& P:Data->Chambers)if(!Network.IsValid()||P.Source.Network==Network)B+=P.Bounds;
    return B;
}
double FCLCompiledCaveVolume::MinimumHalfWidth(FGuid Network)const
{
    if(!Network.IsValid())Network=SelectedNetwork;double M=MAX_dbl;
    for(const auto& P:Data->Paths)if(!Network.IsValid()||P.Network==Network)M=FMath::Min(M,P.MinimumWidth);
    return M;
}
double FCLCompiledCaveVolume::Compose(const FVector3d& P,double Field,FCLSurfaceWeights*)const
{ComposeBatch(MakeArrayView(&P,1),MakeArrayView(&Field,1));return Field;}
void FCLCompiledCaveVolume::ComposeBatch(TConstArrayView<FVector3d> Positions,TArrayView<double> Fields)const
{
    check(Positions.Num()==Fields.Num());if(!Data->Index)return;
    // Native AABB index selects feature/query pairs. No 64-bit operation mask:
    // one real network is no longer squeezed into the authored-edit proof limit.
    TMap<int32,TArray<int32>> Work;
    for(int32 I=0;I<Positions.Num();++I)Data->Index->Intersects(FVoxelFastBox(FVoxelBox(Positions[I],Positions[I])),[&](int32 Feature)
    {Work.FindOrAdd(Feature).Add(I);return false;});
    TArray<int32> Features;Work.GetKeys(Features);Features.Sort();
    TArray<double> Void;Void.Init(MAX_flt,Positions.Num());
    for(int32 Feature:Features)
    {
        const bool Path=Feature<Data->Paths.Num();const auto Network=Path?Data->Paths[Feature].Network:Data->Chambers[Feature-Data->Paths.Num()].Source.Network;
        if(SelectedNetwork.IsValid()&&SelectedNetwork!=Network)continue;
        const auto& Indices=Work[Feature];const int32 N=Indices.Num();FVoxelDoubleVectorBuffer Local,Slot;Local.Allocate(N);TArray<double> Units,SlotUnits;Units.SetNumUninitialized(N);
        const bool Keyhole=Path&&Data->Paths[Feature].Incision>0;if(Keyhole){Slot.Allocate(N);SlotUnits.SetNumUninitialized(N);}
        double Smoothness=0;
        if(Path)
        {
            const auto& C=Data->Paths[Feature];TArray<float> X,Y,Z,Keys;X.SetNumUninitialized(N);Y.SetNumUninitialized(N);Z.SetNumUninitialized(N);Keys.SetNumUninitialized(N);
            for(int32 J=0;J<N;++J){const auto P=Positions[Indices[J]]-C.Origin;X[J]=P.X;Y[J]=P.Y;Z[J]=P.Z;}
            TVoxelArray<ispc::FSegment> Segments;for(const auto& S:C.Segments)Segments.Add(S.Segment);
            ispc::VoxelSplineStamp_FindNearest3D(Keys.GetData(),X.GetData(),Y.GetData(),Z.GetData(),N,Segments.GetData(),Segments.Num());
            for(int32 J=0;J<N;++J)
            {
                const double Key=FMath::Clamp(double(Keys[J]),0.,double(C.Sections->Num()-1));const int32 A=FMath::Min(int32(Key),C.Sections->Num()-2);
                const auto S=FMath::Lerp((*C.Sections)[A],(*C.Sections)[A+1],Key-A);const FVector Center=C.Curve->Position.Eval(Key)+C.Origin;
                const auto Along=C.Curve->Position.EvalDerivative(Key).GetSafeNormal();const auto Across=FVector::VectorPlaneProject(C.Across,Along).GetSafeNormal();const auto Up=FVector::CrossProduct(Along,Across).GetSafeNormal();
                const auto Delta=Positions[Indices[J]]-Center;const double U=FMath::Min(S.X,S.Y);Units[J]=U;
                const double Side=FVector::DotProduct(Delta,Across),Height=FVector::DotProduct(Delta,Up),End=FVector::DotProduct(Delta,Along);
                Local.Set(J,FVector(Side/S.X,Height/S.Y,End/U));

                if(Keyhole)
                {
                    const double W=S.X*.38,H=S.Y*(1+C.Incision)*.5,V=FMath::Min(W,H);SlotUnits[J]=V;
                    Slot.Set(J,FVector(Side/W,(Height+H)/H,End/V));
                }
            }
            Smoothness=C.MinimumWidth*.3;
        }
        else
        {
            const auto& C=Data->Chambers[Feature-Data->Paths.Num()].Source;const double U=C.HalfExtents.GetMin();Smoothness=U*.3;
            for(int32 J=0;J<N;++J){const auto P=C.Rotation.UnrotateVector(Positions[Indices[J]]-C.Center);Local.Set(J,P/C.HalfExtents);Units[J]=U;}
        }
        FVoxelFloatBuffer Native;Native.Allocate(N);FVoxelSphereShape Shape;Shape.Radius=1;Shape.Sample(Native.View(),Local);
        FVoxelFloatBuffer Lower;if(Keyhole){Lower.Allocate(N);Shape.Sample(Lower.View(),Slot);}
        for(int32 J=0;J<N;++J)
        {
            const int32 I=Indices[J];double Value=double(Native[J])*Units[J];
            if(Keyhole)Value=Blend(Value,double(Lower[J])*SlotUnits[J],Smoothness);
            // Native VPP noise in fixed physical coordinates. Correlated metre-
            // scale wall relief cannot close the protected passage core.
            if(Data->Noise)Value+=Data->Noise->Sample(Positions[I]/(Units[J]*1.8),Path?Data->Paths[Feature].NoiseSeed:Data->Chambers[Feature-Data->Paths.Num()].NoiseSeed)*Units[J]*.12;
            Void[I]=Void[I]==MAX_flt?Value:Blend(Void[I],Value,Smoothness);
        }
    }
    for(int32 I=0;I<Fields.Num();++I)
    {
        // Outside the conservative union support: cave contribution is identity.
        // For native empty input, produce known solid so inspection is known air.
        if(Void[I]==MAX_flt){if(!FMath::IsFinite(Fields[I]))Fields[I]=-1.e6;continue;}
        Fields[I]=FVoxelBlendModeUtilities::ComputeDistance(FVoxelVolumeTransform(),float(Fields[I]),float(Void[I]),EVoxelVolumeBlendMode::Subtractive,true,0,nullptr);
    }
}

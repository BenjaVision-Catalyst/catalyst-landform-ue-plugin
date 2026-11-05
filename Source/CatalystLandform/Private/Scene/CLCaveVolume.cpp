#include "Model/CLCaves.h"
#include "CLNativeNoiseKernel.h"
#include "Spline/VoxelSplineStamp.h"
#include "Spline/VoxelSplineMetadata.h"
#include "Components/SplineComponent.h"
#include "Shape/VoxelSphereShape.h"
#include "Shape/VoxelCubeShape.h"
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
        FBox Bounds;double MinimumWidth=0,MaximumWidth=0,Incision=0,Development=0;
        ECLCaveForm Form=ECLCaveForm::LegacyOval,EndForm=ECLCaveForm::LegacyOval;TSharedPtr<TArray<FVector>> BeddingUps;
    };
    struct FChamber {FCLCaveChamber Source;FBox Bounds;int32 NoiseSeed=0;};
    TArray<FPath> Paths;
    TArray<FChamber> Chambers;
    TArray<FCLCaveInfill> Infills;TSharedPtr<const FVoxelAABBTree> InfillIndex;
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
namespace
{
void SampleSection(ECLCaveForm Form,const FVoxelDoubleVectorBuffer& P,double Development,FVoxelFloatBuffer& Out)
{
    const int32 N=P.Num();FVoxelDoubleVectorBuffer Main,Extra;Main.Allocate(N);Extra.Allocate(N);
    const bool Room=Form==ECLCaveForm::FractureHall||Form==ECLCaveForm::KarstHall||Form==ECLCaveForm::RoofScar;
    for(int32 I=0;I<N;++I)
    {
        const auto V=P[I];const double X=V.X,Y=V.Y,Z=V.Z;
        if(Room)
        {
            Main.Set(I,Form==ECLCaveForm::RoofScar?FVector(X,Y,Z):FVector(X+.16*Y,(Y+.10*Z)/(1-.18*FMath::Clamp(FMath::Abs(X+.2*Z),0.,1.)),Z+.06*X-.04*Y+.20*FMath::Abs(X+.25*Y)+.12*FMath::Abs(Y-.4*X)));
            Extra.Set(I,FVector(X/2.2,Y/2.2,(Z+1)/2.05));
        }
        else if(Form==ECLCaveForm::JointCleft)Main.Set(I,FVector(X/(1-.22*FMath::Clamp(Y,-1.,1.))-.12*Y,Y,Z));
        else if(Form==ECLCaveForm::BeddingSlot)Main.Set(I,FVector(X*(1+.09*Y),Y+.035*X,Z));
        else if(Form==ECLCaveForm::Canyon)Main.Set(I,FVector(X/(.72+.09*FMath::Clamp(Y,-1.,1.))+.08*Y,Y,Z));
        else if(Form==ECLCaveForm::Keyhole)
        {Main.Set(I,FVector(X,(Y-.25)/.7,Z));Extra.Set(I,FVector(X/.34,(Y+.55)/(1.1+.35*Development),Z));}
        else Main.Set(I,V);
    }
    FVoxelSphereShape Sphere;Sphere.Radius=1;FVoxelCubeShape Cube;Cube.Size=FVector(2);Cube.Roundness=Form==ECLCaveForm::Canyon?.07:.025;
    if(Form==ECLCaveForm::Phreatic||Form==ECLCaveForm::Keyhole)Sphere.Sample(Out.View(),Main);else Cube.Sample(Out.View(),Main);
    if(Form==ECLCaveForm::Keyhole)for(int32 I=0;I<N;++I)Out.Set(I,Out[I]*.7f);
    if(Form==ECLCaveForm::Keyhole||Form==ECLCaveForm::KarstHall)
    {
        FVoxelFloatBuffer Second;Second.Allocate(N);if(Form==ECLCaveForm::Keyhole)Cube.Sample(Second.View(),Extra);else Sphere.Sample(Second.View(),Extra);
        for(int32 I=0;I<N;++I)Out.Set(I,FVoxelBlendModeUtilities::ComputeDistance(FVoxelVolumeTransform(),Out[I],Second[I]*(Form==ECLCaveForm::Keyhole?.34f:1.f),Form==ECLCaveForm::Keyhole?EVoxelVolumeBlendMode::Additive:EVoxelVolumeBlendMode::Intersect,true,.025,nullptr));
    }
}
}
TSharedPtr<const FCLCompiledCaveVolume> FCLCompiledCaveVolume::Compile(TConstArrayView<FCLCavePassage> Passages,TConstArrayView<FCLCaveChamber> Chambers,TSharedPtr<const FCLNativeNoiseKernel> Noise,int32 Seed,FString& Error,TConstArrayView<FCLCaveInfill> Infills)
{
    auto D=MakeShared<FCLCaveVolumeData>();D->Noise=Noise;D->Seed=Seed;
    FVoxelAABBTree::FElementArray Elements;TMap<FGuid,int32> PathCounts,ChamberCounts;
    for(const auto& P:Passages)
    {
        if(P.Points.Num()<2||P.Points.Num()!=P.Sections.Num()||!FMath::IsFinite(P.Incision)||P.Incision<0||P.Incision>2) {Error=TEXT("Cave passage requires matching curve controls and sections");return nullptr;}
        FCLCaveVolumeData::FPath C;C.Curve=MakeShared<FSplineCurves>();C.Network=P.Network;C.Origin=P.Points[0];C.Sections=MakeShared<TArray<FVector2D>>();*C.Sections=P.Sections;C.MinimumWidth=MAX_dbl;C.MaximumWidth=0;C.Incision=P.Incision;C.Form=P.Form;C.EndForm=P.EndForm;C.Development=P.Development;C.BeddingUps=MakeShared<TArray<FVector>>();
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
            C.Curve->Position.Points.Emplace(float(I),P.Points[I]-C.Origin,Tangent,Tangent,P.Form==ECLCaveForm::JointCleft?CIM_Linear:CIM_CurveUser);
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
            const int32 Steps=P.Form==ECLCaveForm::JointCleft?1:FMath::Max(1,FMath::CeilToInt(Extent/4.));
            for(int32 K=0;K<=Steps;++K)
            {
                const double Key=I+double(K)/Steps;const FVector Tangent=Original->Position.EvalDerivative(Key)/Steps;
                if(I>0&&K==0){C.Curve->Position.Points.Last().LeaveTangent=Tangent;continue;}
                const float At=C.Curve->Position.Points.Num();const auto Section=FMath::Lerp(P.Sections[I],P.Sections[I+1],double(K)/Steps);
                C.Curve->Position.Points.Emplace(At,Original->Position.Eval(Key),Tangent,Tangent,CIM_CurveUser);
                C.Curve->Rotation.Points.Emplace(At,FQuat::Identity,FQuat::Identity,FQuat::Identity,CIM_Linear);
                C.Curve->Scale.Points.Emplace(At,FVector(Section.X,Section.Y,1),FVector::ZeroVector,FVector::ZeroVector,CIM_Linear);C.Sections->Add(Section);if(!P.BeddingUps.IsEmpty())C.BeddingUps->Add(FMath::Lerp(P.BeddingUps[I],P.BeddingUps[I+1],double(K)/Steps).GetSafeNormal());
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
            Part.Bounds=FBox(FVector(Segment.Bounds.Min)+C.Origin,FVector(Segment.Bounds.Max)+C.Origin).ExpandBy(C.MaximumWidth*(P.Form==ECLCaveForm::LegacyOval?1.5:2.25)+1);
            D->Paths.Add(MoveTemp(Part));Elements.Add(FVoxelBox(D->Paths.Last().Bounds),D->Paths.Num()-1);
        }
    }
    for(const auto& C:Chambers)
    {
        auto& V=D->Chambers.AddDefaulted_GetRef();V.Source=C;V.NoiseSeed=Seed^GetTypeHash(C.Network)^0x4348414D^ChamberCounts.FindOrAdd(C.Network)++;
        V.Bounds=FBox(-C.HalfExtents,C.HalfExtents).TransformBy(FTransform(C.Rotation,C.Center).ToMatrixWithScale()).ExpandBy(C.HalfExtents.GetMin()*.6+1);
        Elements.Add(FVoxelBox(V.Bounds),D->Paths.Num()+D->Chambers.Num()-1);
    }
    FVoxelAABBTree::FElementArray Blocks;
    for(const auto& B:Infills){const int32 I=D->Infills.Add(B);const auto Box=FBox(-B.HalfExtents,B.HalfExtents).TransformBy(FTransform(B.Rotation,B.Center)).ExpandBy(B.HalfExtents.GetMax()*.5+1);Blocks.Add(FVoxelBox(Box),I);}
    if(Blocks.Num()>0)D->InfillIndex=FVoxelAABBTree::Create(MoveTemp(Blocks));
    if(Elements.Num()>0)D->Index=FVoxelAABBTree::Create(MoveTemp(Elements));
    auto Out=MakeShared<FCLCompiledCaveVolume>();Out->Data=D;return Out;
}
TSharedPtr<const FCLCompiledCaveVolume> FCLCompiledCaveVolume::Select(FGuid Network)const
{
    auto Out=MakeShared<FCLCompiledCaveVolume>();Out->Data=Data;if(Network.IsValid())Out->SelectedNetworks.Add(Network);return Out;
}
TSharedPtr<const FCLCompiledCaveVolume> FCLCompiledCaveVolume::Select(TConstArrayView<FGuid> Networks)const
{
    auto Out=MakeShared<FCLCompiledCaveVolume>();Out->Data=Data;for(const auto Id:Networks)Out->SelectedNetworks.Add(Id);return Out;
}
FBox FCLCompiledCaveVolume::Bounds(FGuid Network)const
{
    FBox B(ForceInit);
    for(const auto& P:Data->Paths)if((!Network.IsValid()||P.Network==Network)&&(SelectedNetworks.IsEmpty()||SelectedNetworks.Contains(P.Network)))B+=P.Bounds;
    for(const auto& P:Data->Chambers)if((!Network.IsValid()||P.Source.Network==Network)&&(SelectedNetworks.IsEmpty()||SelectedNetworks.Contains(P.Source.Network)))B+=P.Bounds;
    return B;
}
double FCLCompiledCaveVolume::MinimumHalfWidth(FGuid Network)const
{
    double M=MAX_dbl;
    for(const auto& P:Data->Paths)if((!Network.IsValid()||P.Network==Network)&&(SelectedNetworks.IsEmpty()||SelectedNetworks.Contains(P.Network)))M=FMath::Min(M,P.MinimumWidth);
    return M;
}
double FCLCompiledCaveVolume::Compose(const FVector3d& P,double Field,FCLSurfaceWeights*)const
{ComposeBatch(MakeArrayView(&P,1),MakeArrayView(&Field,1));return Field;}
void FCLCompiledCaveVolume::ComposeBatch(TConstArrayView<FVector3d> Positions,TArrayView<double> Fields)const
{ComposeBatchInternal(Positions,Fields,true);}
void FCLCompiledCaveVolume::ComposeBatchInternal(TConstArrayView<FVector3d> Positions,TArrayView<double> Fields,bool IncludeInfills)const
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
        if(!SelectedNetworks.IsEmpty()&&!SelectedNetworks.Contains(Network))continue;
        const auto& Indices=Work[Feature];const int32 N=Indices.Num();FVoxelDoubleVectorBuffer Local,Slot;Local.Allocate(N);TArray<double> Units,SlotUnits,Phases;Units.SetNumUninitialized(N);Phases.SetNumZeroed(N);
        const bool Modern=Path?Data->Paths[Feature].Form!=ECLCaveForm::LegacyOval:Data->Chambers[Feature-Data->Paths.Num()].Source.Form!=ECLCaveForm::LegacyOval;
        const bool Keyhole=!Modern&&Path&&Data->Paths[Feature].Incision>0;if(Keyhole){Slot.Allocate(N);SlotUnits.SetNumUninitialized(N);}
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
                const auto Along=C.Curve->Position.EvalDerivative(Key).GetSafeNormal();FVector Across=FVector::VectorPlaneProject(C.Across,Along).GetSafeNormal();
                if(Modern&&!C.BeddingUps->IsEmpty())
                {
                    const FVector BedUp=FMath::Lerp((*C.BeddingUps)[A],(*C.BeddingUps)[A+1],Key-A).GetSafeNormal();
                    const FVector Candidate=FVector::CrossProduct(BedUp,Along).GetSafeNormal();if(!Candidate.IsNearlyZero())Across=Candidate;
                }
                const auto Up=FVector::CrossProduct(Along,Across).GetSafeNormal();Phases[J]=Key/(C.Sections->Num()-1);
                const auto Delta=Positions[Indices[J]]-Center;const double U=FMath::Min(S.X,S.Y);Units[J]=U;
                const double Side=FVector::DotProduct(Delta,Across),Height=FVector::DotProduct(Delta,Up),End=FVector::DotProduct(Delta,Along);
                Local.Set(J,FVector(Side/S.X,Height/S.Y,End/U));

                if(Keyhole)
                {
                    const double W=S.X*.38,H=S.Y*(1+C.Incision)*.5,V=FMath::Min(W,H);SlotUnits[J]=V;
                    Slot.Set(J,FVector(Side/W,(Height+H)/H,End/V));
                }
            }
            Smoothness=C.MinimumWidth*(Modern?.035:.3);
        }
        else
        {
            const auto& C=Data->Chambers[Feature-Data->Paths.Num()].Source;const double U=C.HalfExtents.GetMin();Smoothness=U*(Modern?.04:.3);
            for(int32 J=0;J<N;++J){const auto P=C.Rotation.UnrotateVector(Positions[Indices[J]]-C.Center);Local.Set(J,P/C.HalfExtents);Units[J]=U;}
        }
        FVoxelFloatBuffer Native;Native.Allocate(N);FVoxelSphereShape Shape;Shape.Radius=1;
        if(Modern)
        {
            const auto Form=Path?Data->Paths[Feature].Form:Data->Chambers[Feature-Data->Paths.Num()].Source.Form;
            SampleSection(Form,Local,Path?Data->Paths[Feature].Development:0,Native);
            if(Path&&Data->Paths[Feature].EndForm!=Form)
            {
                FVoxelFloatBuffer Other;Other.Allocate(N);SampleSection(Data->Paths[Feature].EndForm,Local,Data->Paths[Feature].Development,Other);
                for(int32 J=0;J<N;++J){const double T=Phases[J]*Phases[J]*(3-2*Phases[J]);Native.Set(J,FMath::Lerp(Native[J],Other[J],float(T)));}
            }
        }
        else Shape.Sample(Native.View(),Local);
        FVoxelFloatBuffer Lower;if(Keyhole){Lower.Allocate(N);Shape.Sample(Lower.View(),Slot);}
        TArray<float> Noise,RoomRelief;
        if(Data->Noise)
        {
            TArray<FVector> Points;Points.Reserve(N);for(int32 J=0;J<N;++J)Points.Add(Positions[Indices[J]]/(Modern?FMath::Clamp(Units[J]*.45,.3,1.):Units[J]*1.8));
            Data->Noise->SampleBatch(Points,Path?Data->Paths[Feature].NoiseSeed:Data->Chambers[Feature-Data->Paths.Num()].NoiseSeed,Noise);
            if(Modern&&!Path&&Data->Chambers[Feature-Data->Paths.Num()].Source.Form!=ECLCaveForm::RoofScar)
            {
                Points.Reset();for(int32 J=0;J<N;++J)Points.Add(Positions[Indices[J]]/FMath::Clamp(Units[J]*.6,2.,5.));
                Data->Noise->SampleBatch(Points,Data->Chambers[Feature-Data->Paths.Num()].NoiseSeed^0x52454C49,RoomRelief);
            }
        }
        for(int32 J=0;J<N;++J)
        {
            const int32 I=Indices[J];double Value=double(Native[J])*Units[J];
            if(Keyhole)Value=Blend(Value,double(Lower[J])*SlotUnits[J],Smoothness);
            // Native VPP noise in fixed physical coordinates. Correlated metre-
            // scale wall relief cannot close the protected passage core.
            if(Data->Noise)Value+=double(Noise[J])*(Modern?FMath::Min(.08,Units[J]*.025):Units[J]*.12);
            if(!RoomRelief.IsEmpty())Value+=RoomRelief[J]*FMath::Min(.7,Units[J]*.08);
            Void[I]=Void[I]==MAX_flt?Value:Blend(Void[I],Value,Smoothness);
        }
    }
    if(IncludeInfills&&Data->InfillIndex)
    {
        TMap<int32,TArray<int32>> Blocks;
        for(int32 I=0;I<Positions.Num();++I)if(Void[I]!=MAX_flt)Data->InfillIndex->Intersects(FVoxelFastBox(FVoxelBox(Positions[I],Positions[I])),[&](int32 B){if(SelectedNetworks.IsEmpty()||SelectedNetworks.Contains(Data->Infills[B].Network))Blocks.FindOrAdd(B).Add(I);return false;});
        TArray<int32> Keys;Blocks.GetKeys(Keys);Keys.Sort();
        for(int32 B:Keys)
        {
            const auto& Block=Data->Infills[B];const auto& Indices=Blocks[B];FVoxelDoubleVectorBuffer P;P.Allocate(Indices.Num());
            for(int32 J=0;J<Indices.Num();++J)P.Set(J,Block.Rotation.UnrotateVector(Positions[Indices[J]]-Block.Center));
            FVoxelFloatBuffer Values;Values.Allocate(Indices.Num());FVoxelCubeShape Cube;Cube.Size=Block.HalfExtents*2;Cube.Roundness=.025;Cube.Sample(Values.View(),P);
            for(int32 J=0;J<Indices.Num();++J)Void[Indices[J]]=FVoxelBlendModeUtilities::ComputeDistance(FVoxelVolumeTransform(),float(Void[Indices[J]]),Values[J],EVoxelVolumeBlendMode::Subtractive,true,0,nullptr);
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

FVector FCLCompiledCaveVolume::GeologicalPosition(const FVector& Position)const
{
    if(!Data->InfillIndex)return Position;
    FVector Source=Position;bool Found=false;TArray<int32> Hits;
    Data->InfillIndex->Intersects(FVoxelFastBox(FVoxelBox(Position,Position)),[&](int32 I){Hits.Add(I);return false;});Hits.Sort([](int32 A,int32 B){return A>B;});
    for(int32 I:Hits)
    {
        const auto& B=Data->Infills[I];if(!SelectedNetworks.IsEmpty()&&!SelectedNetworks.Contains(B.Network))continue;
        FVoxelDoubleVectorBuffer P;P.Allocate(1);P.Set(0,B.Rotation.UnrotateVector(Position-B.Center));FVoxelFloatBuffer D;D.Allocate(1);FVoxelCubeShape Cube;Cube.Size=B.HalfExtents*2;Cube.Roundness=.025;Cube.Sample(D.View(),P);
        if(D[0]>0)continue;
        double Air=FVoxelUtilities::NaNf();ComposeBatchInternal(MakeArrayView(&Position,1),MakeArrayView(&Air,1),false);if(Air<=0)continue;
        Source=B.SourceCenter+B.SourceRotation.RotateVector(B.Rotation.UnrotateVector(Position-B.Center));Found=true;break;
    }
    return Found?Source:Position;
}

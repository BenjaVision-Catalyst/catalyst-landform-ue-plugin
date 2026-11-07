#include "Model/CLCaves.h"
#include "Model/CLSpatialCave.h"
#include "Model/CLCaveFloor.h"
#include "CLNativeNoiseKernel.h"
#include "Spline/VoxelSplineStamp.h"
#include "Spline/VoxelSplineMetadata.h"
#include "Components/SplineComponent.h"
#include "Shape/VoxelSphereShape.h"
#include "Shape/VoxelPlaneShape.h"
#include "Model/CLRockLayer.h"
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
        FGuid Network;FVector Origin,Across;int32 NoiseSeed=0,PassageId=0,GeometryProfile=0;double FloorReliefScale=0;TSharedPtr<FSplineCurves> Curve;
        TVoxelArray<FVoxelSplineSegment> Segments;
        TVoxelArray<ispc::FSegment> NativeSegments;
        TSharedPtr<TArray<FVector2D>> Sections;
        TSharedPtr<TArray<double>> FloorRadii;TSharedPtr<const FCLCompiledRock> FloorRock;
        bool RampedFloor=false;FVector FloorStartDirection,FloorForward;double FloorAngle=0,RampStart=0,RampEnd=0;
        FBox Bounds;double MinimumWidth=0,MaximumWidth=0,Incision=0,Development=0;
        ECLCaveForm Form=ECLCaveForm::LegacyOval,EndForm=ECLCaveForm::LegacyOval;TSharedPtr<TArray<FVector>> BeddingUps;
    };
    struct FChamber {FCLCaveChamber Source;FBox Bounds;int32 NoiseSeed=0;};
    TArray<FVoxelFastBox> FeatureBounds;
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
// Geological anatomy is lowered through native sphere and smooth-union kernels.
// The broad vault, incised floor and unequal side recesses are distinct volumes;
// small native wall noise below is only the final surface detail.
void SampleNaturalKarst(ECLCaveForm Form,const FVoxelDoubleVectorBuffer& P,TConstArrayView<double> Phases,int32 Seed,double Development,FVoxelFloatBuffer& Out)
{
    const bool Room=Form==ECLCaveForm::KarstChamber;
    const double Phase=double(uint32(Seed)&65535)/65536.*2*PI;
    const int32 N=P.Num();
    FVoxelDoubleVectorBuffer Vault,Floor,Left,Right;Vault.Allocate(N);Floor.Allocate(N);Left.Allocate(N);Right.Allocate(N);
    for(int32 I=0;I<N;++I)
    {
        const auto V=P[I];
        const double Side=Room?V.Y:V.X,Up=Room?V.Z:V.Y,Along=Room?V.X:V.Z;
        const double T=Room?.5:Phases[I],Envelope=Room?1:FMath::Square(FMath::Sin(PI*T));
        const double Asymmetry=Envelope*(.08*FMath::Sin(2.2*PI*T+Phase)+.04*FMath::Cos(4.1*PI*T-Phase));
        const double W=1+Envelope*.12*FMath::Sin(1.6*PI*T+Phase);
        const double H=1+Envelope*.08*FMath::Cos(2.3*PI*T+Phase*.7);
        const double X=(Side-Asymmetry*(.6+.2*Up))/W;
        const double Y=Up/H;
        // A broad eroded vault and lower bedrock channel. Neither the floor nor
        // the ceiling is a clipped cube face; longitudinal narrowing is smooth.
        Vault.Set(I,FVector(X/(Room?.98:.90),(Y-(Room?.16:.35))/(Room?.92:.70),Along));
        Floor.Set(I,FVector((X+.08*Envelope)/(Room?.84:.50),(Y+(Room?.40:.52))/(Room?.55:.68),Along/(Room?.96:1.)));
        Left.Set(I,FVector((X+(Room?.48:.58))/(Room?.58:.45),(Y-(Room?.03:.28))/(Room?.69:.37),(Along+(Room?.22:0))/(Room?.80:1.)));
        Right.Set(I,FVector((X-(Room?.51:.52))/(Room?.46:.34),(Y-(Room?.18:.42))/(Room?.61:.32),(Along-(Room?.30:0))/(Room?.68:1.)));
    }
    FVoxelSphereShape Sphere;Sphere.Radius=1;
    Sphere.Sample(Out.View(),Vault);
    FVoxelFloatBuffer Part;Part.Allocate(N);
    for(const auto* Lobe:{&Floor,&Left,&Right})
    {
        Sphere.Sample(Part.View(),*Lobe);
        for(int32 I=0;I<N;++I)
            Out.Set(I,FVoxelBlendModeUtilities::ComputeDistance(FVoxelVolumeTransform(),Out[I],Part[I]*.55f,
                EVoxelVolumeBlendMode::Additive,true,Room?.075f:.045f,nullptr));
    }
}
// Profile 7 removes the rectangular roof clip. Unequal dissolution lobes
// create off-centre vaults, undercuts and ledges before physical wall weathering.
void SampleOrganicKarst(ECLCaveForm Form,const FVoxelDoubleVectorBuffer& P,TConstArrayView<double> Phases,int32 Seed,FVoxelFloatBuffer& Out )
{
    const bool Room=Form==ECLCaveForm::BeddedChamber;const int32 N=P.Num();
    const double Phase=(uint32(Seed)&65535)*(2*PI/65536.);
    FVoxelDoubleVectorBuffer Main,Left,Right,Upper;Main.Allocate(N);Left.Allocate(N);Right.Allocate(N);Upper.Allocate(N);
    for(int32 I=0;I<N;++I)
    {
        const auto Q=P[I];const double X=Room?Q.Y:Q.X,Y=Room?Q.Z:Q.Y,Z=Room?Q.X:Q.Z;
        const double T=Room?.5:Phases[I],E=Room?1:.7+.3*FMath::Square(FMath::Sin(PI*T));
        const double W=1+E*.16*FMath::Sin(T*7.1+Phase),H=1+E*.15*FMath::Sin(T*9.3-Phase);
        const double Side=(X-E*.16*FMath::Sin(T*6.3+Phase*.7))/W;
        if(Room)
        {
            Main.Set(I,FVector(Z/.90,(Side+.06)/.85,(Y/H-.70)/1.10));
            Left.Set(I,FVector((Z+.43)/.70,(Side+.42)/.73,(Y/H-.50)/1.38));
            Right.Set(I,FVector((Z-.31)/.58,(Side-.49)/.60,(Y/H-.78)/1.25));
            Upper.Set(I,FVector((Z-.08)/.65,(Side+.20)/.65,(Y/H-1.30)/.68));
        }
        else
        {
            const double V=Y/H;
            Main.Set(I,FVector(Side/.94,(V-.42)/.92,Z));
            Left.Set(I,FVector((Side+.61)/.53,(V-.29)/.63,Z/1.25));
            Right.Set(I,FVector((Side-.53)/.48,(V-.73)/.72,Z/1.18));
            Upper.Set(I,FVector((Side+.19)/.76,(V-1.05)/.45,Z/1.3));
        }
    }
    FVoxelSphereShape Sphere;Sphere.Radius=1;Sphere.Sample(Out.View(),Main);FVoxelFloatBuffer Part;Part.Allocate(N);
    for(const auto* Bay:{&Left,&Right,&Upper})
    {
        Sphere.Sample(Part.View(),*Bay);
        for(int32 I=0;I<N;++I)Out.Set(I,FVoxelBlendModeUtilities::ComputeDistance(FVoxelVolumeTransform(),Out[I],Part[I]*.70f,EVoxelVolumeBlendMode::Additive,true,Room?.06:.055,nullptr));
    }
}
// Profile 6 has a broad bedrock floor independent of its vault. Passage
// ceilings follow bedding with low shoulders, rather than a circular pipe.
void SampleBeddedKarst(ECLCaveForm Form,const FVoxelDoubleVectorBuffer& P,TConstArrayView<double> Phases,int32 Seed,FVoxelFloatBuffer& Out,int32 GeometryProfile=6)
{
    if(GeometryProfile>=7){SampleOrganicKarst(Form,P,Phases,Seed,Out);return;}
    const bool Room=Form==ECLCaveForm::BeddedChamber;const int32 N=P.Num();
    FVoxelDoubleVectorBuffer Main,Left,Right,Roof;Main.Allocate(N);Left.Allocate(N);Right.Allocate(N);Roof.Allocate(N);
    const double Phase=(uint32(Seed)&65535)*(2*PI/65536.);
    for(int32 I=0;I<N;++I)
    {
        const auto V=P[I];const double X=Room?V.Y:V.X,Y=Room?V.Z:V.Y,Z=Room?V.X:V.Z;
        const double T=Room?.5:Phases[I],E=Room?1:FMath::Square(FMath::Sin(PI*T));
        // Unequal wall shoulders and broad changes of plan, correlated along a
        // reach. Bedding ledges are subordinate to the main passage anatomy.
        const double Width=1+E*.10*FMath::Sin(T*7+Phase);
        const double Ledge=0; // Actual broken bed remnants below supply the wall shelves.
        const double Side=(X-E*.07*FMath::Sin(T*6+Phase))/(Width*(1+Ledge));
        if(Room)
        {
            Main.Set(I,FVector(Z/.95,Side/.94,(Y-.65)/1.50));
            Left.Set(I,FVector((Z+.40)/.62,(Side+.45)/.66,(Y+.15)/1.85));
            Right.Set(I,FVector((Z-.42)/.58,(Side-.40)/.61,(Y+.35)/2.1));
        }
        else Main.Set(I,FVector(Side/1.03,(Y+2.2)/4.2,Z));
        Roof.Set(I,FVector(0,0,Y-(Room?2.05:1.23+E*.06*FMath::Cos(T*5+Phase))-.035*FMath::Sin(X*3+Phase)));
    }
    FVoxelSphereShape Sphere;Sphere.Radius=1;Sphere.Sample(Out.View(),Main);
    FVoxelFloatBuffer Part;Part.Allocate(N);
    if(Room)for(const auto* Lobe:{&Left,&Right})
    {
        Sphere.Sample(Part.View(),*Lobe);
        for(int32 I=0;I<N;++I)Out.Set(I,FVoxelBlendModeUtilities::ComputeDistance(FVoxelVolumeTransform(),Out[I],Part[I]*.65f,EVoxelVolumeBlendMode::Additive,true,.04,nullptr));
    }
    FVoxelPlaneShape Plane;Plane.Size=FVector2D(2.e6);Plane.Height=1;Plane.Sample(Part.View(),Roof);
    for(int32 I=0;I<N;++I)Out.Set(I,FVoxelBlendModeUtilities::ComputeDistance(FVoxelVolumeTransform(),Out[I],Part[I],EVoxelVolumeBlendMode::Intersect,true,.055,nullptr));
}
void SampleInfill(const FCLCaveInfill& B,const FVoxelDoubleVectorBuffer& P,FVoxelFloatBuffer& Values,const FCLNativeNoiseKernel* Noise=nullptr,int32 Seed=0)
{
    auto Weather=[&]
    {
        if(!Noise||(!B.BedrockPier&&!B.BeddedRemnant&&!B.BeddedBreakdown))return;
        TArray<FVector> Points;for(int32 I=0;I<P.Num();++I)Points.Add((B.Rotation.RotateVector(P[I])+B.Center)/(B.BedrockPier?2.8:1.6));
        TArray<float> Relief;Noise->SampleBatch(Points,Seed^GetTypeHash(B.Network)^GetTypeHash(B.Center),Relief);
        for(int32 I=0;I<P.Num();++I)Values.Set(I,Values[I]+Relief[I]*(B.BedrockPier?.22f:B.BeddedBreakdown?float(FMath::Min(.12,B.HalfExtents.GetMin()*.14)):.12f));
    };
    if(!B.BedrockPier)
    {
        FVoxelCubeShape Cube;Cube.Size=B.HalfExtents*2;Cube.Roundness=B.BeddedRemnant?.35:B.BeddedBreakdown?.20:.025;Cube.Sample(Values.View(),P);
        if(B.BeddedBreakdown)
        {
            // Two unequal native joint planes fracture the transported block's
            // corners. Legacy breakdown keeps its original cube operator.
            const double Phase=(uint32(GetTypeHash(B.Center))&65535)*(2*PI/65536.);
            FVoxelPlaneShape Plane;Plane.Size=FVector2D(2.e6);Plane.Height=1;
            FVoxelDoubleVectorBuffer Cut;Cut.Allocate(P.Num());FVoxelFloatBuffer Part;Part.Allocate(P.Num());
            for(int32 K=0;K<2;++K)
            {
                const FVector Normal=FVector(K?-.7:1.,K?.8:.5,K?.9:.7).GetSafeNormal();
                const double Offset=.82+.08*FMath::Sin(Phase+K*1.7);
                for(int32 I=0;I<P.Num();++I)Cut.Set(I,FVector(0,0,(FVector::DotProduct(P[I]/B.HalfExtents,Normal)-Offset)*B.HalfExtents.GetMin()));
                Plane.Sample(Part.View(),Cut);
                for(int32 I=0;I<P.Num();++I)Values.Set(I,FVoxelBlendModeUtilities::ComputeDistance(FVoxelVolumeTransform(),Values[I],Part[I],EVoxelVolumeBlendMode::Intersect,true,.04,nullptr));
            }
        }
        Weather();return;
    }
    FVoxelDoubleVectorBuffer Local;Local.Allocate(P.Num());
    for(int32 I=0;I<P.Num();++I)
    {
        const auto Q=P[I]/B.HalfExtents;
        const double W=.78+.14*Q.Z*Q.Z+.045*FMath::Sin(Q.Z*13);
        Local.Set(I,FVector((Q.X+.12*Q.Z)/W,Q.Y/(W*.95),Q.Z/1.4));
    }
    FVoxelSphereShape Sphere;Sphere.Radius=1;Sphere.Sample(Values.View(),Local);
    for(int32 I=0;I<P.Num();++I)Values.Set(I,Values[I]*B.HalfExtents.GetMin());Weather();
}
void SampleSection(ECLCaveForm Form,const FVoxelDoubleVectorBuffer& P,double Development,FVoxelFloatBuffer& Out,TConstArrayView<double> Phases={},int32 Seed=0,int32 GeometryProfile=0)
{
    if(Form==ECLCaveForm::BeddedPassage||Form==ECLCaveForm::BeddedChamber)
    {SampleBeddedKarst(Form,P,Phases,Seed,Out,GeometryProfile);return;}
    if(Form==ECLCaveForm::KarstPassage||Form==ECLCaveForm::KarstChamber)
    {SampleNaturalKarst(Form,P,Phases,Seed,Development,Out);return;}
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
    int32 PassageId=0;
    for(const auto& P:Passages)
    {
        if(P.Points.Num()<2||P.Points.Num()!=P.Sections.Num()||!FMath::IsFinite(P.Incision)||P.Incision<0||P.Incision>2) {Error=TEXT("Cave passage requires matching curve controls and sections");return nullptr;}
        FCLCaveVolumeData::FPath C;C.PassageId=PassageId++;C.GeometryProfile=P.GeometryProfile;C.FloorReliefScale=P.FloorReliefScale;C.Curve=MakeShared<FSplineCurves>();C.Network=P.Network;C.Origin=P.Points[0];C.Sections=MakeShared<TArray<FVector2D>>();*C.Sections=P.Sections;C.MinimumWidth=MAX_dbl;C.MaximumWidth=0;C.Incision=P.Incision;C.Form=P.Form;C.EndForm=P.EndForm;C.Development=P.Development;C.BeddingUps=MakeShared<TArray<FVector>>();
        if(P.Form==ECLCaveForm::BeddedPassage&&(!P.FloorRock||P.FloorRadii.Num()!=P.Points.Num()||P.FloorRadii.ContainsByPredicate([](double V){return !FMath::IsFinite(V)||V<=0;}))){Error=TEXT("Bedded passage requires its canonical rock and complete floor datum");return nullptr;}
        C.FloorRock=P.FloorRock;C.FloorRadii=MakeShared<TArray<double>>();
        C.RampedFloor=P.Entrance&&P.Form==ECLCaveForm::BeddedPassage;
        if(C.RampedFloor)
        {
            C.FloorStartDirection=P.Points[0].GetSafeNormal();const auto EndDirection=P.Points.Last().GetSafeNormal();
            C.FloorAngle=FMath::Acos(FMath::Clamp(FVector::DotProduct(C.FloorStartDirection,EndDirection),-1.,1.));
            C.FloorForward=(EndDirection-C.FloorStartDirection*FMath::Cos(C.FloorAngle)).GetSafeNormal();
            C.RampStart=P.Points[0].Length()-P.FloorOffsetMeters;C.RampEnd=P.Points.Last().Length()-P.FloorOffsetMeters;
            if(C.FloorAngle<1.e-5){Error=TEXT("Walk-in floor needs a finite angular reach");return nullptr;}
        }
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
                C.Curve->Scale.Points.Emplace(At,FVector(Section.X,Section.Y,1),FVector::ZeroVector,FVector::ZeroVector,CIM_Linear);C.Sections->Add(Section);if(!P.FloorRadii.IsEmpty())C.FloorRadii->Add(FMath::Lerp(P.FloorRadii[I],P.FloorRadii[I+1],double(K)/Steps));if(!P.BeddingUps.IsEmpty())C.BeddingUps->Add(FMath::Lerp(P.BeddingUps[I],P.BeddingUps[I+1],double(K)/Steps).GetSafeNormal());
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
            auto Part=C;Part.Segments.Add(Segment);Part.NativeSegments.Add(Segment.Segment);
            const auto Chord=(Segment.PositionB.OutVal-Segment.PositionA.OutVal).GetSafeNormal();
            const auto Radial=(C.Origin+(Segment.PositionA.OutVal+Segment.PositionB.OutVal)*.5).GetSafeNormal();
            Part.Across=FVector::CrossProduct(Radial,Chord).GetSafeNormal();
            if(Part.Across.IsNearlyZero()){FVector Other;Chord.FindBestAxisVectors(Part.Across,Other);}
            Part.Bounds=FBox(FVector(Segment.Bounds.Min)+C.Origin,FVector(Segment.Bounds.Max)+C.Origin).ExpandBy(C.MaximumWidth*(P.Form==ECLCaveForm::LegacyOval?1.5:2.25)+1);
            D->Paths.Add(MoveTemp(Part));D->FeatureBounds.Add(FVoxelFastBox(FVoxelBox(D->Paths.Last().Bounds)));Elements.Add(FVoxelBox(D->Paths.Last().Bounds),D->Paths.Num()-1);
        }
    }
    for(const auto& C:Chambers)
    {
        if(C.Form==ECLCaveForm::BeddedChamber&&(!C.FloorRock||!FMath::IsFinite(C.FloorRadius))){Error=TEXT("Bedded chamber requires its canonical rock floor datum");return nullptr;}
        auto& V=D->Chambers.AddDefaulted_GetRef();V.Source=C;V.NoiseSeed=Seed^GetTypeHash(C.Network)^0x4348414D^ChamberCounts.FindOrAdd(C.Network)++;
        V.Bounds=FBox(-C.HalfExtents,C.HalfExtents).TransformBy(FTransform(C.Rotation,C.Center).ToMatrixWithScale()).ExpandBy(C.HalfExtents.GetMin()*.6+1);
        D->FeatureBounds.Add(FVoxelFastBox(FVoxelBox(V.Bounds)));Elements.Add(FVoxelBox(V.Bounds),D->Paths.Num()+D->Chambers.Num()-1);
    }
    FVoxelAABBTree::FElementArray Blocks;
    for(const auto& B:Infills){const int32 I=D->Infills.Add(B);const auto Box=FBox(-B.HalfExtents,B.HalfExtents).TransformBy(FTransform(B.Rotation,B.Center)).ExpandBy(B.HalfExtents.GetMax()*.5+1);Blocks.Add(FVoxelBox(Box),I);}
    if(Blocks.Num()>0)D->InfillIndex=FVoxelAABBTree::Create(MoveTemp(Blocks));
    if(Elements.Num()>0)D->Index=FVoxelAABBTree::Create(MoveTemp(Elements));
    auto Out=MakeShared<FCLCompiledCaveVolume>();Out->Data=D;return Out;
}
TSharedPtr<const FCLCompiledCaveVolume> FCLCompiledCaveVolume::CompileSpatial(TSharedPtr<const FCLSpatialCave> Cave,FGuid Network)
{auto Out=MakeShared<FCLCompiledCaveVolume>();Out->Spatial=Cave;Out->SpatialNetwork=Network;return Out;}
TSharedPtr<const FCLCompiledCaveVolume> FCLCompiledCaveVolume::Select(FGuid Network)const
{
    auto Out=MakeShared<FCLCompiledCaveVolume>();Out->Data=Data;Out->Spatial=Spatial;Out->SpatialNetwork=SpatialNetwork;if(Network.IsValid())Out->SelectedNetworks.Add(Network);return Out;
}
TSharedPtr<const FCLCompiledCaveVolume> FCLCompiledCaveVolume::Select(TConstArrayView<FGuid> Networks)const
{
    auto Out=MakeShared<FCLCompiledCaveVolume>();Out->Data=Data;Out->Spatial=Spatial;Out->SpatialNetwork=SpatialNetwork;for(const auto Id:Networks)Out->SelectedNetworks.Add(Id);return Out;
}
FBox FCLCompiledCaveVolume::Bounds(FGuid Network)const
{
    if(Spatial)return (!Network.IsValid()||Network==SpatialNetwork)&&(SelectedNetworks.IsEmpty()||SelectedNetworks.Contains(SpatialNetwork))?Spatial->Bounds():FBox(ForceInit);
    FBox B(ForceInit);
    for(const auto& P:Data->Paths)if((!Network.IsValid()||P.Network==Network)&&(SelectedNetworks.IsEmpty()||SelectedNetworks.Contains(P.Network)))B+=P.Bounds;
    for(const auto& P:Data->Chambers)if((!Network.IsValid()||P.Source.Network==Network)&&(SelectedNetworks.IsEmpty()||SelectedNetworks.Contains(P.Source.Network)))B+=P.Bounds;
    return B;
}
double FCLCompiledCaveVolume::MinimumHalfWidth(FGuid Network)const
{
    if(Spatial)return 2.;
    double M=MAX_dbl;
    for(const auto& P:Data->Paths)if((!Network.IsValid()||P.Network==Network)&&(SelectedNetworks.IsEmpty()||SelectedNetworks.Contains(P.Network)))M=FMath::Min(M,P.MinimumWidth);
    return M;
}
double FCLCompiledCaveVolume::Compose(const FVector3d& P,double Field,FCLSurfaceWeights*)const
{ComposeBatch(MakeArrayView(&P,1),MakeArrayView(&Field,1));return Field;}
void FCLCompiledCaveVolume::ComposeBatch(TConstArrayView<FVector3d> Positions,TArrayView<double> Fields)const
{ComposeBatchInternal(Positions,Fields,true);}
void FCLCompiledCaveVolume::ComposeBatchOptimized(TConstArrayView<FVector3d> Positions,TArrayView<double> Fields)const
{ComposeBatchInternal(Positions,Fields,true,true);}
void FCLCompiledCaveVolume::ComposeBatchInternal(TConstArrayView<FVector3d> Positions,TArrayView<double> Fields,bool IncludeInfills,bool Optimized)const
{
    check(Positions.Num()==Fields.Num());
    if(Spatial)
    {
        if(!SelectedNetworks.IsEmpty()&&!SelectedNetworks.Contains(SpatialNetwork))
        {for(double& Field:Fields)if(!FMath::IsFinite(Field))Field=-1.e6;return;}
        TArray<double> Air;Spatial->Sample(Positions,Air);for(int32 I=0;I<Fields.Num();++I)Fields[I]=FMath::IsFinite(Fields[I])?FMath::Max(Fields[I],Air[I]):Air[I];return;
    }
    if(!Data->Index)return;
    // Native AABB index selects feature/query pairs. No 64-bit operation mask:
    // one real network is no longer squeezed into the authored-edit proof limit.
    TMap<int32,TArray<int32>> Work;
    if(Optimized&&!Positions.IsEmpty())
    {
        // Match native BVH element and point bounds, including its outward
        // float rounding. One broad traversal replaces N repeated traversals.
        TArray<FVoxelFastBox> PointBounds;PointBounds.Reserve(Positions.Num());
        for(const auto& P:Positions)PointBounds.Emplace(FVoxelBox(P,P));
        auto Batch=PointBounds[0];for(int32 I=1;I<PointBounds.Num();++I)Batch=Batch.UnionWith(PointBounds[I]);
        const auto BatchMin=Batch.GetMin(),BatchMax=Batch.GetMax();
        TArray<int32> Candidates;Data->Index->Intersects(Batch,[&](int32 Feature){Candidates.Add(Feature);return false;});
        int32 SampleHits=0;
        if(Candidates.Num()>32)for(const int32 I:{0,Positions.Num()/2,Positions.Num()-1})Data->Index->Intersects(PointBounds[I],[&](int32){++SampleHits;return false;});
        // Scattered sparse/PCG queries keep native point traversal. Coherent
        // grid packets amortize traversal while retaining the exact pair set.
        if(Positions.Num()<8||Candidates.Num()>32+SampleHits*2/3)
        {
            for(int32 I=0;I<Positions.Num();++I)Data->Index->Intersects(PointBounds[I],[&](int32 Feature){Work.FindOrAdd(Feature).Add(I);return false;});
        }
        else for(const int32 Feature:Candidates)
        {
            const auto& Bounds=Data->FeatureBounds[Feature];const auto Min=Bounds.GetMin(),Max=Bounds.GetMax();
            const bool Contains=Min.X<=BatchMin.X&&Min.Y<=BatchMin.Y&&Min.Z<=BatchMin.Z&&Max.X>=BatchMax.X&&Max.Y>=BatchMax.Y&&Max.Z>=BatchMax.Z;
            TArray<int32> Indices;Indices.Reserve(Positions.Num());
            for(int32 I=0;I<Positions.Num();++I)if(Contains||PointBounds[I].Intersects(Bounds))Indices.Add(I);
            if(!Indices.IsEmpty())Work.Add(Feature,MoveTemp(Indices));
        }
    }
    else
    {
    for(int32 I=0;I<Positions.Num();++I)Data->Index->Intersects(FVoxelFastBox(FVoxelBox(Positions[I],Positions[I])),[&](int32 Feature)
    {Work.FindOrAdd(Feature).Add(I);return false;});
    }
    TArray<int32> Features;Work.GetKeys(Features);Features.Sort();
    if(Optimized&&Features.IsEmpty())
    {for(double& Field:Fields)if(!FMath::IsFinite(Field))Field=-1.e6;return;}
    TArray<double> Void;Void.Init(MAX_flt,Positions.Num());
    // Cached across overlapping native features in this batch. All floor
    // coordinates use the same immutable planetary rock field.
    TArray<double> Restored;Restored.Init(MAX_dbl,Positions.Num());
    TMap<int32,TArray<double>> BeddedVoids;TMap<int32,FGuid> BeddedOwners;TMap<int32,double> BeddedSmoothness;
    TMap<FGuid,TArray<double>> BeddedFloors,JoinedVoids;
    TMap<double,TArray<double>> RollingByScale;
    for(int32 Feature:Features)
    {
        const bool Path=Feature<Data->Paths.Num();const auto Network=Path?Data->Paths[Feature].Network:Data->Chambers[Feature-Data->Paths.Num()].Source.Network;
        if(!SelectedNetworks.IsEmpty()&&!SelectedNetworks.Contains(Network))continue;
        const auto& Indices=Work[Feature];const int32 N=Indices.Num();FVoxelDoubleVectorBuffer Local,Slot;Local.Allocate(N);TArray<double> Units,SlotUnits,Phases;Units.SetNumUninitialized(N);Phases.SetNumZeroed(N);
        const bool Modern=Path?Data->Paths[Feature].Form!=ECLCaveForm::LegacyOval:Data->Chambers[Feature-Data->Paths.Num()].Source.Form!=ECLCaveForm::LegacyOval;
        const bool Bedded=Path?Data->Paths[Feature].Form==ECLCaveForm::BeddedPassage:Data->Chambers[Feature-Data->Paths.Num()].Source.Form==ECLCaveForm::BeddedChamber;
        const int32 GeometryProfile=Path?Data->Paths[Feature].GeometryProfile:Data->Chambers[Feature-Data->Paths.Num()].Source.GeometryProfile;
        const bool Organic=Bedded&&GeometryProfile>=7;
        TArray<double> FloorHeights;if(Bedded)FloorHeights.SetNumUninitialized(N);
        const bool Keyhole=!Modern&&Path&&Data->Paths[Feature].Incision>0;if(Keyhole){Slot.Allocate(N);SlotUnits.SetNumUninitialized(N);}
        double Smoothness=0;
        if(Path)
        {
            const auto& C=Data->Paths[Feature];TArray<float> X,Y,Z,Keys;X.SetNumUninitialized(N);Y.SetNumUninitialized(N);Z.SetNumUninitialized(N);Keys.SetNumUninitialized(N);
            for(int32 J=0;J<N;++J){const auto P=Positions[Indices[J]]-C.Origin;X[J]=P.X;Y[J]=P.Y;Z[J]=P.Z;}
            TVoxelArray<ispc::FSegment> Segments;
            if(!Optimized)for(const auto& S:C.Segments)Segments.Add(S.Segment);
            const auto& NativeSegments=Optimized?C.NativeSegments:Segments;
            ispc::VoxelSplineStamp_FindNearest3D(Keys.GetData(),X.GetData(),Y.GetData(),Z.GetData(),N,NativeSegments.GetData(),NativeSegments.Num());
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
                double Vertical=Height;
                if(Bedded)
                {
                    const int32 Index=Indices[J];
                    if(C.RampedFloor)
                    {
                        // The declared walk-in floor is a continuous radial
                        // grade, independent of the nearest cubic interval.
                        const auto D=Positions[Index].GetSafeNormal();
                        const double Angle=FMath::Atan2(FVector::DotProduct(D,C.FloorForward),FVector::DotProduct(D,C.FloorStartDirection));
                        const double Den=FMath::Sin(C.FloorAngle)*FMath::Cos(Angle)+(1-FMath::Cos(C.FloorAngle))*FMath::Sin(Angle);
                        const double T=FMath::Clamp(FMath::Sin(Angle)/FMath::Max(Den,1.e-6),0.,1.);
                        Vertical=Positions[Index].Length()-FMath::Lerp(C.RampStart,C.RampEnd,T*T*(2-T));
                    }
                    else
                    {
                    if(Restored[Index]==MAX_dbl)Restored[Index]=C.FloorRock->RestoredRadiusMeters(Positions[Index]);
                    const double Reach=FVector::Distance(C.Curve->Position.Points[A].OutVal,C.Curve->Position.Points[A+1].OutVal);
                    const double Grade=((*C.FloorRadii)[A+1]-(*C.FloorRadii)[A])/FMath::Max(Reach,.001);
                    // Native endpoint caps keep the floor's grade; a clamped
                    // nearest key must not turn each ramp interval into a step.
                    Vertical=Restored[Index]-(FMath::Lerp((*C.FloorRadii)[A],(*C.FloorRadii)[A+1],Key-A)+End*Grade);
                    }
                    FloorHeights[J]=Vertical;
                }
                Local.Set(J,FVector(Side/S.X,Vertical/S.Y,End/U));

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
            for(int32 J=0;J<N;++J){const auto P=C.Rotation.UnrotateVector(Positions[Indices[J]]-C.Center);auto Q=P/C.HalfExtents;
                if(Bedded){const int32 Index=Indices[J];if(Restored[Index]==MAX_dbl)Restored[Index]=C.FloorRock->RestoredRadiusMeters(Positions[Index]);FloorHeights[J]=Restored[Index]-C.FloorRadius;Q.Z=FloorHeights[J]/C.HalfExtents.Z;}
                Local.Set(J,Q);Units[J]=U;}
        }
        FVoxelFloatBuffer Native;Native.Allocate(N);FVoxelSphereShape Shape;Shape.Radius=1;
        if(Modern)
        {
            const auto Form=Path?Data->Paths[Feature].Form:Data->Chambers[Feature-Data->Paths.Num()].Source.Form;
            SampleSection(Form,Local,Path?Data->Paths[Feature].Development:0,Native,Phases,
                Path?Data->Paths[Feature].NoiseSeed:Data->Chambers[Feature-Data->Paths.Num()].NoiseSeed,GeometryProfile);
            if(Path&&Data->Paths[Feature].EndForm!=Form)
            {
                FVoxelFloatBuffer Other;Other.Allocate(N);SampleSection(Data->Paths[Feature].EndForm,Local,Data->Paths[Feature].Development,Other,Phases,Data->Paths[Feature].NoiseSeed,GeometryProfile);
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
            if(Modern&&((!Path&&Data->Chambers[Feature-Data->Paths.Num()].Source.Form!=ECLCaveForm::RoofScar)||(Bedded&&Path)))
            {
                Points.Reset();for(int32 J=0;J<N;++J)
                {
                    // A wavelength changing with the nearest section scales
                    // planet-centred coordinates and creates squeezed ridges.
                    // Keep it fixed over one complete bedded passage.
                    const double Wavelength=Bedded&&Path?FMath::Clamp(Data->Paths[Feature].MinimumWidth*.6,2.,5.):FMath::Clamp(Units[J]*.6,2.,5.);
                    Points.Add(Positions[Indices[J]]/Wavelength);
                }
                Data->Noise->SampleBatch(Points,(Path?Data->Paths[Feature].NoiseSeed:Data->Chambers[Feature-Data->Paths.Num()].NoiseSeed)^0x52454C49,RoomRelief);
            }
        }
        FVoxelFloatBuffer Floor;
        if(Bedded)
        {
            TArray<float> Relief;TArray<FVector> Points;
            if(!Optimized||!Organic)
            {
                Points.Reserve(N);for(int32 J=0;J<N;++J)Points.Add(Positions[Indices[J]]/6.);
                if(Data->Noise)Data->Noise->SampleBatch(Points,Data->Seed^0x464C4F52,Relief);
            }
            TArray<double> Rolling;const TArray<double>* SharedRolling=nullptr;
            if(Organic&&Data->Noise)
            {
                const double Scale=Path?Data->Paths[Feature].FloorReliefScale:Data->Chambers[Feature-Data->Paths.Num()].Source.FloorReliefScale;
                // Native noise is not bit-invariant to regrouping partial
                // packets. Reuse only identical complete input packets; partial
                // feature requests retain their original native batch exactly.
                if(Optimized&&N==Positions.Num())
                {
                    auto& Cached=RollingByScale.FindOrAdd(Scale);
                    if(Cached.IsEmpty())CLSampleCaveFloorRelief(Positions,*Data->Noise,Data->Seed,Scale,Cached);
                    SharedRolling=&Cached;
                }
                else
                {
                    Points.Reset();for(int32 J=0;J<N;++J)Points.Add(Positions[Indices[J]]);
                    CLSampleCaveFloorRelief(Points,*Data->Noise,Data->Seed,Scale,Rolling);
                }
            }
            FVoxelDoubleVectorBuffer FloorPoints;FloorPoints.Allocate(N);Floor.Allocate(N);
            for(int32 J=0;J<N;++J)
            {
                // A broad clear walking floor meets low bedrock shoulders at
                // the margins. The chamber's central crossing shares its datum.
                const auto Q=Local[J];const double Edge=Path?FMath::Abs(Q.X):FVector2D(Q.X,Q.Y).Length();
                const double Bank=FMath::Min(Path?.6:1.2,Units[J]*.10)*FMath::Square(FMath::SmoothStep(Path?.5:.55,.95,Edge));
                FloorPoints.Set(J,FVector(0,0,-FloorHeights[J]+Bank+(Organic?(SharedRolling?(*SharedRolling)[Indices[J]]:(Rolling.IsEmpty()?0:Rolling[J])):(Relief.IsEmpty()?0:Relief[J]*.16))));
            }
            FVoxelPlaneShape Plane;Plane.Size=FVector2D(2.e6);Plane.Height=1;Plane.Sample(Floor.View(),FloorPoints);
        }
        TArray<double>* NetworkVoid=nullptr;TArray<double>* NetworkFloor=nullptr;
        if(Bedded)
        {
            const int32 Group=Path?Data->Paths[Feature].PassageId:Feature;
            NetworkVoid=&BeddedVoids.FindOrAdd(Group);NetworkFloor=&BeddedFloors.FindOrAdd(Network);
            if(NetworkVoid->IsEmpty())NetworkVoid->Init(MAX_flt,Positions.Num());
            if(NetworkFloor->IsEmpty())NetworkFloor->Init(MAX_flt,Positions.Num());
            BeddedOwners.Add(Group,Network);BeddedSmoothness.Add(Group,Smoothness);
        }
        for(int32 J=0;J<N;++J)
        {
            const int32 I=Indices[J];double Value=double(Native[J])*Units[J];
            if(Keyhole)Value=Blend(Value,double(Lower[J])*SlotUnits[J],Smoothness);
            // Native VPP noise in fixed physical coordinates. Correlated metre-
            // scale wall relief cannot close the protected passage core.
            if(Data->Noise)Value+=double(Noise[J])*(Bedded?.08:Modern?FMath::Min(.08,Units[J]*.025):Units[J]*.12);
            if(!RoomRelief.IsEmpty())Value+=RoomRelief[J]*(Organic?FMath::Min(Path?1.:1.4,Units[J]*.18):(Bedded&&Path?.11:FMath::Min(.7,Units[J]*.08)));
            const double BodyValue=Value;
            if(Bedded)Value=FVoxelBlendModeUtilities::ComputeDistance(FVoxelVolumeTransform(),float(Value),Floor[J],EVoxelVolumeBlendMode::Intersect,true,.12,nullptr);
            if(Bedded)
            {
                auto& V=(*NetworkVoid)[I];V=V==MAX_flt?Value:Blend(V,Value,0);
                // A distant interval's conservative AABB is not floor support.
                if(BodyValue<Smoothness*2)(*NetworkFloor)[I]=FMath::Min((*NetworkFloor)[I],double(Floor[J]));
            }
            else Void[I]=Void[I]==MAX_flt?Value:Blend(Void[I],Value,Smoothness);
        }
    }
    // Clamp the shared floor once after native passage/room unions. Repeated
    // overlapping cubic intervals must not erode the floor downward at a mouth.
    // Native hard union joins intervals of one continuous passage exactly.
    // Smooth only complete passages and rooms, avoiding count-dependent roof
    // corrugation where short native intervals overlap.
    TArray<int32> Groups;BeddedVoids.GetKeys(Groups);Groups.Sort();
    for(int32 Group:Groups)
    {
        auto& Joined=JoinedVoids.FindOrAdd(BeddedOwners[Group]);if(Joined.IsEmpty())Joined.Init(MAX_flt,Positions.Num());
        const auto& Values=BeddedVoids[Group];for(int32 I=0;I<Values.Num();++I)if(Values[I]!=MAX_flt)
            Joined[I]=Joined[I]==MAX_flt?Values[I]:Blend(Joined[I],Values[I],BeddedSmoothness[Group]);
    }
    TArray<FGuid> FloorNetworks;JoinedVoids.GetKeys(FloorNetworks);FloorNetworks.Sort();
    for(const auto Network:FloorNetworks)
    {
        const auto& Values=JoinedVoids[Network];const auto& Floors=BeddedFloors[Network];
        for(int32 I=0;I<Positions.Num();++I)
        {
            const double V=Values[I];if(V==MAX_flt)continue;
            const double Clipped=Floors[I]==MAX_flt?V:FVoxelBlendModeUtilities::ComputeDistance(FVoxelVolumeTransform(),float(V),float(Floors[I]),EVoxelVolumeBlendMode::Intersect,true,0,nullptr);
            Void[I]=Void[I]==MAX_flt?Clipped:Blend(Void[I],Clipped,0);
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
            FVoxelFloatBuffer Values;Values.Allocate(Indices.Num());SampleInfill(Block,P,Values,Data->Noise.Get(),Data->Seed);
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
    if(Spatial)return Position;
    if(!Data->InfillIndex)return Position;
    FVector Source=Position;bool Found=false;TArray<int32> Hits;
    Data->InfillIndex->Intersects(FVoxelFastBox(FVoxelBox(Position,Position)),[&](int32 I){Hits.Add(I);return false;});Hits.Sort([](int32 A,int32 B){return A>B;});
    for(int32 I:Hits)
    {
        const auto& B=Data->Infills[I];if(!SelectedNetworks.IsEmpty()&&!SelectedNetworks.Contains(B.Network))continue;
        FVoxelDoubleVectorBuffer P;P.Allocate(1);P.Set(0,B.Rotation.UnrotateVector(Position-B.Center));FVoxelFloatBuffer D;D.Allocate(1);SampleInfill(B,P,D,Data->Noise.Get(),Data->Seed);
        if(D[0]>0)continue;
        if(B.BedrockPier||B.BeddedRemnant)return Position;
        double Air=FVoxelUtilities::NaNf();ComposeBatchInternal(MakeArrayView(&Position,1),MakeArrayView(&Air,1),false);if(Air<=0)continue;
        Source=B.BedrockPier?Position:B.SourceCenter+B.SourceRotation.RotateVector(B.Rotation.UnrotateVector(Position-B.Center));Found=true;break;
    }
    return Found?Source:Position;
}

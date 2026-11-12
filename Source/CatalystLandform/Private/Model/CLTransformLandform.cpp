#include "Model/CLTransformLandform.h"
#include "Model/CLRegionGeneration.h"
#include "Model/CLTectonics.h"
namespace
{
uint32 Mix(uint32 V){V^=V>>16;V*=0x7feb352dU;V^=V>>15;V*=0x846ca68bU;return V^(V>>16);}
double Random(uint32 V){return (double(Mix(V))+.5)/4294967296.;}
double Smooth(double T){T=FMath::Clamp(T,0.,1.);return T*T*T*(T*(T*6-15)+10);}
double Bell(double X){return FMath::Exp(-X*X);}
}
FCLTransformLandform::FCLTransformLandform()
{
    auto Add=[&](const TCHAR* N,double P,double VD,double VW,double RH,double RW,double O,double BD,double BW,double S,double D)
    {auto& E=Examples.AddDefaulted_GetRef();E.Name=N;E.Position=P;E.ValleyDepthMeters=VD;E.ValleyWidthMeters=VW;E.RidgeHeightMeters=RH;E.RidgeWidthMeters=RW;E.LateralOffsetMeters=O;E.BasinDepthMeters=BD;E.BasinWidthMeters=BW;E.SecondaryStrandWeight=S;E.DissectionMeters=D;};
    Add(TEXT("Linear shear valley"),0,12,75,5,55,5,2,65,.12,1);
    Add(TEXT("Offset ridge landscape"),.30,6,55,13,42,48,4,75,.30,2);
    Add(TEXT("Stepover basin landscape"),.65,4,45,8,48,34,28,100,.85,1.5);
    Add(TEXT("Pressure ridge landscape"),1,5,50,39,52,54,10,70,.50,5);
}
bool FCLTransformLandform::Validate(FString& Error)const
{
    if(!IsActive())return true;
    if(AlgorithmVersion!=1||!FMath::IsFinite(Character)||Character<0||Character>1||!FMath::IsFinite(FeatureLengthMeters)||FeatureLengthMeters<20||FeatureLengthMeters>100000||Examples.Num()!=4)
    {Error=TEXT("Transform family requires profile 1, Character 0..1, length 20..100000 m and four authored examples");return false;}
    const double Positions[]={0,.30,.65,1};
    for(int32 I=0;I<4;++I)
    {
        const auto& E=Examples[I];const double V[]={E.ValleyDepthMeters,E.ValleyWidthMeters,E.RidgeHeightMeters,E.RidgeWidthMeters,E.LateralOffsetMeters,E.BasinDepthMeters,E.BasinWidthMeters,E.DissectionMeters};
        for(double X:V)if(!FMath::IsFinite(X)||X<0||X>100000){Error=TEXT("Transform physical values must be finite metres in 0..100000");return false;}
        if(E.Position!=Positions[I]||E.Name.TrimStartAndEnd().IsEmpty()||E.ValleyWidthMeters<1||E.RidgeWidthMeters<1||E.BasinWidthMeters<1||!FMath::IsFinite(E.SecondaryStrandWeight)||E.SecondaryStrandWeight<0||E.SecondaryStrandWeight>1)
        {Error=TEXT("Transform examples require positions 0, 0.30, 0.65, 1, names, positive widths and strand weights 0..1");return false;}
    }
    return true;
}
FCLTransformExample FCLTransformLandform::Interpolate()const
{
    int32 I=0;while(I<2&&Character>Examples[I+1].Position)++I;
    const auto& A=Examples[I];const auto& B=Examples[I+1];const double T=Smooth((Character-A.Position)/(B.Position-A.Position));auto E=A;
#define BLEND(V) E.V=FMath::Lerp(A.V,B.V,T)
    BLEND(ValleyDepthMeters);BLEND(ValleyWidthMeters);BLEND(RidgeHeightMeters);BLEND(RidgeWidthMeters);BLEND(LateralOffsetMeters);BLEND(BasinDepthMeters);BLEND(BasinWidthMeters);BLEND(SecondaryStrandWeight);BLEND(DissectionMeters);
#undef BLEND
    E.Position=Character;return E;
}
TSharedPtr<const FCLCompiledTransformLandform> FCLCompiledTransformLandform::Compile(const FCLTransformLandform& S,int32 Seed,double R,TConstArrayView<FCLGeneratedRegionCell> Cells,TSharedPtr<const FCLCompiledTectonics> Plates,FString& Error)
{
    if(!S.Validate(Error))return nullptr;
    if(!S.IsActive()||!FMath::IsFinite(R)||R<=0||Cells.IsEmpty()){Error=TEXT("Transform compilation requires an active profile, positive reference radius and generated cells");return nullptr;}
    auto Out=MakeShared<FCLCompiledTransformLandform>();Out->Form=S.Interpolate();Out->Radius=R;
    // Scale only when a small planet cannot contain the physical example.
    Out->Scale=FMath::Min(1.,R*.45/S.FeatureLengthMeters);
    for(int32 I=0;I<Cells.Num();++I)
    {
        const auto& Cell=Cells[I];const uint32 H=Mix(uint32(Seed)^uint32(S.Seed)^GetTypeHash(Cell.Id));
        FCorridor C;C.Centre=Cell.Centre;C.Phase=Random(H+1)*2*PI;C.Variation=FMath::Lerp(.80,1.18,Random(H+2));C.Sign=Random(H+3)<.5?-1:1;
        FVector X,Y;C.Centre.FindBestAxisVectors(X,Y);const double A=Random(H+4)*2*PI;C.Along=X*FMath::Cos(A)+Y*FMath::Sin(A);
        double Best=0;
        if(Plates)Plates->VisitExtensionBelts(C.Centre,[&](const FCLPlateBeltSample& B)
        {const double W=B.Envelope*B.StateWeight*B.ShearDrive;if(W>Best&&W>.005){Best=W;C.Along=B.AlongDirection;C.Sign=B.SignedShear<0?-1:1;C.bPlateGuided=true;}});
        C.Along=(C.Along-C.Centre*FVector::DotProduct(C.Along,C.Centre)).GetSafeNormal();C.Across=FVector::CrossProduct(C.Along,C.Centre).GetSafeNormal();
        C.HalfLength=S.FeatureLengthMeters*.5*Out->Scale*FMath::Lerp(.82,1.20,Random(H+5));Out->Corridors.Add(C);
    }
    return Out;
}
FCLCompiledTransformLandform::FSample FCLCompiledTransformLandform::Sample(const FVector& Direction)const
{
    const FVector D=Direction.GetSafeNormal();FSample Out;double Sum=0;
    const auto& E=Form;
    for(const auto& C:Corridors)
    {
        const double Reach=C.HalfLength+Scale*FMath::Max3(E.ValleyWidthMeters,E.BasinWidthMeters,E.RidgeWidthMeters)*3.5+Scale*30;
        const double Angle=FMath::Min(Reach/Radius,PI*.49);
        // Conservative chord bound; avoids a cosine per corridor per voxel.
        if(FVector::DotProduct(D,C.Centre)<1-.5*Angle*Angle)continue;
        const double Along=FMath::Atan2(FVector::DotProduct(D,C.Along),FVector::DotProduct(D,C.Centre))*Radius;
        const double End=1-Smooth((FMath::Abs(Along)/C.HalfLength-.58)/.42);if(End<=0)continue;
        const double U=Along/C.HalfLength;
        const double Meander=Scale*(14*FMath::Sin(U*3.1+C.Phase)+6*FMath::Sin(U*7.3+C.Phase*.7));
        const double Step=Scale*E.BasinWidthMeters*.60*FMath::Tanh(U*4)*E.SecondaryStrandWeight;
        const double Across=FMath::Asin(FMath::Clamp(FVector::DotProduct(D,C.Across),-1.,1.))*Radius-Meander;
        const double Width=FMath::Max(1.,Scale*FMath::Max3(E.ValleyWidthMeters,E.BasinWidthMeters,E.RidgeWidthMeters));
        const double Edge=1-Smooth((FMath::Abs(Across)/Width-1.8)/1.5);if(Edge<=0)continue;
        const double W=End*Edge;Sum+=W;
        const double X=Across-Step;
        const double Valley=-E.ValleyDepthMeters*Bell(X/(Scale*E.ValleyWidthMeters));
        // Offset opposing banks and discontinuous shutter ridges remain visible at anchor zero.
        const double Phase=U*5.8+C.Phase+C.Sign*FMath::Tanh(X/Width)*E.LateralOffsetMeters/45;
        const double Bank=Bell((X-Width*.85)/(Scale*E.RidgeWidthMeters))*(.64+.22*FMath::Sin(Phase))+
            .65*Bell((X+Width*.95)/(Scale*E.RidgeWidthMeters))*(.66+.24*FMath::Sin(Phase+1.8));
        // Opening and constraining bends occupy different along-strike positions.
        const double Basin=-E.BasinDepthMeters*Bell((U+.22)*3.1)*Bell(Across/(Scale*E.BasinWidthMeters));
        const double Pressure=E.RidgeHeightMeters*.95*Bell((U-.34)*3.4)*Bell((Across+Width*.20)/(Scale*E.RidgeWidthMeters));
        const double Strand=-E.ValleyDepthMeters*.65*E.SecondaryStrandWeight*Bell((Across+Step+Width*.60)/(Scale*E.ValleyWidthMeters*.45))*Bell(U*1.3);
        const double Gullies=-E.DissectionMeters*FMath::Pow(.5+.5*FMath::Sin(Along/(Scale*13)+2*FMath::Sin(Across/(Scale*27))+C.Phase),6)*FMath::Min(1.,Bank);
        Out.Relief+=W*Scale*C.Variation*(Valley+E.RidgeHeightMeters*Bank+Basin+Pressure+Strand+Gullies);
        Out.TangentOffset+=W*C.Along*(Scale*E.LateralOffsetMeters*.5*C.Sign*FMath::Tanh(X/FMath::Max(Scale*12.,1.)));
    }
    const double Normalize=1/FMath::Max(1.,Sum);Out.Relief*=Normalize;Out.TangentOffset*=Normalize;return Out;
}
double FCLCompiledTransformLandform::MaximumDisplacementMeters()const
{return Scale*1.2*(Form.ValleyDepthMeters*1.65+Form.BasinDepthMeters+Form.RidgeHeightMeters*2.7+Form.DissectionMeters);}

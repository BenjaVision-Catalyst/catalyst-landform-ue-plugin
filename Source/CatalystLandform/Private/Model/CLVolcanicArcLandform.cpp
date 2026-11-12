#include "Model/CLVolcanicArcLandform.h"
#include "Model/CLRegionGeneration.h"
#include "Model/CLTectonics.h"
namespace
{
uint32 MixArc(uint32 V){V^=V>>16;V*=0x7feb352dU;V^=V>>15;V*=0x846ca68bU;return V^(V>>16);}
double RandomArc(uint32 V){return (double(MixArc(V))+.5)/4294967296.;}
double SmoothArc(double T){T=FMath::Clamp(T,0.,1.);return T*T*T*(T*(T*6-15)+10);}
double BellArc(double X){return FMath::Exp(-X*X);}
using FArc=FCLCompiledVolcanicArcLandform;
int32 DrainageBin(double V)
{return FMath::Clamp(int32(FMath::FloorToDouble((V+FArc::DrainageGridExtent)*(FArc::DrainageGridSize/(2*FArc::DrainageGridExtent)))),0,FArc::DrainageGridSize-1);}
void BuildDrainage(FArc::FCentre& C,const FCLVolcanicArcLandform& S,uint32 Seed,double Scale)
{
    C.DrainageBins.SetNum(FArc::DrainageGridSize*FArc::DrainageGridSize);
    const double Crater=FMath::Clamp(S.CraterRadiusMeters*Scale/C.Radius,.03,.45);
    const double Prominence=C.bSecondary?S.SecondaryHeight:1;
    auto Point=[&](double U,double Theta)
    {
        const double Warp=1+S.Asymmetry*(.58*FMath::Sin(Theta*2+C.Phase)+.24*FMath::Sin(Theta*3-C.Phase)+.18*FMath::Sin(Theta*5+C.Phase));
        return FVector2D(FMath::Cos(Theta),FMath::Sin(Theta))*(U*Warp);
    };
    auto Segment=[&](FVector2D A,FVector2D B,double WA,double WB,double DA,double DB)
    {
        auto& V=C.Drainage.AddDefaulted_GetRef();V.Start=A;V.Delta=B-A;V.InverseLengthSquared=1/FMath::Max(V.Delta.SizeSquared(),1.e-12);
        V.StartWidth=WA;V.EndWidth=WB;V.StartDepth=DA;V.EndDepth=DB;
        const double W=FMath::Max(WA,WB);const uint16 Id=uint16(C.Drainage.Num()-1);
        for(int32 Y=DrainageBin(FMath::Min(A.Y,B.Y)-W);Y<=DrainageBin(FMath::Max(A.Y,B.Y)+W);++Y)
            for(int32 X=DrainageBin(FMath::Min(A.X,B.X)-W);X<=DrainageBin(FMath::Max(A.X,B.X)+W);++X)
                C.DrainageBins[Y*FArc::DrainageGridSize+X].Add(Id);
    };
    // Independent, irregular catchment sizes, stable across Character. Gaps are
    // positive and normalized so no periodic angular frequency is introduced.
    const int32 Count=6+int32(RandomArc(Seed+31)*5);double Total=0;
    double Gaps[11];for(int32 I=0;I<Count;++I){Gaps[I]=FMath::Lerp(.45,1.65,RandomArc(Seed+100+I*43));Total+=Gaps[I];}
    double Theta=C.Phase;
    for(int32 I=0;I<Count;++I)
    {
        const uint32 H=Seed+uint32(I)*337;Theta+=2*PI*Gaps[I]/Total;
        const double Head=FMath::Max(Crater+.055,FMath::Lerp(.20,.47,RandomArc(H+32)));
        const double Toe=FMath::Min(S.ApronReach-.08,FMath::Lerp(1.06,1.40,RandomArc(H+33)));
        const double Bend=FMath::Lerp(-.16,.16,RandomArc(H+34));
        const double Depth=S.IncisionMeters*FMath::Lerp(.26,1.0,RandomArc(H+35));
        const double Width=FMath::Clamp(FMath::Max(FMath::Lerp(.020,.042,RandomArc(H+36)),Depth*Scale*Prominence*2.1/C.Radius),.014,.16);
        auto Heading=[&](double U){const double T=(U-Head)/(Toe-Head);return Theta+Bend*SmoothArc(T)+.022*FMath::Sin(T*7+RandomArc(H+37)*6);};
        auto WidthAt=[&](double U){return Width*FMath::Lerp(.65,1.35,SmoothArc((U-Head)/(Toe-Head)));};
        auto DepthAt=[&](double U)
        {
            const double T=(U-Head)/(Toe-Head);
            return Depth*SmoothArc(T/.22)*(1-SmoothArc((U-.68)/.66))*(1-SmoothArc((T-.73)/.27))*FMath::Lerp(.50,1.,SmoothArc(T));
        };
        FVector2D Previous;double PreviousW=0,PreviousD=0;
        for(int32 J=0;J<=9;++J)
        {
            const double U=FMath::Lerp(Head,Toe,double(J)/9);const auto P=Point(U,Heading(U));
            const double W=WidthAt(U),D=DepthAt(U);
            if(J)Segment(Previous,P,PreviousW,W,PreviousD,D);Previous=P;PreviousW=W;PreviousD=D;
        }
        // One or two unequal headwater tributaries converge into this trunk.
        // Their offset and its derivative vanish at the junction; depths merge
        // with max, not a sum that would excavate a pit at every confluence.
        const int32 Branches=1+int32(RandomArc(H+38)*2);
        for(int32 B=0;B<Branches;++B)
        {
            const double Join=FMath::Lerp(FMath::Max(Head+.16,.60),.88,RandomArc(H+39+B*17));
            const double Start=FMath::Lerp(FMath::Max(Head,.34),Join-.12,RandomArc(H+40+B*17));
            const double Offset=(B?1:-1)*FMath::Lerp(.13,.31,RandomArc(H+41+B*17));
            for(int32 J=0;J<=5;++J)
            {
                const double T=double(J)/5,U=FMath::Lerp(Start,Join,T);
                const auto P=Point(U,Heading(U)+Offset*FMath::Square(1-T));
                const double W=WidthAt(U)*FMath::Lerp(.42,1.,SmoothArc(T));
                const double D=DepthAt(Join)*SmoothArc(T/.30)*FMath::Lerp(.45,1.,SmoothArc(T));
                if(J)Segment(Previous,P,PreviousW,W,PreviousD,D);Previous=P;PreviousW=W;PreviousD=D;
            }
        }
    }
    // A few broad constructional shoulders, with independent orientations and
    // lengths, interrupt drainage without a second repeating set of petals.
    for(int32 I=0;I<3;++I)
    {
        const uint32 H=Seed+I*257;const double A=RandomArc(H+77)*2*PI,U=FMath::Lerp(.43,.72,RandomArc(H+78));
        auto& L=C.LavaLobes.AddDefaulted_GetRef();L.Centre=Point(U,A);L.Along=FVector2D(FMath::Cos(A),FMath::Sin(A));
        L.Length=FMath::Lerp(.24,.43,RandomArc(H+79));L.Width=FMath::Lerp(.10,.23,RandomArc(H+80));L.Height=FMath::Lerp(.28,1.,RandomArc(H+81));
    }
}
double SampleDrainage(const FArc::FCentre& C,const FVector2D& P)
{
    if(FMath::Abs(P.X)>=FArc::DrainageGridExtent||FMath::Abs(P.Y)>=FArc::DrainageGridExtent)return 0;
    double Cut=0;
    for(uint16 Index:C.DrainageBins[DrainageBin(P.Y)*FArc::DrainageGridSize+DrainageBin(P.X)])
    {
        const auto& S=C.Drainage[Index];const double T=FMath::Clamp(FVector2D::DotProduct(P-S.Start,S.Delta)*S.InverseLengthSquared,0.,1.);
        const double W=FMath::Lerp(S.StartWidth,S.EndWidth,T);const double D2=(P-S.Start-S.Delta*T).SizeSquared();
        if(D2>=W*W)continue;
        const double Bank=1-FMath::Sqrt(D2)/W;
        Cut=FMath::Max(Cut,FMath::Lerp(S.StartDepth,S.EndDepth,T)*Bank*Bank*(3-2*Bank));
    }
    return Cut;
}

}
bool FCLVolcanicArcLandform::Validate(FString& Error)const
{
    if(!IsActive())return true;
    const double Metres[]={HeightMeters,CraterDepthMeters,IncisionMeters,LavaShoulderMeters,ApronHeightMeters};
    for(double V:Metres)if(!FMath::IsFinite(V)||V<0||V>100000){Error=TEXT("Volcanic arc values require finite metres in 0..100000");return false;}
    auto In=[](double V,double Lo,double Hi){return FMath::IsFinite(V)&&V>=Lo&&V<=Hi;};
    if(AlgorithmVersion!=1||!In(Character,0,1)||!In(BaseRadiusMeters,20,100000)||!In(CraterRadiusMeters,1,BaseRadiusMeters*.45)||!In(FlankExponent,.5,3)||!In(SecondaryHeight,0,1)||!In(ApronReach,1.1,3)||!In(BreachStrength,0,1)||!In(Asymmetry,0,.6))
    {Error=TEXT("Volcanic arc requires profile 1, bounded shape values and a crater radius below 45% of the base radius");return false;}
    return true;
}
TSharedPtr<const FCLCompiledVolcanicArcLandform> FCLCompiledVolcanicArcLandform::Compile(const FCLVolcanicArcLandform& S,int32 Seed,double R,TConstArrayView<FCLGeneratedRegionCell> Cells,TSharedPtr<const FCLCompiledTectonics> Plates,FString& Error)
{
    if(!S.Validate(Error))return nullptr;
    if(!S.IsActive()||!FMath::IsFinite(R)||R<=0||Cells.IsEmpty()){Error=TEXT("Volcanic arc needs an active profile, positive radius and generated regions");return nullptr;}
    auto Out=MakeShared<FCLCompiledVolcanicArcLandform>();Out->Settings=S;Out->Radius=R;
    Out->Scale=FMath::Min(1.,R*.20/S.BaseRadiusMeters);
    // Stable site identities and offsets do not depend on Character. All three
    // vents exist throughout the progression; their prominence changes smoothly.
    for(const auto& Cell:Cells)
    {
        const uint32 H=MixArc(uint32(Seed)^MixArc(uint32(S.Seed))^GetTypeHash(Cell.Id));
        FVector X,Y;Cell.Centre.FindBestAxisVectors(X,Y);const double A=RandomArc(H+1)*2*PI;
        FVector Along=X*FMath::Cos(A)+Y*FMath::Sin(A);bool Guided=false;double Best=.005;
        if(Plates)Plates->VisitExtensionBelts(Cell.Centre,[&](const FCLPlateBeltSample& B)
        {const double W=B.Envelope*B.StateWeight*B.CompressionDrive;if(W>Best){Best=W;Along=B.AlongDirection;Guided=true;}});
        const FVector Across=FVector::CrossProduct(Along,Cell.Centre).GetSafeNormal();
        const FVector Site=(Cell.Centre+(Along*(RandomArc(H+2)-.5)+Across*(RandomArc(H+3)-.5))*.06).GetSafeNormal();
        for(int32 V=0;V<3;++V)
        {
            const uint32 Q=H+uint32(V)*101;
            const double Offset=V==0?0:V==1?.65:-.83;
            FCentre C;C.Direction=(Site+(Along*Offset+Across*(V==0?0:(RandomArc(Q+4)-.5)*.65))*(FMath::Min(280.,R*.20)/R)).GetSafeNormal();
            C.Along=FVector::VectorPlaneProject(Along,C.Direction).GetSafeNormal();C.Across=FVector::CrossProduct(C.Along,C.Direction).GetSafeNormal();
            C.Radius=S.BaseRadiusMeters*Out->Scale*FMath::Lerp(.80,1.15,RandomArc(Q+5))*(V==0?1:.72);
            C.HeightScale=FMath::Lerp(.82,1.18,RandomArc(Q+6))*(V==0?1:FMath::Lerp(.65,1.,RandomArc(Q+7)));
            C.Phase=RandomArc(Q+8)*2*PI;C.Elongation=FMath::Lerp(.90,1.17,RandomArc(Q+9));
            // Only a seeded subset develops a large open sector. Others remain intact.
            C.Breach=RandomArc(Q+11)>.55?FMath::Lerp(.7,1.,RandomArc(Q+12)):0;
            C.bSecondary=V!=0;C.bPlateGuided=Guided;
            BuildDrainage(C,S,Q,Out->Scale);
            Out->Centres.Add(MoveTemp(C));
        }
    }
    return Out;
}
double FCLCompiledVolcanicArcLandform::SampleMeters(const FVector& Direction)const
{
    const FVector D=Direction.GetSafeNormal();const auto& S=Settings;
    double Terrain=0;
    for(const auto& C:Centres)
    {
        const double Reach=C.Radius*S.ApronReach*(1+S.Asymmetry)*1.25,Angle=FMath::Min(Reach/Radius,PI*.49);
        const double Dot=FVector::DotProduct(D,C.Direction);if(Dot<1-.5*Angle*Angle)continue;
        const double X=FMath::Atan2(FVector::DotProduct(D,C.Along),Dot)*Radius/C.Elongation;
        const double Y=FMath::Atan2(FVector::DotProduct(D,C.Across),Dot)*Radius*C.Elongation;
        const double Theta=FMath::Atan2(Y,X),Radial=FMath::Sqrt(X*X+Y*Y)/C.Radius;
        const double Warp=1+S.Asymmetry*(.58*FMath::Sin(Theta*2+C.Phase)+.24*FMath::Sin(Theta*3-C.Phase)+.18*FMath::Sin(Theta*5+C.Phase));
        const double U=Radial/Warp;if(U>=S.ApronReach)continue;
        const double Prominence=C.bSecondary?S.SecondaryHeight:1;
        const double H=S.HeightMeters*C.HeightScale*Scale*Prominence;
        const double Crater=FMath::Clamp(S.CraterRadiusMeters*Scale/C.Radius,.03,.45);
        const double Rim=H*FMath::Pow(1-Crater,S.FlankExponent);
        double Cone=H*FMath::Pow(FMath::Max(0.,1-FMath::Max(U,Crater)),S.FlankExponent);
        Cone-=Scale*S.CraterDepthMeters*Prominence*(1-SmoothArc(U/Crater));
        // A finite, cached network of unequal catchments replaces angular waves.
        // Tributaries join downslope; compact rounded banks widen toward the foot.
        const FVector2D Local(X/C.Radius,Y/C.Radius);
        const double Apron=Scale*S.ApronHeightMeters*Prominence*(1-SmoothArc(U/S.ApronReach))*(1+SmoothArc(U/.35)*(-.14+.14*FMath::Sin(Theta*3+C.Phase)));
        const double Cut=Scale*Prominence*SampleDrainage(C,Local);
        // Deposition and dwindling relief limit lower-flank incision, so valleys
        // blend into the apron rather than slicing through it to a flat datum.
        Cone-=FMath::Min(Cut,.55*FMath::Max(Cone,0.)+.18*Apron);
        double Lava=0;
        for(const auto& L:C.LavaLobes)
        {
            const FVector2D Offset=Local-L.Centre;
            const double Along=FVector2D::DotProduct(Offset,L.Along)/L.Length;
            const double Across=(Offset.X*L.Along.Y-Offset.Y*L.Along.X)/L.Width;
            const double Distance=Along*Along+Across*Across;
            if(Distance<1)Lava=FMath::Max(Lava,L.Height*FMath::Square(1-Distance));
        }
        Cone+=Scale*S.LavaShoulderMeters*Prominence*Lava;
        // Angular structure must vanish at the vent axis: otherwise atan2's
        // undefined angle creates a discontinuity through the crater floor.
        const double Sector=FMath::Pow(.5+.5*FMath::Cos(Theta-C.Phase),14)*SmoothArc(U/(Crater*.8));
        const double Breach=C.Breach*S.BreachStrength;
        Cone-=Breach*Sector*Rim*(1-SmoothArc((U-.55)/.55));
        // Collapse deposits share the open sector and taper downstream; small
        // irregular mounds are confined to that apron rather than global noise.
        const double Hummocks=Scale*S.ApronHeightMeters*Prominence*Breach*Sector*SmoothArc((U-.45)/.25)*(1-SmoothArc(U/S.ApronReach))*BellArc((U-1.10)/.40)*(.7+.3*FMath::Sin(X/(Scale*19)+C.Phase)*FMath::Sin(Y/(Scale*23)));
        const double Value=FMath::Max(0.,Cone+Apron+Hummocks);
        // Smooth union preserves saddles between overlapping centres without
        // summing their heights into spikes. Zero-valued surroundings stay zero.
        const double K=Scale*3*Prominence;
        if(K>0){const double T=FMath::Max(K-FMath::Abs(Terrain-Value),0.)/K;Terrain=FMath::Max(Terrain,Value)+T*T*T*K/6*SmoothArc(Value/K);}
    }
    return Terrain;
}
double FCLCompiledVolcanicArcLandform::MaximumDisplacementMeters()const
{return Scale*(Settings.HeightMeters*1.2+Settings.LavaShoulderMeters+Settings.ApronHeightMeters*2+3);}

#include "Model/CLVolcanicArcLandform.h"
#include "Model/CLRegionGeneration.h"
#include "Model/CLTectonics.h"
namespace
{
uint32 MixArc(uint32 V){V^=V>>16;V*=0x7feb352dU;V^=V>>15;V*=0x846ca68bU;return V^(V>>16);}
double RandomArc(uint32 V){return (double(MixArc(V))+.5)/4294967296.;}
double SmoothArc(double T){T=FMath::Clamp(T,0.,1.);return T*T*T*(T*(T*6-15)+10);}
double BellArc(double X){return FMath::Exp(-X*X);}
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
            C.Phase=RandomArc(Q+8)*2*PI;C.Elongation=FMath::Lerp(.90,1.17,RandomArc(Q+9));C.Gullies=9+int32(RandomArc(Q+10)*9);
            // Only a seeded subset develops a large open sector. Others remain intact.
            C.Breach=RandomArc(Q+11)>.55?FMath::Lerp(.7,1.,RandomArc(Q+12)):0;
            C.bSecondary=V!=0;C.bPlateGuided=Guided;Out->Centres.Add(C);
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
        // Radial channels follow sinuous downslope paths, branch lower on the
        // flanks, and die into the depositional apron. They never cross the vent.
        const double Path=Theta*C.Gullies+.8*FMath::Sin(U*5+C.Phase)+C.Phase;
        const double Channel=FMath::Pow(.5+.5*FMath::Cos(Path),10);
        const double Branch=FMath::Pow(.5+.5*FMath::Cos(Path*2+.7*FMath::Sin(U*11)),14)*SmoothArc((U-.35)/.45);
        const double Flank=SmoothArc((U-Crater)/.18)*(1-SmoothArc((U-.72)/.55));
        Cone-=Scale*S.IncisionMeters*Prominence*(Channel+.3*Branch)*Flank;
        const double Lobes=FMath::Pow(.5+.5*FMath::Sin(Theta*5+C.Phase+.45*FMath::Sin(U*6)),3);
        Cone+=Scale*S.LavaShoulderMeters*Prominence*Lobes*BellArc((U-.66)/.30)*SmoothArc((U-Crater)/.15)*(1-SmoothArc((U-.85)/.45));
        // Angular structure must vanish at the vent axis: otherwise atan2's
        // undefined angle creates a discontinuity through the crater floor.
        const double Sector=FMath::Pow(.5+.5*FMath::Cos(Theta-C.Phase),14)*SmoothArc(U/(Crater*.8));
        const double Breach=C.Breach*S.BreachStrength;
        Cone-=Breach*Sector*Rim*(1-SmoothArc((U-.55)/.55));
        const double Apron=Scale*S.ApronHeightMeters*Prominence*(1-SmoothArc(U/S.ApronReach))*(1+SmoothArc(U/.35)*(-.14+.14*FMath::Sin(Theta*3+C.Phase)));
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

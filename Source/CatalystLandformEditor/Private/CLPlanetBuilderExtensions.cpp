#include "PlanetBuilder/CLPlanetBuilderExtensions.h"
namespace { TArray<FCLPlanetBuilderProvider>& Registry(){static TArray<FCLPlanetBuilderProvider> R;return R;} }
void FCLPlanetBuilderExtensions::Register(FCLPlanetBuilderProvider P){check(IsInGameThread());Unregister(P.Id);Registry().Add(MoveTemp(P));}
void FCLPlanetBuilderExtensions::Unregister(FName Id){check(IsInGameThread());Registry().RemoveAll([Id](const auto& P){return P.Id==Id;});}
TArray<FCLPlanetBuilderProvider> FCLPlanetBuilderExtensions::Providers(){check(IsInGameThread());auto R=Registry();R.Sort([](const auto& A,const auto& B){return A.Id.LexicalLess(B.Id);});return R;}

bool FCLPlanetBuilderOverlayFlow::BuildArcLengths()
{
    ArcMeters.Reset();if(BodyMeters.Num()<4||BodyMeters.Num()>16385||!BodyMeters[0].Equals(BodyMeters.Last(),1.e-7))return false;
    const double Radius=BodyMeters[0].Length();if(!FMath::IsFinite(Radius)||Radius<=0)return false;
    ArcMeters.Add(0);
    for(int32 I=1;I<BodyMeters.Num();++I)
    {
        if(BodyMeters[I].ContainsNaN()||!FMath::IsNearlyEqual(BodyMeters[I].Length(),Radius,Radius*1.e-8))return false;
        const FVector A=BodyMeters[I-1]/Radius,B=BodyMeters[I]/Radius;
        const double Angle=FMath::Atan2(FVector::CrossProduct(A,B).Length(),FVector::DotProduct(A,B));
        if(Angle<1.e-12||Angle>=UE_DOUBLE_PI-1.e-6)return false;
        ArcMeters.Add(ArcMeters.Last()+Angle*Radius);
    }
    return LengthMeters()>0;
}
double FCLPlanetBuilderOverlayFlow::AdvancePhase(double Phase,double Seconds)const
{
    if(LengthMeters()<=0||ArrowCount<=0||!FMath::IsFinite(Phase)||!FMath::IsFinite(Seconds)||Seconds<0||!FMath::IsFinite(SpeedMetersPerSecond))return 0;
    const double V=Phase+Seconds*SpeedMetersPerSecond*ArrowCount/LengthMeters();return V-FMath::FloorToDouble(V);
}
bool FCLPlanetBuilderOverlayFlow::Sample(double Distance,FVector& Point,FVector& Tangent)const
{
    const double Length=LengthMeters();if(Length<=0||ArcMeters.Num()!=BodyMeters.Num()||!FMath::IsFinite(Distance))return false;
    Distance=FMath::Fmod(Distance,Length);if(Distance<0)Distance+=Length;
    int32 Low=1,High=ArcMeters.Num()-1;while(Low<High){const int32 Mid=(Low+High)/2;if(ArcMeters[Mid]<=Distance)Low=Mid+1;else High=Mid;}
    const double Radius=BodyMeters[0].Length(),Angle=(ArcMeters[Low]-ArcMeters[Low-1])/Radius,F=(Distance-ArcMeters[Low-1])/(Angle*Radius);
    const FVector A=BodyMeters[Low-1]/Radius,B=BodyMeters[Low]/Radius;
    const FVector D=(A*FMath::Sin((1-F)*Angle)+B*FMath::Sin(F*Angle))/FMath::Sin(Angle);
    Point=D.GetSafeNormal()*Radius;Tangent=FVector::CrossProduct(FVector::CrossProduct(A,B).GetSafeNormal(1.e-30),Point).GetSafeNormal();return !Tangent.IsNearlyZero();
}

namespace
{
double WrapArc(double V,double Length){V=FMath::Fmod(V,Length);return V<0?V+Length:V;}
// Closest point on each minor great-circle arc. Preserves physical marker sites
// even when contour extraction changes its first node or traversal direction.
double ClosestArc(const FCLPlanetBuilderOverlayFlow& F,FVector P)
{
    P.Normalize();double Best=-2,Distance=0;
    for(int32 I=1;I<F.BodyMeters.Num();++I)
    {
        const FVector A=F.BodyMeters[I-1].GetSafeNormal(),B=F.BodyMeters[I].GetSafeNormal(),N=FVector::CrossProduct(A,B).GetSafeNormal();
        const double Span=(F.ArcMeters[I]-F.ArcMeters[I-1])/F.BodyMeters[0].Length();
        const FVector Q=FVector::VectorPlaneProject(P,N).GetSafeNormal();
        double Angle=FMath::Atan2(FVector::DotProduct(FVector::CrossProduct(A,Q),N),FVector::DotProduct(A,Q));
        if(Angle<0||Angle>Span)Angle=FVector::DotProduct(P,A)>FVector::DotProduct(P,B)?0:Span;
        const FVector Site=A*FMath::Cos(Angle)+FVector::CrossProduct(N,A)*FMath::Sin(Angle);const double Dot=FVector::DotProduct(P,Site);
        if(Dot>Best){Best=Dot;Distance=F.ArcMeters[I-1]+Angle*F.BodyMeters[0].Length();}
    }
    return WrapArc(Distance,F.LengthMeters());
}
}
void FCLPlanetBuilderFlowAnimation::SetFlow(const FCLPlanetBuilderOverlayFlow& Next)
{
    if(Next.LengthMeters()<=0)return;
    if(Flow.LengthMeters()>0&&Flow.BodyMeters!=Next.BodyMeters)for(auto& H:Heads){FVector P,T;if(Flow.Sample(H.DistanceMeters,P,T))H.DistanceMeters=ClosestArc(Next,P);}
    Flow=Next;
    const int32 Target=FMath::Clamp(Next.ArrowCount,0,128);
    int32 Count=0;for(const auto& H:Heads)Count+=!H.Retiring;
    if(Heads.IsEmpty()&&Target>0){for(int32 I=0;I<Target;++I)Heads.Add({NextId++,I*Flow.LengthMeters()/Target,0,false});return;}
    while(Count>Target)
    {
        int32 Retire=INDEX_NONE;double Gap=1.e100;
        for(int32 I=0;I<Heads.Num();++I)if(!Heads[I].Retiring)for(int32 J=I+1;J<Heads.Num();++J)if(!Heads[J].Retiring)
        {const double D=FMath::Abs(Heads[I].DistanceMeters-Heads[J].DistanceMeters),G=FMath::Min(D,Flow.LengthMeters()-D);if(G<Gap){Gap=G;Retire=Heads[I].Id>Heads[J].Id?I:J;}}
        if(Retire==INDEX_NONE)for(int32 I=Heads.Num()-1;I>=0;--I)if(!Heads[I].Retiring){Retire=I;break;}
        Heads[Retire].Retiring=true;--Count;
    }
    for(auto& H:Heads)if(Count<Target&&H.Retiring){H.Retiring=false;++Count;}
    while(Count<Target&&Heads.Num()<256)
    {
        TArray<double> Sites;for(const auto& H:Heads)if(!H.Retiring)Sites.Add(H.DistanceMeters);Sites.Sort();
        double Distance=0,Gap=-1;
        for(int32 I=0;I<Sites.Num();++I){const double End=I+1<Sites.Num()?Sites[I+1]:Sites[0]+Flow.LengthMeters(),G=End-Sites[I];if(G>Gap){Gap=G;Distance=WrapArc(Sites[I]+G*.5,Flow.LengthMeters());}}
        Heads.Add({NextId++,Distance,0,false});++Count;
    }
}
void FCLPlanetBuilderFlowAnimation::Tick(double Seconds)
{
    if(!FMath::IsFinite(Seconds)||Seconds<0||Flow.LengthMeters()<=0)return;
    for(auto& H:Heads){H.DistanceMeters=WrapArc(H.DistanceMeters+Flow.SpeedMetersPerSecond*Seconds,Flow.LengthMeters());H.Opacity=FMath::Clamp(H.Opacity+float(Seconds/.6)*(H.Retiring?-1:1),0.f,1.f);}
    Heads.RemoveAll([](const auto& H){return H.Retiring&&H.Opacity<=0;});
}

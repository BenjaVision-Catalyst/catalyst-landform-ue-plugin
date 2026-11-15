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

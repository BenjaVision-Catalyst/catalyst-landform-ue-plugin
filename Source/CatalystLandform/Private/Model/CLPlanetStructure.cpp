#include "Model/CLPlanetStructure.h"
FCLPlanetFeature::FCLPlanetFeature()
{
    Relief.Mode=ECLHeightReferenceMode::IntervalDelta;Relief.Guide=TEXT("SurfaceMin");Relief.UpperGuide=TEXT("SurfaceMax");Relief.Fraction=.25;
}
FQuat FCLCompiledPlanetFeature::Frame(const FVector2D& Location,double Heading)
{
    const double L=FMath::DegreesToRadians(Location.X),B=FMath::DegreesToRadians(Location.Y),H=FMath::DegreesToRadians(Heading);
    const FVector N(FMath::Sin(L)*FMath::Cos(B),FMath::Sin(B),FMath::Cos(L)*FMath::Cos(B));
    const FVector East(FMath::Cos(L),0,-FMath::Sin(L)),North=FVector::CrossProduct(N,East);
    return FRotationMatrix::MakeFromXZ(East*FMath::Cos(H)+North*FMath::Sin(H),N).ToQuat();
}
double FCLCompiledPlanetFeature::Weight(const FVector& Direction) const
{
    const FVector P=Orientation.UnrotateVector(Direction.GetSafeNormal());
    if(P.Z<=0)return 0;
    const double R=FMath::Sqrt(P.X*P.X+P.Y*P.Y),A=FMath::Atan2(R,P.Z),K=R>1.e-12?A/R:1;
    const double Q=FMath::Square(P.X*K/HalfExtentRadians.X)+FMath::Square(P.Y*K/HalfExtentRadians.Y);
    if(Q>=1)return 0;const double T=1-Q;return T*T*T;
}

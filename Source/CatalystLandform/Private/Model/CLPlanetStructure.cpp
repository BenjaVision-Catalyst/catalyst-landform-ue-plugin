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

double FCLCompiledLandformPlacement::Weight(const FVector& Direction) const
{
    const FVector P=Orientation.UnrotateVector(Direction.GetSafeNormal());if(P.Z<=0)return 0;
    const double R=FMath::Sqrt(P.X*P.X+P.Y*P.Y),A=FMath::Atan2(R,P.Z),K=R>1.e-12?A/R:1;
    const double Q=FMath::Sqrt(FMath::Square(P.X*K/HalfExtentRadians.X)+FMath::Square(P.Y*K/HalfExtentRadians.Y));
    const double T=FMath::Clamp((1-Q)/TransitionFraction,0.,1.);return T*T*T*(T*(T*6-15)+10);
}
bool FCLCompiledLandformPlacement::Resolve(const FCLLandformPlacement& P,const TArray<FCLPlanetFeature>& Structure,FCLCompiledLandformPlacement& Out,FString& Error)
{
    if(P.LocationDegrees.ContainsNaN() || FMath::Abs(P.LocationDegrees.X)>180 || FMath::Abs(P.LocationDegrees.Y)>90 ||
       !FMath::IsFinite(P.HeadingDegrees) || !FMath::IsFinite(P.CoverageScale) || P.CoverageScale<.01 || P.CoverageScale>4 ||
       !FMath::IsFinite(P.TransitionFraction) || P.TransitionFraction<.01 || P.TransitionFraction>1 ||
       !FMath::IsFinite(P.Strength) || P.Strength<0 || P.Strength>1)
    {Error=TEXT("Invalid Landform placement, coverage, transition or strength");return false;}
    Out.Orientation=FCLCompiledPlanetFeature::Frame(P.LocationDegrees,P.HeadingDegrees);FVector2D Extent=P.HalfExtentDegrees;
    if(P.StructureFeatureId.IsValid())
    {
        const auto* Parent=Structure.FindByPredicate([&](const auto& F){return F.Id==P.StructureFeatureId;});
        if(!Parent){Error=TEXT("Landform references a missing structure feature");return false;}
        Out.Orientation=FCLCompiledPlanetFeature::Frame(Parent->LocationDegrees,Parent->HeadingDegrees)*Out.Orientation;
        if(P.bFollowStructureExtent)Extent=Parent->HalfExtentDegrees;
    }
    Extent*=P.CoverageScale;
    if(Extent.ContainsNaN() || Extent.GetMin()<.1 || Extent.GetMax()>=85){Error=TEXT("Landform angular half extents must be between 0.1 and 85 degrees");return false;}
    Out.HalfExtentRadians=Extent*(UE_DOUBLE_PI/180);Out.TransitionFraction=P.TransitionFraction;Out.Strength=P.Strength;return true;
}

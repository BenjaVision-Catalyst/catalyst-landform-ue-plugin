#include "Model/CLRockLayer.h"
#include "Model/CLSurfaceDistribution.h"

bool FCLRockBedDistribution::Validate(FString& Error) const
{
    auto In=[](double V,double A,double B){return FMath::IsFinite(V)&&V>=A&&V<=B;};
    if(!In(MinimumMeters,.1,1000)||!In(MaximumMeters,.1,1000)||!In(SpreadMeters,.1,1000)||
       !In(ThinTailUpperMeters,MinimumMeters+.01,MaximumMeters-.01)||!In(ThickTailLowerMeters,ThinTailUpperMeters+.02,MaximumMeters-.01)||
       !In(PeakMeters,ThinTailUpperMeters+.01,ThickTailLowerMeters-.01)||!In(ThinBedPercent,.1,20)||!In(ThickBedPercent,.1,20))
    {Error=TEXT("Bed thickness needs finite ordered min < thin tail < peak < thick tail < max (0.1–1000 m), positive spread and 0.1–20% tail counts");return false;}
    return true;
}
FRichCurve FCLRockBedDistribution::QuantileCurve() const
{
    // A truncated Gaussian core plus separately authored finite tails. Integrate
    // the core CDF once, then let native UE's clamped cubic curve invert it.
    TArray<FVector2D> Q;
    const double Low=ThinBedPercent/100,High=ThickBedPercent/100;
    for(int32 I=0;I<=32;++I){const double U=I/32.;Q.Add({Low*U,FMath::Lerp(MinimumMeters,ThinTailUpperMeters,FMath::Sqrt(U))});}
    constexpr int32 N=256;double CDF[N+1]={};
    const double DX=(ThickTailLowerMeters-ThinTailUpperMeters)/N;
    // Clamp the exponent to retain strictly increasing native float curve times
    // even for a narrow authored peak far from a tail edge.
    auto PDF=[&](double X){return FMath::Max(1.e-4,FMath::Exp(-.5*FMath::Square((X-PeakMeters)/SpreadMeters)));};
    for(int32 I=1;I<=N;++I)CDF[I]=CDF[I-1]+.5*DX*(PDF(ThinTailUpperMeters+(I-1)*DX)+PDF(ThinTailUpperMeters+I*DX));
    for(int32 I=1;I<=N;++I)Q.Add({Low+(1-Low-High)*CDF[I]/CDF[N],ThinTailUpperMeters+I*DX});
    for(int32 I=1;I<=32;++I){const double U=I/32.;Q.Add({1-High+High*U,FMath::Lerp(ThickTailLowerMeters,MaximumMeters,1-FMath::Sqrt(1-U))});}
    return FCLSurfaceDistributionSettings::InterpolateMonotone(Q);
}
FCLRockBed FCLCompiledRock::GetBed(int32 I) const
{
    check(MetricUnits.IsValidIndex(I));
    return {MetricUnits[I],I==0?Basement:I==OlderBedCount?0:MetricUppers[I-1],MetricUppers[I],I>=OlderBedCount};
}
FCLRockBedStatistics FCLCompiledRock::ThicknessStatistics() const
{
    FCLRockBedStatistics S;
    S.MinimumMeters=Layer.BedDistribution.MinimumMeters;S.MaximumMeters=Layer.BedDistribution.MaximumMeters;
    for(int32 I=0;I<OlderBedCount-1;++I)
    {
        const auto B=GetBed(I);const double T=B.UpperMeters-B.LowerMeters;
        const int32 Bin=FMath::Clamp(int32((T-S.MinimumMeters)/(S.MaximumMeters-S.MinimumMeters)*S.BinCount),0,S.BinCount-1);
        ++S.Count;++S.Counts[Bin];S.TotalMeters+=T;S.DepthMeters[Bin]+=T;
    }
    return S;
}

#include "Model/CLContinents.h"
#include "Model/CLTectonics.h"

FCLContinentalControls FCLContinentalControls::Resolve(const FCLContinentalLayer& L)
{
    FCLContinentalControls C;
    C.Extent=L.bOverrideExtent?L.Extent:.7+.5*L.Strength;
    C.Relief=L.bOverrideRelief?L.Relief:.05+.17*L.Strength;
    return C;
}
TSharedPtr<const FCLCompiledContinents> FCLCompiledContinents::Compile(const FCLContinentalLayer& L,int32 Seed,double Span,FString& Error,bool Calibrated,double DatumOffset)
{
    if(!FMath::IsFinite(L.Strength)||L.Strength<0||L.Strength>1||L.Orientation.ContainsNaN()||!FMath::IsFinite(L.Extent)||L.Extent<.6||L.Extent>1.4||!FMath::IsFinite(L.Relief)||L.Relief<0||L.Relief>1||!FMath::IsFinite(Span)||Span<=0)
    {Error=TEXT("Continental layer requires strength 0..1, extent 0.6..1.4 and finite orientation/relief");return nullptr;}
    if(!Calibrated)
    {
        FCLTectonicPreset P;P.AlgorithmVersion=3;P.Activity=0;P.MotionDirection=0;
        P.FoundationOrientation=L.Orientation;P.ContinentalExtent=L.bOverrideExtent?L.Extent:.6+.8*L.Strength;
        P.Setting=L.bCoastalMargin?ECLContinentalSetting::Subduction:ECLContinentalSetting::Collision;
        auto Out=MakeShared<FCLCompiledContinents>();Out->LegacyCrust=FCLCompiledTectonics::Compile(P,int32(uint32(Seed)^uint32(L.Seed)),Span,Error);
        if(!Out->LegacyCrust)return nullptr;
        Out->SpanMeters=Span;Out->Relief=L.bOverrideRelief?L.Relief:.2+.8*L.Strength;Out->MaximumDeltaMeters=Span*.42*Out->Relief;return Out;
    }
    const auto Controls=FCLContinentalControls::Resolve(L);
    auto Out=MakeShared<FCLCompiledContinents>();
    FRandomStream R(int32(uint32(Seed)^uint32(L.Seed)^0x434c4332U));
    // A seed changes the broad geography, not just the coastline texture.
    Out->Frame=L.Orientation.Quaternion()*FRotator(R.FRandRange(-65,65),R.FRandRange(-180,180),R.FRandRange(-180,180)).Quaternion();
    Out->NoiseOffset=FVector(R.FRandRange(5,55),R.FRandRange(5,55),R.FRandRange(5,55));
    for(const FVector D:{FVector(.84,.42,.12),FVector(.84,-.42,-.12),FVector(-.72,.55,.34),FVector(-.1,-.7,-.8)})
        Out->Centres.Add((D+FVector(R.FRandRange(-.22,.22),R.FRandRange(-.22,.22),R.FRandRange(-.22,.22))).GetSafeNormal());
    Out->Extent=Controls.Extent*(L.bCoastalMargin?.85:1.);
    Out->DatumOffsetMeters=DatumOffset;Out->SpanMeters=Span;Out->Relief=Controls.Relief;Out->MaximumDeltaMeters=FMath::Abs(DatumOffset)+Span*.42*Out->Relief;return Out;
}
double FCLCompiledContinents::Continentalness(const FVector& D) const
{
    if(LegacyCrust)return LegacyCrust->Continentalness(D);
    const FVector P=Frame.UnrotateVector(D.GetSafeNormal());
    const FVector Q=P*3.1+NoiseOffset;
    const FVector Warp(FMath::PerlinNoise3D(Q),FMath::PerlinNoise3D(Q+FVector(17,3,11)),FMath::PerlinNoise3D(Q+FVector(3,23,7)));
    const FVector W=(P+Warp*.22).GetSafeNormal();
    double Sum=0;
    for(const auto& C:Centres)Sum+=1.7*FMath::Exp((FVector::DotProduct(W,C)-1)*7./FMath::Square(Extent));
    return 1-FMath::Exp(-Sum);
}
double FCLCompiledContinents::SampleDeltaMeters(const FVector& D) const
{
    if(LegacyCrust)return SpanMeters*FCLCompiledTectonics::CrustBaseFraction(Continentalness(D))*Relief;
    const double C=Continentalness(D);
    // A continuous basin-to-upland profile; no saturated shelf/interior terraces.
    // Low-amplitude long wavelengths keep interiors readable without a flat cap.
    const FVector P=Frame.UnrotateVector(D.GetSafeNormal());
    const double Detail=FMath::PerlinNoise3D(P*7.1+NoiseOffset)*.035+FMath::PerlinNoise3D(P*17.3+NoiseOffset)*.012;
    return DatumOffsetMeters+SpanMeters*Relief*(-.26+.52*C+Detail);
}

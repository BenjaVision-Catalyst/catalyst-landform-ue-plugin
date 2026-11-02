#include "Model/CLContinents.h"
#include "Model/CLTectonics.h"

TSharedPtr<const FCLCompiledContinents> FCLCompiledContinents::Compile(const FCLContinentalLayer& L,int32 Seed,double Span,FString& Error)
{
    if(!FMath::IsFinite(L.Strength)||L.Strength<0||L.Strength>1||L.Orientation.ContainsNaN()||!FMath::IsFinite(L.Extent)||L.Extent<.6||L.Extent>1.4||!FMath::IsFinite(L.Relief)||L.Relief<0||L.Relief>1)
    {Error=TEXT("Continental layer requires strength 0..1, extent 0.6..1.4 and finite orientation/relief");return nullptr;}
    // Reuse the qualified crust-domain kernel with zero motion. This owner
    // publishes only base crust; no boundary deformation is included.
    FCLTectonicPreset P;P.AlgorithmVersion=3;P.Activity=0;P.MotionDirection=0;
    P.FoundationOrientation=L.Orientation;P.ContinentalExtent=L.bOverrideExtent?L.Extent:.6+.8*L.Strength;
    P.Setting=L.bCoastalMargin?ECLContinentalSetting::Subduction:ECLContinentalSetting::Collision;
    auto Out=MakeShared<FCLCompiledContinents>();Out->CrustField=FCLCompiledTectonics::Compile(P,int32(uint32(Seed)^uint32(L.Seed)),Span,Error);
    if(!Out->CrustField)return nullptr;
    Out->SpanMeters=Span;Out->Relief=L.bOverrideRelief?L.Relief:.2+.8*L.Strength;Out->MaximumDeltaMeters=Span*.42*Out->Relief;return Out;
}
double FCLCompiledContinents::Continentalness(const FVector& D) const
{return CrustField->Continentalness(D);}
double FCLCompiledContinents::SampleDeltaMeters(const FVector& D) const
{return SpanMeters*FCLCompiledTectonics::CrustBaseFraction(Continentalness(D))*Relief;}

#include "Model/CLRiftLandform.h"
#include "Model/CLTectonics.h"
#include "Model/CLContinents.h"
#include "Model/CLPlanetRecipe.h"
#include "CLNativeNoiseKernel.h"
namespace
{
uint32 Mix(uint32 V){V^=V>>16;V*=0x7feb352dU;V^=V>>15;V*=0x846ca68bU;return V^(V>>16);}
double Random(uint32 V){return (double(Mix(V))+.5)/4294967296.;}
double Smooth(double X){X=FMath::Clamp(X,0.,1.);return X*X*X*(X*(X*6-15)+10);}
}
void FCLRiftLandform::ApplyCuratedCharacter(double Value)
{
    AlgorithmVersion=3;MinimumSpacingMeters=180;MaximumSpacingMeters=440;MinimumThrowMeters=12;MaximumThrowMeters=42;MinimumScarpWidthMeters=8;MaximumScarpWidthMeters=26;
    Character=FMath::Clamp(Value,0.,1.);
}
bool FCLRiftLandform::Validate(FString& Error)const
{
    if(AlgorithmVersion<0||AlgorithmVersion>3){Error=TEXT("Unsupported rift Landform version");return false;}
    if(!IsActive())return true;
    auto Range=[](double A,double B,double Min,double Max){return FMath::IsFinite(A)&&FMath::IsFinite(B)&&A>=Min&&B>=A&&B<=Max;};
    if(!FMath::IsFinite(Character)||Character<0||Character>1||!Range(MinimumSpacingMeters,MaximumSpacingMeters,2,100000)||!Range(MinimumThrowMeters,MaximumThrowMeters,0,10000)||!Range(MinimumScarpWidthMeters,MaximumScarpWidthMeters,.1,10000)||MaximumScarpWidthMeters>MinimumSpacingMeters*.45)
    {Error=TEXT("Rift Character requires finite ordered physical ranges; scarp width must be less than 45% of minimum fault spacing");return false;}
    return true;
}
bool FCLRiftLandform::ValidateNoise(TConstArrayView<FCLNoiseLayer> Layers,FString& Error)const
{
    if(!IsActive())return true;
    if(AlgorithmVersion==1&&!Layers.IsEmpty()){Error=TEXT("Frozen Rift profile 1 has no surface noise layers");return false;}
    for(const auto& L:Layers)if(L.Operation!=ECLTerrainOperation::Noise||L.Blend!=ECLNoiseBlend::Add||L.Scale!=ECLNoiseScale::Meters||!L.Minimum.Guide.IsNone()||!L.Maximum.Guide.IsNone()||!L.Minimum.UpperGuide.IsNone()||!L.Maximum.UpperGuide.IsNone())
    {Error=TEXT("Rift surface noise uses additive metre-scale relief; planet guides and base geology retain their own owners");return false;}
    return true;
}
TSharedPtr<const FCLCompiledRiftLandform> FCLCompiledRiftLandform::Compile(const FCLRiftLandform& S,int32 PlanetSeed,double R,double Span,TSharedPtr<const FCLCompiledTectonics> T,TSharedPtr<const FCLCompiledContinents> C,FString& Error)
{
    if(!S.Validate(Error)||!S.IsActive()||!FMath::IsFinite(R)||R<=0||!FMath::IsFinite(Span)||Span<0){if(Error.IsEmpty())Error=TEXT("Invalid rift reference radius or surface interval");return nullptr;}
    auto Out=MakeShared<FCLCompiledRiftLandform>();Out->Radius=R;Out->Tectonics=MoveTemp(T);Out->Crust=MoveTemp(C);Out->Character=S.AlgorithmVersion==3?FMath::Lerp(.15,.72,S.Character):S.Character;Out->Seed=Mix(uint32(PlanetSeed))^Mix(uint32(S.Seed));
    const double Curation=Smooth(S.Character);
    Out->ProfileVersion=S.AlgorithmVersion;
    if(Out->ProfileVersion>=2)
    {
        Out->Noise=FCLNativeNoiseKernel::Create(Error);if(!Out->Noise)return nullptr;
        FRandomStream Random(int32(Out->Seed));const FVector Axis=Random.VRand();Axis.FindBestAxisVectors(Out->FallbackX,Out->FallbackY);
    }
    Out->Spacing=FMath::Lerp(S.MaximumSpacingMeters,S.MinimumSpacingMeters,Curation);
    Out->Throw=FMath::Min3(FMath::Lerp(S.MinimumThrowMeters,S.MaximumThrowMeters,Curation),R*.04,Span*.4);
    Out->Width=FMath::Lerp(S.MaximumScarpWidthMeters,S.MinimumScarpWidthMeters,Curation);return Out;
}
double FCLCompiledRiftLandform::SampleMeters(const FVector& Direction)const
{
    if(ProfileVersion==1)return StructuralMeters(Direction);
    if(Direction.ContainsNaN()||Direction.IsNearlyZero()||Throw==0)return 0;
    double Confidence=0;const double Structural=StructuralMeters(Direction,&Confidence);
    const FVector P=Direction.GetSafeNormal()*Radius;
    const double Warp=Noise->Sample(P/(Spacing*1.4),int32(Seed));
    const double Phase=2*PI*(FVector::DotProduct(P,FallbackX)/Spacing+.18*Warp);
    const double Soft=FMath::Exp(-2*PI*FMath::Min(Width,Spacing*.2)/Spacing);
    const double Tilt=FMath::Atan2(Soft*FMath::Sin(Phase),1-Soft*FMath::Cos(Phase))/PI;
    const double Segment=.7+.3*Noise->Sample(P/(Spacing*3.2)+FallbackY*7,int32(Seed^0x52494654));
    // A relaxed region has its own seeded local expression. It neither moves
    // plates nor fabricates their evidence. Resolved belts dominate where present.
    return Structural+(1-Confidence)*Throw*.55*Tilt*Segment;
}
double FCLCompiledRiftLandform::StructuralMeters(const FVector& Direction,double* Confidence)const
{
    if(Confidence)*Confidence=0;
    if(!Tectonics||Throw==0||Direction.ContainsNaN()||Direction.IsNearlyZero())return 0;
    const FVector D=Direction.GetSafeNormal();
    const double Continental=Crust?Crust->Continentalness(D):Tectonics->Continentalness(D);
    const double Host=Smooth((Continental-.6)/.12);
    if(Host==0)return 0;
    double Sum=0,Drive=0,Extension=0;
    Tectonics->VisitExtensionBelts(D,[&](const FCLPlateBeltSample& B)
    {
        Extension+=B.ExtensionDrive*B.Envelope*B.StateWeight;
        const double Gate=Smooth((B.ExtensionWeight-.02)/.10);if(Gate==0)return;
        const uint32 Key=Mix(GetTypeHash(B.BoundaryId))^Seed;
        const double Along=B.AlongRadians*Radius,Across=B.AcrossRadians*Radius;
        // Metre spacing is independent of body size; larger areas contain more
        // structure. Small bodies can expose only part of a broad fault block.
        const double Sp=Spacing;
        const double Scar=FMath::Min(Width,Sp*.2);
        const double Segment=Sp*FMath::Lerp(8.,3.,Character),U=Along/Segment+Random(Key+1)*17;
        const int64 Index=FMath::FloorToInt64(U);const double Blend=Smooth(U-Index);
        auto Profile=[&](int64 I)
        {
            const uint32 K=Mix(Key^uint32(I));
            const double Polarity=Random(K+2)<.25?-1.:1.;
            const double Warp=.12*FMath::Sin(Along/(Sp*2.8)+Random(Key+3)*2*PI)+.04*FMath::Sin(Along/(Sp*.7)+Random(Key+4)*2*PI);
            const double Phase=2*PI*(Across/Sp+Warp+Random(Key+5));
            const double Soft=FMath::Exp(-2*PI*Scar/Sp);
            // Analytic smoothed saw: one steep normal scarp and a long tilted
            // back slope, blended with paired-basin form. No box cuts or mesh work.
            const double Tilt=FMath::Atan2(Soft*FMath::Sin(Phase),1-Soft*FMath::Cos(Phase))/PI;
            const double Asymmetry=FMath::Lerp(.35,.9,Character);
            return (Asymmetry*Polarity*Tilt+(1-Asymmetry)*.45*FMath::Cos(Phase))*(.6+.4*Random(K+6));
        };
        const double W=Gate*B.StateWeight;
        Sum+=W*FMath::Lerp(Profile(Index),Profile(Index+1),Blend);Drive+=W;
    });
    const double Context=Host*Smooth((FMath::Tanh(Extension)-.02)/.10);
    if(Confidence)*Confidence=Context;
    return Context*Throw*Sum/FMath::Max(1.,Drive);
}

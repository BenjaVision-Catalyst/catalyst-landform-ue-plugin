#include "Model/CLContinents.h"
#include "CLNativeNoiseKernel.h"

FCLContinentalControls FCLContinentalControls::Resolve(const FCLContinentalLayer& L)
{
    FCLContinentalControls C;
    C.Extent=L.Extent;C.Relief=L.Relief;return C;
}
TSharedPtr<const FCLCompiledContinents> FCLCompiledContinents::Compile(const FCLContinentalLayer& L,int32 Seed,double Span,FString& Error,bool Calibrated,double DatumOffset)
{
    if(!FMath::IsFinite(L.Strength)||L.Strength<0||L.Strength>1||L.Orientation.ContainsNaN()||!FMath::IsFinite(L.Extent)||L.Extent<.6||L.Extent>1.4||!FMath::IsFinite(L.Relief)||L.Relief<0||L.Relief>1||!FMath::IsFinite(Span)||Span<=0)
    {Error=TEXT("Continental layer requires strength 0..1, extent 0.6..1.4 and finite orientation/relief");return nullptr;}
    if(L.AlgorithmVersion<1||L.AlgorithmVersion>3||L.DomainCount<1||L.DomainCount>8||!FMath::IsFinite(L.CoastWarp)||L.CoastWarp<0||L.CoastWarp>1||!FMath::IsFinite(L.InteriorRoughness)||L.InteriorRoughness<0||L.InteriorRoughness>1||!FMath::IsFinite(L.CoastSharpness)||L.CoastSharpness<.5||L.CoastSharpness>3)
    {Error=TEXT("Invalid guided continental shape settings");return nullptr;}
    if((!FMath::IsFinite(L.DomainTransition)||L.DomainTransition<0||L.DomainTransition>1||(L.DomainCount==8&&L.DomainTransition!=0)||!FMath::IsFinite(L.Elongation)||L.Elongation<1||L.Elongation>3||!FMath::IsFinite(L.Fragmentation)||L.Fragmentation<0||L.Fragmentation>1))
    {Error=TEXT("Continental character requires a bounded domain transition, elongation and fragmentation");return nullptr;}
    const auto Controls=FCLContinentalControls::Resolve(L);
    auto Out=MakeShared<FCLCompiledContinents>();

    Out->SpanMeters=Span;Out->Relief=Controls.Relief;Out->Extent=Controls.Extent;
    Out->CoastWarp=L.CoastWarp;Out->InteriorRoughness=L.InteriorRoughness;Out->CoastSharpness=L.CoastSharpness;
    // A zero layer has neither a datum offset nor a latent crust classification.
    if(!Out->HasEffect()){Out->Relief=0;return Out;}
    Out->NativePerlin=FCLNativeNoiseKernel::Create(Error);if(!Out->NativePerlin)return nullptr;
    FRandomStream R(int32(uint32(Seed)^uint32(L.Seed)^0x434c4333U));Out->NoiseSeed=R.GetUnsignedInt();
    const FQuat Rotation=FRotator(R.FRandRange(-65,65),R.FRandRange(-180,180),R.FRandRange(-180,180)).Quaternion();
    Out->Frame=L.Orientation.Quaternion()*Rotation;
    TArray<FVector> Pool,Centres;
    for(int32 I=0;I<128;++I)
    {
        const double Z=1-2*(I+.5)/128.,A=I*2.399963229728653;
        Pool.Emplace(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z);
    }
    // Build the same complete seeded pool at every slider position. Existing
    // centres/axes never move when the next domain enters the composition.
    const int32 First=R.RandRange(0,Pool.Num()-1);
    for(int32 I=0;I<8;++I)
    {
        int32 Best=First;double Score=-1;
        if(I)for(int32 K=0;K<Pool.Num();++K)
        {double Distance=4;for(const auto& C:Centres)Distance=FMath::Min(Distance,(Pool[K]-C).SizeSquared());if(Distance>Score){Score=Distance;Best=K;}}
        const FVector C=Pool[Best];Centres.Add(C);FVector X,Y;C.FindBestAxisVectors(X,Y);
        const double Angle=R.FRandRange(-PI,PI),Bend=R.FRandRange(-.25,.25),Width=R.FRandRange(.82,1.12);
        const FVector Along=X*FMath::Cos(Angle)+Y*FMath::Sin(Angle),Across=FVector::CrossProduct(C,Along).GetSafeNormal();
        FDomain Domain;Domain.Weight=I<L.DomainCount?1:I==L.DomainCount?L.DomainTransition:0;
        for(int32 K=-1;K<=1;++K)
        {
            const double Spacing=(.22+.55*L.Fragmentation)*(K<0?.9:1.1);
            FLobe Lobe;Lobe.Centre=(C+Along*(K*Spacing)+Across*(K*K*Bend)).GetSafeNormal();
            Lobe.Across=FVector::VectorPlaneProject(Across,Lobe.Centre).GetSafeNormal();
            Lobe.Aspect=L.Elongation/Width;Lobe.Weight=K==0?1.:.72;Domain.Lobes.Add(Lobe);
        }
        Out->Domains.Add(MoveTemp(Domain));
    }
    Out->DatumOffsetMeters=DatumOffset;Out->MaximumDeltaMeters=FMath::Abs(DatumOffset)+Span*(.42+1.35*L.InteriorRoughness)*Out->Relief;
    return Out;

}

double FCLCompiledContinents::Continentalness(const FVector& D) const
{return HasEffect()?SampleContinentalness(Frame.UnrotateVector(D.GetSafeNormal())):0;}

double FCLCompiledContinents::CrustContext(const FVector& D,double Neutral)const
{
    if(!HasEffect())return Neutral;
    const double T=FMath::Clamp(Relief/.22,0.,1.),Blend=T*T*(3-2*T);
    return FMath::Lerp(Neutral,Continentalness(D),Blend);
}
double FCLCompiledContinents::SampleContinentalness(const FVector& P)const
{
    const FVector Q=P*2.7;
    const FVector Warp(NativePerlin->Sample(Q,NoiseSeed),NativePerlin->Sample(Q,int32(uint32(NoiseSeed)+1U)),NativePerlin->Sample(Q,int32(uint32(NoiseSeed)+2U)));
    const FVector W=(P+Warp*CoastWarp).GetSafeNormal();double Sum=0;
    for(const auto& Domain:Domains)
    {
        if(Domain.Weight<=0)continue;double Mass=0;
        for(const auto& Lobe:Domain.Lobes)
        {
            const double Chord=2*(1-FVector::DotProduct(W,Lobe.Centre));
            const double Across=FVector::DotProduct(W,Lobe.Across);
            const double Metric=Chord+Across*Across*(Lobe.Aspect*Lobe.Aspect-1);
            Mass+=Lobe.Weight*FMath::Exp(-3.5*Metric/FMath::Square(Extent));
        }
        Sum+=Domain.Weight*Mass;
    }
    return FMath::Pow(1-FMath::Exp(-Sum),CoastSharpness);
}
double FCLCompiledContinents::SampleDeltaMeters(const FVector& D) const
{
    if(!HasEffect())return 0;
    const FVector P=Frame.UnrotateVector(D.GetSafeNormal());const double C=SampleContinentalness(P);
    // Broad basin/rise, shelf and continental interior remain sloping instead
    // of saturating into caps. Fine relief stays subordinate to crust structure.
    const double T=FMath::Clamp((C-.12)/.40,0.,1.);
    const double Margin=T*T*T*(T*(6*T-15)+10);
    const double Interior=C*C;
    const double Detail=NativePerlin->Sample(P*2.3,int32(uint32(NoiseSeed)+3U))+.25*NativePerlin->Sample(P*8.7,int32(uint32(NoiseSeed)+4U));
    return Relief*(DatumOffsetMeters+SpanMeters*(-.27+.36*C+.15*Interior+.035*Margin+InteriorRoughness*Detail*(.25+.75*Interior)));

}

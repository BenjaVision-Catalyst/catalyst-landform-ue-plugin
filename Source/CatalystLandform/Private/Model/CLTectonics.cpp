#include "Model/CLTectonics.h"
namespace
{
double Smooth(double T){T=FMath::Clamp(T,0.,1.);return T*T*T*(T*(6*T-15)+10);}
FVector Unit(FRandomStream& R){const double Z=R.FRandRange(-1.f,1.f),A=R.FRandRange(0.f,2*PI);return FVector(FMath::Sqrt(1-Z*Z)*FMath::Cos(A),FMath::Sqrt(1-Z*Z)*FMath::Sin(A),Z);}
}
bool FCLTectonicControls::Resolve(const FCLTectonicPreset& P,FCLTectonicControls& O,FString& Error)
{
    if((P.AlgorithmVersion!=1&&P.AlgorithmVersion!=2)||!FMath::IsFinite(P.Activity)||P.Activity<0||P.Activity>1||P.LowerGuide.IsNone()||P.UpperGuide.IsNone()||
        P.PlateCount<1||P.PlateCount>12||!FMath::IsFinite(P.Motion)||P.Motion<0||P.Motion>2||!FMath::IsFinite(P.Relief)||P.Relief<0||P.Relief>1)
    {Error=TEXT("Tectonic preset requires algorithm 1 or 2, activity 0..1 and valid advanced controls");return false;}
    const double A=P.Activity,Count=1+11*A;
    O.LowerPlateCount=P.bOverridePlateCount?P.PlateCount:FMath::Min(12,FMath::FloorToInt(Count));
    O.UpperPlateCount=P.bOverridePlateCount?P.PlateCount:FMath::Min(12,O.LowerPlateCount+1);
    O.PlateTransition=Smooth(Count-FMath::FloorToDouble(Count));
    O.Motion=P.bOverrideMotion?P.Motion:1.6*A*A;
    O.CrustThickness=.008+.032*Smooth(A);
    O.BeltWidth=FMath::DegreesToRadians(3.+4.*A);
    O.Relief=P.bOverrideRelief?P.Relief:.12+.8*A;
    return true;
}
double FCLCompiledTectonics::Continentalness(const FVector& D) const
{
    double Sum=0;for(const FVector& C:Continents)Sum+=2.7*FMath::Exp((FVector::DotProduct(D,C)-1)*5.);
    return 1-FMath::Exp(-Sum);
}
TSharedPtr<const FCLCompiledTectonics> FCLCompiledTectonics::Compile(const FCLTectonicPreset& P,int32 Seed,double Span,FString& Error)
{
    auto Out=MakeShared<FCLCompiledTectonics>();if(!FCLTectonicControls::Resolve(P,Out->Controls,Error))return nullptr;
    if(!FMath::IsFinite(Span)||Span<=0){Error=TEXT("Tectonics requires an ordered, nonempty guide interval");return nullptr;}
    Out->AlgorithmVersion=P.AlgorithmVersion;Out->SpanMeters=Span;Out->MaximumDeltaMeters=Span*(.035+.46*Out->Controls.Relief);
    FRandomStream R(int32(uint32(Seed)^0x43544c31U));const FQuat Rotation(Unit(R),R.FRandRange(0.f,2*PI));
    // Independent crust domains: no plate is labelled wholly continental/oceanic.
    Out->Continents={Rotation.RotateVector(FVector(.2,.8,.55).GetSafeNormal()),Rotation.RotateVector(FVector(-.75,-.3,.6).GetSafeNormal()),Rotation.RotateVector(FVector(.65,-.7,-.3).GetSafeNormal())};
    TArray<FVector> Pool;for(int32 I=0;I<256;++I){const double Z=1-2*(I+.5)/256.,A=I*2.399963229728653;Pool.Add(Rotation.RotateVector(FVector(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z)));}
    TArray<FPlate> Candidates;Candidates.Reserve(12);
    for(int32 I=0;I<12;++I)
    {
        int32 Best=0;double Score=-1;
        for(int32 K=0;K<Pool.Num();++K){double Nearest=4.;for(const auto& V:Candidates)Nearest=FMath::Min(Nearest,(Pool[K]-V.Site).SizeSquared());if(Nearest>Score){Score=Nearest;Best=K;}}
        FPlate V;V.Id=FGuid(0x434c5445,uint32(Seed),1,I+1);V.Site=Pool[Best];V.AngularVelocity=Unit(R);Candidates.Add(V);
    }
    Out->NoiseRotation=Rotation;Out->NoiseOffset=Unit(R)*37.;
    auto Build=[&](int32 Count,FState& State)
    {
        State.Plates.Append(Candidates.GetData(),Count);
        auto Owns=[&](FVector V,int32 A){const double Best=FVector::DotProduct(V,Candidates[A].Site);for(int32 K=0;K<Count;++K)if(FVector::DotProduct(V,Candidates[K].Site)>Best+1.e-8)return false;return true;};
        for(int32 A=0;A<Count;++A)for(int32 B=A+1;B<Count;++B)
        {
            const FVector N=(Candidates[A].Site-Candidates[B].Site).GetSafeNormal();TArray<FVector> Vertices;
            for(int32 C=0;C<Count;++C)if(C!=A&&C!=B)
            {
                const FVector V=FVector::CrossProduct(N,Candidates[A].Site-Candidates[C].Site).GetSafeNormal();
                for(const FVector Q:{V,-V})if(!Q.IsNearlyZero()&&Owns(Q,A)&&!Vertices.ContainsByPredicate([&](FVector X){return X.Equals(Q,1.e-6);}))Vertices.Add(Q);
            }
            auto Add=[&](FVector S,FVector E,FVector M,int32 Part=0)
            {
                FBoundary Edge;Edge.Id=FGuid(0x434c5442,uint32(Seed),A+1,(B+1)*8+Part);Edge.A=A;Edge.B=B;Edge.Start=S;Edge.End=E;Edge.Mid=M;Edge.Normal=N;
                // Algorithm 2's widest support is 3 * 1.7 * 1.6 = 8.16 belt widths.
                Edge.HalfCos=FVector::DotProduct(S,M);const double Half=FMath::Acos(FMath::Clamp(Edge.HalfCos,-1.,1.));Edge.CapCos=FMath::Cos(FMath::Min(PI,Half+Out->Controls.BeltWidth*(P.AlgorithmVersion==1?4:9)));
                const FVector Relative=FVector::CrossProduct(Candidates[A].AngularVelocity-Candidates[B].AngularVelocity,M);
                Edge.Convergence=-FVector::DotProduct(Relative,N);Edge.Shear=FMath::Abs(FVector::DotProduct(Relative,FVector::CrossProduct(N,M)));
                Edge.ContinentalA=Out->Continentalness((M+N*.12).GetSafeNormal());Edge.ContinentalB=Out->Continentalness((M-N*.12).GetSafeNormal());State.Boundaries.Add(Edge);
            };
            if(Count==2)
            {
                const FVector X=FVector::CrossProduct(N,FMath::Abs(N.Z)<.9?FVector::ZAxisVector:FVector::XAxisVector).GetSafeNormal(),Y=FVector::CrossProduct(N,X);
                for(int32 K=0;K<4;++K){const double T=K*PI/2;Add(X*FMath::Cos(T)+Y*FMath::Sin(T),X*FMath::Cos(T+PI/2)+Y*FMath::Sin(T+PI/2),X*FMath::Cos(T+PI/4)+Y*FMath::Sin(T+PI/4),K+1);}
            }
            else if(Vertices.Num()==2)
            {
                FVector M=(Vertices[0]+Vertices[1]).GetSafeNormal();if(M.IsNearlyZero())M=FVector::VectorPlaneProject(Candidates[A].Site,N).GetSafeNormal();if(!Owns(M,A))M=-M;Add(Vertices[0],Vertices[1],M);
            }
        }
    };
    Build(Out->Controls.LowerPlateCount,Out->Lower);if(Out->Controls.UpperPlateCount!=Out->Controls.LowerPlateCount)Build(Out->Controls.UpperPlateCount,Out->Upper);
    return Out;
}
double FCLCompiledTectonics::SampleLegacyState(const FState& S,const FVector& D) const
{
    double H=0;const double W=Controls.BeltWidth;
    for(const auto& E:S.Boundaries)
    {
        if(FVector::DotProduct(D,E.Mid)<E.CapCos)continue;
        const double PlaneDistance=FVector::DotProduct(D,E.Normal);const FVector Projected=(D-E.Normal*PlaneDistance).GetSafeNormal();
        const FVector AlongAxis=FVector::CrossProduct(E.Normal,E.Mid).GetSafeNormal();
        const double AlongPosition=FVector::DotProduct(D,AlongAxis);
        // Deformation varies inside a broad plate-boundary zone; relief is not a raised cell outline.
        const double Bend=W*.32*FMath::Sin(AlongPosition*23+E.A*1.7+E.B)*FMath::Cos(FVector::DotProduct(D,E.Mid)*11);
        const double Signed=PlaneDistance+Bend;
        double Along=0;if(FVector::DotProduct(Projected,E.Mid)<E.HalfCos)Along=FMath::Min((Projected-E.Start).SizeSquared(),(Projected-E.End).SizeSquared());
        auto Ribbon=[&](double Offset,double Width)
        {const double Q=(FMath::Square(Signed-Offset)+Along)/FMath::Square(Width);if(Q>=1)return 0.;return FMath::Pow(1-Q,3);};
        const double C=(E.ContinentalA+E.ContinentalB)*.5;
        const FVector Relative=FVector::CrossProduct(S.Plates[E.A].AngularVelocity-S.Plates[E.B].AngularVelocity,Projected);
        const double Con=-FVector::DotProduct(Relative,E.Normal)*Controls.Motion,Shear=FMath::Abs(FVector::DotProduct(Relative,AlongAxis))*Controls.Motion;
        const double Segmentation=.35+.65*Smooth(.5+.5*FMath::Sin(AlongPosition*31+E.A*2.1+E.B));
        const double Drive=FMath::Max(0.,FMath::Abs(Con)-.08)*Segmentation;
        if(Con>0)
        {
            // Collision plateaus broaden continental belts; arc and trench lie on opposite sides of a margin.
            if(E.ContinentalA>.55&&E.ContinentalB>.55)H+=Drive*(Ribbon(0,W*.48)*.42+Ribbon(0,W*1.2)*.10-Ribbon(W*1.6,W)*.035);
            else
            {
                const double Side=E.ContinentalA>=E.ContinentalB?1.:-1.;
                H+=Drive*(Ribbon(Side*W*.45,W*.55)*(.3+.12*C)-Ribbon(-Side*W*.45,W*.35)*.24);
            }
        }
        else H+=Drive*(Ribbon(-W*.6,W*.45)*.05+Ribbon(W*.6,W*.45)*.05+Ribbon(0,W*.45)*(.12-.3*C));
        H+=Shear*.01*FMath::Clamp(Signed/W,-1.,1.)*Ribbon(0,W);
    }
    return H;
}
double FCLCompiledTectonics::SampleState(const FState& S,const FVector& D) const
{
    if(S.Boundaries.IsEmpty())return 0;
    // One continuous deformation for the entire plate network, including junctions.
    // Sampling on the unit sphere avoids latitude seams and polar singularities.
    const FVector P=NoiseRotation.RotateVector(D);
    auto Noise=[&](const FVector& V){return double(FMath::PerlinNoise3D(V+NoiseOffset));};
    auto VectorNoise=[&](double Frequency)
    {
        const FVector Q=P*Frequency;
        return FVector(Noise(Q),Noise(Q+FVector(19.3,7.1,3.7)),Noise(Q+FVector(3.1,23.7,11.9)));
    };
    const FVector Warped=(D+NoiseRotation.UnrotateVector(VectorNoise(2.8)*.38+VectorNoise(7.3)*.08)).GetSafeNormal();
    const double Broad=Noise(P*5.7),Detail=Noise(P*17.3),Fine=Noise(P*37.1);
    const double Width=Controls.BeltWidth*(1.05+.55*Broad);
    // A nonperiodic envelope leaves broad saddles and uneven massifs, rather than
    // equally spaced beads along a cord. Small ridges stay inside the broad belt.
    const double Segmentation=.55+.45*Smooth(.5+Broad);
    const double Texture=.72+.5*FMath::Abs(Detail)+.22*FMath::Abs(Fine);
    double H=0;
    for(const auto& E:S.Boundaries)
    {
        if(FVector::DotProduct(Warped,E.Mid)<E.CapCos)continue;
        const double PlaneDistance=FVector::DotProduct(Warped,E.Normal);
        const double Signed=FMath::Asin(FMath::Clamp(PlaneDistance,-1.,1.));
        const FVector Projected=(Warped-E.Normal*PlaneDistance).GetSafeNormal();
        double Along=0;
        if(FVector::DotProduct(Projected,E.Mid)<E.HalfCos)
            Along=FMath::Min((Projected-E.Start).SizeSquared(),(Projected-E.End).SizeSquared());
        auto Belt=[&](double Offset,double Scale)
        {
            const double Q=(FMath::Square(Signed-Offset)+Along)/FMath::Square(Width*Scale);
            if(Q>=9)return 0.;
            return FMath::Exp(-Q*1.4)*(1-Smooth((Q-4)/5));
        };
        const FVector AlongAxis=FVector::CrossProduct(E.Normal,Projected).GetSafeNormal();
        const FVector Relative=FVector::CrossProduct(S.Plates[E.A].AngularVelocity-S.Plates[E.B].AngularVelocity,Projected);
        const double Con=-FVector::DotProduct(Relative,E.Normal)*Controls.Motion;
        const double Shear=FMath::Abs(FVector::DotProduct(Relative,AlongAxis))*Controls.Motion;
        const double CA=Continentalness((Projected+E.Normal*.12).GetSafeNormal());
        const double CB=Continentalness((Projected-E.Normal*.12).GetSafeNormal());
        const double C=(CA+CB)*.5,Collision=Smooth((CA-.35)/.35)*Smooth((CB-.35)/.35);
        const double Side=FMath::Tanh((CA-CB)*8.);
        const double Drive=FMath::Max(0.,FMath::Abs(Con)-.08)*Segmentation;
        if(Con>0)
        {
            const double Massif=Belt(Width*Detail*.5,.8)*.32*Texture+Belt(0,1.7)*.16;
            const double Margin=Belt(Side*Width*.55,.8)*(.26+.10*C)*Texture-Belt(-Side*Width*.6,.48)*.16;
            H+=Drive*FMath::Lerp(Margin,Massif,Collision);
        }
        else H+=Drive*(Belt(-Width*.7,.7)*.055+Belt(Width*.7,.7)*.055+Belt(Width*Detail*.3,.65)*(.12-.3*C));
        H+=Shear*.008*FMath::Tanh(Signed/Width)*Belt(0,1.2);
    }
    return H;
}
double FCLCompiledTectonics::SampleDeltaMeters(const FVector& Direction) const
{
    const FVector D=Direction.GetSafeNormal();const double C=Continentalness(D);
    const double Crust=(C-.5)*(.12+Controls.CrustThickness*10);
    const double LowDelta=AlgorithmVersion==1?SampleLegacyState(Lower,D):SampleState(Lower,D);
    const double HighDelta=Upper.Plates.IsEmpty()?LowDelta:(AlgorithmVersion==1?SampleLegacyState(Upper,D):SampleState(Upper,D));
    const double Raw=SpanMeters*(Crust+Controls.Relief*FMath::Lerp(LowDelta,HighDelta,Controls.PlateTransition));
    return MaximumDeltaMeters*FMath::Tanh(Raw/MaximumDeltaMeters);
}

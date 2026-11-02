#include "Model/CLTectonics.h"

namespace
{
double Ease(double X){X=FMath::Clamp(X,0.,1.);return X*X*X*(X*(X*6-15)+10);}
}

void FCLCompiledTectonics::BuildContinentalFoundation(const FCLTectonicPreset& P,int32 Seed)
{
    Foundation=P;FoundationFrame=P.FoundationOrientation.Quaternion();
    FRandomStream Random(int32(uint32(Seed)^0x434c4633U));
    NoiseOffset=FVector(Random.FRandRange(5,55),Random.FRandRange(5,55),Random.FRandRange(5,55));
    // Crust domains are independent of the two plate half-spaces.
    Continents={FoundationFrame.RotateVector(FVector(.84,.42,.12).GetSafeNormal()),
        FoundationFrame.RotateVector(FVector(.84,-.42,-.12).GetSafeNormal()),
        FoundationFrame.RotateVector(FVector(-.72,.55,.34).GetSafeNormal())};
    const FVector N=FoundationFrame.GetAxisY(),Axis=FoundationFrame.RotateVector(P.Setting==ECLContinentalSetting::Transform?FVector::YAxisVector:-FVector::ZAxisVector);
    for(int32 I=0;I<2;++I)
    {
        FPlate Plate;Plate.Id=FGuid(0x434c4633,uint32(Seed),1,I+1);Plate.Site=N*(I==0?1:-1);
        Plate.AngularVelocity=Axis*(I==0?.5:-.5)*(P.Setting==ECLContinentalSetting::Rift?-1:1)*P.MotionDirection;Lower.Plates.Add(Plate);
    }
    for(int32 I=0;I<4;++I)
    {
        const double T=I*PI/2;auto At=[&](double A){return FoundationFrame.RotateVector(FVector(FMath::Cos(A),0,FMath::Sin(A)));};
        FBoundary Edge;Edge.Id=FGuid(0x434c4633,uint32(Seed),2,I+1);Edge.A=0;Edge.B=1;Edge.Start=At(T);Edge.End=At(T+PI/2);Edge.Mid=At(T+PI/4);Edge.Normal=N;Edge.HalfCos=FMath::Cos(PI/4);Edge.CapCos=-1;
        const FVector Relative=FVector::CrossProduct(Lower.Plates[0].AngularVelocity-Lower.Plates[1].AngularVelocity,Edge.Mid);
        Edge.Convergence=-FVector::DotProduct(Relative,N);Edge.Shear=FMath::Abs(FVector::DotProduct(Relative,FVector::CrossProduct(N,Edge.Mid)));
        Edge.ContinentalA=Continentalness((Edge.Mid+N*.12).GetSafeNormal());Edge.ContinentalB=Continentalness((Edge.Mid-N*.12).GetSafeNormal());Lower.Boundaries.Add(Edge);
    }
}

FVector FCLCompiledTectonics::FoundationDirection(const FVector& D) const
{
    const FVector P=FoundationFrame.UnrotateVector(D);
    auto Noise=[&](FVector Q){return double(FMath::PerlinNoise3D(Q+NoiseOffset));};
    const FVector Q=P*3.;
    const FVector Warp(Noise(Q),Noise(Q+FVector(19.3,7.1,3.7)),Noise(Q+FVector(3.1,23.7,11.9)));
    return FoundationFrame.RotateVector((P+Warp*.24).GetSafeNormal());
}

double FCLCompiledTectonics::ContinentalDomain(const FVector& D) const
{
    double Remaining=1;
    for(int32 I=0;I<Continents.Num();++I)
    {
        const double Angle=FMath::Acos(FMath::Clamp(FVector::DotProduct(D,Continents[I]),-1.,1.));
        const double Outer=(I==2?.57:.85)*Foundation.ContinentalExtent,Inner=Outer*.51;
        double Weight=1-Ease((Angle-Inner)/(Outer-Inner));
        if(Foundation.Setting==ECLContinentalSetting::Subduction&&I<2)
            Weight*=Ease((FoundationFrame.UnrotateVector(D).Y+.13)/.26);
        Remaining*=1-Weight;
    }
    return 1-Remaining;
}

bool FCLCompiledTectonics::SampleGeology(const FVector& Direction,FCLGeologicalSample& Out) const
{
    if(AlgorithmVersion!=3||Direction.ContainsNaN()||!Direction.IsNormalized())return false;
    const FVector D=FoundationDirection(Direction),Local=FoundationFrame.UnrotateVector(D),N=FoundationFrame.GetAxisY();
    const double C=ContinentalDomain(D),Signed=FMath::Asin(FMath::Clamp(Local.Y,-1.,1.));
    const FVector Projected=FVector::VectorPlaneProject(D,N).GetSafeNormal();
    const FVector Relative=FVector::CrossProduct(Lower.Plates[0].AngularVelocity-Lower.Plates[1].AngularVelocity,Projected)*Controls.Motion;
    const double Con=-FVector::DotProduct(Relative,N),Shear=FVector::DotProduct(Relative,FVector::CrossProduct(N,Projected));
    const double Noise=FMath::PerlinNoise3D(Local*11.7+NoiseOffset),Detail=FMath::PerlinNoise3D(Local*29.1+NoiseOffset);
    const double W=Controls.BeltWidth*(1+.3*Noise),Texture=.85+.35*FMath::Abs(Detail);
    auto Belt=[&](double Offset,double Scale)
    {const double Q=FMath::Square((Signed-Offset)/(W*Scale));return Q>=9?0:FMath::Exp(-Q)*(1-Ease((Q-4)/5));};
    // Broad interiors, a shallow shelf transition and deep oceanic floor share one field.
    FCLGeologicalSample Result;Result.Continentalness=C;
    Result.BaseDeltaMeters=SpanMeters*(-.42+.30*Ease(C/.28)+.34*Ease((C-.22)/.6));
    const double CA=ContinentalDomain((Projected+N*.12).GetSafeNormal()),CB=ContinentalDomain((Projected-N*.12).GetSafeNormal());
    const double Collision=Ease((CA-.3)/.4)*Ease((CB-.3)/.4),Side=CA>=CB?1:-1;
    const double Drive=1-FMath::Exp(-FMath::Abs(Con));double Deformation=0,Thickening=0;
    Result.Convergence=Con;Result.Shear=Shear;
    if(Con>1.e-10)
    {
        const double Broad=FMath::Exp(-FMath::Pow(Signed/(W*2.4),4));
        const double Massif=.29*Broad+.19*Texture*Belt(Noise*W*.35,.65)-.085*(Belt(W*3.1,1.15)+Belt(-W*3.1,1.15));
        const double Margin=.25*Texture*Belt(Side*W*.65,.65)-.20*Belt(-Side*W*.45,.38)-.05*Belt(Side*W*2.,.8);
        Deformation=Drive*FMath::Lerp(Margin,Massif,Collision);Thickening=Drive*Collision*Broad*.12;
        if(FMath::Abs(Signed)<W*4)Result.Setting=Collision>.5?ECLGeologicalSetting::ContinentalCollision:ECLGeologicalSetting::Subduction;
    }
    else if(Con<-1.e-10)
    {
        const double Continental=Ease((C-.2)/.45);
        const double Rift=.10*(Belt(W,1)+Belt(-W,1))-.24*Belt(0,.65)-.07*Belt(0,2.1);
        Deformation=Drive*FMath::Lerp(.18*Texture*Belt(0,.8),Rift,Continental);
        if(FMath::Abs(Signed)<W*4)Result.Setting=Continental>.5?ECLGeologicalSetting::ContinentalRift:ECLGeologicalSetting::OceanSpreading;
    }
    // Transform motion alone has a modest fault corridor, not a mountain belt.
    Deformation+=.014*FMath::Tanh(Shear)*FMath::Tanh(Signed/W)*Belt(0,1.3);
    if(FMath::Abs(Shear)>FMath::Abs(Con)*2&&FMath::Abs(Signed)<W*3)Result.Setting=ECLGeologicalSetting::Transform;
    Result.DeformationMeters=Deformation*SpanMeters*Controls.Relief;
    Result.CrustThicknessMeters=SpanMeters*(.08+.22*C+Thickening*Controls.Relief);
    Result.TotalDeltaMeters=MaximumDeltaMeters*FMath::Tanh((Result.BaseDeltaMeters+Result.DeformationMeters)/MaximumDeltaMeters);
    if(Result.Setting!=ECLGeologicalSetting::Interior)
    {
        const auto* Closest=&Lower.Boundaries[0];for(const auto& Edge:Lower.Boundaries)if(FVector::DotProduct(D,Edge.Mid)>FVector::DotProduct(D,Closest->Mid))Closest=&Edge;
        Result.BoundaryId=Closest->Id;
    }
    Out=Result;return true;
}

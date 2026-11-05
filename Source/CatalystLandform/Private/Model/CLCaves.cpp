#include "Model/CLCaves.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLCompileProgress.h"
#include "Model/CLRegionGeneration.h"
#include "Model/CLProvinceDefinition.h"
#include "Model/CLTectonics.h"

void FCLCaveSettings::ApplyProgression(double Value)
{
    Character=FMath::Clamp(Value,0.,1.);
    NetworksPerSquareKilometer=.8*Character;
    LengthMeters=FMath::Lerp(18.,92.,Character);
    RadiusMeters=FMath::Lerp(.35,1.45,Character);
    DepthMeters=FMath::Lerp(10.,42.,Character);
    Branching=FMath::Clamp((Character-.25)/.75,0.,1.);
}
bool FCLCaveSettings::Validate(FString& Error)const
{
    auto In=[](double V,double A,double B){return FMath::IsFinite(V)&&V>=A&&V<=B;};
    if(AlgorithmVersion!=1||!In(Character,0,1)||!In(NetworksPerSquareKilometer,0,10)||!In(LengthMeters,1,500)||!In(RadiusMeters,.1,10)||!In(MinimumCoverMeters,1,100)||!In(DepthMeters,1,500)||!In(Branching,0,1))
    {Error=TEXT("Caves require algorithm 1 and finite bounded physical density, dimensions, cover and branching");return false;}return true;
}
TSharedPtr<const FCLCompiledCaves> FCLCompiledCaves::Compile(const FCLCaveSettings& S,const FCLCompiledPlanetRecipe& Base,int32 Seed,FString& Error)
{
    if(!S.Validate(Error))return nullptr;
    auto Out=MakeShared<FCLCompiledCaves>();
    if(!S.bEnabled||S.NetworksPerSquareKilometer==0){Out->Readiness=TEXT("No cave development requested");return Out;}
    if(!Base.Rock||!Base.Tectonics||!Base.GeneratedRegions)
    {Out->Readiness=TEXT("Fissures require Rock, tectonics and selected rift/shear Landforms");return Out;}
    if(!Base.Rock->HasFractureHosts())
    {Out->Readiness=TEXT("This bounded fissure profile requires all three rock units to declare a known lithology and positive fracture potential; generic or mixed unknown hosts remain untouched");return Out;}
    const double Reference=Base.RadiusMeters+(Base.MinimumHeightMeters+Base.MaximumHeightMeters)*.5;
    if(S.DepthMeters+S.MinimumCoverMeters+S.RadiusMeters*3>Reference*.3||S.LengthMeters>Reference*.5)
    {Error=TEXT("Cave dimensions exceed this body's bounded physical profile; reduce depth/length or enlarge the body");return nullptr;}
    const double Area=4*UE_DOUBLE_PI*Reference*Reference/1.e6;
    constexpr double CandidateDensity=16;
    const double Requested=FMath::CeilToDouble(Area*CandidateDensity);
    if(Requested>16384){Error=TEXT("Cave placement exceeds the 16384-candidate physical-area proof budget; no coarsening or truncated generation was applied");return nullptr;}
    Out->Candidates=FMath::Max(16,int32(Requested));
    FRandomStream Random(int32(uint32(Seed)^uint32(S.Seed)^0x43415645));
    const FQuat Rotation(FRotator(Random.FRandRange(-90,90),Random.FRandRange(-180,180),Random.FRandRange(-180,180)));
    FCLVolumeSettings Volumes;Volumes.bEnabled=true;
    auto Profile=[&](FVector D)->ECLProvinceType
    {
        const auto& G=*Base.GeneratedRegions;const auto& P=G.Palette[G.Cells[G.Owner(D)].PaletteIndex];
        const auto Type=P.Suitability.Province.Type;
        if(Type!=ECLProvinceType::ContinentalRift&&Type!=ECLProvinceType::TransformBelt)return ECLProvinceType::None;
        return P.Suitability.Province.Match(FCLProvinceGeology::Sample(Base,D))==ECLProvinceMatch::Eligible?Type:ECLProvinceType::None;
    };
    auto Drive=[&](FVector D,ECLProvinceType Type){const auto V=Base.Tectonics->SampleRockDeformation(D,1,0,1);return Type==ECLProvinceType::ContinentalRift?V.Extension:V.Shear;};
    auto Add=[&](FVector A,FVector B,double Radius,FGuid Id,bool Entrance)
    {
        auto& Segment=Out->Segments.AddDefaulted_GetRef();Segment={Id,A,B,Radius,FVector(.55,2,1),Entrance};
        auto& O=Volumes.Operations.AddDefaulted_GetRef();O.Id=FGuid::Combine(Id,FGuid(0,0,0,Volumes.Operations.Num()));O.Name=FName(*FString::Printf(TEXT("Fissure_%d"),Volumes.Operations.Num()));
        O.Primitive=ECLSolidPrimitive::Capsule;O.Composition=ECLSolidComposition::Subtract;O.CenterMeters=(A+B)*.5;O.RadiusMeters=Radius;O.HalfLengthMeters=FVector::Distance(A,B)*.5;O.Scale=Segment.Scale;
        const FVector Along=(B-A).GetSafeNormal(),Up=O.CenterMeters.GetSafeNormal();FVector Across=FVector::CrossProduct(Up,Along).GetSafeNormal();if(Across.IsNearlyZero())Across=FVector::VectorPlaneProject(FVector::XAxisVector,Along).GetSafeNormal();
        O.Rotation=FRotationMatrix::MakeFromXZ(Across,Along).Rotator();
    };
    for(int32 I=0;I<Out->Candidates;++I)
    {
        if(!FCLCompileProgress::Check(Error))return nullptr;
        FCLCompileProgress::Count(I,Out->Candidates);
        const double Z=1-2*(I+.5)/Out->Candidates,A=I*2.399963229728653;
        const FVector D=Rotation.RotateVector(FVector(FMath::Sqrt(1-Z*Z)*FMath::Cos(A),FMath::Sqrt(1-Z*Z)*FMath::Sin(A),Z));
        const double Occurrence=Random.FRand(),Phase=Random.FRandRange(-.35,.35),DepthJitter=Random.FRandRange(.8,1.2),WidthJitter=Random.FRandRange(.8,1.2),BranchDraw=Random.FRand(),EntranceDraw=Random.FRand();
        const auto Type=Profile(D);if(Type==ECLProvinceType::None)continue;++Out->EligibleCandidates;
        if(Occurrence>S.NetworksPerSquareKilometer*Area/Out->Candidates*Base.Rock->FracturePotential())continue;
        const FVector X=FVector::CrossProduct(D,FMath::Abs(D.Z)<.8?FVector::ZAxisVector:FVector::XAxisVector).GetSafeNormal(),Y=FVector::CrossProduct(D,X);
        constexpr double Step=.001;
        const FVector Gradient=X*(Drive((D+X*Step).GetSafeNormal(),Type)-Drive((D-X*Step).GetSafeNormal(),Type))+Y*(Drive((D+Y*Step).GetSafeNormal(),Type)-Drive((D-Y*Step).GetSafeNormal(),Type));
        FVector Tangent=FVector::CrossProduct(D,Gradient).GetSafeNormal();if(Tangent.IsNearlyZero())Tangent=X;
        Tangent=FQuat(D,Phase).RotateVector(Tangent);
        const double Radius=S.RadiusMeters*WidthJitter,Depth=S.MinimumCoverMeters+Radius*2.5+S.DepthMeters*DepthJitter;
        TArray<FVector> Directions,Points;
        for(int32 K=0;K<4;++K)Directions.Add((D+Tangent*((double(K)/3-.5)*S.LengthMeters/Reference)).GetSafeNormal());
        bool Compatible=true;for(const auto& Q:Directions)Compatible&=Profile(Q)==Type;if(!Compatible)continue;
        TArray<double> Heights;Base.SampleHeightsMeters(Directions,Heights);
        for(int32 K=0;K<4;++K)Points.Add(Directions[K]*(Base.RadiusMeters+Heights[K]-Depth-(K%2)*Radius));
        const bool Branch=BranchDraw<S.Branching,Entrance=S.bSurfaceEntrances&&EntranceDraw<.65;
        const int32 Needed=3+int32(Branch)+int32(Entrance);
        if(Volumes.Operations.Num()+Needed>64){Error=TEXT("Caves exceed the existing 64-operation native volume proof budget; no networks were silently dropped");return nullptr;}
        const FGuid Id(0x43415645,uint32(Seed),uint32(S.Seed),I+1);
        for(int32 K=1;K<4;++K)Add(Points[K-1],Points[K],Radius,Id,false);
        if(Branch)
        {
            const FVector Q=(Directions[2]+FVector::CrossProduct(D,Tangent)*S.LengthMeters*.25/Reference).GetSafeNormal();
            if(Profile(Q)==Type){const double H=Base.SampleHeightMeters(Q);Add(Points[2],Q*(Base.RadiusMeters+H-Depth),Radius*.7,Id,false);}
        }
        if(Entrance){Add(Points[0],Directions[0]*(Base.RadiusMeters+Heights[0]+Radius*3),Radius*.65,Id,true);++Out->Entrances;}
        ++Out->Networks;
    }
    FCLCompileProgress::Count(Out->Candidates,Out->Candidates);
    if(!Volumes.Operations.IsEmpty()){Out->Cutters=FCLCompiledVolumes::Compile(Volumes,Base.RadiusMeters,Base.Surfaces.Get(),Error);if(!Out->Cutters)return nullptr;}
    Out->Readiness=FString::Printf(TEXT("%d fissure networks, %d native cutters, %d entrances; %d/%d candidates have selected local rift/shear evidence"),Out->Networks,Out->Segments.Num(),Out->Entrances,Out->EligibleCandidates,Out->Candidates);
    return Out;
}

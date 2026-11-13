#include "Model/CLMaterialLayers.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLRegionGeneration.h"
#include "Model/CLCompileProgress.h"

namespace
{
double Smooth(double T){T=FMath::Clamp(T,0.,1.);return T*T*(3-2*T);}
bool GuidLess(const FGuid& A,const FGuid& B){return A.ToString(EGuidFormats::Digits)<B.ToString(EGuidFormats::Digits);}
}
FCLLandformDeposit::FCLLandformDeposit()
{
    Unit.Name=TEXT("Landform deposit");Unit.Lithology=ECLRockLithology::Clastic;
    Unit.SurfaceId=TEXT("Scree");Unit.SurfaceRevision=2;Unit.MiningResistance=.2;Unit.Erodibility=.8;
}
bool FCLLandformDeposit::Validate(FString& Error)const
{
    auto In=[](double V,double Low,double High){return FMath::IsFinite(V)&&V>=Low&&V<=High;};
    if(!Unit.Validate(Error))return false;
    if(!In(TopDepthMeters,0,10000)||!In(ThicknessMeters,.05,10000)||!In(ContactFeatherMeters,0,5)||
       ContactFeatherMeters>ThicknessMeters||!In(ThicknessVariation,0,1)||!In(Coverage,0,1)||!In(PatchSizeMeters,1,1.e7)||!In(MaximumSlopeDegrees,0,90))
    {Error=TEXT("Deposits require finite depth/thickness, bounded feather, coverage, patch size and slope");return false;}
    return true;
}
bool FCLLandformDeposit::ValidateList(TConstArrayView<FCLLandformDeposit> Deposits,FString& Error)
{
    if(Deposits.Num()>8){Error=TEXT("A landform supports at most eight deposit layers");return false;}
    TSet<FGuid> Ids;for(const auto& D:Deposits){if(!D.Validate(Error))return false;if(Ids.Contains(D.Unit.Id)){Error=TEXT("Deposit identities must be unique within their landform");return false;}Ids.Add(D.Unit.Id);}return true;
}
void FCLMaterialSample::OverlayAppearance(const FCLSurfaceWeights& Weights,double Alpha)
{Surfaces.Lerp(Weights,FMath::Clamp(Alpha,0.,1.));}
void FCLMaterialSample::Overlay(const FCLSurfaceWeights& Weights,const FCLRockSample& Physical,double Alpha)
{
    Alpha=FMath::Clamp(Alpha,0.,1.);if(Alpha<=0)return;
    OverlayAppearance(Weights,Alpha);
    Properties=FMath::Lerp(Properties,FLinearColor(Physical.MiningResistance,Physical.Erodibility,Physical.bValid?1:0,0),float(Alpha));
    DominantContribution*=1-Alpha;
    if(Alpha>DominantContribution){Material=Physical;DominantContribution=Alpha;}
    Material.SubstrateUnitId=Substrate.UnitId;Material.SubstrateStratumId=Substrate.StratumId;
    Material.MiningResistance=Properties.R;Material.Erodibility=Properties.G;
}
bool FCLCompiledMaterialLayers::Compile(const FCLPlanetRecipe& Recipe,const FCLCompiledPlanetRecipe& Plan,TSharedPtr<const FCLCompiledMaterialLayers>& Output,FString& Error)
{
    auto Out=MakeShared<FCLCompiledMaterialLayers>();
    auto Add=[&](const FGuid Source,int32 PaletteIndex,int32 SourcePriority,TConstArrayView<FCLLandformDeposit> Deposits,TFunction<double(const FVector&)> Footprint,int32 Seed)
    {
        if(!FCLLandformDeposit::ValidateList(Deposits,Error))return false;
        for(const auto& D:Deposits)if(D.bEnabled)
        {
            if(!Plan.Surfaces){Error=TEXT("Landform deposits require the Planet material library and fallback surface");return false;}
            const int32 Surface=Plan.Surfaces->Find(D.Unit.SurfaceId,D.Unit.SurfaceRevision);
            if(Surface==INDEX_NONE){Error=TEXT("Missing deposit material revision: ")+D.Unit.SurfaceId.ToString();return false;}
            if(Plan.RadiusMeters/D.PatchSizeMeters>1.e6){Error=TEXT("Deposit patch size exceeds the supported body-coordinate precision");return false;}
            FLayer L;L.Definition=D;L.SourceId=Source;L.PaletteIndex=PaletteIndex;L.SourcePriority=SourcePriority;L.Footprint=Footprint;L.SurfaceIndex=Surface;
            L.UnitId=FGuid::Combine(Source,D.Unit.Id);
            L.StratumId=FGuid::Combine(L.UnitId,FGuid(0x4445504f,uint32(Recipe.Seed),uint32(Seed),uint32(D.Seed)));
            FRandomStream Random(int32(L.StratumId.A^L.StratumId.B^L.StratumId.C^L.StratumId.D));
            L.Offset=FVector(Random.FRandRange(-5000,5000),Random.FRandRange(-5000,5000),Random.FRandRange(-5000,5000));
            Out->MaximumDepthMeters=FMath::Max(Out->MaximumDepthMeters,D.TopDepthMeters+D.ThicknessMeters+D.ContactFeatherMeters);Out->Layers.Add(MoveTemp(L));
        }
        return true;
    };
    if(Recipe.RegionGeneration.bEnabled)for(const auto& P:Recipe.RegionGeneration.Palette)if(P.bEnabled&&!P.Deposits.IsEmpty())
    {
        // Preparation validates material bindings before the deferred partition
        // exists. Only the completed background compilation is queryable.
        if(!Plan.GeneratedRegions&&!FCLCompileProgress::IsPreparing()){Error=TEXT("Deposits require their compiled landform footprint");return false;}
        const int32 Index=Plan.GeneratedRegions?Plan.GeneratedRegions->Palette.IndexOfByPredicate([&](const auto& V){return V.Id==P.Id;}):0;
        if(Index==INDEX_NONE){Error=TEXT("Deposit source is missing from the accepted landform palette");return false;}
        const int32 Seed=P.Rift.IsActive()?P.Rift.Seed:P.Transform.IsActive()?P.Transform.Seed:P.VolcanicArc.IsActive()?P.VolcanicArc.Seed:P.Seed;
        if(!Add(P.Id,Index,0,P.Deposits,{},Seed))return false;
    }
    for(const auto& R:Recipe.TerrainRegions)if(R.bEnabled&&!R.Deposits.IsEmpty())
    {
        if(Recipe.Coordinates!=ECLTerrainCoordinates::SphericalSurface){Error=TEXT("Deposits require spherical landform footprints");return false;}
        TFunction<double(const FVector&)> Footprint;
        if(R.bPlacedLandform)
        {
            FCLCompiledLandformPlacement Placement;if(!FCLCompiledLandformPlacement::Resolve(R.Placement,Recipe.BaseStructure,Placement,Error))return false;
            Footprint=[Placement](const FVector& D){return Placement.Weight(D);};
        }
        else {const auto Mask=R.SphericalMask;const double Radius=Plan.RadiusMeters+Plan.BaseHeightMeters;Footprint=[Mask,Radius](const FVector& D){return Mask.Weight(D,Radius);};}
        if(!Add(R.Id,INDEX_NONE,R.Priority,R.Deposits,MoveTemp(Footprint),R.SeedOffset))return false;
    }
    Out->Layers.Sort([](const FLayer& A,const FLayer& B)
    {
        if(A.Definition.Priority!=B.Definition.Priority)return A.Definition.Priority<B.Definition.Priority;
        if(A.SourcePriority!=B.SourcePriority)return A.SourcePriority<B.SourcePriority;
        return A.SourceId!=B.SourceId?GuidLess(A.SourceId,B.SourceId):GuidLess(A.Definition.Unit.Id,B.Definition.Unit.Id);
    });
    if(Out->Layers.IsEmpty())Output.Reset();else Output=Out;return true;
}
void FCLCompiledMaterialLayers::Apply(const FCLCompiledPlanetRecipe& Plan,TConstArrayView<FVector3d> Positions,TArrayView<FCLMaterialSample> Samples)const
{
    check(Positions.Num()==Samples.Num());if(Layers.IsEmpty())return;
    TArray<FVector> Directions;TArray<int32> Indices;
    for(int32 I=0;I<Positions.Num();++I)
    {
        const auto& P=Positions[I];const double Radius=P.Length();
        if(P.ContainsNaN()||!FMath::IsFinite(Radius)||Radius<1.e-6||Radius<Plan.RadiusMeters+Plan.MinimumHeightMeters-MaximumDepthMeters||Radius>Plan.RadiusMeters)continue;
        Directions.Add(P/Radius);Indices.Add(I);
    }
    if(Directions.IsEmpty())return;
    TArray<double> Heights;Plan.SampleHeightsMeters(Directions,Heights);
    TArray<double> Palette;const double ReferenceRadius=Plan.RadiusMeters+Plan.BaseHeightMeters;
    for(int32 J=0;J<Indices.Num();++J)
    {
        const int32 I=Indices[J];const FVector D=Directions[J];const double Depth=Plan.RadiusMeters+Heights[J]-Positions[I].Length();
        if(Depth>MaximumDepthMeters||Depth<-5)continue;
        if(Plan.GeneratedRegions)Plan.GeneratedRegions->PaletteWeights(D,Palette);
        TOptional<double> Slope;
        for(const auto& L:Layers)
        {
            const auto& S=L.Definition;if(S.Coverage==0)continue;
            const double Footprint=L.PaletteIndex!=INDEX_NONE?Palette[L.PaletteIndex]:L.Footprint(D);
            if(Footprint<=0)continue;
            const double Noise=FMath::Clamp(.5+.5*FMath::PerlinNoise3D((D*ReferenceRadius+L.Offset)/S.PatchSizeMeters),0.,1.);
            const double Coverage=S.Coverage==1?1:Smooth((Noise-(1-S.Coverage))/.12);
            const double Variation=FMath::Clamp(.5+.5*FMath::PerlinNoise3D((D*ReferenceRadius+L.Offset+FVector(317,997,1597))/S.PatchSizeMeters),0.,1.);
            const double Thickness=S.ThicknessMeters*FMath::Lerp(1-S.ThicknessVariation,1.,Variation)*Footprint*Coverage;
            const double LocalDepth=Depth-S.TopDepthMeters;
            if(Thickness<=0||LocalDepth<-S.ContactFeatherMeters||LocalDepth>Thickness)continue;
            double SlopeWeight=1;
            if(S.MaximumSlopeDegrees<90)
            {
                if(!Slope.IsSet())
                {
                    TArray<FVector> Probe;CLSurfaceProbe(D,Probe);TArray<double> H;Plan.SampleHeightsMeters(Probe,H);
                    const FVector N=CLSurfaceNormal(Probe,H,Plan.RadiusMeters,0);
                    Slope=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(N,D),0.,1.)));
                }
                SlopeWeight=Smooth((S.MaximumSlopeDegrees-Slope.GetValue())/5.);
            }
            const double Feather=FMath::Min(S.ContactFeatherMeters,Thickness);
            const double Contact=Feather>0?Smooth(1+LocalDepth/Feather)*Smooth((Thickness-LocalDepth)/Feather):double(LocalDepth>=0&&LocalDepth<=Thickness);
            const double Alpha=Footprint*Coverage*SlopeWeight*Contact;if(Alpha<=0)continue;
            FCLSurfaceWeights W;W.Values[L.SurfaceIndex]=1;
            FCLRockSample Physical;Physical.bValid=true;Physical.Origin=ECLMaterialOrigin::LandformDeposit;
            Physical.UnitId=L.UnitId;Physical.StratumId=L.StratumId;Physical.SourceId=L.SourceId;Physical.DepositId=S.Unit.Id;
            Physical.Name=S.Unit.Name;Physical.SurfaceId=S.Unit.SurfaceId;Physical.SurfaceRevision=S.Unit.SurfaceRevision;
            Physical.MiningResistance=S.Unit.MiningResistance;Physical.Erodibility=S.Unit.Erodibility;
            Physical.DepositDepthMeters=LocalDepth;Physical.DepositThicknessMeters=Thickness;
            Samples[I].Overlay(W,Physical,Alpha);
        }
    }
}

#include "Scene/CLScene.h"
#include "Model/CLSpatialCave.h"
#include "VoxelMinimal.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLSphericalPatches.h"

namespace
{
constexpr int32 SampleBatchSize = 512;

bool ValidateQuery(const FCLSceneQuery& Query, FString& Error)
{
    Error.Reset();
    if (!FMath::IsFinite(Query.FootprintMeters) || Query.FootprintMeters != 0)
    {
        Error = TEXT("Scene supports point queries only; filtered footprints are not qualified");
        return false;
    }
    return true;
}

bool IsFinitePosition(const FVector3d& P)
{
    return !P.ContainsNaN() && FMath::IsFinite(P.SizeSquared());
}
}

FCLScene::FCLScene(TSharedPtr<const FCLCompiledPlanetRecipe> InPlan) : RadialPlan(MoveTemp(InPlan)) {}

TSharedPtr<const FCLScene> FCLScene::FromLegacyRadial(TSharedPtr<const FCLCompiledPlanetRecipe> Plan, FString& Error)
{
    if(Plan&&(Plan->Volumes||(Plan->Caves&&Plan->Caves->Cutters))){Error=TEXT("A volume scene cannot use the radial-only provider");return nullptr;}
    return FromPlanet(MoveTemp(Plan),Error);
}

TSharedPtr<const FCLScene> FCLScene::FromPlanet(TSharedPtr<const FCLCompiledPlanetRecipe> Plan,FString& Error)
{
    Error.Reset();
    if (!Plan || !FMath::IsFinite(Plan->RadiusMeters) || Plan->RadiusMeters <= 0 ||
        Plan->Coordinates == ECLTerrainCoordinates::LocalPatch || FCLSphericalPatches::IsFace(Plan->SurfacePatch))
    {
        Error = TEXT("Legacy radial scene requires a compiled whole spherical planet, not a local or face-scoped plan");
        return nullptr;
    }
    return MakeShareable(new FCLScene(MoveTemp(Plan)));
}

double FCLScene::OuterRadiusMeters() const { return RadialPlan->RadiusMeters; }
bool FCLScene::IsHeightOnly() const { return !RadialPlan->Volumes&&!(RadialPlan->Caves&&RadialPlan->Caves->Cutters); }

FBox3d FCLScene::SolidBoundsMeters() const
{
    return FBox3d(FVector3d(-OuterRadiusMeters()), FVector3d(OuterRadiusMeters()));
}

bool FCLScene::SampleShape(const FCLSceneQuery& Query, TArray<FCLSceneShapeSample>& Out, FString& Error) const
{
    Out.Reset();
    if (!ValidateQuery(Query, Error)) return false;
    Out.SetNum(Query.PositionsMeters.Num());
    for (int32 Start = 0; Start < Out.Num(); Start += SampleBatchSize)
    {
        const int32 Count = FMath::Min(SampleBatchSize, Out.Num() - Start);
        TArray<FVector> Positions;
        Positions.Reserve(Count);
        for (int32 I = 0; I < Count; ++I)
        {
            const FVector3d& P = Query.PositionsMeters[Start + I];
            Positions.Add(IsFinitePosition(P) ? P : FVector3d::ZeroVector);
        }
        TArray<double> Heights;
        TArray<double> Fields;
        if(RadialPlan->Caves&&RadialPlan->Caves->Spatial)
        {RadialPlan->Caves->Spatial->SamplePlanet(*RadialPlan,Positions,Fields);Heights.Init(0,Count);}
        else {RadialPlan->SampleHeightsMeters(Positions, Heights);RadialPlan->SampleRadialFields(Positions,Heights,Fields);}
        for (int32 I = 0; I < Count; ++I)
        {
            const FVector3d& P = Query.PositionsMeters[Start + I];
            auto& Sample = Out[Start + I];
            if (!IsFinitePosition(P) || !FMath::IsFinite(Heights[I])) continue;
            // VPP subtracts the effective double radius before float output.
            // This radial-height field does not claim a Euclidean SDF metric.
            Sample.FieldMeters = Fields[I];
            Sample.State = ECLSceneSampleState::Known;
        }
        if(RadialPlan->Caves&&RadialPlan->Caves->Cutters&&!RadialPlan->Caves->Spatial)RadialPlan->Caves->Cutters->ComposeBatch(Positions,Fields);
        if(RadialPlan->Volumes||RadialPlan->Caves)
        {
            if(RadialPlan->Volumes&&!(RadialPlan->Caves&&RadialPlan->Caves->Spatial))RadialPlan->Volumes->ComposeBatch(Positions,Fields);
            for(int32 I=0;I<Count;++I)if(Out[Start+I].State==ECLSceneSampleState::Known)Out[Start+I].FieldMeters=Fields[I];
        }
    }
    return true;
}

bool FCLScene::SampleCaveVoid(const FCLSceneQuery& Query,TArray<FCLSceneShapeSample>& Out,FString& Error,const FCLCompiledCaveVolume* SelectedCutters)const
{
    Out.Reset();if(!ValidateQuery(Query,Error))return false;Out.SetNum(Query.PositionsMeters.Num());
    for(int32 Start=0;Start<Out.Num();Start+=SampleBatchSize)
    {
        const int32 Count=FMath::Min(SampleBatchSize,Out.Num()-Start);TArray<FVector> Positions;
        for(int32 I=0;I<Count;++I)Positions.Add(IsFinitePosition(Query.PositionsMeters[Start+I])?Query.PositionsMeters[Start+I]:FVector::ZeroVector);
        TArray<double> Air;Air.Init(FVoxelUtilities::NaNf(),Count);
        const auto* AirCutters=SelectedCutters?SelectedCutters:(RadialPlan->Caves?RadialPlan->Caves->Cutters.Get():nullptr);
        if(AirCutters)AirCutters->ComposeBatch(Positions,Air);else Air.Init(-1.,Count);
        TArray<double> H,Natural;
        const bool BoundedSurface=RadialPlan->SurfaceRange.IsValid()||(RadialPlan->WatershedSurface&&RadialPlan->WatershedSurface->SurfaceRange.IsValid());
        if(AirCutters&&!RadialPlan->Volumes&&BoundedSurface)
        {
            // Exact max-intersection pruning using the same native sphere node.
            // The calibrated surface and conditioned valleys are bounded below
            // by MinimumHeightMeters. A larger-radius natural sphere can only
            // decrease this field, so it cannot beat -Air at these points.
            H.Init(RadialPlan->MinimumHeightMeters,Count);RadialPlan->SampleRadialFields(Positions,H,Natural);
            TArray<FVector> Needed;TArray<int32> Indices;
            for(int32 I=0;I<Count;++I)if(Natural[I]>-Air[I]){Needed.Add(Positions[I]);Indices.Add(I);}
            if(!Needed.IsEmpty())
            {
                TArray<double> Height,Field;RadialPlan->SampleHeightsMeters(Needed,Height);RadialPlan->SampleRadialFields(Needed,Height,Field);
                for(int32 I=0;I<Indices.Num();++I)Natural[Indices[I]]=Field[I];
            }
            for(int32 I=0;I<Count;++I)if(IsFinitePosition(Query.PositionsMeters[Start+I])){Out[Start+I].State=ECLSceneSampleState::Known;Out[Start+I].FieldMeters=FMath::Max(Natural[I],-Air[I]);}
            continue;
        }
        RadialPlan->SampleHeightsMeters(Positions,H);RadialPlan->SampleRadialFields(Positions,H,Natural);
        auto Cut=Natural;

        if(RadialPlan->Caves&&RadialPlan->Caves->Cutters)RadialPlan->Caves->Cutters->ComposeBatch(Positions,Cut);
        auto Final=Cut;if(RadialPlan->Volumes)RadialPlan->Volumes->ComposeBatch(Positions,Final);
        for(int32 I=0;I<Count;++I)
        {
            auto& S=Out[Start+I];if(!IsFinitePosition(Query.PositionsMeters[Start+I]))continue;
            S.State=ECLSceneSampleState::Known;
            // Intersection of original solid, generated cave air and final air.
            // This closes entrance cutters at the real surface, respects backfill,
            // and excludes unrelated authored excavations outside the cave network.
            S.FieldMeters=FMath::Max3(Natural[I],-Air[I],-Final[I]);
        }
    }
    return true;
}

bool FCLScene::SampleGeology(const FCLSceneQuery& Query,TArray<FCLRockSample>& Out,FString& Error)const
{
    Out.Reset();if(!ValidateQuery(Query,Error))return false;Out.SetNum(Query.PositionsMeters.Num());
    if(!RadialPlan->Rock)return true;
    for(int32 I=0;I<Out.Num();++I)Out[I]=RadialPlan->Rock->Sample(RadialPlan->RockPositionMeters(Query.PositionsMeters[I]));
    return true;
}

bool FCLScene::SampleSurfaces(const FCLSceneQuery& Query, TArray<FCLSceneSurfaceSample>& Out, FString& Error) const
{
    Out.Reset();
    if (!ValidateQuery(Query, Error)) return false;
    if(RadialPlan->Rock)
    {
        Out.SetNum(Query.PositionsMeters.Num());
        for(int32 I=0;I<Out.Num();++I)
        {
            const auto& P=Query.PositionsMeters[I];auto& S=Out[I];
            if(!IsFinitePosition(P)){S.State=ECLSceneSampleState::InvalidQuery;continue;}
            if(P.Length()>OuterRadiusMeters()){S.State=ECLSceneSampleState::OutsideDomain;continue;}
            FCLSurfaceWeights W;
            if(!(RadialPlan->Caves&&RadialPlan->Caves->Spatial&&RadialPlan->Caves->Spatial->TryPreparedSurfaces(P,W)))
                RadialPlan->Rock->ApplySurfaces(RadialPlan->RockPositionMeters(P),W);
            if(RadialPlan->Volumes&&RadialPlan->Volumes->Operations.ContainsByPredicate([](const auto& O){return O.SurfaceIndex!=INDEX_NONE;}))
            {
                const double H=RadialPlan->SampleHeightMeters(P);TArray<double> Base;RadialPlan->SampleRadialFields(MakeArrayView(&P,1),MakeArrayView(&H,1),Base);if(RadialPlan->Caves&&RadialPlan->Caves->Cutters)Base[0]=RadialPlan->Caves->Cutters->Compose(P,Base[0]);RadialPlan->Volumes->Compose(P,Base[0],&W);
            }
            S.State=ECLSceneSampleState::Known;
            for(int32 K=0;K<RadialPlan->Surfaces->Ids.Num();++K)if(W.Values[K]>0)S.Weights.Add({RadialPlan->Surfaces->Ids[K],RadialPlan->Surfaces->Revisions[K],W.Values[K]});
        }
        return true;
    }
    if(RadialPlan->Volumes)
    {
        Out.SetNum(Query.PositionsMeters.Num());
        for(int32 Start=0;Start<Out.Num();Start+=SampleBatchSize)
        {
            const int32 Count=FMath::Min(SampleBatchSize,Out.Num()-Start);TArray<FVector3d> Probes;
            // Native float fields need a 5 cm derivative baseline to avoid
            // amplifying micrometre distance rounding into material-weight drift.
            constexpr double E=.05;
            for(int32 I=0;I<Count;++I)
            {
                const auto& P=Query.PositionsMeters[Start+I];
                for(const FVector3d Axis:{FVector3d(E,0,0),FVector3d(0,E,0),FVector3d(0,0,E)}){Probes.Add(P+Axis);Probes.Add(P-Axis);}
            }
            TArray<FCLSceneShapeSample> Shape;if(!SampleShape({Probes},Shape,Error)){Out.Reset();return false;}
            for(int32 I=0;I<Count;++I)
            {
                const auto& P=Query.PositionsMeters[Start+I];auto& Sample=Out[Start+I];
                if(!IsFinitePosition(P)){Sample.State=ECLSceneSampleState::InvalidQuery;continue;}
                if(!RadialPlan->Surfaces||P.IsNearlyZero())continue;
                const int32 K=I*6;const FVector3d N=FVector3d(Shape[K].FieldMeters-Shape[K+1].FieldMeters,Shape[K+2].FieldMeters-Shape[K+3].FieldMeters,Shape[K+4].FieldMeters-Shape[K+5].FieldMeters).GetSafeNormal();
                const double Radius=P.Length(),Height=Radius-RadialPlan->RadiusMeters;
                const double Slope=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector3d::DotProduct(N,P/Radius),0.,1.)));
                auto W=RadialPlan->SurfaceWeightsAt(P/Radius,Height,Slope);
                for(const auto& U:RadialPlan->Volumes->Units)
                {
                    auto Ramp=[](double X){const double T=FMath::Clamp(X,0.,1.);return T*T*(3-2*T);};
                    const double Alpha=U.Feather>0?Ramp((Radius-U.Low)/U.Feather)*Ramp((U.High-Radius)/U.Feather):double(Radius>=U.Low&&Radius<=U.High);
                    for(double& V:W.Values)V*=1-Alpha;W.Values[U.SurfaceIndex]+=Alpha;
                }
                const double H=RadialPlan->SampleHeightMeters(P);TArray<double> Base;
                RadialPlan->SampleRadialFields(MakeArrayView(&P,1),MakeArrayView(&H,1),Base);
                if(RadialPlan->Caves&&RadialPlan->Caves->Cutters)Base[0]=RadialPlan->Caves->Cutters->Compose(P,Base[0]);RadialPlan->Volumes->Compose(P,Base[0],&W);
                Sample.State=ECLSceneSampleState::Known;
                for(int32 J=0;J<RadialPlan->Surfaces->Ids.Num();++J)if(W.Values[J]>0)
                    Sample.Weights.Add({RadialPlan->Surfaces->Ids[J],RadialPlan->Surfaces->Revisions[J],W.Values[J]});
            }
        }
        return true;
    }
    Out.SetNum(Query.PositionsMeters.Num());
    for (int32 Start = 0; Start < Out.Num(); Start += SampleBatchSize)
    {
        const int32 Count = FMath::Min(SampleBatchSize, Out.Num() - Start);
        TArray<FVector> Directions;
        Directions.Reserve(Count);
        for (int32 I = 0; I < Count; ++I)
        {
            const FVector3d& P = Query.PositionsMeters[Start + I];
            Directions.Add(IsFinitePosition(P) ? P.GetSafeNormal() : FVector3d::ZeroVector);
        }
        TArray<FCLSurfaceWeights> Weights;
        if (RadialPlan->Surfaces) RadialPlan->SampleSurfaceWeights(Directions, Weights);
        for (int32 I = 0; I < Count; ++I)
        {
            const FVector3d& P = Query.PositionsMeters[Start + I];
            auto& Sample = Out[Start + I];
            if (!IsFinitePosition(P)) { Sample.State = ECLSceneSampleState::InvalidQuery; continue; }
            // Exterior attribution is undefined at the body centre. Absence of a
            // material contribution never means the shape is missing or air.
            if (!RadialPlan->Surfaces || P.IsNearlyZero()) continue;
            Sample.State = ECLSceneSampleState::Known;
            for (int32 K = 0; K < RadialPlan->Surfaces->Ids.Num(); ++K)
            {
                const double Weight = Weights[I].Values[K];
                if (Weight > 0) Sample.Weights.Add({RadialPlan->Surfaces->Ids[K], RadialPlan->Surfaces->Revisions[K], Weight});
            }
        }
    }
    return true;
}

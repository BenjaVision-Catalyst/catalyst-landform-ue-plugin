#include "Scene/CLScene.h"
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
        Error = TEXT("Legacy radial scene supports point queries only; filtered footprints are not qualified");
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
        RadialPlan->SampleHeightsMeters(Positions, Heights);
        for (int32 I = 0; I < Count; ++I)
        {
            const FVector3d& P = Query.PositionsMeters[Start + I];
            auto& Sample = Out[Start + I];
            if (!IsFinitePosition(P) || !FMath::IsFinite(Heights[I])) continue;
            // Preserve the native adapter's outer-sky clipping. Cancel large
            // values before any float conversion; the field is not an exact SDF.
            Sample.FieldMeters = P.Size() - RadialPlan->RadiusMeters - FMath::Min(Heights[I], 0.);
            Sample.State = ECLSceneSampleState::Known;
        }
    }
    return true;
}

bool FCLScene::SampleSurfaces(const FCLSceneQuery& Query, TArray<FCLSceneSurfaceSample>& Out, FString& Error) const
{
    Out.Reset();
    if (!ValidateQuery(Query, Error)) return false;
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

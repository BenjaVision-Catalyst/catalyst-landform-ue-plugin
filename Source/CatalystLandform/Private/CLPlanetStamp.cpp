#include "CLPlanetStamp.h"
#include "CLPlanetShape.h"
#include "Heightmap/VoxelHeightmap.h"
#include "Heightmap/VoxelHeightmap_Height.h"
#include "VoxelStampUtilities.h"

bool FCLSphericalHeightmapPlacement::ToChart(double Radius, const FVector& Direction, FVector2D& Chart) const
{
    FCLPlanetShape Map;
    Map.Radius = Radius;
    Map.AnchorDirection = Anchor;
    Map.ChartXAxis = TangentX;
    return Map.ToChart(Direction, Chart);
}

FVector FCLSphericalHeightmapPlacement::FromChart(double Radius, const FVector2D& Chart) const
{
    FCLPlanetShape Map;
    Map.Radius = Radius;
    Map.AnchorDirection = Anchor;
    Map.ChartXAxis = TangentX;
    return Map.FromChart(Chart);
}

double FCLPlanetStampRuntime::FSource::Weight(double Radius, const FVector& Direction, FVector2D& Chart) const
{
    if (Direction.ContainsNaN() || Direction.IsNearlyZero() ||
        FVector::DotProduct(Direction.GetSafeNormal(), Placement.Anchor) < CapCosine) return 0;
    if (!Placement.ToChart(Radius, Direction, Chart)) return 0;
    const FVector2D Local(Chart.X / Placement.Scale.X, Chart.Y / Placement.Scale.Y);
    // VPP reserves a pixel border for its native bilinear/bicubic sampler.
    const double Margin = FMath::Min(
        FMath::Min(Local.X - SafeMin.X, SafeMax.X - Local.X) * Placement.Scale.X,
        FMath::Min(Local.Y - SafeMin.Y, SafeMax.Y - Local.Y) * Placement.Scale.Y);
    const double T = FMath::Clamp(Margin / Placement.EdgeFalloff, 0.0, 1.0);
    return T * T * (3 - 2 * T);
}

bool FCLPlanetStampRuntime::Initialize(FVoxelDependencyCollector& Dependencies)
{
    check(IsInGameThread());
    if (!FMath::IsFinite(Stamp.Radius) || Stamp.Radius < 1000) return false;
    Extent = Stamp.Radius;
    if (Stamp.bUseRecipe)
    {
        FString Error;
        RecipePlan = FCLPlanetCompiler::Compile(Stamp.Recipe, Error);
        if (!RecipePlan) return false;
        Extent += FMath::Max(FMath::Abs(RecipePlan->MinimumHeightMeters), FMath::Abs(RecipePlan->MaximumHeightMeters)) * 100;
    }
    for (const FCLSphericalHeightmapPlacement& Placement : Stamp.Placements)
    {
        UVoxelHeightmap* Heightmap = Placement.Source.Heightmap;
        if (!Heightmap || !Heightmap->Height || Placement.Scale.GetMin() <= 0 || Placement.EdgeFalloff <= 0) return false;
        const auto Data = Heightmap->Height->GetData();
        if (!Data || Data->SizeX < 4 || Data->SizeY < 4 || Heightmap->ScaleXY <= 0) return false;
        FSource Source;
        Source.Placement = Placement;
        Source.Ref = FVoxelStampRef::New(Placement.Source);
        Source.Runtime = StaticCastSharedPtr<FVoxelHeightmapStampRuntime>(
            FVoxelStampRuntime::Create(GetWorld(), Source.Ref, GetComponent()));
        if (!Source.Runtime) return false;
        Source.SafeMin = (FVector2D(1, 1) - FVector2D(Data->SizeX, Data->SizeY) * .5) * Heightmap->ScaleXY;
        Source.SafeMax = (FVector2D(Data->SizeX - 2.0001, Data->SizeY - 2.0001) -
            FVector2D(Data->SizeX, Data->SizeY) * .5) * Heightmap->ScaleXY;
        const FVector2D Corner(FMath::Max(FMath::Abs(Source.SafeMin.X), FMath::Abs(Source.SafeMax.X)) * Placement.Scale.X,
            FMath::Max(FMath::Abs(Source.SafeMin.Y), FMath::Abs(Source.SafeMax.Y)) * Placement.Scale.Y);
        Source.CapCosine = FMath::Cos(FMath::Min(Corner.Size() / Stamp.Radius, UE_PI));
        // Native compressed caches store raw uint16 extrema. Normalize them
        // exactly like the native sampler before computing our volume bounds.
        const double Unit = Data->bIsUINT16 ? 1.0 / 65535 : 1.0;
        const double Low = (Data->DataMin * Unit * Data->InternalScaleZ + Data->InternalOffsetZ) * Source.Runtime->ScaleZ + Source.Runtime->OffsetZ;
        const double High = (Data->DataMax * Unit * Data->InternalScaleZ + Data->InternalOffsetZ) * Source.Runtime->ScaleZ + Source.Runtime->OffsetZ;
        if (!FMath::IsFinite(Low) || !FMath::IsFinite(High)) return false;
        Extent += FMath::Max(FMath::Abs(Low), FMath::Abs(High)) * Placement.Scale.Z +
            FMath::Abs(Placement.HeightOffset) + Placement.Source.Smoothness;
        Sources.Add(MoveTemp(Source));
    }
    if (RecipePlan) Extent = Stamp.Radius;
    return true;
}

FVoxelBox FCLPlanetStampRuntime::GetLocalBounds() const { return FVoxelBox(-Extent, Extent); }

FVoxelFloatBuffer FCLPlanetStampRuntime::SampleHeights(const FVoxelQuery& Query, const FVoxelDoubleVectorBuffer& Directions) const
{
    FVoxelFloatBuffer Heights;
    Heights.Allocate(Directions.Num());
    for (int32 I = 0; I < Heights.Num(); ++I)
        Heights.Set(I, RecipePlan ? RecipePlan->SampleHeightMeters(Directions[I]) * 100 : 0);
    for (const FSource& Source : Sources)
    {
        FVoxelDoubleVector2DBuffer Positions;
        Positions.Allocate(Directions.Num());
        FVoxelFloatBuffer Next, Weights;
        Next.Allocate(Directions.Num());
        Weights.Allocate(Directions.Num());
        bool bAny = false;
        for (int32 I = 0; I < Directions.Num(); ++I)
        {
            FVector2D Chart;
            Weights.Set(I, Source.Weight(Stamp.Radius, Directions[I], Chart));
            bAny |= Weights[I] > 0;
            Positions.X.Set(I, Weights[I] > 0 ? Chart.X : 1.e12);
            Positions.Y.Set(I, Weights[I] > 0 ? Chart.Y : 1.e12);
            Next.Set(I, Heights[I]);
        }
        if (!bAny) continue;
        FVoxelHeightTransform Transform;
        Transform.Scale = FVector2f(Source.Placement.Scale.X, Source.Placement.Scale.Y);
        Transform.ScaleZ = Source.Placement.Scale.Z;
        Transform.OffsetZ = Source.Placement.HeightOffset;
        const FVoxelHeightSparseQuery NativeQuery = FVoxelHeightSparseQuery::Create(
            Query, Next.View(), {}, FVoxelMetadataMap(), Positions, false, {});
        Source.Runtime->Apply(NativeQuery, Transform);
        for (int32 I = 0; I < Heights.Num(); ++I)
        {
            Heights.Set(I, FMath::Lerp(Heights[I], Next[I], Weights[I]));
        }
    }
    // A recipe radius is the outer sky boundary, including native stamp output.
    if (RecipePlan) for (int32 I = 0; I < Heights.Num(); ++I) Heights.Set(I, FMath::Min(Heights[I], 0.f));
    return Heights;
}

FVoxelFloatBuffer FCLPlanetStampRuntime::SampleDistances(const FVoxelQuery& Query, const FVoxelDoubleVectorBuffer& Positions) const
{
    FVoxelFloatBuffer Distances = SampleHeights(Query, Positions);
    for (int32 I = 0; I < Positions.Num(); ++I)
        Distances.Set(I, Positions[I].Size() - Stamp.Radius - Distances[I]);
    return Distances;
}

void FCLPlanetStampRuntime::Apply(const FVoxelVolumeBulkQuery& Query, const FVoxelVolumeTransform& Transform) const
{
    if (!AffectShape()) return;
    FVoxelStampUtilities::ApplyDistances(*this, Query, Transform,
        SampleDistances(Query.Query, FVoxelStampUtilities::ComputePositions(Query, Transform)));
}

void FCLPlanetStampRuntime::Apply(const FVoxelVolumeSparseQuery& Query, const FVoxelVolumeTransform& Transform) const
{
    if (!AffectShape()) return;
    FVoxelStampUtilities::ApplyDistances(*this, Query, Transform,
        SampleDistances(Query.Query, FVoxelStampUtilities::ComputePositions(Query, Transform)), true, nullptr);
}

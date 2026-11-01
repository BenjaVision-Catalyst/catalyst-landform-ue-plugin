#include "CLPlanetStamp.h"
#include "CLPlanetShape.h"
#include "Heightmap/VoxelHeightmap.h"
#include "Heightmap/VoxelHeightmap_Height.h"
#include "VoxelStampUtilities.h"
#include "Surface/VoxelSurfaceTypeBlendBuilder.h"

bool FCLSphericalHeightmapPlacement::ToChart(double Radius, const FVector& Direction, FVector2D& Chart) const
{
    if (bPlanar)
    {
        const FVector2D D = FVector2D(Direction)-Center;
        Chart = FVector2D(D.X*TangentX.X+D.Y*TangentX.Y,-D.X*TangentX.Y+D.Y*TangentX.X);
        return true;
    }
    FCLPlanetShape Map;
    Map.Radius = ChartRadius > 0 ? ChartRadius : Radius;
    Map.AnchorDirection = Anchor;
    Map.ChartXAxis = TangentX;
    return Map.ToChart(Direction, Chart);
}

FVector FCLSphericalHeightmapPlacement::FromChart(double Radius, const FVector2D& Chart) const
{
    FCLPlanetShape Map;
    Map.Radius = ChartRadius > 0 ? ChartRadius : Radius;
    Map.AnchorDirection = Anchor;
    Map.ChartXAxis = TangentX;
    return Map.FromChart(Chart);
}

double FCLPlanetStampRuntime::FSource::Weight(double Radius, const FVector& Direction, FVector2D& Chart) const
{
    return Direction.ContainsNaN() || (!Placement.bPlanar && Direction.IsNearlyZero()) ? 0 : WeightNormalized(Radius, Placement.bPlanar ? Direction : Direction.GetSafeNormal(), Chart);
}

double FCLPlanetStampRuntime::FSource::WeightNormalized(double Radius, const FVector& Direction, FVector2D& Chart) const
{
    if (Direction.ContainsNaN() || (!Placement.bPlanar && (Direction.IsNearlyZero() ||
        FVector::DotProduct(Direction, Placement.Anchor) < CapCosine))) return 0;
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
        Source.CapCosine = FMath::Cos(FMath::Min(Corner.Size() / (Placement.ChartRadius > 0 ? Placement.ChartRadius : Stamp.Radius), UE_PI));
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
    TArray<FVector> UnitDirections;
    TArray<double> Samples;
    if (RecipePlan)
    {
        UnitDirections.SetNumUninitialized(Directions.Num());
        for (int32 I = 0; I < Directions.Num(); ++I) UnitDirections[I] = Directions[I];
        RecipePlan->SampleHeightsMeters(UnitDirections, Samples);
    }
    for (int32 I = 0; I < Heights.Num(); ++I)
        Heights.Set(I, RecipePlan ? Samples[I] * 100 : 0);
    ApplySources(Query, Directions, Heights);
    // A recipe radius is the outer sky boundary, including native stamp output.
    if (RecipePlan) for (int32 I = 0; I < Heights.Num(); ++I) Heights.Set(I, FMath::Min(Heights[I], 0.f));
    return Heights;
}

void FCLPlanetStampRuntime::ApplySources(const FVoxelQuery& Query, const FVoxelDoubleVectorBuffer& Directions, FVoxelFloatBuffer& Heights) const
{
    TArray<FVector> Units;Units.SetNumUninitialized(Directions.Num());
    for(int32 I=0;I<Directions.Num();++I)Units[I]=Stamp.bPlanarSources?Directions[I]:Directions[I].GetSafeNormal();
    for(const FSource& Source:Sources)
    {
        TArray<int32> Indices;TArray<FVector2D> Charts;TArray<float> Weights;
        for(int32 I=0;I<Units.Num();++I)
        {
            FVector2D Chart;const double W=Source.WeightNormalized(Stamp.Radius,Units[I],Chart);
            if(W>0){Indices.Add(I);Charts.Add(Chart);Weights.Add(W);}
        }
        if(Indices.IsEmpty())continue;
        FVoxelDoubleVector2DBuffer Positions;Positions.Allocate(Indices.Num());
        FVoxelFloatBuffer Next;Next.Allocate(Indices.Num());
        for(int32 I=0;I<Indices.Num();++I){Positions.Set(I,Charts[I]);Next.Set(I,Heights[Indices[I]]);}
        FVoxelHeightTransform Transform;
        Transform.Scale=FVector2f(Source.Placement.Scale.X,Source.Placement.Scale.Y);
        Transform.ScaleZ=Source.Placement.Scale.Z;Transform.OffsetZ=Source.Placement.HeightOffset;
        const auto NativeQuery=FVoxelHeightSparseQuery::Create(Query,Next.View(),{},FVoxelMetadataMap(),Positions,false,{});
        Source.Runtime->Apply(NativeQuery,Transform);
        for(int32 I=0;I<Indices.Num();++I)Heights.Set(Indices[I],FMath::Lerp(Heights[Indices[I]],Next[I],Weights[I]));
    }
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
    const bool Surfaces=AffectSurfaceType()&&Query.bQuerySurfaceTypes&&RecipePlan&&RecipePlan->Surfaces;
    if(!AffectShape()&&!Surfaces)return;
    const auto Positions=FVoxelStampUtilities::ComputePositions(Query,Transform);FVoxelFloatBuffer Alphas;
    FVoxelStampUtilities::ApplyDistances(*this,Query,Transform,SampleDistances(Query.Query,Positions),AffectShape(),Surfaces?&Alphas:nullptr);
    if(Surfaces)FVoxelStampUtilities::ApplySurfaceTypes(Query,SampleSurfaceTypes(Query.Query,Positions),Alphas);
}

FVoxelSurfaceTypeBlendBuffer FCLPlanetStampRuntime::SampleSurfaceTypes(const FVoxelQuery& Query,const FVoxelDoubleVectorBuffer& Positions) const
{
    FVoxelSurfaceTypeBlendBuffer Out;Out.Allocate(Positions.Num());
    if(!RecipePlan||!RecipePlan->Surfaces){for(int32 I=0;I<Positions.Num();++I)Out.Set(I,FVoxelSurfaceTypeBlend());return Out;}
    TArray<FVector> D;D.SetNumUninitialized(Positions.Num());for(int32 I=0;I<D.Num();++I)D[I]=Positions[I].GetSafeNormal();
    TArray<FCLSurfaceWeights> Weights;
    if(Sources.IsEmpty())RecipePlan->SampleSurfaceWeights(D,Weights);
    else
    {
        Weights.SetNumZeroed(D.Num());
        for(int32 Start=0;Start<D.Num();Start+=512)
        {
            const int32 N=FMath::Min(512,D.Num()-Start);FVoxelDoubleVectorBuffer Probe;Probe.Allocate(N*5);
            for(int32 I=0;I<N;++I){const FVector P=D[Start+I],X=FVector::CrossProduct(P,FMath::Abs(P.Z)<.8?FVector::ZAxisVector:FVector::XAxisVector).GetSafeNormal(),Y=FVector::CrossProduct(P,X);Probe.Set(5*I,P);Probe.Set(5*I+1,(P+X*.0001).GetSafeNormal());Probe.Set(5*I+2,(P-X*.0001).GetSafeNormal());Probe.Set(5*I+3,(P+Y*.0001).GetSafeNormal());Probe.Set(5*I+4,(P-Y*.0001).GetSafeNormal());}
            const auto H=SampleHeights(Query,Probe);
            for(int32 I=0;I<N;++I){const int32 K=I*5;auto Point=[&](int32 J){return Probe[K+J]*(Stamp.Radius+H[K+J]);};const auto Normal=FVector::CrossProduct(Point(1)-Point(2),Point(3)-Point(4)).GetSafeNormal();const double Slope=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FMath::Abs(FVector::DotProduct(Normal,D[Start+I])),0.,1.)));Weights[Start+I]=RecipePlan->SurfaceWeightsAt(D[Start+I],H[K]/100.,Slope);}
        }
    }
    FVoxelSurfaceTypeBlendBuilder Builder;
    for(int32 I=0;I<Weights.Num();++I)
    {
        Builder.Reset();for(int32 J=0;J<RecipePlan->Surfaces->NativeTypes.Num();++J)if(Weights[I].Values[J]>0)Builder.AddLayer(RecipePlan->Surfaces->NativeTypes[J],Weights[I].Values[J]);
        FVoxelSurfaceTypeBlend Blend;Builder.Build(Blend);Out.Set(I,Blend);
    }return Out;
}

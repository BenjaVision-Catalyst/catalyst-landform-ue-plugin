#include "CLPlanetStamp.h"
#include "VoxelQuery.h"
#include "Misc/ScopeExit.h"
#include "VoxelLinearColorMetadata.h"
#include "UObject/StrongObjectPtr.h"
#include "CLPlanetShape.h"
#include "Heightmap/VoxelHeightmap.h"
#include "Heightmap/VoxelHeightmap_Height.h"
#include "VoxelStampUtilities.h"
#include "Surface/VoxelSurfaceTypeBlendBuilder.h"
#include "Scene/CLBodyFrame.h"
#include "Scene/CLScene.h"
#include "Shape/VoxelCubeShape.h"
#include "Shape/VoxelShapeFunctionLibrary.h"
#include "VoxelBlendModeUtilities.h"

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

namespace {
FVoxelSurfaceTypeBlendBuffer NativeMaterialTypes(TConstArrayView<FCLMaterialSample> Samples,const FCLCompiledSurfaceLibrary& Library)
{
    FVoxelSurfaceTypeBlendBuffer Out;Out.Allocate(Samples.Num());FVoxelSurfaceTypeBlendBuilder Builder;
    for(int32 I=0;I<Samples.Num();++I)
    {
        Builder.Reset();for(int32 K=0;K<Library.NativeTypes.Num();++K)if(Samples[I].Surfaces.Values[K]>0)Builder.AddLayer(Library.NativeTypes[K],Samples[I].Surfaces.Values[K]);
        FVoxelSurfaceTypeBlend Blend;Builder.Build(Blend);Out.Set(I,Blend);
    }
    return Out;
}
}

class FCLRockMetadataBinding
{
public:
    explicit FCLRockMetadataBinding(UVoxelLinearColorMetadata* Value):Asset(Value){}
    TStrongObjectPtr<UVoxelLinearColorMetadata> Asset;
};

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
    NativeShapes=GetDefault<UVoxelShapeFunctionLibrary>();
    RuntimeMetadata=Stamp.MetadataOverrides.CreateRuntime();
    if (!FMath::IsFinite(Stamp.Radius) || Stamp.Radius < 1000) return false;
    if(Stamp.Window.bEnabled)
    {
        FString Error;
        if(!Stamp.Window.Validate(Error)||!Stamp.bUseRecipe||Stamp.bPlanarSources||!Stamp.Placements.IsEmpty()||Stamp.bSectionPreview)
        return false;
        SamplingFrame=Stamp.Window.Frame();
    }
    if(Stamp.bSectionPreview&&(!FMath::IsFinite(Stamp.SectionWidthMeters)||Stamp.SectionWidthMeters<1||!FMath::IsFinite(Stamp.SectionReferenceRadiusMeters)||Stamp.SectionReferenceRadiusMeters<=0))return false;
    if(Stamp.bCaveInspectionPreview&&(Stamp.bSectionPreview||Stamp.Window.bEnabled||!Stamp.bUseRecipe))return false;
    Extent = Stamp.Radius;
    if (Stamp.bUseRecipe)
    {
        FString Error;
        RecipePlan = Stamp.CompiledRecipe ? Stamp.CompiledRecipe : FCLPlanetCompiler::Compile(Stamp.Recipe, Error);
        if (!RecipePlan) return false;
        if(RecipePlan->Rock||(RecipePlan->MaterialLayers&&RecipePlan->MaterialLayers->HasDeposits()))
        {
            auto* Asset=LoadObject<UVoxelLinearColorMetadata>(nullptr,FCLCompiledRock::PropertiesAssetPath());if(!Asset)return false;
            RockBinding=MakeShared<FCLRockMetadataBinding>(Asset);RockMetadata=FVoxelMetadataRef(Asset);
        }
        Extent += FMath::Max(FMath::Abs(RecipePlan->MinimumHeightMeters), FMath::Abs(RecipePlan->MaximumHeightMeters)) * 100;
    }
    if(Stamp.bCaveInspectionPreview&&(!RecipePlan||!RecipePlan->Caves||!RecipePlan->Caves->Cutters))return false;
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
    if (RecipePlan)
    {
        Extent = Stamp.Radius;
        if (Sources.IsEmpty() && !Stamp.bPlanarSources && RecipePlan->Coordinates != ECLTerrainCoordinates::LocalPatch &&
            FMath::IsNearlyEqual(Stamp.Radius, RecipePlan->RadiusMeters * 100., 1.e-6))
        {
            FString Error;
            Scene = FCLScene::FromPlanet(RecipePlan, Error);
            if (!Scene) return false;
        }
        if((RecipePlan->MaterialLayers||RecipePlan->Volumes||(RecipePlan->Caves&&RecipePlan->Caves->Cutters)||Stamp.Window.bEnabled)&&!Scene)return false; // Shared body-space composition requires a whole-body scene.
    }
    if(Stamp.bCaveInspectionPreview&&Stamp.CaveInspectionDomain.IsValid())
    {
        const auto* Domain=RecipePlan->Caves->InspectionDomains.FindByPredicate([&](const auto& D){return D.Id==Stamp.CaveInspectionDomain;});
        if(!Domain)return false;InspectionCutters=RecipePlan->Caves->Cutters->Select(Domain->Networks);
    }
    else if(Stamp.bCaveInspectionPreview&&Stamp.CaveInspectionNetwork.IsValid())
    {
        InspectionCutters=RecipePlan->Caves->Cutters->Select(Stamp.CaveInspectionNetwork);
        if(!InspectionCutters->Bounds().IsValid)return false;
    }
    return true;
}

FVector FCLPlanetStampRuntime::SectionToBodyMeters(const FVector& P,double Reference)
{
    const double Arc=FVector2D(P.X,P.Y).Size(),A=Arc/Reference;
    const double S=Arc>1.e-10?FMath::Sin(A)/Arc:1./Reference;
    return FVector(P.X*S,P.Y*S,FMath::Cos(A))*P.Z;
}
FVoxelBox FCLPlanetStampRuntime::GetLocalBounds() const
{
    if(Stamp.bCaveInspectionPreview&&RecipePlan&&RecipePlan->Caves&&RecipePlan->Caves->Cutters)
    {const auto& Cutters=InspectionCutters?InspectionCutters:RecipePlan->Caves->Cutters;const auto Bounds=Cutters->Bounds();return FVoxelBox(Bounds.Min*100,Bounds.Max*100);}
    if(Stamp.Window.bEnabled)return FVoxelBox(Stamp.Window.BoundsCm());
    if(Stamp.bSectionPreview){const double H=Stamp.SectionWidthMeters*50.;return FVoxelBox(FVector(-H,-H,0),FVector(H,H,Extent));}
    return FVoxelBox(-Extent,Extent);
}

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
    if(Stamp.bSectionPreview)
    {
        FVoxelFloatBuffer Distances;Distances.Allocate(Positions.Num());
        TArray<FVector3d> Body;TArray<FVector> Local;Body.Reserve(Positions.Num());Local.Reserve(Positions.Num());
        FVoxelDoubleVectorBuffer ClipPositions;ClipPositions.Allocate(Positions.Num());
        for(int32 I=0;I<Positions.Num();++I)
        {
            const FVector P=Positions[I]*.01;Body.Add(SectionToBodyMeters(P,Stamp.SectionReferenceRadiusMeters));Local.Add(FVector(P.X,P.Y,0));
            ClipPositions.Set(I,Positions[I]-FVector(0,0,Extent*.5));
        }
        if(Scene)
        {
            TArray<FCLSceneShapeSample> S;FString Error;checkf(Scene->SampleShape({Body},S,Error),TEXT("Section scene query failed: %s"),*Error);
            for(int32 I=0;I<Positions.Num();++I)Distances.Set(I,S[I].FieldMeters*100);
        }
        else
        {
            TArray<double> H;
            if(RecipePlan&&RecipePlan->Coordinates==ECLTerrainCoordinates::LocalPatch)RecipePlan->SampleLocalHeightsMeters(Local,H);
            else {TArray<FVector> D;for(const auto& P:Body)D.Add(P);RecipePlan->SampleHeightsMeters(D,H);}
            for(int32 I=0;I<Positions.Num();++I)Distances.Set(I,Positions[I].Z-Stamp.Radius-H[I]*100);
        }
        FVoxelCubeShape Clip;Clip.Size=FVector(Stamp.SectionWidthMeters*100,Stamp.SectionWidthMeters*100,Extent);
        FVoxelFloatBuffer Box;Box.Allocate(Positions.Num());Clip.Sample(Box.View(),ClipPositions);
        for(int32 I=0;I<Positions.Num();++I)Distances.Set(I,FVoxelBlendModeUtilities::ComputeDistance(FVoxelVolumeTransform(),Distances[I],Box[I],EVoxelVolumeBlendMode::Intersect,true,0,nullptr));
        return Distances;
    }
    if (Scene)
    {
        FVoxelFloatBuffer Distances;
        Distances.Allocate(Positions.Num());
        const FCLBodyFrame& Frame=SamplingFrame;
        for (int32 Start = 0; Start < Positions.Num(); Start += 512)
        {
            const int32 Count = FMath::Min(512, Positions.Num() - Start);
            TArray<FVector3d> BodyPositions;
            BodyPositions.Reserve(Count);
            for (int32 I = 0; I < Count; ++I)
            {
                const FVector P=Frame.NativeToBodyMeters(Positions[Start+I]);
                BodyPositions.Add(P);
            }
            TArray<FCLSceneShapeSample> Samples;
            FString Error;
            const bool bSampled = Stamp.bCaveInspectionPreview?Scene->SampleCaveVoid({BodyPositions},Samples,Error,InspectionCutters.Get()):Scene->SampleShape({BodyPositions}, Samples, Error);
            checkf(bSampled, TEXT("Compiled point scene query failed: %s"), *Error);
            for (int32 I = 0; I < Count; ++I)
            {
                check(Samples[I].State == ECLSceneSampleState::Known);
                Distances.Set(Start + I, Samples[I].FieldMeters * 100.);
            }
        }
        return Distances;
    }
    const auto Heights=SampleHeights(Query,Positions);
    FVoxelDoubleBuffer Radii;Radii.Allocate(Positions.Num());
    for(int32 I=0;I<Positions.Num();++I)Radii.Set(I,Stamp.Radius+double(Heights[I]));
    return NativeShapes->MakeSphere(Positions,Radii);
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
    const bool Rock=AffectMetadata()&&RockMetadata&&Query.MetadatasToQuery.Contains(RockMetadata);
    const bool Metadata=AffectMetadata()&&Query.MetadatasToQuery.Num()>0&&RuntimeMetadata->ShouldCompute(Query);
    if(!AffectShape()&&!Surfaces&&!Metadata&&!Rock)return;
    const auto Positions=FVoxelStampUtilities::ComputePositions(Query,Transform);FVoxelFloatBuffer Alphas;
    FVoxelStampUtilities::ApplyDistances(*this,Query,Transform,SampleDistances(Query.Query,Positions),AffectShape(),(Surfaces||Metadata||Rock)?&Alphas:nullptr);
    TArray<FCLMaterialSample> Materials;
    if(Scene&&(Surfaces||Rock))
    {
        TArray<FVector3d> Body;Body.Reserve(Positions.Num());
        for(int32 I=0;I<Positions.Num();++I){const FVector P=SamplingFrame.NativeToBodyMeters(Positions[I]);Body.Add(Stamp.bSectionPreview?SectionToBodyMeters(P,Stamp.SectionReferenceRadiusMeters):P);}
        FString Error;const bool Sampled=Scene->SampleMaterials({Body},Materials,Error,Surfaces);checkf(Sampled,TEXT("Material composition failed: %s"),*Error);if(!Sampled)return;
    }
    if(Surfaces)FVoxelStampUtilities::ApplySurfaceTypes(Query,Scene?NativeMaterialTypes(Materials,*RecipePlan->Surfaces):SampleSurfaceTypes(Query.Query,Positions),Alphas);
    if(Metadata)FVoxelStampUtilities::ApplyMetadatas(Query,*RuntimeMetadata,{},Alphas);
    if(Rock)
    {
        FVoxelLinearColorBuffer Values;Values.Allocate(Positions.Num());
        for(int32 I=0;I<Positions.Num();++I)
        {
            FVector P=SamplingFrame.NativeToBodyMeters(Positions[I]);if(Stamp.bSectionPreview)P=SectionToBodyMeters(P,Stamp.SectionReferenceRadiusMeters);
            Values.Set(I,Scene?Materials[I].Properties:RecipePlan->Rock->Properties(RecipePlan->RockPositionMeters(P)));
        }
        RockMetadata.IndirectBlend(Query.Indirection,Values,Alphas,Query.IndirectMetadata.FindChecked(RockMetadata));
    }
}

FVoxelSurfaceTypeBlendBuffer FCLPlanetStampRuntime::SampleSurfaceTypes(const FVoxelQuery& Query,const FVoxelDoubleVectorBuffer& Positions) const
{
    FVoxelSurfaceTypeBlendBuffer Out;Out.Allocate(Positions.Num());
    if(!RecipePlan||!RecipePlan->Surfaces){for(int32 I=0;I<Positions.Num();++I)Out.Set(I,FVoxelSurfaceTypeBlend());return Out;}
    if (Scene)
    {
        TArray<FVector3d> Body;Body.Reserve(Positions.Num());
        for(int32 I=0;I<Positions.Num();++I){const FVector P=SamplingFrame.NativeToBodyMeters(Positions[I]);Body.Add(Stamp.bSectionPreview?SectionToBodyMeters(P,Stamp.SectionReferenceRadiusMeters):P);}
        TArray<FCLMaterialSample> Samples;FString Error;
        const bool Sampled=Scene->SampleMaterials({Body},Samples,Error);checkf(Sampled,TEXT("Compiled material query failed: %s"),*Error);if(!Sampled)return Out;
        return NativeMaterialTypes(Samples,*RecipePlan->Surfaces);
    }
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

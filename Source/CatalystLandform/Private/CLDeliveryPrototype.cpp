#include "VoxelQuery.h"
#include "CLDeliveryPrototype.h"
#include "Model/CLSpatialCave.h"
#include "CLPlanet.h"
#include "VoxelStampComponent.h"
#include "VoxelStampUtilities.h"
#include "VoxelBlendModeUtilities.h"
#include "Graphs/VoxelVolumeGraph.h"
#include "Graphs/VoxelVolumeGraphStamp.h"
#include "VoxelGraphQuery.h"
#include "VoxelGraphPositionParameter.h"
#include "Graphs/VoxelStampGraphParameters.h"
#if WITH_EDITOR
#include "VoxelTerminalGraph.h"
#include "VoxelTerminalGraphRuntime.h"
#include "VoxelGraphTracker.h"
#endif

void FCLDeliveryCounters::Record(int32 N,int32 H,int32 LOD,uint64 Elapsed)
{
    Calls.fetch_add(1,std::memory_order_relaxed);Points.fetch_add(N,std::memory_order_relaxed);HeightPoints.fetch_add(H,std::memory_order_relaxed);Cycles.fetch_add(Elapsed,std::memory_order_relaxed);LODPoints[FMath::Clamp(LOD,0,15)].fetch_add(N,std::memory_order_relaxed);
}
bool FCLDeliveryBinding::IsValid() const
{
    if(!Source||!Source->Scene||!Source->RecipePlan||!Source->Sources.IsEmpty())return false;
    const auto& S=Source->Stamp;const auto& P=*Source->RecipePlan;
    return !S.Window.bEnabled&&!S.bSectionPreview&&!S.bCaveInspectionPreview&&!S.bPlanarSources&&
        P.Caves&&P.Caves->Cutters&&P.Rock&&P.Surfaces&&P.SourceRecipe&&(P.SourceRecipe->Caves.AlgorithmVersion==7||P.SourceRecipe->Caves.AlgorithmVersion==8);
}
FVoxelFloatBuffer FCLDeliveryBinding::Distances(const FVoxelQuery& Query,const FVoxelDoubleVectorBuffer& Positions) const
{
    check(IsValid());const auto& P=*Source->RecipePlan;int32 HeightCount=0;const uint64 Started=FPlatformTime::Cycles64();
    FVoxelFloatBuffer Result;Result.Allocate(Positions.Num());
    for(int32 Start=0;Start<Positions.Num();Start+=512)
    {
        const int32 N=FMath::Min(512,Positions.Num()-Start);TArray<FVector> Points;Points.Reserve(N);
        for(int32 I=0;I<N;++I)Points.Add(Source->SamplingFrame.NativeToBodyMeters(Positions[Start+I]));
        TArray<double> Heights,Fields;
        if(P.Caves->Spatial){int32 H=0;P.Caves->Spatial->SamplePlanet(P,Points,Fields,&H);HeightCount+=H;}
        else
        {
        HeightCount+=N;P.SampleHeightsMeters(Points,Heights);P.SampleRadialFields(Points,Heights,Fields);
        // Keep the native ordered blend, including its zero-smooth floating-point
        // interpolation. Algebraic max/height pruning is not bit-identical to it.
        P.Caves->Cutters->ComposeBatchOptimized(Points,Fields);
        if(P.Volumes)P.Volumes->ComposeBatch(Points,Fields);
        }
        for(int32 I=0;I<N;++I)Result.Set(Start+I,Fields[I]*100.);
    }
    if(Counters)Counters->Record(Positions.Num(),HeightCount,Query.LOD,FPlatformTime::Cycles64()-Started);
    return Result;
}

namespace
{
FVoxelLinearColorBuffer Properties(const FCLDeliveryBinding& B,const FVoxelDoubleVectorBuffer& Positions)
{
    FVoxelLinearColorBuffer Values;Values.Allocate(Positions.Num());const auto& P=*B.Source->RecipePlan;
    for(int32 I=0;I<Positions.Num();++I)Values.Set(I,P.Rock->Properties(P.RockPositionMeters(Positions[I]*.01)));
    return Values;
}
}
bool FCLDeliveryStampRuntime::Initialize(FVoxelDependencyCollector&){return Stamp.Binding.IsValid();}
FVoxelBox FCLDeliveryStampRuntime::GetLocalBounds()const{return Stamp.Binding.Source->GetLocalBounds();}
void FCLDeliveryStampRuntime::Apply(const FVoxelVolumeBulkQuery& Query,const FVoxelVolumeTransform& Transform)const
{
    if(AffectShape())FVoxelStampUtilities::ApplyDistances(*this,Query,Transform,Stamp.Binding.Distances(Query.Query,FVoxelStampUtilities::ComputePositions(Query,Transform)));
}
void FCLDeliveryStampRuntime::Apply(const FVoxelVolumeSparseQuery& Query,const FVoxelVolumeTransform& Transform)const
{
    const auto& Source=*Stamp.Binding.Source;
    const bool Surfaces=AffectSurfaceType()&&Query.bQuerySurfaceTypes&&Source.RecipePlan->Surfaces;
    const bool Rock=AffectMetadata()&&Source.RockMetadata&&Query.MetadatasToQuery.Contains(Source.RockMetadata);
    const bool Metadata=AffectMetadata()&&Query.MetadatasToQuery.Num()>0&&Source.RuntimeMetadata->ShouldCompute(Query);
    if(!AffectShape()&&!Surfaces&&!Rock&&!Metadata)return;
    const auto Positions=FVoxelStampUtilities::ComputePositions(Query,Transform);FVoxelFloatBuffer Alphas;
    FVoxelStampUtilities::ApplyDistances(*this,Query,Transform,Stamp.Binding.Distances(Query.Query,Positions),AffectShape(),(Surfaces||Rock||Metadata)?&Alphas:nullptr);
    if(Surfaces)FVoxelStampUtilities::ApplySurfaceTypes(Query,Source.SampleSurfaceTypes(Query.Query,Positions),Alphas);
    if(Metadata)FVoxelStampUtilities::ApplyMetadatas(Query,*Source.RuntimeMetadata,{},Alphas);
    if(Rock)Source.RockMetadata.IndirectBlend(Query.Indirection,Properties(Stamp.Binding,Positions),Alphas,Query.IndirectMetadata.FindChecked(Source.RockMetadata));
}
void FCLNode_DeliveryDistance::Compute(FVoxelGraphQuery Query)const
{
    const TValue<FCLDeliveryBinding> Binding=BindingPin.Get(Query);
    VOXEL_GRAPH_WAIT(Binding)
    {
        if(!Binding->IsValid()){DistancePin.Set(Query,FVoxelFloatBuffer(MAX_flt));return;}
        const auto* Position=Query->FindParameter<FVoxelGraphParameters::FPosition3D>();
        if(!ensure(Position))return;
        const auto Positions=Position->GetLocalPosition_Double(Query);
        DistancePin.Set(Query,Binding->Distances(Query->FindParameter<FVoxelGraphParameters::FQuery>()->Query,Positions));
    };
}
void FCLNode_DeliverySurfaceType::Compute(FVoxelGraphQuery Query)const
{
    const TValue<FCLDeliveryBinding> Binding=BindingPin.Get(Query);
    VOXEL_GRAPH_WAIT(Binding)
    {
        if(!Binding->IsValid()){SurfaceTypePin.Set(Query,FVoxelSurfaceTypeBlendBuffer(FVoxelSurfaceTypeBlend()));return;}
        const auto* Position=Query->FindParameter<FVoxelGraphParameters::FPosition3D>();
        if(!ensure(Position))return;
        const auto Positions=Position->GetLocalPosition_Double(Query);
        SurfaceTypePin.Set(Query,Binding->Source->SampleSurfaceTypes(Query->FindParameter<FVoxelGraphParameters::FQuery>()->Query,Positions));
    };
}
void FCLNode_DeliveryRockProperties::Compute(FVoxelGraphQuery Query)const
{
    const TValue<FCLDeliveryBinding> Binding=BindingPin.Get(Query);
    VOXEL_GRAPH_WAIT(Binding)
    {
        if(!Binding->IsValid()){RockPropertiesPin.Set(Query,FVoxelLinearColorBuffer(FLinearColor::Black));return;}
        const auto* Position=Query->FindParameter<FVoxelGraphParameters::FPosition3D>();
        if(!ensure(Position))return;
        const auto Positions=Position->GetLocalPosition_Double(Query);
        RockPropertiesPin.Set(Query,Properties(*Binding,Positions));
    };
}
void FCLNode_DeliveryBounds::Compute(FVoxelGraphQuery Query)const
{
    const TValue<FCLDeliveryBinding> Binding=BindingPin.Get(Query);
    VOXEL_GRAPH_WAIT(Binding)
    {
        if(!Binding->IsValid()){BoundsPin.Set(Query,FVoxelBox(-1,1));return;}
        BoundsPin.Set(Query,Binding->Source->GetLocalBounds());
    };
}
FGuid CLDeliveryPrototype::ParameterGuid(){return FGuid(0xC1DE1101,0xC0DE4000,0xA1000000,0x00000001);}
bool CLDeliveryPrototype::Install(ACLPlanet& Planet,const FString& Backend,UVoxelVolumeGraph* Graph,FString& Error,TSharedPtr<FCLDeliveryCounters>* Counters)
{
    Error.Reset();if(Counters)Counters->Reset();
    if(Planet.IsRuntimeCreated()||!Planet.GetSamplingRuntime()){Error=TEXT("Install delivery after recipe activation and before native runtime creation");return false;}
    auto Stats=MakeShared<FCLDeliveryCounters>();if(Counters)*Counters=Stats;
    if(Backend==TEXT("legacy")){auto S=Planet.GetSamplingRuntime()->Stamp;S.DeliveryCounters=Stats;Planet.TerrainStamp->SetStamp(S);return true;}
    if(Backend!=TEXT("cpp")&&Backend!=TEXT("graph")){Error=TEXT("Unknown delivery backend");return false;}
    FCLDeliveryBinding B;B.Snapshot=FGuid::NewGuid();B.Source=Planet.GetSamplingRuntime();B.Counters=Stats;
    if(!B.IsValid())
    {Error=TEXT("Delivery prototype requires an unframed profile-7 or profile-8 rock planet");return false;}
    auto Copy=[&](FVoxelVolumeStamp& S)
    {
        // Copy every native base setting on the game thread, without copying
        // the source virtual type or its shared ownership bookkeeping.
        for(TFieldIterator<FProperty> It(FVoxelVolumeStamp::StaticStruct());It;++It)It->CopyCompleteValue_InContainer(&S,&B.Source->Stamp);
    };
    if(Backend==TEXT("cpp")){FCLDeliveryStamp S;Copy(S);S.Binding=B;Planet.TerrainStamp->SetStamp(S);}
    else
    {
        if(!Graph){Error=TEXT("Missing native delivery graph asset");return false;}
#if WITH_EDITOR
        // Finish native graph load/compile notifications before a terrain owner
        // subscribes; otherwise queued editor notifications can regenerate it.
        Graph->GetMainTerminalGraph().GetRuntime().EnsureIsCompiled();GVoxelGraphTracker->Flush();
#endif
        FVoxelVolumeGraphStamp S;Copy(S);S.Graph=Graph;
        auto& Override=S.ParameterOverrides.GuidToValueOverride.FindOrAdd(ParameterGuid());Override.bEnable=true;Override.CachedName=TEXT("Delivery");Override.Value=FVoxelPinValue::Make(B);
        Planet.TerrainStamp->SetStamp(S);
    }
    return true;
}

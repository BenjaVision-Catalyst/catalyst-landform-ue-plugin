#pragma once
#include "CLPlanetStamp.h"
#include "VoxelNode.h"
#include "Buffer/VoxelFloatBuffers.h"
#include <atomic>
#include "CLDeliveryPrototype.generated.h"

struct CATALYSTLANDFORM_API FCLDeliveryCounters
{
    std::atomic<uint64> Calls{0},Points{0},HeightPoints{0},Cycles{0};
    std::atomic<uint64> LODPoints[16]{};
    std::atomic<uint64> SurfaceCalls{0},SurfacePoints{0},SurfaceCycles{0};
    std::atomic<uint64> PropertyCalls{0},PropertyPoints{0},PropertyCycles{0};
    void Record(int32 Num,int32 Heights,int32 LOD,uint64 Elapsed);
};

/** Activation-only immutable snapshot. The reflected identity prevents graph cache aliasing. */
USTRUCT()
struct CATALYSTLANDFORM_API FCLDeliveryBinding
{
    GENERATED_BODY()
    UPROPERTY() FGuid Snapshot;
    TSharedPtr<const FCLPlanetStampRuntime> Source;
    TSharedPtr<FCLDeliveryCounters> Counters;
    FVoxelFloatBuffer Distances(const FVoxelQuery& Query,const FVoxelDoubleVectorBuffer& Positions) const;
    bool IsValid() const;
};

template<> struct TStructOpsTypeTraits<FCLDeliveryBinding> : TStructOpsTypeTraitsBase2<FCLDeliveryBinding>
{
    enum { WithCopy=true };
};

/** Opt-in prototype; native VPP still owns all sampling, meshing, collision and edits. */
USTRUCT()
struct CATALYSTLANDFORM_API FCLDeliveryStamp final : public FVoxelVolumeStamp
{
    GENERATED_BODY()
    GENERATED_VIRTUAL_STRUCT_BODY()
    UPROPERTY() FCLDeliveryBinding Binding;
};
USTRUCT()
struct CATALYSTLANDFORM_API FCLDeliveryStampRuntime : public FVoxelVolumeStampRuntime
{
    GENERATED_BODY()
    GENERATED_VOXEL_RUNTIME_STAMP_BODY(FCLDeliveryStamp)
    virtual bool Initialize(FVoxelDependencyCollector& Dependencies) override;
    virtual FVoxelBox GetLocalBounds() const override;
    virtual void Apply(const FVoxelVolumeBulkQuery& Query,const FVoxelVolumeTransform& Transform) const override;
    virtual void Apply(const FVoxelVolumeSparseQuery& Query,const FVoxelVolumeTransform& Transform) const override;
};

/** Separate output nodes keep native bounds and shape evaluation independent of attributes. */
USTRUCT(meta=(Category="Landform|Prototype"))
struct CATALYSTLANDFORM_API FCLNode_DeliveryDistance : public FVoxelNode
{
    GENERATED_BODY()
    GENERATED_VOXEL_NODE_BODY()
    VOXEL_INPUT_PIN(FCLDeliveryBinding, Binding, nullptr);
    VOXEL_OUTPUT_PIN(FVoxelFloatBuffer, Distance);
    virtual void Compute(FVoxelGraphQuery Query) const override;
};
USTRUCT(meta=(Category="Landform|Prototype"))
struct CATALYSTLANDFORM_API FCLNode_DeliverySurfaceType : public FVoxelNode
{
    GENERATED_BODY()
    GENERATED_VOXEL_NODE_BODY()
    VOXEL_INPUT_PIN(FCLDeliveryBinding, Binding, nullptr);
    VOXEL_OUTPUT_PIN(FVoxelSurfaceTypeBlendBuffer, SurfaceType);
    virtual void Compute(FVoxelGraphQuery Query) const override;
};
USTRUCT(meta=(Category="Landform|Prototype"))
struct CATALYSTLANDFORM_API FCLNode_DeliveryRockProperties : public FVoxelNode
{
    GENERATED_BODY()
    GENERATED_VOXEL_NODE_BODY()
    VOXEL_INPUT_PIN(FCLDeliveryBinding, Binding, nullptr);
    VOXEL_OUTPUT_PIN(FVoxelLinearColorBuffer, RockProperties);
    virtual void Compute(FVoxelGraphQuery Query) const override;
};
USTRUCT(meta=(Category="Landform|Prototype"))
struct CATALYSTLANDFORM_API FCLNode_DeliveryBounds : public FVoxelNode
{
    GENERATED_BODY()
    GENERATED_VOXEL_NODE_BODY()
    VOXEL_INPUT_PIN(FCLDeliveryBinding, Binding, nullptr);
    VOXEL_OUTPUT_PIN(FVoxelBox, Bounds);
    virtual void Compute(FVoxelGraphQuery Query) const override;
};

class ACLPlanet;
class UVoxelVolumeGraph;
namespace CLDeliveryPrototype
{
    CATALYSTLANDFORM_API FGuid ParameterGuid();
    /** Unsupported profiles fail explicitly; production/UI defaults are unchanged. Before CreateRuntime only. */
    CATALYSTLANDFORM_API bool Install(ACLPlanet& Planet,const FString& Backend,UVoxelVolumeGraph* Graph,FString& Error,TSharedPtr<FCLDeliveryCounters>* Counters=nullptr);
}

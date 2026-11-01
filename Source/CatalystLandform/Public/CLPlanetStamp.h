#pragma once

#include "CoreMinimal.h"
#include "Heightmap/VoxelHeightmapStamp.h"
#include "VoxelVolumeStamp.h"
#include "VoxelStampRef.h"
#include "Model/CLPlanetRecipe.h"
#include "CLPlanetStamp.generated.h"

/** Immutable placement copied from an authoring actor; all distances are cm. */
USTRUCT()
struct CATALYSTLANDFORM_API FCLSphericalHeightmapPlacement
{
    GENERATED_BODY()

    UPROPERTY() FGuid Id;
    UPROPERTY() bool bPlanar = false;
    UPROPERTY() FVector2D Center = FVector2D::ZeroVector;
    UPROPERTY() FVoxelHeightmapStamp Source;
    UPROPERTY() FVector Anchor = FVector::ZAxisVector;
    UPROPERTY() FVector TangentX = FVector::XAxisVector;
    UPROPERTY() FVector Scale = FVector::OneVector;
    /** Optional physical surface radius for the chart; zero preserves authored actor semantics. */
    UPROPERTY() double ChartRadius = 0;
    UPROPERTY() double HeightOffset = 0;
    UPROPERTY() double EdgeFalloff = 800;

    bool ToChart(double Radius, const FVector& Direction, FVector2D& Chart) const;
    FVector FromChart(double Radius, const FVector2D& Chart) const;
};

/** A radial volume assembled by evaluating native VPP heightmap stamp runtimes. */
USTRUCT()
struct CATALYSTLANDFORM_API FCLPlanetStamp final : public FVoxelVolumeStamp
{
    GENERATED_BODY()
    GENERATED_VIRTUAL_STRUCT_BODY()

    UPROPERTY() double Radius = 10000;
    UPROPERTY() bool bUseRecipe = false;
    UPROPERTY() bool bPlanarSources = false;
    UPROPERTY() FCLPlanetRecipe Recipe;
    UPROPERTY() TArray<FCLSphericalHeightmapPlacement> Placements;
};

USTRUCT()
struct CATALYSTLANDFORM_API FCLPlanetStampRuntime : public FVoxelVolumeStampRuntime
{
    GENERATED_BODY()
    GENERATED_VOXEL_RUNTIME_STAMP_BODY(FCLPlanetStamp)

    struct FSource
    {
        FCLSphericalHeightmapPlacement Placement;
        FVoxelStampRef Ref;
        TSharedPtr<const FVoxelHeightmapStampRuntime> Runtime;
        FVector2D SafeMin = FVector2D::ZeroVector;
        FVector2D SafeMax = FVector2D::ZeroVector;
        double CapCosine = -1;

        double Weight(double Radius, const FVector& Direction, FVector2D& Chart) const;
        double WeightNormalized(double Radius, const FVector& UnitDirection, FVector2D& Chart) const;
    };
    TArray<FSource> Sources;
    double Extent = 10000;
    TSharedPtr<const FCLCompiledPlanetRecipe> RecipePlan;

    virtual bool Initialize(FVoxelDependencyCollector& Dependencies) override;
    virtual FVoxelBox GetLocalBounds() const override;
    virtual void Apply(const FVoxelVolumeBulkQuery& Query, const FVoxelVolumeTransform& Transform) const override;
    virtual void Apply(const FVoxelVolumeSparseQuery& Query, const FVoxelVolumeTransform& Transform) const override;

    void ApplySources(const FVoxelQuery& Query, const FVoxelDoubleVectorBuffer& Directions, FVoxelFloatBuffer& Heights) const;
    FVoxelFloatBuffer SampleHeights(const FVoxelQuery& Query, const FVoxelDoubleVectorBuffer& Directions) const;
    FVoxelFloatBuffer SampleDistances(const FVoxelQuery& Query, const FVoxelDoubleVectorBuffer& Positions) const;
};

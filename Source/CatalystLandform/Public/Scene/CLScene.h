#pragma once

#include "CoreMinimal.h"
#include "Scene/CLSceneQuery.h"
#include "Model/CLRockLayer.h"

struct FCLCompiledPlanetRecipe;
class FCLCompiledVolumes;
class FCLCompiledCaveVolume;

/** Immutable, body-space query boundary. This first provider preserves the pinned
 * radial compiler. It is not yet the generalized-volume recipe/acceptance format. */
class CATALYSTLANDFORM_API FCLScene final
{
public:
    static constexpr int32 QueryContractVersion = 1;

    /** Only compiled whole-body spherical plans are accepted, not local/face
     * evaluation views. No authoring/world UObject state is read here; native shape kernels are stateless. */
    static TSharedPtr<const FCLScene> FromPlanet(TSharedPtr<const FCLCompiledPlanetRecipe> Plan,FString& Error);
    bool IsHeightOnly() const;

    double OuterRadiusMeters() const;
    FBox3d SolidBoundsMeters() const;
    ECLSceneFieldMetric FieldMetric() const { return ECLSceneFieldMetric::SignedImplicit; }

    /** No Euclidean-distance/Lipschitz bound is claimed by the radial provider. */
    bool HasConservativeStepBound() const { return false; }
    bool SampleShape(const FCLSceneQuery& Query, TArray<FCLSceneShapeSample>& Out, FString& Error) const;
    /** Negative inside generated cave air within the original solid, after authored backfill. */
    bool SampleCaveVoid(const FCLSceneQuery& Query,TArray<FCLSceneShapeSample>& Out,FString& Error,const FCLCompiledCaveVolume* SelectedCutters=nullptr)const;
    bool SampleGeology(const FCLSceneQuery& Query,TArray<FCLRockSample>& Out,FString& Error)const;
    bool SampleSurfaces(const FCLSceneQuery& Query, TArray<FCLSceneSurfaceSample>& Out, FString& Error) const;

private:
    explicit FCLScene(TSharedPtr<const FCLCompiledPlanetRecipe> InPlan);
    const TSharedPtr<const FCLCompiledPlanetRecipe> RadialPlan;
};

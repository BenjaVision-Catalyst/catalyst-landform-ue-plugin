#pragma once
#include "CoreMinimal.h"
class FCLScene;
struct FCLCompiledPlanetRecipe;

struct FCLGroundSurfaceSample
{
    bool bSupported=false;
    FVector PositionMeters=FVector::ZeroVector;
    FVector Normal=FVector::ZAxisVector;
};

/** Exposed ground after immutable cave/volume composition, in canonical body metres.
 * Fixed spatial probes bracket air-to-solid contacts; field values are never
 * treated as conservative distance steps. Contacts require 0.5 m radial support.
 * Unsupported/budget-exhausted rays emit no point.
 */
class CATALYSTLANDFORM_API FCLGroundSurfaceSampler
{
public:
    static constexpr int32 Profile=3;
    static constexpr double ProbeStepMeters=.125;
    static constexpr int32 MaximumProbeSteps=2048;
    static constexpr double MinimumSupportDepthMeters=.5;
    static TSharedPtr<const FCLGroundSurfaceSampler> Make(TSharedPtr<const FCLCompiledPlanetRecipe>,FString& Error);
    bool Sample(TConstArrayView<FVector> Directions,TArray<FCLGroundSurfaceSample>& Out,FString& Error)const;
private:
    TSharedPtr<const FCLCompiledPlanetRecipe> Plan;
    TSharedPtr<const FCLScene> Scene;
    TArray<FBox> CutBounds,AddBounds;
};

#pragma once
#include "CoreMinimal.h"
#include "CLProjectionGPU.h"
/** ProjectionOrigin.W=9150 selects world-anchor qualification: SourceCurvature.xyz
 * is source high, BodyFromLocal.xyz source low, OriginHighScale/OriginLow anchor
 * high/low. Output position is relative to that world anchor; Phase is the forward
 * differential of Normal.xyz, MacroPhase is the affine residual for Normal.w cm. */
struct FCLAnchoredGPUInput
{
    FVector4f SourceCurvature,AnchorRadius,OriginHighScale,OriginLow,BodyFromLocal,Normal,ProjectionOrigin;
};
struct FCLAnchoredGPUOutput {FVector4f Position,Normal,Phase,MacroPhase;};
/** Bounded qualification readback of the functions used by native materials/culling. */
class CATALYSTLANDFORMRENDER_API FCLAnchoredGPUReadback
{
public:
    static TSharedPtr<FCLAnchoredGPUReadback> TryStart(TArray<FCLAnchoredGPUInput> Inputs,FString& Error);
    ~FCLAnchoredGPUReadback();
    ECLProjectionReadbackStatus Poll(TArray<FCLAnchoredGPUOutput>& Out);
private:
    struct FState;FCLAnchoredGPUReadback();TSharedPtr<FState,ESPMode::ThreadSafe> State;
};

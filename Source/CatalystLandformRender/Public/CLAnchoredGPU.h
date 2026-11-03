#pragma once
#include "CoreMinimal.h"
#include "CLProjectionGPU.h"
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

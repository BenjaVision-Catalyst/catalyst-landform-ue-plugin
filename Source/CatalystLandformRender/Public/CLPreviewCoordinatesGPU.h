#pragma once
#include "CoreMinimal.h"
#include "CLProjectionGPU.h"
struct FCLPreviewCoordinatesGPUInput
{
    FVector4f WorldHigh,WorldLow,ConfigHigh,ConfigLow,ExpectedHigh,ExpectedLow,Phase200,Phase250,Phase270;
};
struct FCLPreviewCoordinatesGPUOutput {FVector4f Errors;};
/** Bounded qualification readback of the coordinate function used by native preview materials. */
class CATALYSTLANDFORMRENDER_API FCLPreviewCoordinatesGPUReadback
{
public:
    static TSharedPtr<FCLPreviewCoordinatesGPUReadback> TryStart(TArray<FCLPreviewCoordinatesGPUInput> Inputs,FString& Error);
    ~FCLPreviewCoordinatesGPUReadback();
    ECLProjectionReadbackStatus Poll(TArray<FCLPreviewCoordinatesGPUOutput>& Out);
private:
    struct FState;FCLPreviewCoordinatesGPUReadback();TSharedPtr<FState,ESPMode::ThreadSafe> State;
};

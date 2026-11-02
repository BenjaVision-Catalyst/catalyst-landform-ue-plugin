#pragma once
#include "CoreMinimal.h"

/** Shader chart coordinates are planet-relative and divided by physical reference radius.
 * The caller owns the double-precision origin/radius conversion and chart lifetime.
 */
struct FCLProjectionGPUFrame
{
    FVector3f AnchorDirection = FVector3f(0, 0, 1);
    float Curvature = 1;
    float MaxArcRadians = UE_PI / 3;
    float MinAltitude = -.1f;
    float MaxAltitude = .1f;
};

struct FCLProjectionGPUInput
{
    FVector4f Source, Presented, Vector, Normal;
};
struct FCLProjectionGPUOutput
{
    // xyz is the value, w is 1 on success or 0 on rejection. Rejected values are zero.
    FVector4f Position, Inverse, Vector, Normal;
};

enum class ECLProjectionReadbackStatus : uint8 { Pending, Complete, Failed };

/** Bounded, asynchronous qualification diagnostic. Never run readback in the gameplay frame loop.
 * Start/Poll/destruction belong to the game thread; GPU work and readback belong to the render
 * thread. No flush, GPU-idle wait or callback into UObjects. Each request admits at most 4096 queries.
 */
class CATALYSTLANDFORMRENDER_API FCLProjectionGPUReadback
{
public:
    static TSharedPtr<FCLProjectionGPUReadback> TryStart(
        const FCLProjectionGPUFrame& Frame, TArray<FCLProjectionGPUInput> Inputs, FString& Error);
    ~FCLProjectionGPUReadback();
    ECLProjectionReadbackStatus Poll(TArray<FCLProjectionGPUOutput>& Out);

private:
    struct FState;
    FCLProjectionGPUReadback();
    TSharedPtr<FState, ESPMode::ThreadSafe> State;
};

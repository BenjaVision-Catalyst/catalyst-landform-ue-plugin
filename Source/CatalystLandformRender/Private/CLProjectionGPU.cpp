#include "CLProjectionGPU.h"
#include "DynamicRHI.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIGPUReadback.h"
#include "ShaderParameterStruct.h"

class FCLProjectionParityCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FCLProjectionParityCS);
    SHADER_USE_PARAMETER_STRUCT(FCLProjectionParityCS, FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, NumQueries)
        SHADER_PARAMETER(FVector3f, AnchorDirection)
        SHADER_PARAMETER(float, Curvature)
        SHADER_PARAMETER(float, MaxArcRadians)
        SHADER_PARAMETER(float, MinAltitude)
        SHADER_PARAMETER(float, MaxAltitude)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FCLProjectionQuery>, Inputs)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FCLProjectionResult>, Outputs)
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }
};
IMPLEMENT_GLOBAL_SHADER(FCLProjectionParityCS, "/Plugin/CatalystLandform/Private/CLProjectionParity.usf", "MainCS", SF_Compute);

struct FCLProjectionGPUReadback::FState
{
    TAtomic<ECLProjectionReadbackStatus> Status{ECLProjectionReadbackStatus::Pending};
    TAtomic<bool> bPollQueued{false};
    TUniquePtr<FRHIGPUBufferReadback> Readback;
    TArray<FCLProjectionGPUOutput> Results;
    uint32 NumQueries = 0;
};

FCLProjectionGPUReadback::FCLProjectionGPUReadback() : State(MakeShared<FState, ESPMode::ThreadSafe>()) {}
FCLProjectionGPUReadback::~FCLProjectionGPUReadback()
{
    check(IsInGameThread());
    // Resource retirement follows all previously admitted render commands.
    ENQUEUE_RENDER_COMMAND(CLProjectionRetire)([Retired = MoveTemp(State)](FRHICommandListImmediate&) {});
}

TSharedPtr<FCLProjectionGPUReadback> FCLProjectionGPUReadback::TryStart(
    const FCLProjectionGPUFrame& Frame, TArray<FCLProjectionGPUInput> Inputs, FString& Error)
{
    check(IsInGameThread());
    if (!GDynamicRHI || GUsingNullRHI || GMaxRHIFeatureLevel < ERHIFeatureLevel::SM5)
    { Error = TEXT("Projection parity requires a real SM5+ GPU"); return nullptr; }
    if (Inputs.IsEmpty() || Inputs.Num() > 4096 || Frame.AnchorDirection.ContainsNaN() ||
        !FMath::IsFinite(Frame.AnchorDirection.SizeSquared()) || Frame.AnchorDirection.SizeSquared() < 1.e-8f ||
        !FMath::IsFinite(Frame.Curvature) || Frame.Curvature < 0 || Frame.Curvature > 1 ||
        !FMath::IsFinite(Frame.MaxArcRadians) || Frame.MaxArcRadians <= 0 || Frame.MaxArcRadians >= UE_HALF_PI ||
        !FMath::IsFinite(Frame.MinAltitude) || !FMath::IsFinite(Frame.MaxAltitude) ||
        Frame.MinAltitude <= -1 || Frame.MinAltitude > Frame.MaxAltitude)
    { Error = TEXT("Invalid projection GPU frame or query count (1..4096)"); return nullptr; }
    static_assert(sizeof(FCLProjectionGPUInput) == 64 && sizeof(FCLProjectionGPUOutput) == 64);
    auto Job = TSharedPtr<FCLProjectionGPUReadback>(new FCLProjectionGPUReadback());
    Job->State->NumQueries = Inputs.Num();
    ENQUEUE_RENDER_COMMAND(CLProjectionDispatch)([State = Job->State, Frame, Inputs = MoveTemp(Inputs)](FRHICommandListImmediate& RHICmdList)
    {
        FRDGBuilder GraphBuilder(RHICmdList);
        auto* Parameters = GraphBuilder.AllocParameters<FCLProjectionParityCS::FParameters>();
        Parameters->NumQueries = Inputs.Num();
        Parameters->AnchorDirection = Frame.AnchorDirection.GetSafeNormal();
        Parameters->Curvature = Frame.Curvature;
        Parameters->MaxArcRadians = Frame.MaxArcRadians;
        Parameters->MinAltitude = Frame.MinAltitude;
        Parameters->MaxAltitude = Frame.MaxAltitude;
        const auto InputBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("CL.Projection.Inputs"), Inputs);
        const auto OutputBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FCLProjectionGPUOutput), Inputs.Num()), TEXT("CL.Projection.Outputs"));
        Parameters->Inputs = GraphBuilder.CreateSRV(InputBuffer);
        Parameters->Outputs = GraphBuilder.CreateUAV(OutputBuffer);
        TShaderMapRef<FCLProjectionParityCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("CL Projection Parity"), Shader, Parameters,
            FIntVector(FMath::DivideAndRoundUp(Inputs.Num(), 64), 1, 1));
        State->Readback = MakeUnique<FRHIGPUBufferReadback>(TEXT("CL.Projection.Readback"));
        AddEnqueueCopyPass(GraphBuilder, State->Readback.Get(), OutputBuffer, Inputs.Num() * sizeof(FCLProjectionGPUOutput));
        GraphBuilder.Execute();
    });
    Error.Reset();
    return Job;
}

ECLProjectionReadbackStatus FCLProjectionGPUReadback::Poll(TArray<FCLProjectionGPUOutput>& Out)
{
    check(IsInGameThread());
    auto Status = State->Status.Load();
    if (Status == ECLProjectionReadbackStatus::Complete) Out = State->Results;
    if (Status != ECLProjectionReadbackStatus::Pending || State->bPollQueued.Exchange(true)) return Status;
    ENQUEUE_RENDER_COMMAND(CLProjectionPoll)([State = State](FRHICommandListImmediate&)
    {
        if (State->Readback && State->Readback->IsReady())
        {
            const uint32 Bytes = State->NumQueries * sizeof(FCLProjectionGPUOutput);
            if (const void* Data = State->Readback->Lock(Bytes))
            {
                State->Results.SetNumUninitialized(State->NumQueries);
                FMemory::Memcpy(State->Results.GetData(), Data, Bytes);
                State->Readback->Unlock();
                State->Status.Store(ECLProjectionReadbackStatus::Complete);
            }
            else State->Status.Store(ECLProjectionReadbackStatus::Failed);
        }
        State->bPollQueued.Store(false);
    });
    return Status;
}

#include "CLPreviewCoordinatesGPU.h"
#include "DynamicRHI.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIGPUReadback.h"
#include "ShaderParameterStruct.h"

class FCLPreviewCoordinatesCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FCLPreviewCoordinatesCS);
    SHADER_USE_PARAMETER_STRUCT(FCLPreviewCoordinatesCS, FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, NumQueries)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FCLPreviewCoordinatesQuery>, Inputs)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FCLPreviewCoordinatesResult>, Outputs)
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }
};
IMPLEMENT_GLOBAL_SHADER(FCLPreviewCoordinatesCS, "/Plugin/CatalystLandform/Private/CLPreviewCoordinates.usf", "MainCS", SF_Compute);

struct FCLPreviewCoordinatesGPUReadback::FState
{
    TAtomic<ECLProjectionReadbackStatus> Status{ECLProjectionReadbackStatus::Pending};
    TAtomic<bool> bPollQueued{false};
    TUniquePtr<FRHIGPUBufferReadback> Readback;
    TArray<FCLPreviewCoordinatesGPUOutput> Results;
    uint32 NumQueries = 0;
};

FCLPreviewCoordinatesGPUReadback::FCLPreviewCoordinatesGPUReadback() : State(MakeShared<FState, ESPMode::ThreadSafe>()) {}
FCLPreviewCoordinatesGPUReadback::~FCLPreviewCoordinatesGPUReadback()
{
    check(IsInGameThread());
    // Resource retirement follows all previously admitted render commands.
    ENQUEUE_RENDER_COMMAND(CLPreviewCoordinatesRetire)([Retired = MoveTemp(State)](FRHICommandListImmediate&) {});
}

TSharedPtr<FCLPreviewCoordinatesGPUReadback> FCLPreviewCoordinatesGPUReadback::TryStart(
    TArray<FCLPreviewCoordinatesGPUInput> Inputs, FString& Error)
{
    check(IsInGameThread());
    if (!GDynamicRHI || GUsingNullRHI || GMaxRHIFeatureLevel < ERHIFeatureLevel::SM5)
    { Error = TEXT("Preview coordinates require a real SM5+ GPU"); return nullptr; }
    if(Inputs.IsEmpty()||Inputs.Num()>4096){Error=TEXT("Preview coordinate GPU query count must be 1..4096");return nullptr;}
    for(const auto& Q:Inputs)
    {
        for(const FVector4f V:{Q.WorldHigh,Q.WorldLow,Q.ConfigHigh,Q.ConfigLow,Q.ExpectedHigh,Q.ExpectedLow,Q.Phase200,Q.Phase250,Q.Phase270})
            for(int32 I=0;I<4;++I)if(!FMath::IsFinite(V[I])){Error=TEXT("Preview coordinate GPU inputs must be finite");return nullptr;}

    }
    static_assert(sizeof(FCLPreviewCoordinatesGPUInput)==144&&sizeof(FCLPreviewCoordinatesGPUOutput)==16);
    auto Job = TSharedPtr<FCLPreviewCoordinatesGPUReadback>(new FCLPreviewCoordinatesGPUReadback());
    Job->State->NumQueries = Inputs.Num();
    ENQUEUE_RENDER_COMMAND(CLPreviewCoordinatesDispatch)([State = Job->State, Inputs = MoveTemp(Inputs)](FRHICommandListImmediate& RHICmdList)
    {
        FRDGBuilder GraphBuilder(RHICmdList);
        auto* Parameters = GraphBuilder.AllocParameters<FCLPreviewCoordinatesCS::FParameters>();
        Parameters->NumQueries = Inputs.Num();
        const auto InputBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("CL.PreviewCoordinates.Inputs"), Inputs);
        const auto OutputBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FCLPreviewCoordinatesGPUOutput), Inputs.Num()), TEXT("CL.PreviewCoordinates.Outputs"));
        Parameters->Inputs = GraphBuilder.CreateSRV(InputBuffer);
        Parameters->Outputs = GraphBuilder.CreateUAV(OutputBuffer);
        TShaderMapRef<FCLPreviewCoordinatesCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("CL Preview Coordinates"), Shader, Parameters,
            FIntVector(FMath::DivideAndRoundUp(Inputs.Num(), 64), 1, 1));
        State->Readback = MakeUnique<FRHIGPUBufferReadback>(TEXT("CL.PreviewCoordinates.Readback"));
        AddEnqueueCopyPass(GraphBuilder, State->Readback.Get(), OutputBuffer, Inputs.Num() * sizeof(FCLPreviewCoordinatesGPUOutput));
        GraphBuilder.Execute();
    });
    Error.Reset();
    return Job;
}

ECLProjectionReadbackStatus FCLPreviewCoordinatesGPUReadback::Poll(TArray<FCLPreviewCoordinatesGPUOutput>& Out)
{
    check(IsInGameThread());
    auto Status = State->Status.Load();
    if (Status == ECLProjectionReadbackStatus::Complete) Out = State->Results;
    if (Status != ECLProjectionReadbackStatus::Pending || State->bPollQueued.Exchange(true)) return Status;
    ENQUEUE_RENDER_COMMAND(CLPreviewCoordinatesPoll)([State = State](FRHICommandListImmediate&)
    {
        if (State->Readback && State->Readback->IsReady())
        {
            const uint32 Bytes = State->NumQueries * sizeof(FCLPreviewCoordinatesGPUOutput);
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

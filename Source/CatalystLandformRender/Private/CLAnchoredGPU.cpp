#include "CLAnchoredGPU.h"
#include "DynamicRHI.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIGPUReadback.h"
#include "ShaderParameterStruct.h"

class FCLAnchoredParityCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FCLAnchoredParityCS);
    SHADER_USE_PARAMETER_STRUCT(FCLAnchoredParityCS, FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, NumQueries)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FCLAnchoredQuery>, Inputs)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FCLAnchoredResult>, Outputs)
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }
};
IMPLEMENT_GLOBAL_SHADER(FCLAnchoredParityCS, "/Plugin/CatalystLandform/Private/CLAnchoredParity.usf", "MainCS", SF_Compute);

struct FCLAnchoredGPUReadback::FState
{
    TAtomic<ECLProjectionReadbackStatus> Status{ECLProjectionReadbackStatus::Pending};
    TAtomic<bool> bPollQueued{false};
    TUniquePtr<FRHIGPUBufferReadback> Readback;
    TArray<FCLAnchoredGPUOutput> Results;
    uint32 NumQueries = 0;
};

FCLAnchoredGPUReadback::FCLAnchoredGPUReadback() : State(MakeShared<FState, ESPMode::ThreadSafe>()) {}
FCLAnchoredGPUReadback::~FCLAnchoredGPUReadback()
{
    check(IsInGameThread());
    // Resource retirement follows all previously admitted render commands.
    ENQUEUE_RENDER_COMMAND(CLProjectionRetire)([Retired = MoveTemp(State)](FRHICommandListImmediate&) {});
}

TSharedPtr<FCLAnchoredGPUReadback> FCLAnchoredGPUReadback::TryStart(
    TArray<FCLAnchoredGPUInput> Inputs, FString& Error)
{
    check(IsInGameThread());
    if (!GDynamicRHI || GUsingNullRHI || GMaxRHIFeatureLevel < ERHIFeatureLevel::SM5)
    { Error = TEXT("Projection parity requires a real SM5+ GPU"); return nullptr; }
    if(Inputs.IsEmpty()||Inputs.Num()>4096){Error=TEXT("Anchored GPU query count must be 1..4096");return nullptr;}
    for(const auto& Q:Inputs)
    {
        for(const FVector4f V:{Q.SourceCurvature,Q.AnchorRadius,Q.OriginHighScale,Q.OriginLow,Q.BodyFromLocal,Q.Normal,Q.ProjectionOrigin})
            for(int32 I=0;I<4;++I)if(!FMath::IsFinite(V[I])){Error=TEXT("Anchored GPU inputs must be finite");return nullptr;}
        if(Q.OriginHighScale.W<=0||Q.AnchorRadius.W<=0||Q.SourceCurvature.W<0||Q.SourceCurvature.W>1||
            FMath::Abs(Q.BodyFromLocal.SizeSquared()-1)>1.e-5||FVector3f(Q.AnchorRadius).IsNearlyZero()||FVector3f(Q.Normal).IsNearlyZero())
        {Error=TEXT("Anchored GPU inputs require a positive radius/tile and rigid unit frame");return nullptr;}
    }
    static_assert(sizeof(FCLAnchoredGPUInput)==112&&sizeof(FCLAnchoredGPUOutput)==64);
    auto Job = TSharedPtr<FCLAnchoredGPUReadback>(new FCLAnchoredGPUReadback());
    Job->State->NumQueries = Inputs.Num();
    ENQUEUE_RENDER_COMMAND(CLProjectionDispatch)([State = Job->State, Inputs = MoveTemp(Inputs)](FRHICommandListImmediate& RHICmdList)
    {
        FRDGBuilder GraphBuilder(RHICmdList);
        auto* Parameters = GraphBuilder.AllocParameters<FCLAnchoredParityCS::FParameters>();
        Parameters->NumQueries = Inputs.Num();
        const auto InputBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("CL.Projection.Inputs"), Inputs);
        const auto OutputBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FCLAnchoredGPUOutput), Inputs.Num()), TEXT("CL.Projection.Outputs"));
        Parameters->Inputs = GraphBuilder.CreateSRV(InputBuffer);
        Parameters->Outputs = GraphBuilder.CreateUAV(OutputBuffer);
        TShaderMapRef<FCLAnchoredParityCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("CL Projection Parity"), Shader, Parameters,
            FIntVector(FMath::DivideAndRoundUp(Inputs.Num(), 64), 1, 1));
        State->Readback = MakeUnique<FRHIGPUBufferReadback>(TEXT("CL.Projection.Readback"));
        AddEnqueueCopyPass(GraphBuilder, State->Readback.Get(), OutputBuffer, Inputs.Num() * sizeof(FCLAnchoredGPUOutput));
        GraphBuilder.Execute();
    });
    Error.Reset();
    return Job;
}

ECLProjectionReadbackStatus FCLAnchoredGPUReadback::Poll(TArray<FCLAnchoredGPUOutput>& Out)
{
    check(IsInGameThread());
    auto Status = State->Status.Load();
    if (Status == ECLProjectionReadbackStatus::Complete) Out = State->Results;
    if (Status != ECLProjectionReadbackStatus::Pending || State->bPollQueued.Exchange(true)) return Status;
    ENQUEUE_RENDER_COMMAND(CLProjectionPoll)([State = State](FRHICommandListImmediate&)
    {
        if (State->Readback && State->Readback->IsReady())
        {
            const uint32 Bytes = State->NumQueries * sizeof(FCLAnchoredGPUOutput);
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

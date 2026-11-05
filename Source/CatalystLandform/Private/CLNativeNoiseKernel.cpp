#include "CLNativeNoiseKernel.h"
#include "Nodes/VoxelNoiseNodes.h"
namespace
{
// Fail closed if a future VPP changes the compute node's pin layout. The
// function pointer is cached in the plan; workers never inspect node metadata.
bool NativePerlinABIIsCompatible()
{
    static const bool bCompatible = []
    {
        const FVoxelComputeNode_PerlinNoise3D Node;
        const FName Names[] = {TEXT("Position"), TEXT("Amplitude"), TEXT("FeatureScale"), TEXT("Seed"), TEXT("Value")};
        const FVoxelPinType Types[] = {FVoxelPinType::Make<FVoxelVectorBuffer>(), FVoxelPinType::Make<FVoxelFloatBuffer>(),
            FVoxelPinType::Make<FVoxelFloatBuffer>(), FVoxelPinType::Make<FVoxelSeedBuffer>(), FVoxelPinType::Make<FVoxelFloatBuffer>()};
        int32 Index = 0;
        for (const FVoxelPin& Pin : Node.GetPins())
        {
            if (Index >= 5 || Pin.Name != Names[Index] || Pin.GetType() != Types[Index] || Pin.bIsInput != (Index < 4)) return false;
            ++Index;
        }
        return Index == 5;
    }();
    return bCompatible;
}
}
TSharedPtr<const FCLNativeNoiseKernel> FCLNativeNoiseKernel::Create(FString& Error)
{
    if(!NativePerlinABIIsCompatible())
    {Error=TEXT("VPP PerlinNoise3D pin layout changed; native adapter requires qualification");return nullptr;}
    const auto Kernel=GVoxelNodeISPCPtrs.FindRef(TEXT("VoxelComputeNode_PerlinNoise3D"));
    if(!Kernel)
    {Error=TEXT("VPP PerlinNoise3D kernel is unavailable; native generation cannot fall back to algorithm 1");return nullptr;}
    return MakeShared<FCLNativeNoiseKernel>(Kernel);
}
double FCLNativeNoiseKernel::Sample(const FVector& Position,int32 Seed)const
{
    const float X=Position.X,Y=Position.Y,Z=Position.Z,One=1;float Value=0;
    const ispc::FVoxelInputBuffer Inputs[]={{&X,true},{&Y,true},{&Z,true},{&One,true},{&One,true},{&Seed,true}};
    void* Outputs[]={&Value};Evaluate(Inputs,Outputs,1);return Value;
}

void FCLNativeNoiseKernel::SampleBatch(TConstArrayView<FVector> Positions,int32 Seed,TArray<float>& Values)const
{
    const int32 N=Positions.Num();Values.SetNumUninitialized(N);if(!N)return;
    TArray<float> X,Y,Z;X.SetNumUninitialized(N);Y.SetNumUninitialized(N);Z.SetNumUninitialized(N);
    for(int32 I=0;I<N;++I){X[I]=Positions[I].X;Y[I]=Positions[I].Y;Z[I]=Positions[I].Z;}
    const float One=1;const ispc::FVoxelInputBuffer Inputs[]={{X.GetData(),false},{Y.GetData(),false},{Z.GetData(),false},{&One,true},{&One,true},{&Seed,true}};
    void* Outputs[]={Values.GetData()};Evaluate(Inputs,Outputs,N);
}
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLNativeCaveNoiseBatchTest,"Catalyst.Landform.Caves.NativeNoiseBatchParity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLNativeCaveNoiseBatchTest::RunTest(const FString&)
{
    FString E;const auto Kernel=FCLNativeNoiseKernel::Create(E);if(!Kernel){AddError(E);return false;}
    // Independent prior scalar binding, including non-SIMD-multiple counts.
    for(int32 N:{0,1,3,17,512})for(int32 Seed:{-991,0,1729})
    {
        TArray<FVector> Points;for(int32 I=0;I<N;++I)Points.Add(FVector(I*.731-1100,I*-.419+44,I*.213+700));
        TArray<float> Values;Kernel->SampleBatch(Points,Seed,Values);TestEqual(TEXT("Native batch preserves count"),Values.Num(),N);
        for(int32 I=0;I<N;++I)TestTrue(TEXT("Native SIMD batch preserves the previous scalar noise field"),FMath::Abs(double(Values[I])-Kernel->Sample(Points[I],Seed))<1.e-6);
    }
    return true;
}
#endif

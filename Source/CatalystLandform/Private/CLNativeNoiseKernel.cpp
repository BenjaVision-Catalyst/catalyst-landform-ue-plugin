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

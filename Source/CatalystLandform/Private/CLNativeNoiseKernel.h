#pragma once
#include "CoreMinimal.h"
#include "VoxelISPCNodeHelpers.h"

// One private binding to VPP's Perlin node, shared by authored field providers.
// Resolve the ABI once during compilation; workers retain only the native pointer.
class FCLNativeNoiseKernel
{
public:
    static TSharedPtr<const FCLNativeNoiseKernel> Create(FString& Error);
    explicit FCLNativeNoiseKernel(FVoxelNodeISPCPtr InFunction):Function(InFunction){}
    void Evaluate(const ispc::FVoxelInputBuffer* Inputs,void* const* Outputs,int32 Num)const
    {Function(Inputs,Outputs,Num);}
    double Sample(const FVector& Position,int32 Seed)const;
    void SampleBatch(TConstArrayView<FVector> Positions,int32 Seed,TArray<float>& Values)const;
private:
    const FVoxelNodeISPCPtr Function;
};

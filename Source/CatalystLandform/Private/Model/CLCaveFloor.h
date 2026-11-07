#pragma once
#include "CoreMinimal.h"
class FCLNativeNoiseKernel;
// One physical relief field is shared by all passage and chamber floors.
void CLSampleCaveFloorRelief(TConstArrayView<FVector>,const FCLNativeNoiseKernel&,int32 Seed,double Scale,TArray<double>&);
double CLCaveFloorRelief(const FVector&,const FCLNativeNoiseKernel&,int32 Seed,double Scale);

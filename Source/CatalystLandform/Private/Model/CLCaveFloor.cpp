#include "Model/CLCaveFloor.h"
#include "CLNativeNoiseKernel.h"
void CLSampleCaveFloorRelief(TConstArrayView<FVector> P,const FCLNativeNoiseKernel& Noise,int32 Seed,double Scale,TArray<double>& Out)
{
    TArray<FVector> Points;Points.Reserve(P.Num()*3);
    for(const auto& Q:P)Points.Add(Q/19.+FVector(71,-93,137));
    for(const auto& Q:P)Points.Add(Q/7.+FVector(-37,311,-61));
    for(const auto& Q:P)Points.Add(Q/2.5+FVector(113,29,-43));
    TArray<float> Values;Noise.SampleBatch(Points,Seed^0x464C4F37,Values);Out.SetNumUninitialized(P.Num());
    for(int32 I=0;I<P.Num();++I)Out[I]=Scale*(Values[I]*2.+Values[P.Num()+I]*.5+Values[P.Num()*2+I]*.08);
}
double CLCaveFloorRelief(const FVector& P,const FCLNativeNoiseKernel& Noise,int32 Seed,double Scale)
{
    TArray<double> Result;CLSampleCaveFloorRelief(MakeArrayView(&P,1),Noise,Seed,Scale,Result);return Result[0];
}

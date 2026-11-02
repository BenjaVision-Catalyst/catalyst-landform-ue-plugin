#include "Model/CLSurfaceRange.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLStampDistribution.h"

double FCLSurfaceRange::Map(double H) const
{return FMath::Lerp(TargetMin,TargetMax,FMath::Clamp((H-SourceMin)/(SourceMax-SourceMin),0.,1.));}
TSharedPtr<const FCLSurfaceRange> FCLSurfaceRange::Compile(const FCLCompiledPlanetRecipe& Surface,double Low,double High,FString& Error)
{
    if(!FMath::IsFinite(Low)||!FMath::IsFinite(High)||Low>=High||Low < -Surface.RadiusMeters||High>0)
    {Error=TEXT("SurfaceMin and SurfaceMax must be ordered heights inside the planet radius");return nullptr;}
    TArray<FVector> Directions;Directions.Reserve(8192);
    for(int32 I=0;I<4096;++I)
    {
        const double Z=1-2*(I+.5)/4096.,A=I*2.399963229728653;
        Directions.Emplace(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z);
    }
    // Include small stamp footprints rather than relying on a globe grid to hit them.
    TArray<FCLResolvedStampPlacement> Placements;Surface.GetStampPlacements(Placements);
    for(const auto& P:Placements)if(!P.bLocalCoordinates)
    {
        const FVector Right=P.TangentX.GetSafeNormal(),Up=FVector::CrossProduct(P.Anchor,Right).GetSafeNormal();
        const double Angle=FMath::Min(.5,P.WidthMeters/FMath::Max(1.,P.ChartRadiusMeters));
        for(int32 Y=-2;Y<=2;++Y)for(int32 X=-2;X<=2;++X)
            Directions.Add((P.Anchor+(Right*X+Up*Y)*Angle*.2).GetSafeNormal());
    }
    TArray<double> Heights;Surface.SampleHeightsMeters(Directions,Heights);
    TArray<int32> Order;for(int32 I=0;I<Heights.Num();++I)
    {if(!FMath::IsFinite(Heights[I])){Error=TEXT("Surface generation returned a non-finite height");return nullptr;}Order.Add(I);}
    Order.Sort([&](int32 A,int32 B){return Heights[A]==Heights[B]?A<B:Heights[A]<Heights[B];});
    auto Out=MakeShared<FCLSurfaceRange>();Out->TargetMin=Low;Out->TargetMax=High;
    Out->SourceMin=Heights[Order[0]];Out->SourceMax=Heights[Order.Last()];
    Out->MinimumDirection=Directions[Order[0]];Out->MaximumDirection=Directions[Order.Last()];
    // Refine several extrema independently; one deterministic range is shared by all patches.
    for(int32 Sign:{-1,1})for(int32 Rank=0;Rank<4;++Rank)
    {
        const int32 Index=Order[Sign<0?Rank:Order.Num()-1-Rank];FVector D=Directions[Index];double H=Heights[Index];
        for(double Step=.08;Step>1.e-6;Step*=.5)for(int32 Attempt=0;Attempt<3;++Attempt)
        {
            FVector Right,Up;D.FindBestAxisVectors(Right,Up);TArray<FVector> Probe;
            for(int32 Y=-1;Y<=1;++Y)for(int32 X=-1;X<=1;++X)if(X||Y)Probe.Add((D+(Right*X+Up*Y)*Step).GetSafeNormal());
            TArray<double> Values;Surface.SampleHeightsMeters(Probe,Values);int32 Best=INDEX_NONE;
            for(int32 I=0;I<Values.Num();++I)if(Sign*Values[I]>Sign*H){H=Values[I];Best=I;}
            if(Best==INDEX_NONE)break;D=Probe[Best];
        }
        if(H<Out->SourceMin){Out->SourceMin=H;Out->MinimumDirection=D;}
        if(H>Out->SourceMax){Out->SourceMax=H;Out->MaximumDirection=D;}
    }
    if(Out->SourceMax-Out->SourceMin<1.e-8)
    {Error=TEXT("A constant surface cannot reach two distinct surface guides; enable a shape layer or separate the guide values");return nullptr;}
    return Out;
}

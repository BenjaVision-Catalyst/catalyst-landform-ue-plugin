#include "Model/CLSurfaceRange.h"
#include "Model/CLCompileProgress.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLStampDistribution.h"
#include "Curves/RichCurve.h"

class FCLCompiledSurfaceDistribution
{
public:
    struct FRegional
    {
        FGuid Id;FString Name;bool bPlaced=false;FCLSphericalRegionMask Mask;FCLCompiledLandformPlacement Placement;FRichCurve Curve;
        double Weight(const FVector& D,double Radius)const{return bPlaced?Placement.Weight(D):Mask.Weight(D,Radius);}
    };
    bool bGlobal=false;FRichCurve Global;TArray<FRegional> Regions;double ReferenceRadius=1;
    double Map(double X,const FVector& D)const
    {
        double Value=bGlobal?Global.Eval(float(X)):X;
        if(!D.IsNearlyZero())for(const auto& R:Regions){const double W=R.Weight(D,ReferenceRadius);if(W>0)Value=FMath::Lerp(Value,double(R.Curve.Eval(float(X))),W);}
        return FMath::Clamp(Value,0.,1.);
    }
    static bool Fit(const FCLSurfaceDistributionSettings& Settings,TArray<double> Heights,double Low,double High,FRichCurve& Out,FString& Error)
    {
        if(!Settings.Validate(Error))return false;
        const double Tail=FMath::Min(Settings.LowEndAreaPercent,Settings.HighEndAreaPercent)/100;
        if(Heights.Num()*Tail<16){Error=TEXT("Elevation region has insufficient equal-area samples to resolve its requested rare tails; enlarge the region or increase calibration samples");return false;}
        Heights.Sort();const auto Target=Settings.TargetCurve();
        TArray<double> Probabilities;for(int32 I=1;I<128;++I)Probabilities.Add(I/128.);
        Probabilities.Add(Settings.LowEndAreaPercent/100);Probabilities.Add(1-Settings.HighEndAreaPercent/100);Probabilities.Sort();
        TArray<FVector2D> Points{{0,0}};
        for(double P:Probabilities)
        {
            const double Rank=P*(Heights.Num()-1);const int32 I=FMath::FloorToInt(Rank);
            const double H=FMath::Lerp(Heights[I],Heights[FMath::Min(I+1,Heights.Num()-1)],Rank-I);
            const double X=FMath::Clamp((H-Low)/(High-Low),0.,1.);
            if(X>Points.Last().X+1.e-7&&X<1-1.e-7)Points.Add({X,Target.Eval(float(P))});
        }
        Points.Add({1,1});Out=FCLSurfaceDistributionSettings::InterpolateMonotone(Points);
        const auto Quantiles=Settings.Quantiles();int32 Lower=0,Upper=0,Middle=0,QuarterLow=0,QuarterHigh=0;
        for(double H:Heights)
        {
            const double V=Out.Eval(float(FMath::Clamp((H-Low)/(High-Low),0.,1.)));
            Lower+=V<=Settings.EndBandHeightPercent/100;Upper+=V>=1-Settings.EndBandHeightPercent/100;Middle+=V<=Settings.TypicalHeightPercent/100;QuarterLow+=V<=Quantiles[2].Y;QuarterHigh+=V<=Quantiles[4].Y;
        }
        const double N=Heights.Num();
        if(FMath::Abs(Lower/N-Settings.LowEndAreaPercent/100)>.005||FMath::Abs(Upper/N-Settings.HighEndAreaPercent/100)>.005||FMath::Abs(Middle/N-.5)>.02||FMath::Abs(QuarterLow/N-.25)>.02||FMath::Abs(QuarterHigh/N-.75)>.02)
        {Error=TEXT("Elevation distribution cannot fit the requested area fractions: flat/tied source elevations prevent a monotone fit; no extra noise is injected");return false;}
        return true;
    }
    static TSharedPtr<const FCLCompiledSurfaceDistribution> Compile(const FCLPlanetRecipe& Recipe,const FCLCompiledPlanetRecipe& Surface,FCLSurfaceRange& Range,FString& Error)
    {
        const bool Global=Recipe.ElevationDistribution.bEnabled;
        int32 Count=0;for(const auto& R:Recipe.TerrainRegions)Count+=R.bEnabled&&R.ElevationDistribution.bEnabled;
        if(!Global&&!Count)return nullptr;
        if(Count>8){Error=TEXT("Elevation distribution supports at most eight explicit regional profiles");return nullptr;}
        auto Out=MakeShared<FCLCompiledSurfaceDistribution>();Out->ReferenceRadius=Surface.RadiusMeters+Surface.BaseHeightMeters;
        int32 Samples=Global?Recipe.ElevationDistribution.CalibrationSamples:8192;
        for(const auto& R:Recipe.TerrainRegions)if(R.bEnabled&&R.ElevationDistribution.bEnabled)Samples=FMath::Max(Samples,R.ElevationDistribution.CalibrationSamples);
        if(Samples<8192||Samples>262144){Error=TEXT("Elevation calibration requires 8192–262144 bounded samples");return nullptr;}
        TArray<FVector> Directions;Directions.Reserve(Samples);
        for(int32 I=0;I<Samples;++I)
        {
            const double Z=1-2*(I+.5)/Samples,A=(I+.371)*2.399963229728653;
            Directions.Add(FVector(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z));
        }
        TArray<double> Heights;if(!FCLCompileProgress::Sample(Surface,Directions,Heights,Error))return nullptr;
        for(int32 I=0;I<Heights.Num();++I)
        {
            if(!FMath::IsFinite(Heights[I])){Error=TEXT("Elevation calibration found a non-finite native height");return nullptr;}
            if(Heights[I]<Range.SourceMin){Range.SourceMin=Heights[I];Range.MinimumDirection=Directions[I];}
            if(Heights[I]>Range.SourceMax){Range.SourceMax=Heights[I];Range.MaximumDirection=Directions[I];}
        }
        if(Global&&!Fit(Recipe.ElevationDistribution,Heights,Range.SourceMin,Range.SourceMax,Out->Global,Error))return nullptr;
        Out->bGlobal=Global;
        auto Regions=Recipe.TerrainRegions;Regions.Sort([](const auto& A,const auto& B){return A.Priority!=B.Priority?A.Priority<B.Priority:A.Id.ToString(EGuidFormats::Digits)<B.Id.ToString(EGuidFormats::Digits);});
        for(const auto& R:Regions)if(R.bEnabled&&R.ElevationDistribution.bEnabled)
        {
            auto& Compiled=Out->Regions.Emplace_GetRef();Compiled.Id=R.Id;Compiled.Name=R.Name;Compiled.bPlaced=R.bPlacedLandform;Compiled.Mask=R.SphericalMask;
            if(R.bPlacedLandform&&!FCLCompiledLandformPlacement::Resolve(R.Placement,Recipe.BaseStructure,Compiled.Placement,Error))return nullptr;
            TArray<double> Local;
            for(int32 I=0;I<Samples;++I)if(Compiled.Weight(Directions[I],Out->ReferenceRadius)>.9999)Local.Add(Heights[I]);
            if(!Fit(R.ElevationDistribution,MoveTemp(Local),Range.SourceMin,Range.SourceMax,Compiled.Curve,Error)){Error=R.Name+TEXT(": ")+Error;return nullptr;}
        }
        return Out;
    }
};
double FCLSurfaceRange::Map(double H)const{return Map(H,FVector::ZeroVector);}
double FCLSurfaceRange::Map(double H,const FVector& D)const
{
    const double X=FMath::Clamp((H-SourceMin)/(SourceMax-SourceMin),0.,1.);
    // Preserve published double-precision linear mapping and exact endpoints.
    return FMath::Lerp(TargetMin,TargetMax,Distribution&&X>0&&X<1?Distribution->Map(X,D.GetSafeNormal()):X);
}
void FCLSurfaceRange::MapHeights(TConstArrayView<FVector> Directions,TArray<double>& Heights)const
{for(int32 I=0;I<Heights.Num();++I)Heights[I]=Map(Heights[I],Directions[I]);}
TSharedPtr<const FCLSurfaceRange> FCLSurfaceRange::Compile(const FCLCompiledPlanetRecipe& Surface,double Low,double High,FString& Error,const FCLPlanetRecipe* DistributionRecipe)
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
    TArray<double> Heights;if(!FCLCompileProgress::Sample(Surface,Directions,Heights,Error))return nullptr;
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
            TArray<double> Values;if(!FCLCompileProgress::Sample(Surface,Probe,Values,Error))return nullptr;int32 Best=INDEX_NONE;
            for(int32 I=0;I<Values.Num();++I)if(Sign*Values[I]>Sign*H){H=Values[I];Best=I;}
            if(Best==INDEX_NONE)break;D=Probe[Best];
        }
        if(H<Out->SourceMin){Out->SourceMin=H;Out->MinimumDirection=D;}
        if(H>Out->SourceMax){Out->SourceMax=H;Out->MaximumDirection=D;}
    }
    if(Out->SourceMax-Out->SourceMin<1.e-8)
    {Error=TEXT("A constant surface cannot reach two distinct surface guides; enable a shape layer or separate the guide values");return nullptr;}
    if(DistributionRecipe)
    {
        Out->Distribution=FCLCompiledSurfaceDistribution::Compile(*DistributionRecipe,Surface,*Out,Error);
        if(!Error.IsEmpty())return nullptr;
    }
    return Out;
}

#pragma once
#include "Widgets/SLeafWidget.h"
#include "Model/CLRockLayer.h"
#include "Rendering/DrawElements.h"

/** A distribution diagnostic only. Terrain/materials are presented by VPP. */
class SCLRockThicknessPlot final:public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SCLRockThicknessPlot){}
        SLATE_ATTRIBUTE(FCLRockBedStatistics,Measured)
        SLATE_ATTRIBUTE(TArray<double>,Target)
    SLATE_END_ARGS()
    void Construct(const FArguments& A){Measured=A._Measured;Target=A._Target;}
    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(330,120);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& D,int32 L,const FWidgetStyle&,bool)const override
    {
        const auto S=Measured.Get();const auto T=Target.Get();if(!S.Count||T.Num()!=S.BinCount)return L;
        double Max=0;for(int32 I=0;I<S.BinCount;++I)Max=FMath::Max(Max,FMath::Max(T[I],FMath::Max(S.Counts[I]/double(S.Count),S.DepthMeters[I]/S.TotalMeters)));
        const auto Size=G.GetLocalSize();const FVector2D O(8,8),E(Size.X-16,Size.Y-16);
        auto P=[&](double X,double Y){return O+FVector2D(E.X*X,E.Y*(1-Y/FMath::Max(Max,.001)));};
        for(int32 I=0;I<=4;++I){TArray<FVector2D> Line{P(0,Max*I/4.),P(1,Max*I/4.)};FSlateDrawElement::MakeLines(D,L,G.ToPaintGeometry(),Line,ESlateDrawEffect::None,FLinearColor(.18,.18,.18),true);}
        TArray<FVector2D> Desired,Count,Depth;
        for(int32 I=0;I<S.BinCount;++I){const double X=(I+.5)/S.BinCount;Desired.Add(P(X,T[I]));Count.Add(P(X,S.Counts[I]/double(S.Count)));Depth.Add(P(X,S.DepthMeters[I]/S.TotalMeters));}
        FSlateDrawElement::MakeLines(D,L+1,G.ToPaintGeometry(),Desired,ESlateDrawEffect::None,FLinearColor(.2,.65,1),true,2);
        FSlateDrawElement::MakeLines(D,L+2,G.ToPaintGeometry(),Count,ESlateDrawEffect::None,FLinearColor(1,.65,.2),true,2);
        FSlateDrawElement::MakeLines(D,L+3,G.ToPaintGeometry(),Depth,ESlateDrawEffect::None,FLinearColor(.5,.85,.4),true,1);
        return L+3;
    }
private:
    TAttribute<FCLRockBedStatistics> Measured;
    TAttribute<TArray<double>> Target;
};

#pragma once
#include "Widgets/SLeafWidget.h"
#include "Model/CLSurfaceDistribution.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"

/** Statistical area/elevation plot only; terrain remains the native VPP preview. */
class SCLSurfaceDistributionPlot final:public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SCLSurfaceDistributionPlot){}
        SLATE_ATTRIBUTE(FCLSurfaceDistributionSettings,Profile)
        SLATE_ATTRIBUTE(TArray<FVector2D>,Measured)
    SLATE_END_ARGS()
    void Construct(const FArguments& A){Profile=A._Profile;Measured=A._Measured;}
    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(330,155);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Draw,int32 Layer,const FWidgetStyle&,bool)const override
    {
        const FVector2D Size=G.GetLocalSize(),Origin(8,8),Extent(Size.X-16,Size.Y-16);
        auto Point=[&](double X,double Y){return FVector2D(Origin.X+Extent.X*X,Origin.Y+Extent.Y*(1-Y));};
        for(int32 I=0;I<=4;++I)
        {
            const double T=I/4.;TArray<FVector2D> H{Point(0,T),Point(1,T)},V{Point(T,0),Point(T,1)};
            FSlateDrawElement::MakeLines(Draw,Layer,G.ToPaintGeometry(),H,ESlateDrawEffect::None,FLinearColor(.18,.18,.18),true);
            FSlateDrawElement::MakeLines(Draw,Layer,G.ToPaintGeometry(),V,ESlateDrawEffect::None,FLinearColor(.18,.18,.18),true);
        }
        const auto Curve=Profile.Get().TargetCurve();TArray<FVector2D> Target;
        for(int32 I=0;I<=128;++I){const double P=I/128.;Target.Add(Point(Curve.Eval(float(P)),P));}
        FSlateDrawElement::MakeLines(Draw,Layer+1,G.ToPaintGeometry(),Target,ESlateDrawEffect::None,FLinearColor(.2,.65,1),true,2);
        TArray<FVector2D> Actual;for(const auto& P:Measured.Get())Actual.Add(Point(P.X,P.Y));
        if(Actual.Num()>1)FSlateDrawElement::MakeLines(Draw,Layer+2,G.ToPaintGeometry(),Actual,ESlateDrawEffect::None,FLinearColor(1,.65,.2),true,2);
        return Layer+2;
    }
private:
    TAttribute<FCLSurfaceDistributionSettings> Profile;
    TAttribute<TArray<FVector2D>> Measured;
};

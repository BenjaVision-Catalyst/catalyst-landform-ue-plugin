#include "Presentation/CLSceneryRenderPolicy.h"
#include "Presentation/CLPlanetProjection.h"
FCLSceneryRenderPolicy FCLSceneryRenderPolicy::ForSize(double Size)
{
    FCLSceneryRenderPolicy P;
    if(Size>=600)return P;
    P.bDistantCanopy=false;
    if(Size>=180){P.FadeStartCm=30000;P.DrawEndCm=40000;P.WpoEndCm=8000;P.ShadowEndCm=10000;}
    else {P.FadeStartCm=7500;P.DrawEndCm=10000;P.WpoEndCm=4000;P.ShadowEndCm=4000;}
    return P;
}
bool FCLSceneryRenderPolicy::ProjectBounds(const FBox& Source,const FCLPlanetProjection& Projection,double Stretch,FBox& Out)
{
    if(!Source.IsValid||!FMath::IsFinite(Stretch)||Stretch<1)return false;
    FVector Center;if(!Projection.TryProjectPosition(Source.GetCenter(),Center))return false;
    const double Radius=Source.GetExtent().Length()*Stretch+200.;
    Out=FBox(Center-FVector(Radius),Center+FVector(Radius));return true;
}
double FCLSceneryRenderPolicy::CenteredBoundsScale(const FBox& Source,const FBox& Presented)
{
    const FVector Required=(Presented.GetCenter()-Source.GetCenter()).GetAbs()+Presented.GetExtent(),Extent=Source.GetExtent();
    double Scale=1;
    for(int32 Axis=0;Axis<3;++Axis)Scale=FMath::Max(Scale,Required[Axis]/FMath::Max(1.,Extent[Axis]));
    return Scale;
}

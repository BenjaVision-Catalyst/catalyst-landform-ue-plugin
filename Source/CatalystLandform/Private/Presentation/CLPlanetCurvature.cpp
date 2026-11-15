#include "Presentation/CLPlanetCurvature.h"
#include "Model/CLPlanetRecipe.h"
bool FCLPlanetCurvature::Evaluate(const FCLCompiledPlanetRecipe& Plan,double Radius,double Ground,
    double& Curvature,double& SurfaceMax,double& Sky,FString& Error)
{
    const auto* Guide=Plan.Guides.FindByPredicate([](const FCLHeightGuide& G){return G.Name==TEXT("SurfaceMax");});
    const double Outer=Plan.RadiusMeters,Surface=Guide?Outer+Guide->HeightMeters:0;
    if(!Guide||!FMath::IsFinite(Outer)||!FMath::IsFinite(Surface)||Surface<=0||Outer<=Surface||
       !FMath::IsFinite(Radius)||Radius<0||!FMath::IsFinite(Ground)||Ground<0||Ground>1)
    {Error=TEXT("Curvature requires ordered SurfaceMax/Sky boundaries, a finite radius and ground curvature in 0..1");return false;}
    const double T=FMath::Clamp((Radius-Surface)/(Outer-Surface),0.,1.);
    // Quintic smoothstep has zero first and second derivatives at both authored boundaries.
    const double Ease=T*T*T*(T*(T*6-15)+10);
    Curvature=FMath::Lerp(Ground,1.,Ease);SurfaceMax=Surface;Sky=Outer;Error.Reset();return true;
}

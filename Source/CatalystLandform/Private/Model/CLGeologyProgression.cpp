#include "Model/CLTectonics.h"
#include "Model/CLContinents.h"
namespace
{
// Five authored configurations, joined smoothly. This is a character tour,
// not a simulation timeline or a multiplier applied to a fixed landscape.
double At(double Position,std::initializer_list<double> Values)
{
    const double X=FMath::Clamp(Position,0.,1.)*4;const int32 I=FMath::Min(3,FMath::FloorToInt(X));
    const double T=X-I,S=T*T*(3-2*T);const auto* V=Values.begin();return FMath::Lerp(V[I],V[I+1],S);
}
}
void FCLTectonicPreset::ApplyProgression(double Position)
{
    AlgorithmVersion=4;Activity=FMath::Clamp(Position,0.,1.);
    PlateCount=FMath::RoundToInt(At(Activity,{2,4,7,9,12}));
    Motion=At(Activity,{.12,.7,1.15,.95,1.85});
    Relief=At(Activity,{.12,.4,.65,.82,1});
    BeltWidthDegrees=At(Activity,{18,14,10,8,12});
    CrustThickness=At(Activity,{.012,.02,.028,.045,.07});
    BoundaryWarp=At(Activity,{.12,.27,.38,.3,.52});
    Roughness=At(Activity,{.08,.2,.38,.58,.9});
    CollisionStrength=At(Activity,{.18,.3,.65,.9,1.3});
    RiftStrength=At(Activity,{.08,.75,.45,.3,.75});
    TransformStrength=At(Activity,{.05,.35,.5,.35,.8});
    InteriorRelief=At(Activity,{.15,.1,.08,.055,.035});
    bOverridePlateCount=bOverrideMotion=bOverrideRelief=false;
}
void FCLContinentalLayer::ApplyProgression(double Position)
{
    AlgorithmVersion=2;Strength=FMath::Clamp(Position,0.,1.);
    DomainCount=FMath::RoundToInt(At(Strength,{1,2,3,5,8}));
    Extent=At(Strength,{.7,.95,1.25,.95,.7});
    Relief=At(Strength,{.25,.45,.6,.8,1});
    CoastWarp=At(Strength,{.06,.16,.28,.4,.6});
    InteriorRoughness=At(Strength,{.01,.02,.035,.055,.08});
    CoastSharpness=At(Strength,{.8,1,1.4,1.7,2.2});
    bOverrideExtent=bOverrideRelief=false;
}

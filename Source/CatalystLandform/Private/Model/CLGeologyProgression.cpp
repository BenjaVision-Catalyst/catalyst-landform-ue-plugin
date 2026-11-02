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
    AlgorithmVersion=5;Activity=FMath::Clamp(Position,0.,1.);
    // Keep broad interior structure present while narrow deformation develops.
    // Final planet range fitting would otherwise amplify even young ridges to full height.
    PlateCount=FMath::RoundToInt(At(Activity,{2,3,5,7,10}));
    Motion=At(Activity,{.10,.24,.42,.36,1.40});
    Relief=At(Activity,{.08,.12,.20,.36,.75});
    BeltWidthDegrees=At(Activity,{18,15,12,9,10});
    CrustThickness=At(Activity,{.012,.017,.023,.030,.050});
    BoundaryWarp=At(Activity,{.10,.16,.24,.22,.36});
    Roughness=At(Activity,{.06,.10,.17,.30,.65});
    CollisionStrength=At(Activity,{.10,.16,.24,.40,.95});
    RiftStrength=At(Activity,{.06,.24,.18,.16,.60});
    TransformStrength=At(Activity,{.04,.12,.20,.24,.55});
    InteriorRelief=At(Activity,{.16,.16,.15,.14,.14});
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

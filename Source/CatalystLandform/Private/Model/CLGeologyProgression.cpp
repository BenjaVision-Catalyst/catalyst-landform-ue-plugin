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
struct FKey { double Position,Value; };
double Track(double Position,std::initializer_list<FKey> Keys)
{
    auto Previous=Keys.begin();
    for(auto Next=Previous+1;Next!=Keys.end();++Next)
    {
        if(Position<=Next->Position)
        {
            const double T=FMath::Clamp((Position-Previous->Position)/(Next->Position-Previous->Position),0.,1.);
            return FMath::Lerp(Previous->Value,Next->Value,T*T*(3-2*T));
        }
        Previous=Next;
    }
    return Previous->Value;
}
}
void FCLTectonicPreset::ApplyProgression(double Position)
{
    AlgorithmVersion=6;Activity=FMath::Clamp(Position,0.,1.);
    // Each channel has its own entry, peak and hold intervals. This is a curated
    // character progression, not a common intensity multiplier or a history clock.
    const double Count=Track(Activity,{{0,1},{.12,1.5},{.30,3},{.52,5},{.74,7},{.84,8},{1,8}});
    PlateCount=FMath::FloorToInt(Count);const double Fraction=Count-PlateCount;
    PlateTransition=Fraction*Fraction*(3-2*Fraction);
    Motion=Track(Activity,{{0,0},{.10,.35},{.28,.90},{.50,.75},{.72,.85},{.88,1.20},{1,1.50}});
    Relief=Track(Activity,{{0,0},{.12,.14},{.30,.32},{.50,.46},{.72,.64},{.88,.78},{1,.92}});
    BeltWidthDegrees=Track(Activity,{{0,20},{.16,20},{.36,16},{.60,12},{.82,8},{1,7}});
    CrustThickness=Track(Activity,{{0,.008},{.22,.014},{.58,.028},{.82,.042},{1,.05}});
    BoundaryWarp=Track(Activity,{{0,.10},{.12,.10},{.40,.22},{.68,.34},{.88,.34},{1,.40}});
    Roughness=Track(Activity,{{0,0},{.18,0},{.45,.10},{.72,.26},{.90,.58},{1,.76}});
    CollisionStrength=Track(Activity,{{0,0},{.22,0},{.42,.35},{.62,.85},{.84,1.15},{1,1.30}});
    RiftStrength=Track(Activity,{{0,0},{.08,0},{.25,.85},{.38,1.25},{.58,.70},{.78,.55},{1,.55}});
    TransformStrength=Track(Activity,{{0,0},{.18,0},{.42,.90},{.60,.90},{.84,1.15},{1,1.15}});
    InteriorRelief=Track(Activity,{{0,0},{.10,.035},{.24,.060},{.44,.050},{.68,.040},{1,.040}});
    InteriorFrequency=Track(Activity,{{0,.8},{.12,1.0},{.34,1.8},{.56,2.8},{.78,3.6},{1,4.5}});
    FoldStrength=Track(Activity,{{0,0},{.38,0},{.60,.30},{.78,.75},{.92,1.25},{1,1.65}});
    ArcStrength=Track(Activity,{{0,0},{.32,0},{.54,.65},{.74,1.05},{.90,1.30},{1,1.30}});
    TrenchStrength=Track(Activity,{{0,0},{.40,0},{.66,.55},{.88,1.05},{1,1.20}});
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

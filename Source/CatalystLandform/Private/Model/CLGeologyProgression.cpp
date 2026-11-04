#include "Model/CLTectonics.h"
#include "Model/CLContinents.h"
#include "Model/CLRockLayer.h"
namespace
{
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
    AlgorithmVersion=3;Strength=FMath::Clamp(Position,0.,1.);
    const double Count=Track(Strength,{{0,1},{.22,1.6},{.50,3},{.78,4.8},{.94,6},{1,6}});
    DomainCount=FMath::FloorToInt(Count);const double Fraction=Count-DomainCount;
    DomainTransition=Fraction*Fraction*(3-2*Fraction);
    Extent=Track(Strength,{{0,.8},{.18,.95},{.40,1.18},{.55,1.3},{.78,1.02},{1,.74}});
    Relief=Track(Strength,{{0,0},{.15,.28},{.40,.60},{.70,.85},{1,1}});
    CoastWarp=Track(Strength,{{0,.08},{.22,.13},{.50,.24},{.78,.35},{1,.42}});
    InteriorRoughness=Track(Strength,{{0,.012},{.35,.018},{.68,.026},{1,.035}});
    CoastSharpness=Track(Strength,{{0,1},{.40,1.1},{.70,1.25},{1,1.4}});
    Elongation=Track(Strength,{{0,1.4},{.25,1.8},{.55,2.2},{.80,1.9},{1,1.6}});
    Fragmentation=Track(Strength,{{0,0},{.35,.08},{.58,.22},{.80,.55},{1,.85}});
    bOverrideExtent=bOverrideRelief=false;
}

void FCLRockLayer::ApplyProgression(double Position)
{
    if(AlgorithmVersion<4&&Units.Num()>0)FoldCompetentUnit=Units[0].Id;
    AlgorithmVersion=4;Character=FMath::Clamp(Position,0.,1.);
    BedThicknessRadiusFraction=Track(Character,{{0,.25},{.18,.16},{.42,.07},{.70,.025},{1,.008}});
    InterbedFraction=Track(Character,{{0,0},{.15,.08},{.40,.30},{.70,.48},{1,.65}});
    DipDegrees=Track(Character,{{0,0},{.20,0},{.50,4},{.80,10},{1,15}});
    FoldAmplitudeRadiusFraction=Track(Character,{{0,0},{.25,0},{.55,.006},{.80,.014},{1,.020}});
    FoldWavelengthRadiusFraction=Track(Character,{{0,.8},{.35,.65},{.65,.35},{1,.18}});
    TectonicResponse=Track(Character,{{0,0},{.20,.05},{.50,.30},{.80,.65},{1,.85}});
    FaultThrowRadiusFraction=Track(Character,{{0,0},{.45,0},{.70,.003},{1,.010}});
    FaultZoneRadiusFraction=Track(Character,{{0,.008},{.55,.006},{1,.002}});
    YoungerCoverRadiusFraction=Track(Character,{{0,0},{.45,0},{.70,.012},{1,.025}});
    DepositionalVariation=Track(Character,{{0,0},{.15,.15},{.45,.5},{.80,.9},{1,1}});
    FoldShorteningStrain=Track(Character,{{0,0},{.30,.03},{.55,.08},{.80,.13},{1,.15}});
}

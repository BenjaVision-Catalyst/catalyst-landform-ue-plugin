#include "Model/CLTectonics.h"
#include "Model/CLContinents.h"
#include "Model/CLRockLayer.h"
#include "Model/CLTransformLandform.h"
#include "Model/CLVolcanicArcLandform.h"
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
    AlgorithmVersion=5;Character=FMath::Clamp(Position,0.,1.);
    // Independent tracks make a geological palette, not an intensity/history
    // clock: quiet -> rhythmic -> laminated -> folded -> faulted -> basin fill.
    // Physical beds throughout. Tail rarity/limits remain independently authored.
    BedDistribution.PeakMeters=FMath::Lerp(BedDistribution.ThinTailUpperMeters,BedDistribution.ThickTailLowerMeters,Track(Character,{{0,.45},{.18,.35},{.38,.2},{.58,.3},{.78,.45},{1,.25}}));
    BedDistribution.SpreadMeters=FMath::Max(.1,(BedDistribution.ThickTailLowerMeters-BedDistribution.ThinTailUpperMeters)*Track(Character,{{0,.175},{.18,.2},{.38,.1375},{.58,.2125},{.78,.225},{1,.1875}}));
    // Keep the author's unit palette, rheology, crust depth and contact feather.
    InterbedFraction=Track(Character,{{0,0},{.18,.08},{.38,.64},{.58,.18},{.78,.32},{1,.52}});
    DipDegrees=Track(Character,{{0,0},{.18,1},{.38,4},{.58,14},{.78,7},{1,2}});
    FoldAmplitudeRadiusFraction=Track(Character,{{0,0},{.18,.0005},{.38,.002},{.58,.03},{.78,.012},{1,.004}});
    // Compatibility-only artistic wavelength; algorithm 4 derives it from beds.
    TectonicResponse=Track(Character,{{0,0},{.18,.03},{.38,.12},{.58,.5},{.78,.95},{1,.65}});
    FaultThrowRadiusFraction=Track(Character,{{0,0},{.38,0},{.58,.001},{.78,.024},{1,.004}});
    FaultZoneRadiusFraction=Track(Character,{{0,.01},{.18,.01},{.38,.006},{.58,.005},{.78,.0008},{1,.006}});
    DepositionalVariation=Track(Character,{{0,0},{.18,.08},{.38,.25},{.58,.35},{.78,.7},{1,1}});
    FoldShorteningStrain=Track(Character,{{0,0},{.18,.005},{.38,.025},{.58,.15},{.78,.08},{1,.025}});
}

void FCLTransformLandform::ApplyProgression(double Position)
{
    AlgorithmVersion=1;Character=FMath::Clamp(Position,0.,1.);
    ValleyDepthMeters=Track(Character,{{0,12},{.28,6},{.66,4},{1,5}});
    ValleyWidthMeters=Track(Character,{{0,75},{.35,55},{.70,45},{1,50}});
    RidgeHeightMeters=Track(Character,{{0,5},{.28,13},{.65,8},{1,39}});
    RidgeWidthMeters=Track(Character,{{0,55},{.30,42},{.65,48},{1,52}});
    LateralOffsetMeters=Track(Character,{{0,5},{.30,48},{.65,34},{1,54}});
    BasinDepthMeters=Track(Character,{{0,2},{.25,4},{.65,28},{1,10}});
    BasinWidthMeters=Track(Character,{{0,65},{.30,75},{.65,100},{1,70}});
    SecondaryStrandWeight=Track(Character,{{0,.12},{.30,.30},{.70,.85},{1,.50}});
    DissectionMeters=Track(Character,{{0,1},{.30,2},{.60,1.5},{1,5}});
}

void FCLVolcanicArcLandform::ApplyProgression(double Position)
{
    AlgorithmVersion=1;Character=FMath::Clamp(Position,0.,1.);
    BaseRadiusMeters=Track(Character,{{0,280},{.18,260},{.37,220},{.66,270},{1,295}});
    HeightMeters=Track(Character,{{0,32},{.20,44},{.43,76},{.72,107},{1,132}});
    FlankExponent=Track(Character,{{0,1.45},{.35,1.60},{.67,1.30},{1,1.25}});
    SecondaryHeight=Track(Character,{{0,.24},{.30,.12},{.58,.55},{.76,.80},{1,.72}});
    CraterRadiusMeters=Track(Character,{{0,38},{.34,27},{.67,44},{1,62}});
    CraterDepthMeters=Track(Character,{{0,6},{.40,12},{.72,23},{1,38}});
    IncisionMeters=Track(Character,{{0,7},{.33,3},{.62,9},{.85,18},{1,22}});
    LavaShoulderMeters=Track(Character,{{0,2},{.35,7},{.64,11},{1,6}});
    ApronHeightMeters=Track(Character,{{0,5},{.30,4},{.69,11},{1,14}});
    ApronReach=Track(Character,{{0,1.8},{.32,1.6},{.72,1.9},{1,2.15}});
    BreachStrength=Track(Character,{{0,.05},{.38,.02},{.68,.12},{.88,.43},{1,.70}});
    Asymmetry=Track(Character,{{0,.22},{.32,.10},{.67,.20},{1,.30}});
}

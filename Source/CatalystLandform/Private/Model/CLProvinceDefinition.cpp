#include "Model/CLProvinceDefinition.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLTectonics.h"
#include "Model/CLContinents.h"
namespace {uint64 Bit(ECLGeologicalEvidence E){return uint64(1)<<uint8(E);}}
void FCLProvinceGeology::Set(ECLGeologicalEvidence E,bool Value)
{Known|=Bit(E);if(Value)Present|=Bit(E);else Present&=~Bit(E);}
FCLProvinceGeology FCLProvinceGeology::Sample(const FCLCompiledPlanetRecipe& Base,const FVector& Direction)
{
    FCLProvinceGeology Out;const FVector D=Direction.GetSafeNormal();
    if(Base.Continents||Base.Tectonics)
    {
        const double C=Base.Continents?Base.Continents->Continentalness(D):Base.Tectonics->Continentalness(D);
        Out.Set(ECLGeologicalEvidence::ContinentalCrust,C>=.6);Out.Set(ECLGeologicalEvidence::OceanicCrust,C<=.4);
    }
    if(Base.Tectonics)
    {
        // Reuse the same finite warped plate belts that deform the rock. These
        // structural weights are independent of the fold phase/wavelength arguments.
        const auto S=Base.Tectonics->SampleRockDeformation(D,1,0,1);
        Out.Set(ECLGeologicalEvidence::Compression,S.Compression>=.02);
        Out.Set(ECLGeologicalEvidence::Extension,S.Extension>=.02);
        Out.Set(ECLGeologicalEvidence::Shear,S.Shear>=.02);
    }
    // Generic strength/erosion palettes, height and basin shape do not establish
    // lithology, age, deposition, eruption history, metamorphism or an ice crust.
    return Out;
}
const TCHAR* FCLProvinceDefinition::EvidenceName(ECLGeologicalEvidence E)
{
    static const TCHAR* Names[]={TEXT("Continental crust"),TEXT("Oceanic crust"),TEXT("Compression"),TEXT("Extension"),TEXT("Shear"),
        TEXT("Crystalline basement"),TEXT("Ancient basement"),TEXT("Sedimentary cover"),TEXT("Clastic sequence"),TEXT("Carbonate sequence"),TEXT("Evaporite sequence"),
        TEXT("Metamorphic sequence"),TEXT("Subduction history"),TEXT("Mafic flow history"),TEXT("Silicic eruption history"),TEXT("Intrusion history"),TEXT("Passive margin history"),TEXT("Impact history"),TEXT("Ice crust"),TEXT("Volcanic history")};
    return uint8(E)<UE_ARRAY_COUNT(Names)?Names[uint8(E)]:TEXT("Invalid geological evidence");
}
bool FCLProvinceDefinition::Validate(FString& Error)const
{
    auto Fail=[&](const TCHAR* Text){Error=Text;return false;};
    if(AlgorithmVersion!=1||uint8(Type)>uint8(ECLProvinceType::IcyCrust)||!FMath::IsFinite(MinimumCoverage)||MinimumCoverage<.01||MinimumCoverage>1||Requirements.Num()>20||RegionalForms.Num()>24||SubsurfacePotential.Num()>16)
        return Fail(TEXT("Province definitions require version 1, a valid type, bounded lists and coverage 0.01..1"));
    if(!IsActive()&&(bPlaceholder||!Requirements.IsEmpty()||!RegionalForms.IsEmpty()||!SubsurfacePotential.IsEmpty()))return Fail(TEXT("An untyped Landform cannot carry a province definition"));
    if(IsActive()&&(Requirements.IsEmpty()||RegionalForms.IsEmpty()))return Fail(TEXT("A province requires geological prerequisites and intended regional forms"));
    TSet<uint8> Seen;for(auto E:Requirements){if(uint8(E)>uint8(ECLGeologicalEvidence::VolcanicHistory)||Seen.Contains(uint8(E)))return Fail(TEXT("Province geological prerequisites must be valid and unique"));Seen.Add(uint8(E));}
    if(Seen.Contains(uint8(ECLGeologicalEvidence::ContinentalCrust))&&Seen.Contains(uint8(ECLGeologicalEvidence::OceanicCrust)))return Fail(TEXT("A province cannot require both continental and oceanic crust at the same sample"));
    for(const auto* Names:{&RegionalForms,&SubsurfacePotential})for(FName N:*Names)if(N.IsNone()||N.ToString().Len()>96)return Fail(TEXT("Province form/potential names must be nonempty and bounded"));
    return true;
}
ECLProvinceMatch FCLProvinceDefinition::Match(const FCLProvinceGeology& G,FString* Reason)const
{
    if(!IsActive())return ECLProvinceMatch::Eligible;
    FString Unknown;for(auto E:Requirements)
    {
        if(!(G.Known&Bit(E))){if(!Unknown.IsEmpty())Unknown+=TEXT(", ");Unknown+=EvidenceName(E);}
        else if(!(G.Present&Bit(E))){if(Reason)*Reason=FString(TEXT("Incompatible base geology: "))+EvidenceName(E);return ECLProvinceMatch::Incompatible;}
    }
    if(!Unknown.IsEmpty()){if(Reason)*Reason=TEXT("Missing base geological evidence: ")+Unknown;return ECLProvinceMatch::Unknown;}
    if(Reason)*Reason=TEXT("Base geological prerequisites satisfied");return ECLProvinceMatch::Eligible;
}

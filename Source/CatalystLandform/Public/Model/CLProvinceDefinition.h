#pragma once
#include "CoreMinimal.h"
#include "CLProvinceDefinition.generated.h"
struct FCLCompiledPlanetRecipe;

/** Reusable province profiles; not an exclusive partition of the planet's geological history. */
UENUM(BlueprintType)
enum class ECLProvinceType:uint8
{
    None,CrystallineShield,ContinentalPlatform,ClasticBasin,CarbonatePlatform,EvaporiteBasin,
    FoldThrustBelt,MetamorphicCore,ContinentalRift,TransformBelt,VolcanicArc,
    BasalticVolcanic,SilicicVolcanic,IntrusiveProvince,PassiveMargin,OceanicCrust,ImpactProvince,IcyCrust
};
UENUM(BlueprintType)
enum class ECLGeologicalEvidence:uint8
{
    ContinentalCrust,OceanicCrust,Compression,Extension,Shear,
    CrystallineBasement,AncientBasement,SedimentaryCover,ClasticSequence,CarbonateSequence,EvaporiteSequence,
    MetamorphicSequence,SubductionHistory,MaficFlowHistory,SilicicEruptionHistory,IntrusionHistory,
    PassiveMarginHistory,ImpactHistory,IceCrust,VolcanicHistory
};
/** Read-only evidence from upstream geology. Unavailable properties remain unknown. */
struct CATALYSTLANDFORM_API FCLProvinceGeology
{
    uint64 Known=0,Present=0;
    static FCLProvinceGeology Sample(const FCLCompiledPlanetRecipe&,const FVector& Direction);
    void Set(ECLGeologicalEvidence Evidence,bool Value);
};
UENUM()
enum class ECLProvinceMatch:uint8 {Eligible,Unknown,Incompatible};

/** Ordered gameplay fallback; the original requirements remain recorded. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLProvinceRelaxation
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName Name;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<ECLGeologicalEvidence> RelaxedRequirements;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.01",ClampMax="1")) double MinimumCoverage=.1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.01",ClampMax="1")) double PlacementWeight=.25;
};

USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLProvinceDefinition
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 AlgorithmVersion=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) ECLProvinceType Type=ECLProvinceType::None;
    /** Definition-only sources contribute neither relief nor a replacement surface material. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bPlaceholder=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<ECLGeologicalEvidence> Requirements;
    /** Soft placement evidence, including when the strict history requirements are relaxed. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<ECLGeologicalEvidence> PlacementPreferences;
    /** Share of pre-Landform area samples that must positively satisfy every requirement. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.01",ClampMax="1")) double MinimumCoverage=.5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FName> RegionalForms;
    /** Planned geological potential only; no cave generator is supplied by these definitions. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FName> SubsurfacePotential;
    /** Version 2: ordered per-cell relaxation after stricter fits have been preferred. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FCLProvinceRelaxation> Relaxations;
    /** Preferred relaxed-placement budget; mandatory full coverage may exceed it. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",ClampMax="128")) int32 MaximumRelaxedRegions=1;
    bool IsActive()const{return Type!=ECLProvinceType::None;}
    bool Validate(FString& Error)const;
    ECLProvinceMatch Match(const FCLProvinceGeology&,FString* Reason=nullptr,int32 RelaxationTier=0)const;
    static const TCHAR* EvidenceName(ECLGeologicalEvidence);
};

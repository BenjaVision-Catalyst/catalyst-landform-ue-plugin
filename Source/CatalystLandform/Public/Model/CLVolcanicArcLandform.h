#pragma once
#include "CoreMinimal.h"
#include "CLVolcanicArcLandform.generated.h"
class FCLCompiledTectonics;
struct FCLGeneratedRegionCell;

/** Current physical values. Character writes independent curves; compilation honors manual edits. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLVolcanicArcLandform
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 AlgorithmVersion=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Character=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Seed=137;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="20",Units="m",ForceUnits="m")) double BaseRadiusMeters=280;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="m",ForceUnits="m")) double HeightMeters=32;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.5",ClampMax="3")) double FlankExponent=1.45;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double SecondaryHeight=.24;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",Units="m",ForceUnits="m")) double CraterRadiusMeters=38;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="m",ForceUnits="m")) double CraterDepthMeters=6;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="m",ForceUnits="m")) double IncisionMeters=7;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="m",ForceUnits="m")) double LavaShoulderMeters=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="m",ForceUnits="m")) double ApronHeightMeters=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1.1",ClampMax="3")) double ApronReach=1.8;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double BreachStrength=.05;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="0.6")) double Asymmetry=.22;
    void ApplyProgression(double Position);
    bool IsActive()const{return AlgorithmVersion!=0;}
    bool Validate(FString& Error)const;
};

/** Seeded, finite volcanic centres shared by regional generation, Solo, rock and native VPP. */
class CATALYSTLANDFORM_API FCLCompiledVolcanicArcLandform
{
public:
    struct FCentre
    {
        FVector Direction,Along,Across;
        double Radius=1,HeightScale=1,Phase=0,Elongation=1,Breach=0;
        int32 Gullies=12;
        bool bSecondary=false,bPlateGuided=false;
    };
    static TSharedPtr<const FCLCompiledVolcanicArcLandform> Compile(const FCLVolcanicArcLandform&,int32 PlanetSeed,double ReferenceRadius,TConstArrayView<FCLGeneratedRegionCell>,TSharedPtr<const FCLCompiledTectonics>,FString& Error);
    double SampleMeters(const FVector& Direction)const;
    double MaximumDisplacementMeters()const;
    const TArray<FCentre>& GetCentres()const{return Centres;}
    double ReferenceRadius()const{return Radius;}
private:
    FCLVolcanicArcLandform Settings;
    double Radius=1,Scale=1;
    TArray<FCentre> Centres;
};

#pragma once
#include "CoreMinimal.h"
#include "Scene/CLSceneQuery.h"
#include "CLRockLayer.generated.h"
class FCLCompiledTectonics;
class FCLCompiledContinents;
class FCLNativeNoiseKernel;
struct FCLCompiledSurfaceLibrary;
struct FCLSurfaceWeights;

/** Geological identity is independent of the client artwork used to expose it. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLRockUnit
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FGuid Id;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName Name;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName SurfaceId=TEXT("Rock");
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 SurfaceRevision=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double MiningResistance=.8;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Erodibility=.2;
};

/** Resolved structure controls, not a second height generator. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLRockLayer
{
    GENERATED_BODY()
    FCLRockLayer();
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 AlgorithmVersion=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Character=.55;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Seed=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="AlgorithmVersion == 1",EditConditionHides)) FRotator Orientation=FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.00001",ClampMax="1")) double BedThicknessRadiusFraction=.04;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="0.8")) double InterbedFraction=.3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="25",Units="deg")) double DipDegrees=8;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="0.05")) double FoldAmplitudeRadiusFraction=.005;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.01",ClampMax="2")) double FoldWavelengthRadiusFraction=.4;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double TectonicResponse=.3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="0.05")) double FaultThrowRadiusFraction=.004;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.0001",ClampMax="0.1")) double FaultZoneRadiusFraction=.002;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double CrustDepthRadiusFraction=.15;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="0.1")) double ContactFeatherFraction=.015;
    /** V2 cover thickness; V3 reference thickness controls the finite younger bed sequence, not sediment supply. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="0.1",EditCondition="AlgorithmVersion >= 2",EditConditionHides)) double YoungerCoverRadiusFraction=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(TitleProperty="Name")) TArray<FCLRockUnit> Units;
    /** Algorithm 3: onlap over crust- and extension-conditioned basement relief. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1",EditCondition="AlgorithmVersion >= 3",EditConditionHides)) double DepositionalVariation=0;
    void ApplyProgression(double Position);
};

USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLRockSample
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bValid=false;
    UPROPERTY(BlueprintReadOnly) FGuid UnitId;
    UPROPERTY(BlueprintReadOnly) FGuid StratumId;
    UPROPERTY(BlueprintReadOnly) int64 StratumIndex=0;
    UPROPERTY(BlueprintReadOnly) FName Name;
    UPROPERTY(BlueprintReadOnly) FName SurfaceId;
    UPROPERTY(BlueprintReadOnly) int32 SurfaceRevision=0;
    UPROPERTY(BlueprintReadOnly) double MiningResistance=0;
    UPROPERTY(BlueprintReadOnly) double Erodibility=0;
    UPROPERTY(BlueprintReadOnly) double PlacedFraction=0;
};

/** One finite deposited bed; Unit indexes the authored property palette. */
struct CATALYSTLANDFORM_API FCLRockBed
{
    int32 Unit=0;
    double LowerMeters=0,UpperMeters=0;
    bool bYounger=false;
};

/** Canonical history inspection; radii in body metres. No live terrain/edit state. */
struct CATALYSTLANDFORM_API FCLRockColumn
{
    bool bValid=false;
    double Continentalness=0,Extension=0;
    double RestoredBasementRadiusMeters=0,DeformationMeters=0;
    double YoungerFloorRadiusMeters=0,YoungerThicknessMeters=0;
};

class CATALYSTLANDFORM_API FCLCompiledRock final
{
public:
    static TSharedPtr<const FCLCompiledRock> Compile(const FCLRockLayer&,int32 PlanetSeed,double OuterRadius,double SurfaceMinRadius,double SurfaceMaxRadius,const FCLCompiledSurfaceLibrary*,TSharedPtr<const FCLCompiledTectonics>,FString& Error,TSharedPtr<const FCLCompiledContinents> Crust=nullptr);
    FCLRockSample Sample(const FVector3d& BodyMeters)const;
    FLinearColor Properties(const FVector3d& BodyMeters)const;
    void ApplySurfaces(const FVector3d& BodyMeters,FCLSurfaceWeights&)const;
    FCLRockColumn SampleColumn(const FVector3d& UnitDirection) const;
    const TArray<FCLRockBed>& GetBeds() const {return Beds;}
    /** Restored pre-deformation radius; also used for geological section inspection. */
    double RestoredRadiusMeters(const FVector3d&) const;
    static const TCHAR* PropertiesAssetPath();
    static FLinearColor FillProperties(double Resistance,double Erodibility);
private:
    struct FPoint {int32 Unit=0,Other=0;int64 Bed=0;double Alpha=1;};
    FPoint Classify(const FVector3d&)const;
    FPoint ClassifyFinite(const FVector3d&)const;
    double DeformationMeters(const FVector3d& UnitDirection,double* Extension=nullptr) const;
    TArray<FCLRockBed> Beds;
    int32 OlderBedCount=0;
    FCLRockLayer Layer;
    TArray<int32> SurfaceIndices;
    TSharedPtr<const FCLNativeNoiseKernel> NativeNoise;
    TSharedPtr<const FCLCompiledTectonics> Tectonics;
    TSharedPtr<const FCLCompiledContinents> Crust;
    FQuat Frame=FQuat::Identity;
    FGuid Namespace;
    double Radius=1,Datum=1,Basement=0,Phase=0,Unconformity=0;
    int32 NoiseSeed=0;
};

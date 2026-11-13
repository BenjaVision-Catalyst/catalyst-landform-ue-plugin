#pragma once
class FCLCompiledRegionGeneration;
#include "CoreMinimal.h"
#include "Scene/CLSceneQuery.h"
#include "Curves/RichCurve.h"
#include "CLRockLayer.generated.h"
class FCLCompiledTectonics;
class FCLCompiledContinents;
struct FCLCompiledSurfaceLibrary;
struct FCLSurfaceWeights;

UENUM(BlueprintType)
enum class ECLRockLithology:uint8 {Unknown,Crystalline,Clastic,Carbonate,Evaporite,MaficVolcanic,SilicicVolcanic,Metamorphic,Ice};

UENUM(BlueprintType)
enum class ECLMaterialOrigin:uint8 {Unknown,Rock,LandformDeposit,PlacedFill};

/** Geological identity is independent of the client artwork used to expose it. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLRockUnit
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FGuid Id;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName Name;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) ECLRockLithology Lithology=ECLRockLithology::Unknown;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double FracturePotential=0;
    /** Declared soluble-host capability; the first dissolution family accepts carbonate only. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double DissolutionPotential=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(DisplayName="Exposed rock material",GetOptions="CatalystLandform.CLSurfaceLibraryAsset.GetSurfaceIds")) FName SurfaceId=TEXT("Rock");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(DisplayName="Material revision")) int32 SurfaceRevision=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double MiningResistance=.8;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Erodibility=.2;
    bool Validate(FString& Error)const;
};

/** Algorithm 5: probabilities are by deposited bed count, independently of planet radius. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLRockBedDistribution
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.1",ClampMax="1000",ForceUnits="m")) double MinimumMeters=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.1",ClampMax="1000",ForceUnits="m")) double ThinTailUpperMeters=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.1",ClampMax="1000",ForceUnits="m")) double PeakMeters=3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.1",ClampMax="1000",ForceUnits="m")) double SpreadMeters=.85;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.1",ClampMax="1000",ForceUnits="m")) double ThickTailLowerMeters=6;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.1",ClampMax="1000",ForceUnits="m")) double MaximumMeters=10;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.1",ClampMax="20",Units="%")) double ThinBedPercent=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.1",ClampMax="20",Units="%")) double ThickBedPercent=5;
    bool Validate(FString& Error) const;
    FRichCurve QuantileCurve() const;
};

/** Stored older sequence, excluding the final possibly truncated bed. No exposure/area weighting. */
struct CATALYSTLANDFORM_API FCLRockBedStatistics
{
    static constexpr int32 BinCount=20;
    int32 Count=0;
    double TotalMeters=0,MinimumMeters=0,MaximumMeters=0;
    int32 Counts[BinCount]={};
    double DepthMeters[BinCount]={};
};

/** Resolved structure controls, not a second height generator. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLRockLayer
{
    GENERATED_BODY()
    FCLRockLayer();
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 AlgorithmVersion=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Character=.55;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Seed=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="0.8")) double InterbedFraction=.3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="25",Units="deg")) double DipDegrees=8;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="0.05")) double FoldAmplitudeRadiusFraction=.005;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double TectonicResponse=.3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="0.05")) double FaultThrowRadiusFraction=.004;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.0001",ClampMax="0.1")) double FaultZoneRadiusFraction=.002;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double CrustDepthRadiusFraction=.15;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="0.1")) double ContactFeatherFraction=.015;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(TitleProperty="Name")) TArray<FCLRockUnit> Units;
    /** Onlap over crust- and extension-conditioned basement relief. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double DepositionalVariation=0;
    /** Effective competent-bed / weak-matrix Newtonian viscosity ratio; independent of mining hardness. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",ClampMax="1000")) double FoldViscosityContrast=64;
    /** Maximum shortening proxy under the existing local compression field. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="0.15")) double FoldShorteningStrain=.08;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FGuid FoldCompetentUnit;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FCLRockBedDistribution BedDistribution;
    void ApplyProgression(double Position);
    /** Explicit appearance-only rebind; structural and geological values stay exact. */
    bool BindBedrockMaterials(const FCLCompiledSurfaceLibrary& Library,FString& Error);
};

USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLRockSample
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bValid=false;
    UPROPERTY(BlueprintReadOnly) ECLMaterialOrigin Origin=ECLMaterialOrigin::Unknown;
    UPROPERTY(BlueprintReadOnly) FGuid SourceId;
    UPROPERTY(BlueprintReadOnly) FGuid DepositId;
    UPROPERTY(BlueprintReadOnly) FGuid SubstrateUnitId;
    UPROPERTY(BlueprintReadOnly) FGuid SubstrateStratumId;
    UPROPERTY(BlueprintReadOnly) double DepositDepthMeters=0;
    UPROPERTY(BlueprintReadOnly) double DepositThicknessMeters=0;
    UPROPERTY(BlueprintReadOnly) FGuid UnitId;
    UPROPERTY(BlueprintReadOnly) FGuid StratumId;
    UPROPERTY(BlueprintReadOnly) int64 StratumIndex=0;
    UPROPERTY(BlueprintReadOnly) FName Name;
    UPROPERTY(BlueprintReadOnly) FName SurfaceId;
    UPROPERTY(BlueprintReadOnly) int32 SurfaceRevision=0;
    UPROPERTY(BlueprintReadOnly) FName VisibleSurfaceId;
    UPROPERTY(BlueprintReadOnly) int32 VisibleSurfaceRevision=0;
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

/** Homogenized early-stage single-competent-layer prescription, not a continuum solver. */
struct CATALYSTLANDFORM_API FCLRockFolding
{
    double CompetentThicknessMeters=0,ViscosityContrast=1,WavelengthMeters=0,ReferenceRadiusMeters=0;
    double AmplificationRate=0,ShorteningStrain=0,AmplitudeLimitMeters=0;
    bool bActive=false;
    static bool Resolve(double ThicknessMeters,double Contrast,double Shortening,double ArtistAmplitudeLimitMeters,FCLRockFolding& Out,FString& Error);
    double AmplitudeMeters(double Compression) const;
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
    /** Conservative first profile: no selective host clipping is claimed. */
    bool HasFractureHosts()const;
    double FracturePotential()const;
    bool HasCarbonateSequence()const;
    bool HasKarstHosts()const;
    FLinearColor Properties(const FVector3d& BodyMeters)const;
    void ApplySurfaces(const FVector3d& BodyMeters,FCLSurfaceWeights&)const;
    /** Prepared directional inputs may resolve constant bed interiors only.
     * Contacts, feathering and uncertain column boundaries retain exact evaluation. */
    bool TryStableSurfaces(const FVector3d&,double Deformation,double BasementRadius,double InputTolerance,FCLSurfaceWeights&)const;
    const FCLRockFolding& GetFolding() const {return Folding;}
    FCLRockColumn SampleColumn(const FVector3d& UnitDirection) const;
    int32 GetBedCount() const {return MetricUnits.Num();}
    int32 GetOlderBedCount() const {return OlderBedCount;}
    FCLRockBed GetBed(int32 Index) const;
    FCLRockBedStatistics ThicknessStatistics() const;
    uint64 HistoryBytes() const {return MetricUppers.GetAllocatedSize()+MetricUnits.GetAllocatedSize();}
    static constexpr int32 MaximumMetricBeds=4194304;
    TSharedPtr<const FCLCompiledRock> WithProvinceDeformation(TSharedPtr<const FCLCompiledRegionGeneration>)const;
    /** Restored pre-deformation radius; also used for geological section inspection. */
    double RestoredRadiusMeters(const FVector3d&) const;
    static const TCHAR* PropertiesAssetPath();
    static FLinearColor FillProperties(double Resistance,double Erodibility);
private:
    struct FPoint {int32 Unit=0,Other=0;int64 Bed=0;double Alpha=1;};
    FPoint Classify(const FVector3d&)const;
    FPoint ClassifyFinite(const FVector3d&)const;
    double DeformationMeters(const FVector3d& UnitDirection,double* Extension=nullptr) const;
    // Immutable double-precision contacts + palette indices; no per-view state.
    TArray<double> MetricUppers;
    TArray<uint8> MetricUnits;
    int32 OlderBedCount=0;
    FCLRockLayer Layer;
    FCLRockFolding Folding;
    TArray<int32> SurfaceIndices;
    TSharedPtr<const FCLCompiledTectonics> Tectonics;
    TSharedPtr<const FCLCompiledRegionGeneration> Province;
    TSharedPtr<const FCLCompiledRock> ProvinceBase;
    FVector3d ProvinceReferencePosition(const FVector3d&)const;
    TSharedPtr<const FCLCompiledContinents> Crust;
    FGuid Namespace;
    double Radius=1,Datum=1,Basement=0,Phase=0;
};

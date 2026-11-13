#pragma once
#include "CoreMinimal.h"
#include "CLTectonics.generated.h"
class FCLCompiledContinents;

/** Finite warped belt query in body coordinates; not a second plate solver. */
struct CATALYSTLANDFORM_API FCLPlateBeltSample
{
    FGuid BoundaryId;
    FVector AlongDirection=FVector::ZeroVector;
    double SignedShear=0;
    double AcrossRadians=0,AlongRadians=0,WidthRadians=0,Envelope=0;
    double CompressionDrive=0,ExtensionDrive=0,ExtensionWeight=0,ShearDrive=0,StateWeight=1;
};

/** Local kinematic bedding response in the existing finite plate belts.
 * Fold and Fault are bounded weights; Tilt is an angular offset. No terrain output. */
struct CATALYSTLANDFORM_API FCLRockDeformation
{
    double UpliftMeters=0,Fold=0,Tilt=0,Fault=0,Extension=0,Compression=0,FoldWave=0,Shear=0;
};

/** Versioned artistic progression. Body-space geometry uses +Z north, +X prime meridian. */
USTRUCT(BlueprintType,meta=(DisplayName="Tectonic layer"))
struct CATALYSTLANDFORM_API FCLTectonicPreset
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=false;
    // Keep the serialized default: assets that omit this property retain algorithm 1.
    // Guided authoring explicitly adopts algorithm 6; old fixtures retain their version.
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 AlgorithmVersion=6;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Activity=.55;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Seed=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName LowerGuide=TEXT("SurfaceMin");
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName UpperGuide=TEXT("SurfaceMax");
    UPROPERTY() bool bOverridePlateCount=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",ClampMax="12")) int32 PlateCount=7;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1",DisplayName="Next plate blend",EditCondition="AlgorithmVersion >= 6",EditConditionHides)) double PlateTransition=0;
    UPROPERTY() bool bOverrideMotion=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="2")) double Motion=1;
    UPROPERTY() bool bOverrideRelief=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Relief=.5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="2",ClampMax="25",Units="deg")) double BeltWidthDegrees=10;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.001",ClampMax="0.1",DisplayName="Crust thickness ratio")) double CrustThickness=.03;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double BoundaryWarp=.3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Roughness=.4;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="2")) double CollisionStrength=.65;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="2")) double RiftStrength=.45;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="2")) double TransformStrength=.5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double InteriorRelief=.08;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.5",ClampMax="8",DisplayName="Regional frequency",EditCondition="AlgorithmVersion >= 6",EditConditionHides)) double InteriorFrequency=1.8;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="2",EditCondition="AlgorithmVersion >= 6",EditConditionHides)) double FoldStrength=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="2",EditCondition="AlgorithmVersion >= 6",EditConditionHides)) double ArcStrength=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="2",EditCondition="AlgorithmVersion >= 6",EditConditionHides)) double TrenchStrength=1;
    /** Replaces the resolved shape settings; seed, enabled state and guides are retained. */
    void ApplyProgression(double Position);
};
struct CATALYSTLANDFORM_API FCLTectonicControls
{
    int32 LowerPlateCount=1,UpperPlateCount=1;
    double PlateTransition=0,Motion=0,CrustThickness=0,BeltWidth=0,Relief=0;
    double BoundaryWarp=.38,Roughness=1,CollisionStrength=1,RiftStrength=1,TransformStrength=1,InteriorRelief=0;
    double InteriorFrequency=1.8,FoldStrength=0,ArcStrength=1,TrenchStrength=1;
    static bool Resolve(const FCLTectonicPreset& Preset,FCLTectonicControls& Out,FString& Error,bool Layered=false);
};
/** Immutable kinematic construction, not a mantle/history simulation. All lengths are metre offsets. */
class CATALYSTLANDFORM_API FCLCompiledTectonics
{
public:
    struct FPlate {FGuid Id;FVector Site,AngularVelocity;};
    struct FBoundary
    {
        TArray<FVector2D> CrustSamples;
        FGuid Id;int32 A=0,B=0;
        FVector Start,End,Mid,Normal;
        double HalfCos=0,CapCos=0,Convergence=0,Shear=0,ContinentalA=0,ContinentalB=0;
    };
    struct FState {TArray<FPlate> Plates;TArray<FBoundary> Boundaries;};
    FCLTectonicControls Controls;
    FState Lower,Upper;
    TArray<FVector> Continents;
    double SpanMeters=0,MaximumDeltaMeters=0;
    double Continentalness(const FVector& UnitDirection) const;
    double SampleDeltaMeters(const FVector& UnitDirection) const;
    FCLRockDeformation SampleRockDeformation(const FVector& UnitDirection,double WavelengthRadians,double PhaseRadians,double FaultWidthRadians) const;
    void VisitExtensionBelts(const FVector& Direction,TFunctionRef<void(const FCLPlateBeltSample&)> Visitor)const;
    /** Effective shape contribution, independent of the authored enabled checkbox. */
    bool HasEffect() const;
    /** Algorithm 3 only. Invalid inputs preserve Out. */
    static TSharedPtr<const FCLCompiledTectonics> Compile(const FCLTectonicPreset& Preset,int32 Seed,double SpanMeters,FString& Error,bool Layered=false,TSharedPtr<const FCLCompiledContinents> Crust=nullptr,bool Calibrated=false);
private:
    void VisitBelts(const FState&,const FVector& Direction,double StateWeight,TFunctionRef<void(const FCLPlateBeltSample&)> Visitor)const;
    bool bLayered=false,bCalibrated=false;
    TSharedPtr<const FCLCompiledContinents> SharedCrust;
    FQuat NoiseRotation=FQuat::Identity;
    FVector NoiseOffset=FVector::ZeroVector;
    FVector WarpDirection(const FVector&) const;
    double SampleState(const FState& State,const FVector& D) const;
};

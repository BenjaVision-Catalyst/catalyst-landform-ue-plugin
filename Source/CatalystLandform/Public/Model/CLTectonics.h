#pragma once
#include "CoreMinimal.h"
#include "CLTectonics.generated.h"
class FCLCompiledContinents;

UENUM(BlueprintType)
enum class ECLContinentalSetting : uint8 { Collision, Subduction, Rift, Transform };
enum class ECLGeologicalSetting : uint8 { Interior, ContinentalCollision, Subduction, ContinentalRift, OceanSpreading, Transform };

/** Shared body-space context for later regional assignment. Thickness is a scaled approximation. */
struct CATALYSTLANDFORM_API FCLGeologicalSample
{
    double Continentalness=0,CrustThicknessMeters=0,BaseDeltaMeters=0,DeformationMeters=0,TotalDeltaMeters=0;
    double Convergence=0,Shear=0;
    FGuid BoundaryId;
    ECLGeologicalSetting Setting=ECLGeologicalSetting::Interior;
};

/** Versioned artistic progression. Body-space geometry uses +Z north, +X prime meridian. */
USTRUCT(BlueprintType,meta=(DisplayName="Tectonic layer"))
struct CATALYSTLANDFORM_API FCLTectonicPreset
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=false;
    // Keep the serialized default: assets that omit this property retain algorithm 1.
    // Guided authoring explicitly adopts algorithm 5; old fixtures retain their version.
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 AlgorithmVersion=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Activity=.55;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Seed=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName LowerGuide=TEXT("SurfaceMin");
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName UpperGuide=TEXT("SurfaceMax");
    UPROPERTY() bool bOverridePlateCount=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",ClampMax="12")) int32 PlateCount=7;
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
    /** Replaces the resolved shape settings; seed, enabled state and guides are retained. */
    void ApplyProgression(double Position);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="AlgorithmVersion == 3",EditConditionHides)) ECLContinentalSetting Setting=ECLContinentalSetting::Collision;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="AlgorithmVersion == 3",EditConditionHides)) FRotator FoundationOrientation=FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="AlgorithmVersion == 3",EditConditionHides,ClampMin="-1",ClampMax="1")) double MotionDirection=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="AlgorithmVersion == 3",EditConditionHides,ClampMin="0.6",ClampMax="1.4")) double ContinentalExtent=1;
};
struct CATALYSTLANDFORM_API FCLTectonicControls
{
    int32 LowerPlateCount=1,UpperPlateCount=1;
    double PlateTransition=0,Motion=0,CrustThickness=0,BeltWidth=0,Relief=0;
    double BoundaryWarp=.38,Roughness=1,CollisionStrength=1,RiftStrength=1,TransformStrength=1,InteriorRelief=0;
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
    /** Algorithm 3 only. Invalid inputs preserve Out. */
    bool SampleGeology(const FVector& UnitDirection,FCLGeologicalSample& Out) const;
    static double CrustBaseFraction(double Continentalness);
    static TSharedPtr<const FCLCompiledTectonics> Compile(const FCLTectonicPreset& Preset,int32 Seed,double SpanMeters,FString& Error,bool Layered=false,TSharedPtr<const FCLCompiledContinents> Crust=nullptr,bool Calibrated=false);
private:
    bool bLayered=false,bCalibrated=false;
    TSharedPtr<const FCLCompiledContinents> SharedCrust;
    int32 AlgorithmVersion=2;
    FQuat NoiseRotation=FQuat::Identity;
    FVector NoiseOffset=FVector::ZeroVector;
    FCLTectonicPreset Foundation;
    FQuat FoundationFrame=FQuat::Identity;
    void BuildContinentalFoundation(const FCLTectonicPreset& Preset,int32 Seed);
    FVector FoundationDirection(const FVector& D) const;
    double ContinentalDomain(const FVector& WarpedDirection) const;
    double SampleLegacyState(const FState& State,const FVector& D) const;
    double SampleState(const FState& State,const FVector& D) const;
};

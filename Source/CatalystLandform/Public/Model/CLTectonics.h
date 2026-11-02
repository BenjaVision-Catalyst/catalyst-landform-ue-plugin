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
    // New presets explicitly select algorithm 2.
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 AlgorithmVersion=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Activity=.55;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Seed=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName LowerGuide=TEXT("SurfaceMin");
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName UpperGuide=TEXT("SurfaceMax");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="AlgorithmVersion != 3",EditConditionHides)) bool bOverridePlateCount=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="bOverridePlateCount && AlgorithmVersion != 3",EditConditionHides,ClampMin="1",ClampMax="12")) int32 PlateCount=7;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bOverrideMotion=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="bOverrideMotion",ClampMin="0",ClampMax="2")) double Motion=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bOverrideRelief=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="bOverrideRelief",ClampMin="0",ClampMax="1")) double Relief=.5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="AlgorithmVersion == 3",EditConditionHides)) ECLContinentalSetting Setting=ECLContinentalSetting::Collision;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="AlgorithmVersion == 3",EditConditionHides)) FRotator FoundationOrientation=FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="AlgorithmVersion == 3",EditConditionHides,ClampMin="-1",ClampMax="1")) double MotionDirection=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="AlgorithmVersion == 3",EditConditionHides,ClampMin="0.6",ClampMax="1.4")) double ContinentalExtent=1;
};
struct CATALYSTLANDFORM_API FCLTectonicControls
{
    int32 LowerPlateCount=1,UpperPlateCount=1;
    double PlateTransition=0,Motion=0,CrustThickness=0,BeltWidth=0,Relief=0;
    static bool Resolve(const FCLTectonicPreset& Preset,FCLTectonicControls& Out,FString& Error);
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
    static TSharedPtr<const FCLCompiledTectonics> Compile(const FCLTectonicPreset& Preset,int32 Seed,double SpanMeters,FString& Error,bool Layered=false,TSharedPtr<const FCLCompiledContinents> Crust=nullptr);
private:
    bool bLayered=false;
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

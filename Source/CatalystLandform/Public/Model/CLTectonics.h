#pragma once
#include "CoreMinimal.h"
#include "CLTectonics.generated.h"

/** Versioned artistic progression. Body-space geometry uses +Z north, +X prime meridian. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLTectonicPreset
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 AlgorithmVersion=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Activity=.55;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName LowerGuide=TEXT("SurfaceMin");
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName UpperGuide=TEXT("SurfaceMax");
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bOverridePlateCount=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="bOverridePlateCount",ClampMin="1",ClampMax="12")) int32 PlateCount=7;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bOverrideMotion=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="bOverrideMotion",ClampMin="0",ClampMax="2")) double Motion=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bOverrideRelief=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="bOverrideRelief",ClampMin="0",ClampMax="1")) double Relief=.5;
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
    static TSharedPtr<const FCLCompiledTectonics> Compile(const FCLTectonicPreset& Preset,int32 Seed,double SpanMeters,FString& Error);
private:
    double SampleState(const FState& State,const FVector& D) const;
};

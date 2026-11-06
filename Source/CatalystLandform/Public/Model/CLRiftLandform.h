#pragma once
#include "CoreMinimal.h"
#include "CLRiftLandform.generated.h"
class FCLCompiledTectonics;
class FCLCompiledContinents;
/** Physical ranges curated by Character; zero version preserves existing neutral provinces. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLRiftLandform
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 AlgorithmVersion=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Character=.5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Seed=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="2",Units="m")) double MinimumSpacingMeters=100;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="2",Units="m")) double MaximumSpacingMeters=500;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="m")) double MinimumThrowMeters=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="m")) double MaximumThrowMeters=80;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.1",Units="m")) double MinimumScarpWidthMeters=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.1",Units="m")) double MaximumScarpWidthMeters=15;
    bool IsActive()const{return AlgorithmVersion!=0;}
    bool Validate(FString& Error)const;
};
/** Immutable secondary fault-block field inside the existing warped extensional belts.
 * Surface relief and restored rock coordinates consume exactly this metre displacement. */
class CATALYSTLANDFORM_API FCLCompiledRiftLandform
{
public:
    static TSharedPtr<const FCLCompiledRiftLandform> Compile(const FCLRiftLandform&,int32 PlanetSeed,double ReferenceRadius,double GuideSpan,TSharedPtr<const FCLCompiledTectonics>,TSharedPtr<const FCLCompiledContinents>,FString& Error);
    double SampleMeters(const FVector& Direction)const;
    double MaximumDisplacementMeters()const{return Throw;}
    double SpacingMeters()const{return Spacing;}
    double ScarpWidthMeters()const{return Width;}
private:
    TSharedPtr<const FCLCompiledTectonics> Tectonics;
    TSharedPtr<const FCLCompiledContinents> Crust;
    double Radius=1,Spacing=1,Throw=0,Width=1,Character=.5;
    uint32 Seed=0;
};

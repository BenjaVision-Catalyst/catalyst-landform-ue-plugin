#pragma once
#include "PCGSettings.h"
#include "CLSphereStampPoints.generated.h"

/** Versioned spherical or tiled metric PCG source with weighted stamp-index metadata. */
UCLASS()
class UCLSphereStampPointsSettings : public UPCGSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Settings") int32 Count = 64;
    UPROPERTY() bool bLocal = false;
    UPROPERTY() FVector2D RegionMin;
    UPROPERTY() FVector2D RegionMax;
    UPROPERTY() double Density = 0;
    UPROPERTY(EditAnywhere, Category="Settings") double RadiusMeters = 500;
    UPROPERTY(EditAnywhere, Category="Settings") TArray<double> Weights = {1,1};
    virtual bool UseSeed() const override { return true; }
#if WITH_EDITOR
    virtual FName GetDefaultNodeName() const override { return TEXT("CLSphericalStampPoints"); }
    virtual FText GetDefaultNodeTitle() const override { return FText::FromString(TEXT("Landform Spherical Stamp Points")); }
    virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }
#endif
protected:
    virtual TArray<FPCGPinProperties> InputPinProperties() const override { return {}; }
    virtual TArray<FPCGPinProperties> OutputPinProperties() const override { return DefaultPointOutputPinProperties(); }
    virtual FPCGElementPtr CreateElement() const override;
};

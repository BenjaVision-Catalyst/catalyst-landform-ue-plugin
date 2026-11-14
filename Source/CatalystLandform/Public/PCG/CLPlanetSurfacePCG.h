#pragma once
#include "PCGSettings.h"
#include "Components/ActorComponent.h"
#include "CLPlanetSurfacePCG.generated.h"
class ACLPlanet;
class UTransformProviderData;

/** A disjoint cube-sphere generation domain, expressed in the canonical planet frame. */
UCLASS(BlueprintType, ClassGroup=(Landform), meta=(BlueprintSpawnableComponent))
class CATALYSTLANDFORM_API UCLPlanetSurfaceRegion : public UActorComponent
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface") TObjectPtr<ACLPlanet> Planet;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface") int32 Face=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface") int32 TileX=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface") int32 TileY=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface", meta=(ClampMin="1",ClampMax="64")) int32 TilesPerFace=4;
    /** Derived scenery is valid only for this accepted surface. Serialized with the region. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Surface") FString GeneratedSurfaceKey;
    /** Preserve the region's authored native animation provider across PCG regeneration. */
    UPROPERTY() TObjectPtr<UTransformProviderData> TransformProvider;
};

/** One bounded, editor-only queue shared by all configured regions. */
CATALYSTLANDFORM_API void CLStartPlanetSurfaceRefresh();
CATALYSTLANDFORM_API void CLStopPlanetSurfaceRefresh();
CATALYSTLANDFORM_API bool CLPlanetSurfaceRegionsReady(const ACLPlanet* Planet,FString& Status);

/** Samples the accepted Landform outer surface, rather than a rectangular world-Z patch.
 * Each region has a disjoint portion of one shared seeded, area-corrected lattice.
 * Outputs at most 65536 points per data set. All heights/normals use the accepted plan.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural))
class CATALYSTLANDFORM_API UCLPlanetSurfacePCGSettings : public UPCGSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface", meta=(PCG_Overridable,ClampMin="0.000001")) double PointsPerSquaredMeter=.02;
    UFUNCTION(BlueprintCallable, Category="Landform|PCG")
    static UCLPlanetSurfaceRegion* ConfigureRegion(AActor* Owner,ACLPlanet* Planet,int32 Face,int32 TileX,int32 TileY,int32 TilesPerFace);
    static FVector FaceDirection(int32 Face,double U,double V);
#if WITH_EDITOR
    virtual FName GetDefaultNodeName() const override {return TEXT("LandformPlanetSurface");}
    virtual FText GetDefaultNodeTitle() const override {return NSLOCTEXT("CL","PlanetSurfacePCG","Landform Planet Surface");}
    virtual EPCGSettingsType GetType() const override {return EPCGSettingsType::Sampler;}
#endif
    virtual bool UseSeed() const override {return true;}
protected:
    virtual TArray<FPCGPinProperties> InputPinProperties() const override {return {};}
    virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
    virtual FPCGElementPtr CreateElement() const override;
};

/** Align classified surface points to planetary up, preserving metadata and position. */
UCLASS(BlueprintType, ClassGroup=(Procedural))
class CATALYSTLANDFORM_API UCLPlanetRadialOrientationPCGSettings : public UPCGSettings
{
    GENERATED_BODY()
public:
#if WITH_EDITOR
    virtual FName GetDefaultNodeName() const override {return TEXT("LandformRadialOrientation");}
    virtual FText GetDefaultNodeTitle() const override {return NSLOCTEXT("CL","RadialOrientationPCG","Landform Radial Orientation");}
    virtual EPCGSettingsType GetType() const override {return EPCGSettingsType::PointOps;}
#endif
protected:
    virtual TArray<FPCGPinProperties> InputPinProperties() const override;
    virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
    virtual FPCGElementPtr CreateElement() const override;
};

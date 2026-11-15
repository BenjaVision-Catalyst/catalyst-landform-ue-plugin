#pragma once
#include "PCGSettings.h"
#include "Components/ActorComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CLPlanetSurfacePCG.generated.h"
class ACLPlanet;
class UTransformProviderData;
class UPrimitiveComponent;
class UWorld;
DECLARE_MULTICAST_DELEGATE_TwoParams(FCLPlanetSceneryEvent,ACLPlanet*,AActor*);
CATALYSTLANDFORM_API FCLPlanetSceneryEvent& CLPlanetSceneryReady();
CATALYSTLANDFORM_API FCLPlanetSceneryEvent& CLPlanetSceneryRetiring();
/** Optional world-scoped presentation adapter; geometry generation stays independent. */
struct CATALYSTLANDFORM_API FCLSceneryMaterials
{
    static void Register(UWorld*,TFunction<bool(UPrimitiveComponent&,FString&)>);
    static void Unregister(UWorld*);
    static bool Prepare(UPrimitiveComponent&,FString&);
};
struct CATALYSTLANDFORM_API FCLSceneryReadiness
{
    static void Register(FName,TFunction<bool(const ACLPlanet&,FVector,FString&)>);
    static void Unregister(FName);
    static bool IsReady(const ACLPlanet&,FVector,FString&);
};

/** Explicit Planet context on an original native PCG execution source.
 * Native partition components resolve this through GetOriginalComponent(). */
UCLASS(BlueprintType, ClassGroup=(Landform), meta=(BlueprintSpawnableComponent))
class CATALYSTLANDFORM_API UCLPlanetSurfaceRegion : public UActorComponent
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface") TObjectPtr<ACLPlanet> Planet;
    UPROPERTY() TObjectPtr<UTransformProviderData> TransformProvider;
};
UCLASS()
class CATALYSTLANDFORM_API UCLPlanetPCGLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="Landform|PCG")
    static UCLPlanetSurfaceRegion* BindSource(AActor* Owner,ACLPlanet* Planet);
};
/** Observe native PCG lifecycle for presentation only; never schedules generation. */
CATALYSTLANDFORM_API void CLStartPlanetSceneryObservation();
CATALYSTLANDFORM_API void CLStopPlanetSceneryObservation();

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

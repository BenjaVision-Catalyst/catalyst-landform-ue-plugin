#pragma once

#include "CoreMinimal.h"
#include "VoxelCharacter.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "Camera/CameraModifier.h"
#include "CLPlanetWalker.generated.h"

class ACLPlanet;
class UCameraComponent;
class UCLRadialGravityMovement;
class UVoxelCollisionInvokerComponent;
class FCLWalkingPresentation;
struct FMinimalViewInfo;

/** First-person inspection of one explicitly bound native planet. */
UCLASS()
class CATALYSTLANDFORM_API ACLPlanetWalker : public AVoxelCharacter
{
    GENERATED_BODY()
public:
    ACLPlanetWalker(const FObjectInitializer& Initializer);
    UFUNCTION(BlueprintCallable, Category="Landform|Inspection")
    bool BeginInspection(ACLPlanet* Target, FVector Direction, bool HasSurfacePoint,
        FVector SurfacePointMeters, FString& Error);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Landform|Inspection")
    TObjectPtr<ACLPlanet> Planet;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Landform|Inspection")
    bool bReady=false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Landform|Inspection")
    FString Status;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Landform|Inspection")
    double LandingAdjustmentMeters=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Landform|Inspection")
    double InitialFloorErrorCm=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Landform|Inspection") bool bPresented=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Landform|Inspection") double PresentationRadiusCm=0;
    UCLRadialGravityMovement* RadialMovement() const;
    bool PresentCamera(FMinimalViewInfo& View);
    UFUNCTION(BlueprintPure,Category="Landform|Inspection") FString GetPresentationReport() const;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
private:
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<UVoxelCollisionInvokerComponent> CollisionInvoker;
    UPROPERTY() TObjectPtr<UVoxelCollisionInvokerComponent> FootCollisionInvoker;
    TSharedPtr<FCLWalkingPresentation> Presentation;
    FVector HoldPosition=FVector::ZeroVector;
    double Started=0,Released=0,Grounded=0;
    float Pitch=-5;
    bool bFailed=false;
    bool bForward=false,bBackward=false,bRight=false,bLeft=false;
    void Fail(const FString& Error);
    bool IsPlanetFloor(const FHitResult& Hit) const;
    void ForwardPressed(){bForward=true;} void ForwardReleased(){bForward=false;}
    void BackwardPressed(){bBackward=true;} void BackwardReleased(){bBackward=false;}
    void RightPressed(){bRight=true;} void RightReleased(){bRight=false;}
    void LeftPressed(){bLeft=true;} void LeftReleased(){bLeft=false;}
    void LookYaw(float Value);
    void LookPitch(float Value);
    void StartJump();
};

/** Inspection supplies its pawn only after locating the duplicated planet. */
UCLASS()
class CATALYSTLANDFORM_API ACLPlanetInspectionGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ACLPlanetInspectionGameMode();
};

UCLASS()
class CATALYSTLANDFORM_API ACLPlanetInspectionHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};

/** Same-frame terrain/scenery/camera chart; simulation stays canonical. */
UCLASS()
class CATALYSTLANDFORM_API UCLInspectionPresentationCamera : public UCameraModifier
{
    GENERATED_BODY()
public:
    UCLInspectionPresentationCamera();
    virtual bool ModifyCamera(float DeltaTime,FMinimalViewInfo& View) override;
};

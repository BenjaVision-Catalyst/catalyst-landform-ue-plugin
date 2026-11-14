#include "CLPlanetWalker.h"
#include "CLPlanet.h"
#include "Presentation/CLNativePlanetPresentation.h"
#include "Presentation/CLPlanetProjection.h"
#include "Presentation/CLPresentationTransform.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Materials/MaterialInterface.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<float> CVarCLWalkingCurvature(
    TEXT("landform.WalkingCurvature"), .05f, TEXT("Walking visual curvature: .05 gives 20x physical radius; 1 natural, 0 diagnostic flat"));

class FCLWalkingPresentation
{
public:
    TUniquePtr<FCLNativePlanetPresentation> Terrain;
    FTransform PlanetPose;
    struct FSun {TWeakObjectPtr<UDirectionalLightComponent> Component; FQuat Rotation;};
    TArray<FSun> Suns;
    TArray<TWeakObjectPtr<USkyAtmosphereComponent>> Atmospheres;
    TArray<TWeakObjectPtr<USkyLightComponent>> SkyLights;
    IConsoleVariable* CachedEV=nullptr;
    FString OldEV;
    TWeakObjectPtr<UGameViewportClient> Viewport;
    bool OldAtmosphere=true,OldCloud=true;
    double MaxCameraInverseError=0;
    int32 CameraFrames=0;
    struct FCVar {IConsoleVariable* Variable;FString Before,Applied;};
    TArray<FCVar> RenderCVars;
    void RenderSetting(const TCHAR* Name,const TCHAR* Value)
    {if(auto* V=IConsoleManager::Get().FindConsoleVariable(Name)){const FString Before=V->GetString();V->Set(Value,ECVF_SetByCode);RenderCVars.Add({V,Before,V->GetString()});}}
    ~FCLWalkingPresentation()
    {
        Terrain.Reset();
        for(auto& C:RenderCVars)if(C.Variable->GetString()==C.Applied)C.Variable->Set(*C.Before,ECVF_SetByCode);
        for(auto& Sun:Suns)if(auto* C=Sun.Component.Get())C->SetWorldRotation(Sun.Rotation);
        if(auto* V=Viewport.Get())
        {
            if(!V->EngineShowFlags.Atmosphere) V->EngineShowFlags.SetAtmosphere(OldAtmosphere);
            if(!V->EngineShowFlags.Cloud) V->EngineShowFlags.SetCloud(OldCloud);
        }
        if(CachedEV && CachedEV->GetFloat()==10.f) CachedEV->Set(*OldEV,ECVF_SetByCode);
    }
    bool Start(ACLPlanetWalker& Walker,FString& Error)
    {
        auto& Planet=*Walker.Planet; PlanetPose=Planet.GetActorTransform();
        auto* V=Walker.GetWorld()->GetGameViewport();
        if(!V){Error=TEXT("Walking inspection requires its game viewport");return false;}
        Viewport=V;OldAtmosphere=V->EngineShowFlags.Atmosphere;OldCloud=V->EngineShowFlags.Cloud;
        // Hide the background in this walking view. Sky capture illumination is
        // independent; no atmosphere/cloud backdrop is added to the raw preview.
        V->EngineShowFlags.SetAtmosphere(false);V->EngineShowFlags.SetCloud(false);
        auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/CatalystLandform/Surfaces/M_Surface.M_Surface"));
        if(!Material){Error=TEXT("Missing Landform surface material");return false;}
        FCLNanitePresentationOptions Options;
        Options.bWorldAnchored=true;Options.bNativeMaterials=true;Options.bConsoleCurvature=false;
        Options.bGameplay=true;Options.InitialCurvature=CVarCLWalkingCurvature.GetValueOnGameThread();
        Options.MaxArcRadians=FMath::DegreesToRadians(89.);Options.ReferenceRadiusCm=Walker.PresentationRadiusCm;
        // Coarse native chunks have boxes wider than the thin surface shell.
        // Admit their full bounds so mountain/distant chunks are not discarded.
        Options.MinRadiusRatio=.1;Options.MaxRadiusRatio=2;
        Terrain=FCLNativePlanetPresentation::TryCreate(Planet,*Planet.GetSamplingRuntime()->RecipePlan,*Material,Options,Error);
        if(!Terrain) return false;
        // Keep the distant Nanite canopy, using a four-pixel geometric error budget.
        // Shadow clipmaps stop at 327.68 m instead of covering tens of kilometres.
        RenderSetting(TEXT("r.Nanite.MaxPixelsPerEdge"),TEXT("4"));
        RenderSetting(TEXT("r.Shadow.Virtual.Clipmap.LastLevel"),TEXT("14"));
        RenderSetting(TEXT("r.Shadow.Virtual.ResolutionLodBiasDirectional"),TEXT("1"));
        for(TActorIterator<AActor> It(Walker.GetWorld());It;++It)
        {
            TInlineComponentArray<UActorComponent*> Components;It->GetComponents(Components);
            for(auto* Component:Components)
            {
                if(auto* P=Cast<UPrimitiveComponent>(Component);P &&
                    (P->IsA<UInstancedStaticMeshComponent>()||P->IsA<UInstancedSkinnedMeshComponent>()))
                    if(!Terrain->AddPrimitive(*P,Error)) return false;
                if(auto* SkyLight=Cast<USkyLightComponent>(Component)) SkyLights.Add(SkyLight);
                if(auto* Sun=Cast<UDirectionalLightComponent>(Component)) Suns.Add({Sun,Sun->GetComponentQuat()});
                if(auto* Sky=Cast<USkyAtmosphereComponent>(Component))
                {Sky->TransformMode=ESkyAtmosphereTransformMode::PlanetTopAtComponentTransform;
                 Sky->SetAerialPespectiveViewDistanceScale(0);Sky->SetMieScatteringScale(0);Sky->SetMieAbsorptionScale(0);
                 Sky->MarkRenderStateDirty();Atmospheres.Add(Sky);}
                // Height fog is fixed to global Z in UE. Its authored layer cannot
                // follow arbitrary planetary tangents. Raw inspection also disables
                // atmospheric distance haze; the editor world's rig is untouched.
                if(auto* Fog=Cast<UExponentialHeightFogComponent>(Component)) {Fog->SetVolumetricFog(false);Fog->SetVisibility(false);}
            }
        }
        CachedEV=IConsoleManager::Get().FindConsoleVariable(TEXT("r.EyeAdaptation.CachedLightingPreExposure"));
        if(CachedEV){OldEV=CachedEV->GetString();CachedEV->Set(TEXT("10"),ECVF_SetByCode);}
        return true;
    }
    bool Present(ACLPlanetWalker& Walker,FMinimalViewInfo& View,FString& Error)
    {
        auto& Planet=*Walker.Planet;
        if(!Planet.GetActorTransform().Equals(PlanetPose,0))
        {Error=TEXT("Walking presentation changed its canonical Planet pose");return false;}
        if(!Terrain->SetCurvature(CVarCLWalkingCurvature.GetValueOnGameThread()))
        {Error=TEXT("Walking curvature must be finite and between zero and one");return false;}
        const FVector Up=(View.Location-Planet.BodyCenterWorld()).GetSafeNormal();
        if(!Terrain->Tick(Up,FPlatformTime::Seconds(),false,Error)) return false;
        FCLPlanetProjection Projection;
        if(!Terrain->GetCurrentProjection(Projection)){Error=TEXT("Missing walking presentation chart");return false;}
        FVector Position,Back;FQuat Rotation;
        if(!FCLPresentationTransform::TryProjectCamera(Projection,View.Location,View.Rotation.Quaternion(),Position,Rotation)||
            !Projection.TryUnprojectPosition(Position,Back))
        {Error=TEXT("Walking camera left its shared terrain/scenery chart");return false;}
        MaxCameraInverseError=FMath::Max(MaxCameraInverseError,FVector::Distance(Back,View.Location));
        View.Location=Position;View.Rotation=Rotation.Rotator();
        const FQuat LocalSky=FQuat::FindBetweenNormals(FVector::ZAxisVector,Up);
        // Establish daylight at the selected landing once. A walking camera must
        // not rotate a directional light and evict every cached shadow page.
        if(CameraFrames==0)for(auto& Sun:Suns) if(auto* C=Sun.Component.Get()) C->SetWorldRotation(LocalSky*Sun.Rotation);
        for(auto& Sky:Atmospheres) if(auto* C=Sky.Get())
        {C->SetWorldLocationAndRotation(Planet.BodyCenterWorld()+Up*Walker.PresentationRadiusCm,LocalSky);}
        for(auto& SkyLight:SkyLights) if(auto* C=SkyLight.Get();C && FVector::DistSquared(C->GetComponentLocation(),Position)>1000.*1000.) C->SetWorldLocation(Position);
        ++CameraFrames; return true;
    }
    FString Report() const
    {
        auto Json=MakeShared<FJsonObject>();
        if(Terrain) Terrain->WriteReport(*Json);
        Json->SetNumberField(TEXT("presentedCameraFrames"),CameraFrames);
        Json->SetNumberField(TEXT("maxCameraInverseErrorCm"),MaxCameraInverseError);
        FCLPlanetProjection Projection;
        if(Terrain && Terrain->GetCurrentProjection(Projection)) Json->SetNumberField(TEXT("curvature"),Projection.GetSettings().Curvature);
        Json->SetNumberField(TEXT("outdoorExposureBiasEV"),-.75);
        Json->SetNumberField(TEXT("localSkyCaptures"),SkyLights.Num());
        Json->SetStringField(TEXT("directionalLighting"),TEXT("Stable at landing; does not rotate with each walking step"));
        Json->SetNumberField(TEXT("nanitePixelError"),4);
        Json->SetNumberField(TEXT("maximumShadowClipmapRadiusMeters"),327.68);
        Json->SetBoolField(TEXT("fogAndHazeDisabled"),true);
        const auto* V=Viewport.Get();
        Json->SetBoolField(TEXT("skyBackgroundDisabled"),V && !V->EngineShowFlags.Atmosphere && !V->EngineShowFlags.Cloud);
        Json->SetStringField(TEXT("heightFog"),TEXT("Disabled: height fog, volumetric fog, aerial perspective and Mie haze"));
        FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Json,Writer);return Text;
    }
};
bool ACLPlanetWalker::PresentCamera(FMinimalViewInfo& View)
{
    if(!bReady || !IsValid(Planet)) return false;
    FString Error;
    if(!Presentation)
    {
        Presentation=MakeShared<FCLWalkingPresentation>();
        if(!Presentation->Start(*this,Error)){Presentation.Reset();Fail(Error);return false;}
    }
    if(!Presentation->Present(*this,View,Error)){Fail(Error);Presentation.Reset();return false;}
    bPresented=true;return true;
}
FString ACLPlanetWalker::GetPresentationReport() const
{return Presentation?Presentation->Report():TEXT("{}");}
void ACLPlanetWalker::EndPlay(const EEndPlayReason::Type Reason)
{Presentation.Reset();bPresented=false;Super::EndPlay(Reason);}
UCLInspectionPresentationCamera::UCLInspectionPresentationCamera(){Priority=255;}
bool UCLInspectionPresentationCamera::ModifyCamera(float DeltaTime,FMinimalViewInfo& View)
{
    if(CameraOwner) if(auto* PC=CameraOwner->GetOwningPlayerController())
        if(auto* Walker=Cast<ACLPlanetWalker>(PC->GetPawn())) Walker->PresentCamera(View);
    return false;
}

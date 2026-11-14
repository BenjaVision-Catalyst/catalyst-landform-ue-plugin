#include "CLPlanetInspection.h"
#include "CLInspectionMaterials.h"
#include "CLNaniteProjection.h"
#include "CLPlanet.h"
#include "PCG/CLPlanetSurfacePCG.h"
#include "CLPlanetWalker.h"
#include "Surface/CLSurfaceLibrary.h"
#include "MegaMaterial/VoxelMegaMaterial.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Containers/Ticker.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"
#include "HAL/IConsoleManager.h"

FCLLaunchPlanetInspection FCLPlanetInspection::Launch;

namespace
{
TAutoConsoleVariable<int32> CVarWalkPreviewWidth(TEXT("landform.WalkingPreviewWidth"),2560,TEXT("Walking preview width in rendered pixels; applied on launch"));
TAutoConsoleVariable<int32> CVarWalkPreviewHeight(TEXT("landform.WalkingPreviewHeight"),1440,TEXT("Walking preview height in rendered pixels; applied on launch"));
class FCLInspectionHost
{
    TOptional<FCLPlanetInspectionRequest> Request;
    TWeakObjectPtr<UWorld> EditorWorld,PlayWorld;
    TWeakObjectPtr<ACLPlanet> EditorPlanet,PlayPlanet;
    TWeakObjectPtr<ACLPlanetWalker> Walker;
    TStrongObjectPtr<UCLPlanetRecipeAsset> Snapshot;
    TStrongObjectPtr<ULevelEditorPlaySettings> Settings;
    FDelegateHandle StartedHandle,EndedHandle,CancelledHandle;
    FTSTicker::FDelegateHandle SizingHandle;
    int32 SizingAttempts=0;
    double SizingStarted=0;
    FIntPoint TargetSize=FIntPoint(2560,1440);
    FVector2D RequestedClient=FVector2D::ZeroVector;
    FString Result;
    TUniquePtr<FCLInspectionMaterials> Materials;
public:
    FCLInspectionHost()
    {
        FCLPlanetInspection::Launch.BindRaw(this,&FCLInspectionHost::Launch);
        StartedHandle=FEditorDelegates::PostPIEStarted.AddRaw(this,&FCLInspectionHost::Started);
        EndedHandle=FEditorDelegates::EndPIE.AddRaw(this,&FCLInspectionHost::Ended);
        CancelledHandle=FEditorDelegates::CancelPIE.AddRaw(this,&FCLInspectionHost::Return);
    }
    ~FCLInspectionHost()
    {
        FEditorDelegates::PostPIEStarted.Remove(StartedHandle);
        FEditorDelegates::EndPIE.Remove(EndedHandle);FEditorDelegates::CancelPIE.Remove(CancelledHandle);
        if(FCLPlanetInspection::Launch.IsBoundToObject(this))FCLPlanetInspection::Launch.Unbind();
        Return();
    }
    bool Launch(FCLPlanetInspectionRequest Input,FString& Error)
    {
        Error.Reset();
        if(!GEditor||GEditor->PlayWorld||GEditor->GetPlaySessionRequest().IsSet()||Request)
        {Error=TEXT("Finish the current play session before starting another walk.");return false;}
        TargetSize=FIntPoint(CVarWalkPreviewWidth.GetValueOnGameThread(),CVarWalkPreviewHeight.GetValueOnGameThread());
        if(TargetSize.X<320||TargetSize.Y<180||TargetSize.X>8192||TargetSize.Y>8192)
        {Error=TEXT("Walking preview dimensions must be between 320x180 and 8192x8192 pixels.");return false;}
        UWorld* World=GEditor->GetEditorWorldContext().World();
        if(!World||!Input.SourceAsset.IsValid()||Input.Direction.ContainsNaN()||Input.Direction.IsNearlyZero()||Input.RecipeJson.IsEmpty())
        {Error=TEXT("Choose a location on the open Planet first.");return false;}
        // Match the explicit source asset. Never select the first planet in a world.
        ACLPlanet* Target=nullptr;
        for(TActorIterator<ACLPlanet> It(World);It;++It)if(It->RecipeAsset==Input.SourceAsset.Get())
        {
            if(Target){Error=TEXT("More than one actor uses this Planet. Keep one inspection target in the world.");return false;}
            Target=*It;
        }
        if(Target&&!CLPlanetSurfaceRegionsReady(Target,Error))return false;
        if(!CLNaniteProjectionShaderOverlayEnabled())
        {Error=TEXT("Walking requires Landform projection shader setup. Run the plugin's stage-nanite-projection.py and enable bEnableProjectionShaders in CatalystLandform.Presentation.");return false;}
        auto* Mega=Target?Target->MegaMaterial.Get():GetDefault<UCLSurfaceSettings>()->PlanetMaterial.LoadSynchronous();
        Materials=MakeUnique<FCLInspectionMaterials>();
        if(!Mega||!Materials->Prepare(*World,*Mega,Error)){Materials.Reset();return false;}
        Snapshot.Reset(DuplicateObject<UCLPlanetRecipeAsset>(Input.SourceAsset.Get(),GetTransientPackage()));
        if(!Snapshot->ImportRecipeJson(Input.RecipeJson,Error)){Snapshot.Reset();return false;}
        EditorWorld=World;EditorPlanet=Target;Request=MoveTemp(Input);
        Result=TEXT("Walking cancelled before landing.");
        Settings.Reset(DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage()));
        Settings->SetPlayNetMode(PIE_Standalone);Settings->SetPlayNumberOfClients(1);
        Settings->SetRunUnderOneProcess(true);Settings->bLaunchSeparateServer=false;
        Settings->NewWindowWidth=TargetSize.X;Settings->NewWindowHeight=TargetSize.Y;
        Settings->CenterNewWindow=true;
        FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings.Get();Params.bAllowOnlineSubsystem=false;
        Params.GameModeOverride=ACLPlanetInspectionGameMode::StaticClass();
        // No map override: PIE duplicates the existing world, including its biome actors.
        GEditor->RequestPlaySession(Params);
        UE_LOG(LogTemp,Display,TEXT("LANDFORM_WALK_REQUESTED: %s | source %s"),*World->GetPathName(),*Request->SourceAsset->GetPathName());
        return true;
    }
    bool SizeWindow(float)
    {
        auto* World=PlayWorld.Get();auto* Viewport=World?World->GetGameViewport():nullptr;
        if(!Request||!Viewport||!Viewport->Viewport) {SizingHandle.Reset();return false;}
        const FIntPoint Actual=Viewport->Viewport->GetSizeXY(),Wanted=TargetSize;
        if(Actual==Wanted)
        {
            UE_LOG(LogTemp,Display,TEXT("LANDFORM_WALK_VIEWPORT: %dx%d"),Actual.X,Actual.Y);
            SizingHandle.Reset();return false;
        }
        if(SizingAttempts>=8||FPlatformTime::Seconds()-SizingStarted>10)
        {
            UE_LOG(LogTemp,Warning,TEXT("LANDFORM_WALK_VIEWPORT: requested %dx%d, got %dx%d"),Wanted.X,Wanted.Y,Actual.X,Actual.Y);
            SizingHandle.Reset();return false;
        }
        if(auto Window=Viewport->GetWindow();Window&&Actual.X>0&&Actual.Y>0)
        {
            // Measure rendered pixels instead of assuming that Slate client units
            // exclude the platform title bar or match the display's DPI scale.
            const FVector2D Client=Window->GetClientSizeInScreen();
            RequestedClient-=FVector2D(Client.X*(Actual.X-Wanted.X)/Actual.X,Client.Y*(Actual.Y-Wanted.Y)/Actual.Y);
            Window->Resize(RequestedClient);
            ++SizingAttempts;
        }
        return true;
    }
    void Started(bool Simulating)
    {
        if(!Request)return;
        if(Simulating||!GEditor||!GEditor->PlayWorld||!EditorWorld.IsValid()||EditorWorld.Get()!=GEditor->GetEditorWorldContext().World())
        {Fail(TEXT("The source world changed before walking could start."));return;}
        UWorld* World=GEditor->PlayWorld;PlayWorld=World;
        SizingAttempts=0;SizingStarted=FPlatformTime::Seconds();RequestedClient=FVector2D(TargetSize.X,TargetSize.Y);
        SizingHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this,&FCLInspectionHost::SizeWindow),.25f);
        ACLPlanet* Target=EditorPlanet.IsValid()?Cast<ACLPlanet>(EditorUtilities::GetSimWorldCounterpartActor(EditorPlanet.Get())):nullptr;
        if(EditorPlanet.IsValid()&&(!Target||Target->GetWorld()!=World))
        {Fail(TEXT("The source Planet was not duplicated into this play world."));return;}
        if(!Target)
        {
            Target=World->SpawnActor<ACLPlanet>();
            if(Target)Target->MegaMaterial=GetDefault<UCLSurfaceSettings>()->PlanetMaterial.LoadSynchronous();
        }
        PlayPlanet=Target;
        if(!Target){Fail(TEXT("Could not activate the Planet in this world."));return;}
        Target->MegaMaterial=Materials->GetMegaMaterial();
        FString Error;
        if(!Materials->Apply(*World,Error)){Fail(Error);return;}
        Target->RecipeAsset=Snapshot.Get();Target->RebuildTerrain();
        if(!Target->RecipeStatus.StartsWith(TEXT("Compiled recipe:"))){Fail(Target->RecipeStatus);return;}
        auto* Player=World->GetFirstPlayerController();
        if(!Player){Fail(TEXT("The walking play session has no local player."));return;}
        FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Pawn=World->SpawnActor<ACLPlanetWalker>(ACLPlanetWalker::StaticClass(),FTransform::Identity,Spawn);
        if(!Pawn||!Pawn->BeginInspection(Target,Request->Direction,Request->SurfacePointMeters.IsSet(),Request->SurfacePointMeters.Get(FVector::ZeroVector),Error))
        {Fail(Error.IsEmpty()?TEXT("Could not create the walking player."):Error);return;}
        Walker=Pawn;APawn* Previous=Player->GetPawn();Player->Possess(Pawn);
        if(Previous&&Previous!=Pawn)Previous->Destroy();
        Player->bShowMouseCursor=false;Player->SetInputMode(FInputModeGameOnly());
        Result=TEXT("Returned from walking inspection.");
    }
    void Fail(const FString& Error)
    {
        Result=Error;UE_LOG(LogTemp,Warning,TEXT("LANDFORM_WALK_FAILED: %s"),*Error);
        if(GEditor&&GEditor->PlayWorld)GEditor->RequestEndPlayMap();else Return();
    }
    void Ended(bool){Return();}
    void Return()
    {
        if(!Request)return;
        FTSTicker::RemoveTicker(SizingHandle);SizingHandle.Reset();
        FVector Direction=Request->Direction;TOptional<FVector> Point=Request->SurfacePointMeters;
        if(Walker.IsValid())Result=Walker->Status;
        if(Walker.IsValid()&&Walker->bReady&&PlayPlanet.IsValid())
        {
            const auto* Movement=Walker->GetCharacterMovement();
            const FVector Position=Movement->IsMovingOnGround()?FVector(Movement->CurrentFloor.HitResult.ImpactPoint):Walker->GetActorLocation();
            const FVector Local=PlayPlanet->GetActorTransform().InverseTransformPosition(Position);
            Direction=Local.GetSafeNormal();Point=Local*.01;Result=TEXT("Returned from walking inspection.");
        }
        // Retire ownership before callbacks, so a return cannot be delivered twice.
        auto Previous=MoveTemp(Request);Request.Reset();Walker.Reset();PlayPlanet.Reset();PlayWorld.Reset();
        EditorPlanet.Reset();EditorWorld.Reset();Snapshot.Reset();Settings.Reset();Materials.Reset();
        if(Previous->OnReturned)Previous->OnReturned(Previous->Id,Direction,Result);
        if(Point&&Previous->OnPositionReturned)Previous->OnPositionReturned(Previous->Id,*Point);
        UE_LOG(LogTemp,Display,TEXT("LANDFORM_WALK_RETURNED: %s"),*Result);
    }
};
TUniquePtr<FCLInspectionHost> Host;
}
void FCLPlanetInspection::Startup()
{
    if(!Host&&!Launch.IsBound())Host=MakeUnique<FCLInspectionHost>();
}
void FCLPlanetInspection::Shutdown(){Host.Reset();}

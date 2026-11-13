#include "CLPlanetInspection.h"
#include "CLPlanet.h"
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

FCLLaunchPlanetInspection FCLPlanetInspection::Launch;

namespace
{
class FCLInspectionHost
{
    TOptional<FCLPlanetInspectionRequest> Request;
    TWeakObjectPtr<UWorld> EditorWorld,PlayWorld;
    TWeakObjectPtr<ACLPlanet> EditorPlanet,PlayPlanet;
    TWeakObjectPtr<ACLPlanetWalker> Walker;
    TStrongObjectPtr<UCLPlanetRecipeAsset> Snapshot;
    TStrongObjectPtr<ULevelEditorPlaySettings> Settings;
    FDelegateHandle StartedHandle,EndedHandle,CancelledHandle;
    FString Result;
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
        Snapshot.Reset(DuplicateObject<UCLPlanetRecipeAsset>(Input.SourceAsset.Get(),GetTransientPackage()));
        if(!Snapshot->ImportRecipeJson(Input.RecipeJson,Error)){Snapshot.Reset();return false;}
        EditorWorld=World;EditorPlanet=Target;Request=MoveTemp(Input);
        Result=TEXT("Walking cancelled before landing.");
        Settings.Reset(DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage()));
        Settings->SetPlayNetMode(PIE_Standalone);Settings->SetPlayNumberOfClients(1);
        Settings->SetRunUnderOneProcess(true);Settings->bLaunchSeparateServer=false;
        FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings.Get();Params.bAllowOnlineSubsystem=false;
        Params.GameModeOverride=ACLPlanetInspectionGameMode::StaticClass();
        // No map override: PIE duplicates the existing world, including its biome actors.
        GEditor->RequestPlaySession(Params);
        UE_LOG(LogTemp,Display,TEXT("LANDFORM_WALK_REQUESTED: %s | source %s"),*World->GetPathName(),*Request->SourceAsset->GetPathName());
        return true;
    }
    void Started(bool Simulating)
    {
        if(!Request)return;
        if(Simulating||!GEditor||!GEditor->PlayWorld||!EditorWorld.IsValid()||EditorWorld.Get()!=GEditor->GetEditorWorldContext().World())
        {Fail(TEXT("The source world changed before walking could start."));return;}
        UWorld* World=GEditor->PlayWorld;PlayWorld=World;
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
        Target->RecipeAsset=Snapshot.Get();Target->RebuildTerrain();FString Error;
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
        EditorPlanet.Reset();EditorWorld.Reset();Snapshot.Reset();Settings.Reset();
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

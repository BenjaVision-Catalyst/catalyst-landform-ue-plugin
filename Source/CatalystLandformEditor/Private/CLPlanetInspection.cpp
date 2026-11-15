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
namespace {TMap<FName,FCLInspectionPreparation> Preparations;}
namespace {TMap<FName,TFunction<void(ACLPlanet&,TArray<UMaterialInterface*>&)>> MaterialSources;}
void FCLPlanetInspection::RegisterMaterialSources(FName N,TFunction<void(ACLPlanet&,TArray<UMaterialInterface*>&)> F){MaterialSources.Add(N,MoveTemp(F));}
void FCLPlanetInspection::UnregisterMaterialSources(FName N){MaterialSources.Remove(N);}
void FCLPlanetInspection::CollectMaterials(ACLPlanet& P,TArray<UMaterialInterface*>& M){for(auto& F:MaterialSources)F.Value(P,M);}
void FCLPlanetInspection::RegisterPreparation(FName Owner,FCLInspectionPreparation Prepare){Preparations.Add(Owner,MoveTemp(Prepare));}
void FCLPlanetInspection::UnregisterPreparation(FName Owner){Preparations.Remove(Owner);}
ECLInspectionPreparation FCLPlanetInspection::Prepare(ACLPlanet& Planet,FString& Status)
{
    TArray<FName> Owners;Preparations.GetKeys(Owners);Owners.Sort(FNameLexicalLess());
    for(FName Owner:Owners)
    {
        const auto State=Preparations.FindChecked(Owner)(Planet,Status);
        if(State!=ECLInspectionPreparation::Ready)return State;
    }
    Status.Reset();return ECLInspectionPreparation::Ready;
}

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
    FTSTicker::FDelegateHandle PreparationHandle;
    FTSTicker::FDelegateHandle MaterialHandle;
    double PreparationStarted=0;
    FString LastProgress;
    IConsoleVariable* NativePCGBudget=nullptr;FString PreviousPCGBudget;
    void RestoreNativePCGBudget()
    {if(NativePCGBudget&&NativePCGBudget->GetInt()==2)NativePCGBudget->Set(*PreviousPCGBudget,ECVF_SetByCode);NativePCGBudget=nullptr;}
    TArray<TWeakObjectPtr<AActor>> ExcludedEditorScenery;
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
        if(!CLNaniteProjectionShaderOverlayEnabled())
        {Error=TEXT("Walking requires Landform projection shader setup. Run the plugin's stage-nanite-projection.py and enable bEnableProjectionShaders in CatalystLandform.Presentation.");return false;}
        Snapshot.Reset(DuplicateObject<UCLPlanetRecipeAsset>(Input.SourceAsset.Get(),GetTransientPackage()));
        if(!Snapshot->ImportRecipeJson(Input.RecipeJson,Error)){Snapshot.Reset();return false;}
        if(!Target)
        {
            const auto* Source=Input.SourceAsset.Get();
            auto* Mega=Source->WalkingMaterial.IsNull()?GetDefault<UCLSurfaceSettings>()->PlanetMaterial.LoadSynchronous():Cast<UVoxelMegaMaterial>(Source->WalkingMaterial.TryLoad());
            if(!Mega){Error=TEXT("The Planet's walking material is missing. Configure its native MegaMaterial before walking.");Snapshot.Reset();return false;}
            if(!Source->WalkingFrame.IsValid()||!FMath::IsFinite(Source->WalkingVoxelSize)||Source->WalkingVoxelSize<1)
            {Error=TEXT("The Planet's walking frame or voxel size is invalid.");Snapshot.Reset();return false;}
            FActorSpawnParameters Spawn;Spawn.ObjectFlags|=RF_Transient;Spawn.bDeferConstruction=true;
            Target=World->SpawnActor<ACLPlanet>(ACLPlanet::StaticClass(),Source->WalkingFrame,Spawn);
            if(!Target){Error=TEXT("Could not prepare the Planet in the current world.");Snapshot.Reset();return false;}
            Target->RecipeAsset=Input.SourceAsset.Get();Target->MegaMaterial=Mega;Target->LivePresentationMaterial=Mega;Target->VoxelSize=Source->WalkingVoxelSize;
            Target->FinishSpawning(Source->WalkingFrame);
            Target->SetActorLabel(TEXT("Planet | Walking preparation"));Target->RebuildTerrain();
            // This actor supplies configuration only; native terrain/PCG start in PIE.
        }
        EditorWorld=World;EditorPlanet=Target;Request=MoveTemp(Input);
        NativePCGBudget=IConsoleManager::Get().FindConsoleVariable(TEXT("pcg.RuntimeGeneration.NumGeneratingComponents"));
        if(NativePCGBudget){PreviousPCGBudget=NativePCGBudget->GetString();NativePCGBudget->Set(TEXT("2"),ECVF_SetByCode);}
        Result=TEXT("Walking cancelled before landing.");
        PreparationStarted=FPlatformTime::Seconds();LastProgress.Reset();
        PreparationHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this,&FCLInspectionHost::PrepareWorld),.25f);
        UE_LOG(LogTemp,Display,TEXT("LANDFORM_WALK_PREPARING: %s | source %s"),*World->GetPathName(),*Request->SourceAsset->GetPathName());
        return true;
    }
    bool PrepareWorld(float)
    {
        FString Error,Current;
        if(!Request||!EditorWorld.IsValid()||!EditorPlanet.IsValid()||!Request->SourceAsset.IsValid()||
            !GEditor||GEditor->GetEditorWorldContext().World()!=EditorWorld.Get())
        {PreparationHandle.Reset();Fail(TEXT("The source world changed while preparing walking."));return false;}
        if(!Request->SourceAsset->ExportJsonString(Current,Error)||Current!=Request->RecipeJson)
        {PreparationHandle.Reset();Fail(TEXT("The Planet changed while preparing walking. Click the player icon again."));return false;}
        auto* Target=EditorPlanet.Get();
        auto State=FCLPlanetInspection::Prepare(*Target,Error);
        if(State==ECLInspectionPreparation::Failed||FPlatformTime::Seconds()-PreparationStarted>1800)
        {PreparationHandle.Reset();Fail(Error.IsEmpty()?TEXT("Walking preparation timed out; no incomplete scene was launched."):Error);return false;}
        if(State==ECLInspectionPreparation::Pending)
        {
            if(LastProgress!=Error){LastProgress=Error;if(Request->OnProgress)Request->OnProgress(Request->Id,Error);UE_LOG(LogTemp,Display,TEXT("LANDFORM_WALK_WAITING: %s"),*Error);}return true;
        }
        Materials=MakeUnique<FCLInspectionMaterials>();
        TArray<UMaterialInterface*> Sources;FCLPlanetInspection::CollectMaterials(*Target,Sources);
        if(!Target->MegaMaterial||!Materials->Prepare(*EditorWorld.Get(),*Target->MegaMaterial,Error,Sources))
        {PreparationHandle.Reset();Fail(Error.IsEmpty()?TEXT("The Planet has no walking MegaMaterial."):Error);return false;}
        Settings.Reset(DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage()));
        Settings->SetPlayNetMode(PIE_Standalone);Settings->SetPlayNumberOfClients(1);
        Settings->SetRunUnderOneProcess(true);Settings->bLaunchSeparateServer=false;
        Settings->NewWindowWidth=TargetSize.X;Settings->NewWindowHeight=TargetSize.Y;
        Settings->CenterNewWindow=true;
        FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings.Get();Params.bAllowOnlineSubsystem=false;
        Params.GameModeOverride=ACLPlanetInspectionGameMode::StaticClass();
        // Keep the authored world, but never turn editor-generated vegetation
        // into a frozen gameplay snapshot. The running world owns its scenery.
        for(TActorIterator<AActor> It(EditorWorld.Get());It;++It)
            if(auto* R=It->FindComponentByClass<UCLPlanetSurfaceRegion>();R&&R->Planet==Target&&!It->HasAnyFlags(RF_Transient))
            {ExcludedEditorScenery.Add(*It);It->SetFlags(RF_Transient);}
        GEditor->RequestPlaySession(Params);
        UE_LOG(LogTemp,Display,TEXT("LANDFORM_WALK_REQUESTED: %s | source %s"),*EditorWorld->GetPathName(),*Request->SourceAsset->GetPathName());
        PreparationHandle.Reset();return false;
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
        if(auto* View=World->GetGameViewport())
        {View->EngineShowFlags.SetAtmosphere(false);View->EngineShowFlags.SetCloud(false);View->EngineShowFlags.SetFog(false);}

        for(auto& A:ExcludedEditorScenery)if(A.IsValid())A->ClearFlags(RF_Transient);ExcludedEditorScenery.Reset();
        SizingAttempts=0;SizingStarted=FPlatformTime::Seconds();RequestedClient=FVector2D(TargetSize.X,TargetSize.Y);
        SizingHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this,&FCLInspectionHost::SizeWindow),.25f);
        ACLPlanet* Target=EditorPlanet.IsValid()?Cast<ACLPlanet>(EditorUtilities::GetSimWorldCounterpartActor(EditorPlanet.Get())):nullptr;
        if(!Target)
        {
            FActorSpawnParameters Spawn;Spawn.ObjectFlags|=RF_Transient;Spawn.bDeferConstruction=true;
            const FTransform Frame=EditorPlanet.IsValid()?EditorPlanet->GetActorTransform():Snapshot->WalkingFrame;
            Target=World->SpawnActor<ACLPlanet>(ACLPlanet::StaticClass(),Frame,Spawn);
            if(Target){Target->RecipeAsset=Snapshot.Get();Target->LivePresentationMaterial=Materials->GetMegaMaterial();Target->MegaMaterial=Materials->GetMegaMaterial();Target->VoxelSize=Snapshot->WalkingVoxelSize;Target->bWaitOnBeginPlay=false;Target->bWaitForPCG=false;Target->FinishSpawning(Frame);}
        }
        PlayPlanet=Target;
        if(!Target){Fail(TEXT("Could not activate the Planet in this world."));return;}
        // Local collision readiness is checked by the walker. Distant PCG must
        // never hold up VPP terrain publication or the first local landing.
        Target->bWaitForPCG=false;
        Target->MegaMaterial=Materials->GetMegaMaterial();
        Target->LivePresentationMaterial=Materials->GetMegaMaterial();
        FCLSceneryMaterials::Register(World,[this](UPrimitiveComponent& C,FString& E){return Materials&&Materials->ApplyPrimitive(C,E);});
        MaterialHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
        {if(!PlayPlanet.IsValid()||!Materials)return false;Materials->ApplyTextures(*PlayPlanet);return true;}),.25f);
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
        RestoreNativePCGBudget();
        FTSTicker::RemoveTicker(SizingHandle);SizingHandle.Reset();
        FTSTicker::RemoveTicker(PreparationHandle);PreparationHandle.Reset();
        FTSTicker::RemoveTicker(MaterialHandle);MaterialHandle.Reset();
        for(auto& A:ExcludedEditorScenery)if(A.IsValid())A->ClearFlags(RF_Transient);ExcludedEditorScenery.Reset();
        if(PlayWorld.IsValid())FCLSceneryMaterials::Unregister(PlayWorld.Get());
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

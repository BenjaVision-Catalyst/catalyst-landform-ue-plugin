#include "CLNativePreview.h"
#include "CLSectionGeometry.h"
#include "CLPlanetStamp.h"
#include "VoxelLayer.h"
#include "VoxelStampComponent.h"
#include "VoxelMesh.h"
#include "VoxelChunkKey.h"
#include "Shape/VoxelSphereShape.h"
#include "Shape/VoxelCubeShape.h"
#include "Render/VoxelMeshComponent.h"
#include "Render/VoxelMeshRenderProxy.h"
#include "Collision/VoxelCollisionComponent.h"
#include "Surface/CLSurfaceLibrary.h"
#include "MegaMaterial/VoxelMegaMaterial.h"
#include "Engine/World.h"

FCLNativePreview::~FCLNativePreview(){Reset();}
void FCLNativePreview::Reset()
{
    Pending.Reset();VisiblePlan.Reset();Plan.Reset();Stamp.Reset();
    if(Native){Native->DestroyRuntime();Native->Destroy();Native.Reset();}
    Last=FBuffers();PublishedHash=0;AwaitingPublication=false;Failure.Reset();
}
bool FCLNativePreview::Start(UWorld& World,TSharedPtr<const FCLCompiledPlanetRecipe> Input,bool Flat,double Area,FString& Error,bool Draft)
{
    if(!Input){Error=TEXT("Native preview requires an accepted compiled recipe");return false;}
    if(!Native)
    {
        FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Native.Reset(World.SpawnActor<AVoxelWorld>(Params));
        if(!Native){Error=TEXT("Native preview world could not be created");return false;}
        Native->DestroyRuntime();Native->SetupForPreview();Native->bEnableNanite=false;Native->bEnableTessellation=false;
        Native->bEnableNavigation=false;Native->bWaitForPCG=false;Native->VoxelSize=50;
        Native->bCreateRuntimeOnBeginPlay=false;Native->bDoubleSidedCollision=true;
        if(auto* Material=GetDefault<UCLSurfaceSettings>()->PlanetMaterial.LoadSynchronous())Native->MegaMaterial=Material;
        auto* Component=NewObject<UVoxelStampComponent>(Native.Get());Component->SetupAttachment(Native->GetRootComponent());Component->RegisterComponent();Stamp=Component;
    }
    Pending=MoveTemp(Input);PendingSection=Flat;PendingWidth=FMath::Clamp(Area,100.,1000.);PendingDraft=Draft;
    Error.Reset();return true;
}
bool FCLNativePreview::ApplyPending()
{
    if(!Pending||AwaitingPublication||Native->IsProcessingNewState())return false;
    Plan=MoveTemp(Pending);Section=PendingSection;Width=PendingWidth;AppliedDraft=PendingDraft;PublishedHash=0;
    // Scale the stamp through VPP's supported transform, never the voxel world
    // or the produced mesh. Native LOD sees the same geometry the viewport sees.
    const FVector Scale=Section?FVector(1000./Width,1000./Width,1000./Plan->RadiusMeters):FVector(1000./Plan->RadiusMeters);
    Stamp->SetRelativeScale3D(Scale);
    FCLPlanetStamp Source;Source.bUseRecipe=true;Source.CompiledRecipe=Plan;Source.Radius=Plan->RadiusMeters*100;
    check(Plan->SourceRecipe);Source.Recipe=*Plan->SourceRecipe;
    Source.Layer=UVoxelVolumeLayer::Default();Source.Behavior=EVoxelStampBehavior::AffectAll;Source.Smoothness=0;Source.BoundsExtension=.05;
    Source.bSectionPreview=Section;Source.SectionWidthMeters=Width;Source.SectionReferenceRadiusMeters=FCLSectionGeometry::ReferenceSurfaceRadius(*Plan);
    Native->LODQuality.EditorQuality=AppliedDraft?FFloatInterval(1,1):FFloatInterval(4,4);
    Stamp->SetStamp(Source);
    if(!Native->IsRuntimeCreated())Native->CreateRuntime();
    AwaitingPublication=true;Native->OnNextStateRendered(FSimpleDelegate::CreateLambda([this]{AwaitingPublication=false;}));
    return true;
}
bool FCLNativePreview::IsUpdating()const
{return Pending.IsValid()||(Native&&Failure.IsEmpty()&&(AwaitingPublication||!VisiblePlan||Native->IsProcessingNewState()||!Native->IsVoxelWorldReady()));}
const void* FCLNativePreview::RuntimeIdentity()const{return Native?Native->GetRuntime().Get():nullptr;}
bool FCLNativePreview::UsesNativePresentation()const
{
    if(!Native||!VisiblePlan)return false;
    TInlineComponentArray<UVoxelMeshComponent*> Components;Native->GetComponents(Components);int32 Count=0;
    for(auto* C:Components)if(C->IsRegistered()&&C->GetRenderProxy()){if(!C->IsVisible()||C->bHiddenInGame)return false;++Count;}
    return Count>0;
}
FString FCLNativePreview::Status()const
{
    if(!Failure.IsEmpty())return Failure;if(!Native)return TEXT("No native producer");
    return FString::Printf(TEXT("native producer/presenter %u, ready %d, processing %d, tasks %d, inspected vertices %d"),Native->GetUniqueID(),Native->IsVoxelWorldReady(),Native->IsProcessingNewState(),Native->GetNumPendingTasks(),Last.Vertices.Num());
}
FBox FCLNativePreview::DisplayBounds()const
{
    const auto Input=Pending?Pending:Plan;if(!Input)return FBox(ForceInit);
    const bool Flat=Pending?PendingSection:Section;const double Area=Pending?PendingWidth:Width;
    const FVector Scale=Flat?FVector(1000./Area,1000./Area,1000./Input->RadiusMeters):FVector(1000./Input->RadiusMeters);
    if(Flat){FVoxelCubeShape Box;Box.Size=FVector(Area*100,Area*100,Input->RadiusMeters*100);return FBox(Box.GetLocalBounds().Min,Box.GetLocalBounds().Max).ShiftBy(FVector(0,0,Input->RadiusMeters*50)).TransformBy(FTransform(FQuat::Identity,FVector::ZeroVector,Scale));}
    FVoxelSphereShape Sphere;Sphere.Radius=Input->RadiusMeters*100;return FBox(Sphere.GetLocalBounds().Min,Sphere.GetLocalBounds().Max).TransformBy(FTransform(FQuat::Identity,FVector::ZeroVector,Scale));
}
bool FCLNativePreview::Poll(bool)
{
    if(!Native||!Failure.IsEmpty())return false;
    ApplyPending();
    if(Pending||AwaitingPublication||!Plan||!Native->IsVoxelWorldReady()||Native->IsProcessingNewState())return false;
    TInlineComponentArray<UVoxelMeshComponent*> Components;Native->GetComponents(Components);uint32 Hash=0;int32 Count=0;
    for(auto* C:Components)if(const auto& Proxy=C->GetRenderProxy()){Hash=HashCombine(Hash,PointerHash(Proxy.Get()));Count+=Proxy->Mesh->Vertices.Num();}
    if(!Hash||Hash==PublishedHash)return false;
    if(Count>1000000){Failure=TEXT("Native preview inspection exceeds one million vertices");return false;}
    FBuffers Next;const FTransform SourceToDisplay=Stamp->GetComponentTransform();
    for(auto* C:Components)if(const auto& Proxy=C->GetRenderProxy())
    {
        const auto& M=*Proxy->Mesh;const int32 Offset=Next.Vertices.Num();
        // Inspection only. The original UVoxelMeshComponent remains visible and
        // owns material binding, render proxy, collision and vertex publication.
        for(const auto& V:M.GetDisplacedVertices({}))
        {
            const FVector Display=C->GetComponentTransform().TransformPosition(FVector(V));
            const FVector Source=SourceToDisplay.InverseTransformPosition(Display)*.01;
            Next.Vertices.Add(Display);Next.SourcePoints.Add((Section?FCLPlanetStampRuntime::SectionToBodyMeters(Source,FCLSectionGeometry::ReferenceSurfaceRadius(*Plan)):Source)*100);
        }
        for(int32 I:M.Indices)Next.Triangles.Add(Offset+I);
    }
    Last=MoveTemp(Next);PublishedHash=Hash;VisiblePlan=Plan;return true;
}
bool FCLNativePreview::Pick(const FVector& Start,const FVector& Direction,FVector& BodyMeters)const
{
    // A queued recipe can already own the stamp transform while retained native
    // collision still belongs to the displayed state. Admit picks only after
    // publication/readiness and application metadata agree on the same input.
    if(!Native||!VisiblePlan||VisiblePlan!=Plan||IsUpdating()||Direction.IsNearlyZero())return false;
    FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(CLNativePreviewPick),true);
    if(!Native->GetWorld()->LineTraceSingleByChannel(Hit,Start,Start+Direction.GetSafeNormal()*10000000,ECC_Visibility,Params)||Hit.GetActor()!=Native.Get())return false;
    const FVector Source=Stamp->GetComponentTransform().InverseTransformPosition(Hit.ImpactPoint)*.01;
    BodyMeters=Section?FCLPlanetStampRuntime::SectionToBodyMeters(Source,FCLSectionGeometry::ReferenceSurfaceRadius(*Plan)):Source;return true;
}
bool FCLNativePreview::PickGlobe(const FVector& A,const FVector& B,FVector& P)const{return !Section&&Pick(A,B,P);}
bool FCLNativePreview::PickSection(const FVector& A,const FVector& B,FVector& P)const{return Section&&Pick(A,B,P);}

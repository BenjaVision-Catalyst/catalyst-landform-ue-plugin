#include "CLNativePreview.h"
#include "CLSectionGeometry.h"
#include "CLPlanetStamp.h"
#include "VoxelLayer.h"
#include "VoxelLinearColorMetadata.h"
#include "VoxelPinValue.h"
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

FCLNativePreview::FMaterialCoordinates FCLNativePreview::MaterialCoordinates(const FVoxelVolumeTransform& Transform,bool Flat,double ReferenceMeters)
{
    check(Transform.Position.IsNearlyZero()&&Transform.Rotation.IsIdentity()&&Transform.Scale.X==Transform.Scale.Y);
    const FVector3d Value(1./double(Transform.Scale.X),1./double(Transform.Scale.Z),ReferenceMeters*100.);
    const FVector3f High(Value);const FVector3f Low(Value-FVector3d(High));
    return {FLinearColor(High.X,High.Y,High.Z,Flat?2:1),FLinearColor(Low.X,Low.Y,Low.Z,0)};
}
FVoxelVolumeTransform FCLNativePreview::StampToDisplay()const
{
    return FVoxelVolumeTransform::Create(Stamp->GetComponentTransform(),FTransform::Identity,FVoxelBox(-1,1),0);
}
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
    auto Coordinates=MaterialCoordinates(StampToDisplay(),Section,Source.SectionReferenceRadiusMeters);
#if WITH_DEV_AUTOMATION_TESTS
    if(!MaterialCoordinatesEnabled)Coordinates.High.A=0;
#endif
    for(const auto& Item:TArray<TPair<FString,FLinearColor>>{{TEXT("MD_PreviewMappingHigh"),Coordinates.High},{TEXT("MD_PreviewMappingLow"),Coordinates.Low}})
    {
        auto* Metadata=LoadObject<UVoxelLinearColorMetadata>(nullptr,*(TEXT("/CatalystLandform/Surfaces/")+Item.Key+TEXT(".")+Item.Key));
        if(!Metadata){Failure=TEXT("Missing native preview coordinate metadata: ")+Item.Key;return false;}
        auto& Override=Source.MetadataOverrides.Overrides.AddDefaulted_GetRef();Override.Metadata=Metadata;Override.Value=FVoxelPinValue::Make(Item.Value);
    }
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
    FBuffers Next;const auto SourceToDisplay=StampToDisplay();
    for(auto* C:Components)if(const auto& Proxy=C->GetRenderProxy())
    {
        const auto& M=*Proxy->Mesh;const int32 Offset=Next.Vertices.Num();
        // Inspection only. The original UVoxelMeshComponent remains visible and
        // owns material binding, render proxy, collision and vertex publication.
        for(const auto& V:M.GetDisplacedVertices({}))
        {
            const FVector Display=C->GetComponentTransform().TransformPosition(FVector(V));
            const FVector Source=SourceToDisplay.InverseTransform(Display)*.01;
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
    const FVector Source=StampToDisplay().InverseTransform(Hit.ImpactPoint)*.01;
    BodyMeters=Section?FCLPlanetStampRuntime::SectionToBodyMeters(Source,FCLSectionGeometry::ReferenceSurfaceRadius(*Plan)):Source;return true;
}
bool FCLNativePreview::PickGlobe(const FVector& A,const FVector& B,FVector& P)const{return !Section&&Pick(A,B,P);}
bool FCLNativePreview::PickSection(const FVector& A,const FVector& B,FVector& P)const{return Section&&Pick(A,B,P);}

#if WITH_DEV_AUTOMATION_TESTS
void FCLNativePreview::SetMaterialCoordinatesEnabledForTesting(bool Enabled)
{MaterialCoordinatesEnabled=Enabled;FString Error;Start(*Native->GetWorld(),Plan,Section,Width,Error,AppliedDraft);}
bool FCLNativePreview::CheckMaterialCoordinates(FString& Error)const
{
    if(IsUpdating()||!VisiblePlan){Error=TEXT("Native material input is not published");return false;}
    auto Expected=MaterialCoordinates(StampToDisplay(),Section,FCLSectionGeometry::ReferenceSurfaceRadius(*VisiblePlan));
    if(!MaterialCoordinatesEnabled)Expected.High.A=0;
    TInlineComponentArray<UVoxelMeshComponent*> Components;Native->GetComponents(Components);int32 Samples=0;
    if(!VisiblePlan->Surfaces){Error=TEXT("Untextured native default has no canonical-coordinate consumer");return true;}
    for(const auto& Item:TArray<TPair<FString,FLinearColor>>{{TEXT("MD_PreviewMappingHigh"),Expected.High},{TEXT("MD_PreviewMappingLow"),Expected.Low}})
    {
        auto* Metadata=LoadObject<UVoxelLinearColorMetadata>(nullptr,*(TEXT("/CatalystLandform/Surfaces/")+Item.Key+TEXT(".")+Item.Key));
        if(!Metadata||Metadata->GPUPacking!=EVoxelFloatMetadataPacking::FourBytes){Error=TEXT("Native mapping metadata must use full floats");return false;}
        for(auto* C:Components)if(const auto& Proxy=C->GetRenderProxy())
        {
            // VPP queries metadata only for materials used by this mesh. Core
            // cut faces using its untextured default need no mapping payload.
            bool UsesCatalog=false;for(const auto Type:Proxy->Mesh->UsedSurfaceTypes)UsesCatalog|=VisiblePlan->Surfaces->NativeTypes.Contains(Type);
            if(!UsesCatalog)continue;
            const auto* Found=Proxy->Mesh->MetadataToBuffer.Find(FVoxelMetadataRef(Metadata));
            if(!Found){Error=TEXT("Published native mesh has no mapping metadata: ")+Item.Key;return false;}
            const auto& Values=(*Found)->AsChecked<FVoxelLinearColorBuffer>();
            for(int32 I=0;I<Proxy->Mesh->Vertices.Num();++I)
            {if(!Values[I].Equals(Item.Value,1.e-6f)){Error=TEXT("Published native metadata belongs to another input: ")+Item.Key;return false;}++Samples;}
        }
    }
    Error=FString::Printf(TEXT("%d native full-float mapping values checked"),Samples);return Samples>0;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "CLPreviewCoordinatesGPU.h"
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPreviewMaterialGPUTest,"Catalyst.Landform.NativeMaterials.PreviewCoordinateGPU",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPreviewMaterialGPUTest::RunTest(const FString&)
{
    TArray<FCLPreviewCoordinatesGPUInput> Inputs;
    auto Split=[](const FVector3d& P,FVector4f& High,FVector4f& Low){const FVector3f H(P);High=FVector4f(H,0);Low=FVector4f(FVector3f(P-FVector3d(H)),0);};
    for(double Radius:{100.,1000.,3389500.,6371000.12345})for(bool Flat:{false,true})for(double Width:{100.,500.,1000.})
    {
        const double Reference=Radius*.45;const FVector Scale=Flat?FVector(1000./Width,1000./Width,1000./Radius):FVector(1000./Radius);
        const auto Native=FVoxelVolumeTransform::Create(FTransform(FQuat::Identity,FVector::ZeroVector,Scale),FTransform::Identity,FVoxelBox(-1,1),0);
        const auto Config=FCLNativePreview::MaterialCoordinates(Native,Flat,Reference);
        for(const FVector3d World:{FVector3d(0,0,0),FVector3d(0,0,45000.123),FVector3d(12345.678,-23456.789,55000.123),FVector3d(-49999.125,49999.625,99999.5),FVector3d(.001,-.002,70000.111)})
        {
            FCLPreviewCoordinatesGPUInput Q;Split(World,Q.WorldHigh,Q.WorldLow);Q.ConfigHigh=FVector4f(Config.High);Q.ConfigLow=FVector4f(Config.Low);
            const auto Local=Native.InverseTransform(World);const FVector3d Body=Flat?FCLPlanetStampRuntime::SectionToBodyMeters(Local*.01,Reference)*100:Local;Split(Body,Q.ExpectedHigh,Q.ExpectedLow);
            auto Phase=[&](double Tile){return FVector4f(float(Body.X/Tile-FMath::FloorToDouble(Body.X/Tile)),float(Body.Y/Tile-FMath::FloorToDouble(Body.Y/Tile)),float(Body.Z/Tile-FMath::FloorToDouble(Body.Z/Tile)),0);};
            Q.Phase200=Phase(200);Q.Phase250=Phase(250);Q.Phase270=Phase(270);Inputs.Add(Q);
        }
    }
    FString Error;auto Job=FCLPreviewCoordinatesGPUReadback::TryStart(Inputs,Error);if(!Job){AddError(Error);return false;}
    class FWait final:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;TSharedPtr<FCLPreviewCoordinatesGPUReadback> Job;double Start=FPlatformTime::Seconds();
    public:
        FWait(FAutomationTestBase* T,TSharedPtr<FCLPreviewCoordinatesGPUReadback> J):Test(T),Job(J){}
        bool Update()override
        {
            TArray<FCLPreviewCoordinatesGPUOutput> Results;const auto Status=Job->Poll(Results);
            if(Status==ECLProjectionReadbackStatus::Pending&&FPlatformTime::Seconds()-Start<30)return false;
            if(Status!=ECLProjectionReadbackStatus::Complete){Test->AddError(TEXT("Preview material GPU readback failed/timed out"));return true;}
            double Position=0,Phase=0;for(const auto& R:Results){Position=FMath::Max(Position,double(R.Errors.X));Phase=FMath::Max(Phase,double(R.Errors.Y));Test->TestFalse(TEXT("Finite GPU coordinate errors"),R.Errors.ContainsNaN());}
            Test->TestEqual(TEXT("All radii, domains, widths and core/edge probes"),Results.Num(),120);
            Test->AddInfo(FString::Printf(TEXT("120 GPU coordinate witnesses: position %.12g cm, physical tile phase %.12g cm"),Position,Phase));
            Test->TestTrue(TEXT("Canonical coordinates retain the 1 mm numerical budget"),Position<=.1);Test->TestTrue(TEXT("2/2.5/2.7 m texture phases retain the 1 mm budget"),Phase<=.1);return true;
        }
    };
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FWait>(this,Job));return true;
}
#endif

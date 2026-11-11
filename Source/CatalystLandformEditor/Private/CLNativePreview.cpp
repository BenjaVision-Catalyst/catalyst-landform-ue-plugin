#include "CLNativePreview.h"
#include "Surface/CLNativeMaterialCoordinates.h"
#include "Misc/ScopeExit.h"
#include "CLSectionGeometry.h"
#include "CLPlanetStamp.h"
#include "Model/CLRegionGeneration.h"
#include "VoxelLayer.h"
#include "VoxelLayerStack.h"
#include "Materials/Material.h"
#include "MegaMaterial/MaterialExpressionGetVoxelFlatNormal.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "VoxelLinearColorMetadata.h"
#include "VoxelPinValue.h"
#include "VoxelStampComponent.h"
#include "VoxelMesh.h"
#include "VoxelRuntime.h"
#include "VoxelState.h"
#include "VoxelChunkKey.h"
#include "Shape/VoxelSphereShape.h"
#include "Shape/VoxelCubeShape.h"
#include "Render/VoxelMeshComponent.h"
#include "Render/VoxelMeshRenderProxy.h"
#include "Collision/VoxelCollisionComponent.h"
#include "Surface/CLSurfaceLibrary.h"
#include "MegaMaterial/VoxelMegaMaterial.h"
#include "Engine/World.h"

namespace
{
UVoxelMegaMaterial* CaveInspectionMaterial()
{
    static TWeakObjectPtr<UVoxelMegaMaterial> Shared;
    if(Shared.IsValid())return Shared.Get();
    auto* Mega=UVoxelMegaMaterial::CreateTransient();Mega->bDetectNewSurfaces=false;
    auto* M=NewObject<UMaterial>(Mega,NAME_None,RF_Transient);M->BlendMode=BLEND_Translucent;M->bDisableDepthTest=true;M->SetShadingModel(MSM_Unlit);
    M->GetEditorOnlyData()->Opacity.UseConstant=true;M->GetEditorOnlyData()->Opacity.Constant=1;
    auto* N=NewObject<UMaterialExpressionGetVoxelFlatNormal>(M);auto* Direction=NewObject<UMaterialExpressionConstant3Vector>(M);Direction->Constant=FLinearColor(.3,-.4,.85);
    auto* Dot=NewObject<UMaterialExpressionDotProduct>(M);Dot->A.Expression=N;Dot->B.Expression=Direction;
    auto* Half=NewObject<UMaterialExpressionMultiply>(M);Half->A.Expression=Dot;Half->ConstB=.45;
    auto* Shade=NewObject<UMaterialExpressionAdd>(M);Shade->A.Expression=Half;Shade->ConstB=.55;
    auto* Color=NewObject<UMaterialExpressionConstant3Vector>(M);Color->Constant=FLinearColor(.005,.32,.5);
    auto* Result=NewObject<UMaterialExpressionMultiply>(M);Result->A.Expression=Shade;Result->B.Expression=Color;
    for(auto* E:TArray<UMaterialExpression*>{N,Direction,Dot,Half,Shade,Color,Result})M->GetExpressionCollection().AddExpression(E);
    M->GetEditorOnlyData()->EmissiveColor.Expression=Result;M->PostEditChange();
    Mega->NonNaniteMaterialType=EVoxelMegaMaterialGenerationType::Custom;Mega->CustomNonNaniteMaterial=M;Mega->CustomLumenMaterial=M;Mega->PostEditChange();
    Shared=Mega;return Mega;
}
}
void FCLNativePreview::SetVisible(bool Visible)
{
    if(IsCaveOverview()){for(const auto& N:CaveDomains)N->SetVisible(Visible);return;}
    if(!Native)return;
    // Hiding the actor also disables its source stamp in VPP. Hide only the
    // native render components so inspection toggles retain the accepted field.
    TInlineComponentArray<UVoxelMeshComponent*> Components;Native->GetComponents(Components);
    for(auto* C:Components){C->SetVisibility(Visible);C->SetHiddenInGame(!Visible);}
}

FCLNativePreview::FMaterialCoordinates FCLNativePreview::MaterialCoordinates(const FVoxelVolumeTransform& Transform,bool Flat,double ReferenceMeters)
{
    const auto Coordinates=FCLNativeMaterialCoordinates::Make(Transform,Flat,ReferenceMeters);
    return {Coordinates.High,Coordinates.Low};
}
FVoxelVolumeTransform FCLNativePreview::StampToDisplay()const
{
    return FVoxelVolumeTransform::Create(Stamp->GetComponentTransform(),FTransform::Identity,FVoxelBox(-1,1),0);
}
FCLNativePreview::~FCLNativePreview(){Reset();}
void FCLNativePreview::Reset()
{
    CaveDomains.Reset();Pending.Reset();VisiblePlan.Reset();Plan.Reset();Stamp.Reset();
    if(Native){Native->DestroyRuntime();Native->Destroy();Native.Reset();}
    Last=FBuffers();InspectionHash=0;PublishedHash=0;AwaitingPublication=false;Failure.Reset();
}
bool FCLNativePreview::Start(UWorld& World,TSharedPtr<const FCLCompiledPlanetRecipe> Input,bool Flat,double Area,FString& Error,bool Draft)
{
    if(!Input){Error=TEXT("Native preview requires an accepted compiled recipe");return false;}
    if(IsCaveOverview())
    {
        if(!Input->Caves||!Input->Caves->Cutters){Error=TEXT("Native cave overview requires generated caves");return false;}
        TArray<FGuid> Ids;for(const auto& D:Input->Caves->InspectionDomains)Ids.Add(D.Id);
        CaveDomains.RemoveAll([&](const auto& N){return !Ids.Contains(N->CaveDomain);});
        for(const auto Id:Ids)
        {
            auto* Found=CaveDomains.FindByPredicate([&](const auto& N){return N->CaveDomain==Id;});
            if(!Found){CaveDomains.Add(MakeUnique<FCLNativePreview>(true,Id));Found=&CaveDomains.Last();}
            if(!(*Found)->Start(World,Input,false,Area,Error,false))return false;
        }
        Plan=MoveTemp(Input);
#if WITH_DEV_AUTOMATION_TESTS
        ++Metrics.Starts;
#endif
        Error.Reset();return true;
    }
    if(!Native)
    {
        FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Native.Reset(World.SpawnActor<AVoxelWorld>(Params));
        if(!Native){Error=TEXT("Native preview world could not be created");return false;}
        Native->DestroyRuntime();Native->SetupForPreview();Native->bEnableNanite=false;Native->bEnableTessellation=false;
        Native->bEnableNavigation=false;Native->bWaitForPCG=false;Native->VoxelSize=25;
        Native->bCreateRuntimeOnBeginPlay=false;Native->bDoubleSidedCollision=true;
        if(CaveInspection)
        {
            auto* Stack=NewObject<UVoxelLayerStack>(Native.Get());Stack->HeightLayers.Reset();Stack->VolumeLayers={NewObject<UVoxelVolumeLayer>(Stack)};Native->LayerStack=Stack;UVoxelLayerStack::OnChanged.Broadcast();
            Native->MegaMaterial=CaveInspectionMaterial();Native->VoxelSize=10;
            Native->VisibilityCollision.SetCollisionEnabled(ECollisionEnabled::NoCollision);Native->InvokerCollision.SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        else if(auto* Material=GetDefault<UCLSurfaceSettings>()->PlanetMaterial.LoadSynchronous())Native->MegaMaterial=Material;
        auto* Component=NewObject<UVoxelStampComponent>(Native.Get());Component->SetupAttachment(Native->GetRootComponent());Component->RegisterComponent();Stamp=Component;
    }
#if WITH_DEV_AUTOMATION_TESTS
    ++Metrics.Starts;
#endif
    Pending=MoveTemp(Input);PendingSection=Flat;PendingWidth=FMath::Clamp(Area,100.,1000.);PendingDraft=Draft;
    Error.Reset();return true;
}
bool FCLNativePreview::ApplyPending()
{
    if(!Pending||AwaitingPublication||Native->IsProcessingNewState())return false;
    Plan=MoveTemp(Pending);
    if(CaveInspection){}
    else if(Plan->MeasuredTerrain&&Plan->MeasuredTerrain->GetMaterial())Native->MegaMaterial=Plan->MeasuredTerrain->GetMaterial();
    else if(auto* Material=GetDefault<UCLSurfaceSettings>()->PlanetMaterial.LoadSynchronous())Native->MegaMaterial=Material;
    Section=PendingSection;Width=PendingWidth;AppliedDraft=PendingDraft;PublishedHash=0;InspectionHash=0;
    // Permit finer close-up cells; native camera LOD still selects refinement.
    if(!CaveInspection)
    {
        Native->VoxelSize=25;
        if(Plan->GeneratedRegions&&Plan->GeneratedRegions->bHasProvinceDeformation)
        {
            double WidthMeters=MAX_dbl;for(const auto& Rift:Plan->GeneratedRegions->Rifts)if(Rift)WidthMeters=FMath::Min(WidthMeters,Rift->ScarpWidthMeters());
            Native->VoxelSize=FMath::Clamp(FMath::FloorToInt(WidthMeters*.25*100000./Plan->RadiusMeters),1,25);
        }
    }
    // Scale the stamp through VPP's supported transform, never the voxel world
    // or the produced mesh. Native LOD sees the same geometry the viewport sees.
    const FVector Scale=Section?FVector(1000./Width,1000./Width,1000./Plan->RadiusMeters):FVector(1000./Plan->RadiusMeters);
    if(CaveInspection&&CaveDomain.IsValid())
    {
        const auto* Domain=Plan->Caves->InspectionDomains.FindByPredicate([&](const auto& D){return D.Id==CaveDomain;});check(Domain);
        const auto Cutters=Plan->Caves->Cutters->Select(Domain->Networks);const FBox Bounds=Cutters->Bounds();
        // Centre the native voxel domain around its own geometry. VPP's render
        // tree is rooted at the voxel-world origin, so an offset planet-sized
        // domain would still subdivide enormous empty regions at fine quality.
        Native->SetActorLocation(Bounds.GetCenter()*100000/Plan->RadiusMeters);
        // Native diagnostic resolution follows physical passage size: at most
        // 50 cm for wide caves, while narrow sections retain the prior 25 cm
        // cap and at least four samples across their minimum diameter.
        const double HalfWidth=Cutters->MinimumHalfWidth();
        const double CellCm=FMath::Min(HalfWidth*50.,FMath::Clamp(HalfWidth*25.,25.,50.));
        Native->VoxelSize=FMath::Max(1,FMath::FloorToInt(CellCm*1000./Plan->RadiusMeters));
    }
    Stamp->SetRelativeScale3D(Scale);if(CaveInspection)Stamp->SetWorldLocation(FVector::ZeroVector);
    FCLPlanetStamp Source;Source.bUseRecipe=true;Source.CompiledRecipe=Plan;Source.Radius=Plan->RadiusMeters*100;
    check(Plan->SourceRecipe);Source.Recipe=*Plan->SourceRecipe;
    Source.bCaveInspectionPreview=CaveInspection;Source.CaveInspectionDomain=CaveDomain;
    Source.Layer=CaveInspection?Native->LayerStack->VolumeLayers[0].Get():UVoxelVolumeLayer::Default();Source.Behavior=CaveInspection?EVoxelStampBehavior::AffectShape:EVoxelStampBehavior::AffectAll;Source.Smoothness=0;Source.BoundsExtension=.05;
    Source.bSectionPreview=Section;Source.SectionWidthMeters=Width;Source.SectionReferenceRadiusMeters=FCLSectionGeometry::ReferenceSurfaceRadius(*Plan);
    // Inspection retains native SetupForPreview quality and chunk size.
    if(!CaveInspection)Native->LODQuality.EditorQuality=AppliedDraft?FFloatInterval(1,1):FFloatInterval(4,4);
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
{if(IsCaveOverview())return Plan!=VisiblePlan||CaveDomains.ContainsByPredicate([](const auto& N){return N->IsUpdating();});return Pending.IsValid()||(Native&&Failure.IsEmpty()&&(AwaitingPublication||!VisiblePlan||Native->IsProcessingNewState()||!Native->IsVoxelWorldReady()));}
bool FCLNativePreview::IsApplyingInput()const
{if(IsCaveOverview())return Plan!=VisiblePlan||CaveDomains.ContainsByPredicate([](const auto& N){return N->IsApplyingInput();});return Failure.IsEmpty()&&(Pending.IsValid()||(Native&&(AwaitingPublication||Plan!=VisiblePlan)));}
TOptional<float> FCLNativePreview::InputProgress()const
{if(IsCaveOverview())
    {float Sum=0;for(const auto& N:CaveDomains){if(!N->IsApplyingInput()){Sum+=1;continue;}const auto P=N->InputProgress();if(!P.IsSet())return {};Sum+=P.GetValue();}return CaveDomains.IsEmpty()?TOptional<float>():TOptional<float>(Sum/CaveDomains.Num());}
    return !Pending&&IsApplyingInput()&&Native&&Native->IsProcessingNewState()?TOptional<float>(Native->GetProgress()):TOptional<float>();}
FString FCLNativePreview::InputProgressText()const
{
    if(IsCaveOverview()){int32 Ready=0;for(const auto& N:CaveDomains)Ready+=N->PublishedPlan()==Plan&&!N->IsApplyingInput();return FString::Printf(TEXT("Generating cave surfaces · %d/%d ready"),Ready,CaveDomains.Num());}
    if(Pending)return TEXT("Preparing preview…");
    if(Native&&Native->IsProcessingNewState())return FString::Printf(TEXT("Generating preview · %.0f%% · %d native tasks remaining"),Native->GetProgress()*100,Native->GetNumPendingTasks());
    return TEXT("Publishing preview…");
}
const void* FCLNativePreview::RuntimeIdentity()const{return Native?Native->GetRuntime().Get():nullptr;}
bool FCLNativePreview::UsesNativePresentation()const
{
    if(IsCaveOverview())return !CaveDomains.IsEmpty()&&!CaveDomains.ContainsByPredicate([](const auto& N){return !N->UsesNativePresentation();});
    if(!Native||!VisiblePlan)return false;
    TInlineComponentArray<UVoxelMeshComponent*> Components;Native->GetComponents(Components);int32 Count=0;
    for(auto* C:Components)if(C->IsRegistered()&&C->GetRenderProxy()){if(!C->IsVisible()||C->bHiddenInGame)return false;++Count;}
    return Count>0;
}
FString FCLNativePreview::Status()const
{
    if(IsCaveOverview()){FString Result=InputProgressText();for(const auto& N:CaveDomains)Result+=TEXT(" | ")+N->Status();return Result;}
    if(!Failure.IsEmpty())return Failure;if(!Native)return TEXT("No native producer");
    TInlineComponentArray<UVoxelMeshComponent*> Components;Native->GetComponents(Components);
    int32 MinLOD=MAX_int32,MaxLOD=0;for(auto* C:Components)if(const auto& Proxy=C->GetRenderProxy()){MinLOD=FMath::Min(MinLOD,Proxy->Mesh->ChunkLOD);MaxLOD=FMath::Max(MaxLOD,Proxy->Mesh->ChunkLOD);}
    const auto State=Native->GetRuntime()?Native->GetRuntime()->GetNewestState():nullptr;
    const FString Config=State?FString::Printf(TEXT(", actor %.1f config editor %d quality %.1f/%.1f game %.1f/%.1f voxel %d chunk %d lod %d/%d"),Native->LODQuality.EditorQuality.Min,State->Config->bIsEditorWorld,State->Config->LODQuality.EditorQuality.Min,State->Config->LODQuality.EditorQuality.Max,State->Config->LODQuality.GameQuality.Min,State->Config->LODQuality.GameQuality.Max,State->Config->VoxelSize,State->Config->RenderChunkSize,MinLOD,MaxLOD):FString();
    return FString::Printf(TEXT("components %d, native producer/presenter %u, ready %d, processing %d, tasks %d, inspected vertices %d"),Components.Num(),Native->GetUniqueID(),Native->IsVoxelWorldReady(),Native->IsProcessingNewState(),Native->GetNumPendingTasks(),Last.Vertices.Num())+Config;
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
#if WITH_DEV_AUTOMATION_TESTS
    const double PollStart=FPlatformTime::Seconds();ON_SCOPE_EXIT{Metrics.MaxPollMs=FMath::Max(Metrics.MaxPollMs,(FPlatformTime::Seconds()-PollStart)*1000);};
#endif
    if(IsCaveOverview())
    {
        bool Changed=false,Ready=Plan.IsValid();uint32 Hash=0;
        for(const auto& N:CaveDomains){Changed|=N->Poll(true);Ready&=N->PublishedPlan()==Plan;Hash=HashCombine(Hash,N->PublishedHash);}
        if(!Ready)return false;const bool Published=VisiblePlan!=Plan||PublishedHash!=Hash;VisiblePlan=Plan;PublishedHash=Hash;return Changed||Published;
    }
    if(!Native||!Failure.IsEmpty())return false;
    ApplyPending();
    if(Pending||AwaitingPublication||!Plan||!Native->IsVoxelWorldReady()||Native->IsProcessingNewState())return false;
    TInlineComponentArray<UVoxelMeshComponent*> Components;Native->GetComponents(Components);uint32 Hash=0;
    for(auto* C:Components)if(const auto& Proxy=C->GetRenderProxy())Hash=HashCombine(Hash,PointerHash(Proxy.Get()));
    if((!Hash&&!CaveInspection)||(Hash==PublishedHash&&VisiblePlan==Plan))return false;
    PublishedHash=Hash;VisiblePlan=Plan;return true;
}
const FCLNativePreview::FBuffers& FCLNativePreview::Published()const
{
    if(IsCaveOverview())
    {
        if(!VisiblePlan||VisiblePlan!=Plan||IsUpdating()||InspectionHash==PublishedHash)return Last;
        FBuffers Next;for(const auto& N:CaveDomains){const auto& M=N->Published();const int32 Offset=Next.Vertices.Num();Next.Vertices.Append(M.Vertices);Next.SourcePoints.Append(M.SourcePoints);Next.SourceCellMeters.Append(M.SourceCellMeters);for(const auto I:M.Triangles)Next.Triangles.Add(Offset+I);}
        Last=MoveTemp(Next);InspectionHash=PublishedHash;
#if WITH_DEV_AUTOMATION_TESTS
        ++Metrics.MeshCopies;Metrics.VerticesCopied+=Last.Vertices.Num();
#endif
        return Last;
    }
    if(!Native||!VisiblePlan||VisiblePlan!=Plan||IsUpdating()||InspectionHash==PublishedHash)return Last;
    TInlineComponentArray<UVoxelMeshComponent*> Components;Native->GetComponents(Components);int32 Count=0;
    for(auto* C:Components)if(const auto& Proxy=C->GetRenderProxy())Count+=Proxy->Mesh->Vertices.Num();
    // This bound belongs only to explicit inspection. Native presentation and
    // LOD have no application inspection budget or copying dependency.
    if(Count>(CaveInspection?16000000:1000000)){Last=FBuffers();InspectionHash=PublishedHash;return Last;}
#if WITH_DEV_AUTOMATION_TESTS
    const double CopyStart=FPlatformTime::Seconds();
#endif
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
            Next.SourceCellMeters.Add(float(double(Native->VoxelSize)*(1<<M.ChunkLOD)/100./Stamp->GetComponentScale().GetAbsMax()));Next.Vertices.Add(Display);Next.SourcePoints.Add((Section?FCLPlanetStampRuntime::SectionToBodyMeters(Source,FCLSectionGeometry::ReferenceSurfaceRadius(*Plan)):Source)*100);
        }
        for(int32 I:M.Indices)Next.Triangles.Add(Offset+I);
    }
#if WITH_DEV_AUTOMATION_TESTS
    ++Metrics.MeshCopies;Metrics.VerticesCopied+=Next.Vertices.Num();Metrics.TotalCopyMs+=(FPlatformTime::Seconds()-CopyStart)*1000;
#endif
    Last=MoveTemp(Next);InspectionHash=PublishedHash;return Last;
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
TArray<uint32> FCLNativePreview::CaveWorldIdentities()const
{TArray<uint32> Ids;for(const auto& N:CaveDomains)Ids.Add(N->WorldIdentity());return Ids;}
TMap<FGuid,uint32> FCLNativePreview::CaveDomainIdentities()const
{TMap<FGuid,uint32> Ids;for(const auto& N:CaveDomains)Ids.Add(N->CaveDomain,N->WorldIdentity());return Ids;}
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

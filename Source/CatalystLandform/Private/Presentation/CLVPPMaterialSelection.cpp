#include "CLVPPMaterialSelection.h"
#include "VoxelMinimal.h"
#include "VoxelWorld.h"
#include "VoxelRuntime.h"
#include "VoxelState.h"
#include "VoxelMesh.h"
#include "Render/VoxelRenderSubsystem.h"
#include "MegaMaterial/VoxelMegaMaterialProxy.h"
#include "Nanite/VoxelNaniteComponent.h"
#include "Nanite/VoxelNaniteMesh.h"
#include "Nanite/VoxelMaterialSelectionCS.h"
#include "ScenePrivate.h"
#include "SceneViewExtension.h"
#include "BasePassRendering.h"
#include "BasePassRendering.inl"
#include "Materials/MaterialRenderProxy.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/NaniteStreamingManager.h"
#include "PrimitiveUniformShaderParametersBuilder.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Engine.h"

// These private native uniform types expose no DLL accessor. Resolve their
// existing registered layouts rather than registering duplicate metadata.
#if !IS_MONOLITHIC
const FShaderParametersMetadata* FVoxelMaterialSelectionParameters::GetStructMetadata()
{return FShaderParametersMetadata::GetNameStructMap().FindChecked(FHashedName(TEXT("MaterialSelectionParameters")));}
const FShaderParametersMetadata* FNaniteShadingUniformParameters::GetStructMetadata()
{return FShaderParametersMetadata::GetNameStructMap().FindChecked(FHashedName(TEXT("NaniteShading")));}
const FShaderParametersMetadata* FNaniteRasterUniformParameters::GetStructMetadata()
{return FShaderParametersMetadata::GetNameStructMap().FindChecked(FHashedName(TEXT("NaniteRaster")));}

#endif

class FCLVPPSelectionPassParameters;
class FCLVPPSelectionContext : public TSharedFromThis<FCLVPPSelectionContext>
{
public:
 FSceneInterface* SceneInterface=nullptr;
 const TSharedRef<FVoxelMegaMaterialProxy> MegaMaterialProxy;
 TSharedPtr<FVoxelMaterialRef> MaterialRef;
 FTransform LocalToWorld;
 TVoxelArray<FVoxelSurfaceType> UsedSurfaceTypes;
 uint32 UniqueId=0;
 bool bLoggedBindings=false;
 TMap<uint8,TSharedPtr<FVoxelMaterialRef>> Bindings;
 TSharedPtr<TUniformBuffer<FPrimitiveUniformShaderParameters>> PrimitiveUniformBuffer;
 explicit FCLVPPSelectionContext(const FVoxelConfig& Config):SceneInterface(Config.World.Resolve_Ensured()->Scene),MegaMaterialProxy(Config.MegaMaterialProxy),LocalToWorld(Config.LocalToWorld),UniqueId(Config.VoxelWorldId){}
 TSharedPtr<FVoxelMaterialRef> GetMaterialInstance(FVoxelMaterialRenderIndex Index) const{return Bindings.FindRef(Index.Index);}
 void PreRenderBasePass_RenderThread(FRDGBuilder& GraphBuilder,FSceneView& View);
 void ComputeMaterialSelection(const FSceneView& View,const FIntPoint& Extent,const FCLVPPSelectionPassParameters& PassParameters,FRHICommandList& RHICmdList);
};
DECLARE_GPU_STAT(CLVPPMaterialSelection);
#include "CLVPPSelection.inc"

static void CLRefreshVPPContexts(FSceneInterface* Scene);

class FCLVPPSelectionExtension final : public FSceneViewExtensionBase
{
public:
 using FSceneViewExtensionBase::FSceneViewExtensionBase;
 TArray<TSharedPtr<FCLVPPSelectionContext>> Contexts;
 const FSceneViewFamily* Family=nullptr;
 void SetupViewFamily(FSceneViewFamily&) override{}
 void SetupView(FSceneViewFamily&,FSceneView&) override{}
 void BeginRenderViewFamily(FSceneViewFamily& ViewFamily) override{CLRefreshVPPContexts(ViewFamily.Scene);}
 void PreRenderViewFamily_RenderThread(FRDGBuilder&,FSceneViewFamily& InFamily) override{Family=&InFamily;}
 void PreRenderBasePass_RenderThread(FRDGBuilder& GraphBuilder,bool) override
 {
  if(!Family)return;
  for(const FSceneView* View:Family->Views) for(const auto& Context:Contexts)
   if(Context->SceneInterface==Family->Scene) Context->PreRenderBasePass_RenderThread(GraphBuilder,*const_cast<FSceneView*>(View));
 }
 void PostRenderViewFamily_RenderThread(FRDGBuilder&,FSceneViewFamily&) override{Family=nullptr;}
};
namespace
{
TSharedPtr<FCLVPPSelectionExtension,ESPMode::ThreadSafe> Extension;
FDelegateHandle WorldCleanup;
IConsoleVariable* NativeSelection=nullptr;
int32 PreviousSelection=0;
TMap<uint32,TSharedPtr<FCLVPPSelectionContext>> ContextCache;
FDelegateHandle DeferredStart;
}
void CLStartVPPMaterialSelection()
{
 if(Extension)return;
 if(!GEngine||!GEngine->ViewExtensions)
 {DeferredStart=FCoreDelegates::OnPostEngineInit.AddStatic(&CLStartVPPMaterialSelection);return;}
 const auto V=FEngineVersion::Current();
 if(V.GetMajor()!=5||V.GetMinor()!=8||V.GetPatch()!=4)return;
 NativeSelection=IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.MegaMaterial.DisableMaterialSelection"));
 check(NativeSelection);PreviousSelection=NativeSelection->GetInt();
 // Respect a user's deliberate native material-selection disable.
 if(PreviousSelection)return;
 Extension=FSceneViewExtensions::NewExtension<FCLVPPSelectionExtension>();
 NativeSelection->Set(1,ECVF_SetByCode);
 WorldCleanup=FWorldDelegates::OnWorldCleanup.AddLambda([](UWorld* World,bool,bool)
 {
  auto* Scene=World->Scene;
  for(auto It=ContextCache.CreateIterator();It;++It)if(It.Value()->SceneInterface==Scene)It.RemoveCurrent();
  if(Extension)
  {
   ENQUEUE_RENDER_COMMAND(CLRetireSelection)([Keep=Extension,Scene](FRHICommandListImmediate&)
    {Keep->Contexts.RemoveAll([Scene](const auto& C){return C->SceneInterface==Scene;});});
  }
 });
 UE_LOG(LogTemp,Display,TEXT("CL_VPP_MATERIAL_SELECTION: qualified UE5.8 bridge; native surfaces and shader retained; only bound terrain proxies indexed"));
}
static void CLRefreshVPPContexts(FSceneInterface* Scene)
{
 if(!Scene||!Scene->GetWorld()||!Extension)return;
  auto& Cache=ContextCache;
  TMap<uint32,TSharedPtr<FCLVPPSelectionContext>> NewCache=Cache;
  for(auto It=NewCache.CreateIterator();It;++It)if(It.Value()->SceneInterface==Scene)It.RemoveCurrent();
  TArray<TSharedPtr<FCLVPPSelectionContext>> NewContexts;
  for(TActorIterator<AVoxelWorld> It(Scene->GetWorld());It;++It)
  {
   auto* World=It->GetWorld();if(!World||It->IsTemplate())continue;
   auto Runtime=It->GetRuntime();if(!Runtime)continue;
   auto State=Runtime->GetState();if(!State||!State->IsRendered()||!State->Config->bEnableNanite)continue;
   auto* Render=State->GetSubsystemPtr<FVoxelRenderSubsystem>();if(!Render)continue;
   TSharedPtr<FCLVPPSelectionContext> Context=MakeShared<FCLVPPSelectionContext>(*State->Config);
   Context->MaterialRef=Render->GetMaterialInstanceRef(EVoxelMegaMaterialTarget::NaniteMaterialSelection);
   Context->UsedSurfaceTypes.AddUnique(FVoxelSurfaceType());
   TInlineComponentArray<UVoxelNaniteComponent*> Components;It->GetComponents(Components);
   for(auto* C:Components)
   {
    if(!C->IsRegistered()||!C->GetMesh())continue;
    if(auto* Default=C->GetMaterial(1))Context->Bindings.FindOrAdd(0)=FVoxelMaterialRef::Make(Default);
    int32 Slot=2;
    for(const auto Surface:C->GetMesh()->Mesh->UsedSurfaceTypes)
    {
     const auto Index=Context->MegaMaterialProxy->GetRenderIndex(Surface);
     if(auto* M=C->GetMaterial(Slot))Context->Bindings.FindOrAdd(Index.Index)=FVoxelMaterialRef::Make(M);
     Context->UsedSurfaceTypes.AddUnique(Surface);++Slot;
    }
   }
   if(!Context->Bindings.IsEmpty())
   {
    // Reuse native resources until actual material bindings or the chart owner change.
    if(auto* Cached=Cache.Find(Context->UniqueId))
    {
     bool Same=(*Cached)->MegaMaterialProxy==Context->MegaMaterialProxy && (*Cached)->MaterialRef==Context->MaterialRef && (*Cached)->Bindings.Num()==Context->Bindings.Num() && (*Cached)->LocalToWorld.Equals(Context->LocalToWorld);
     for(const auto& Entry:Context->Bindings)
     {auto* Old=(*Cached)->Bindings.Find(Entry.Key);Same &= Old && (*Old)->GetMaterial()==Entry.Value->GetMaterial();}
     if(Same)Context=*Cached;
    }
    NewCache.Add(Context->UniqueId,Context);
   }
  }
  Cache=MoveTemp(NewCache);
  for(const auto& Entry:Cache)NewContexts.Add(Entry.Value);
  ENQUEUE_RENDER_COMMAND(CLPublishSelection)([Keep=Extension,NewContexts=MoveTemp(NewContexts)](FRHICommandListImmediate&) mutable
   {Keep->Contexts=MoveTemp(NewContexts);});

}
void CLStopVPPMaterialSelection()
{
 FCoreDelegates::OnPostEngineInit.Remove(DeferredStart);DeferredStart.Reset();
 FWorldDelegates::OnWorldCleanup.Remove(WorldCleanup);WorldCleanup.Reset();
 ContextCache.Reset();Extension.Reset();FlushRenderingCommands();
 if(NativeSelection&&NativeSelection->GetInt()==1)NativeSelection->Set(PreviousSelection,ECVF_SetByCode);
 NativeSelection=nullptr;
}

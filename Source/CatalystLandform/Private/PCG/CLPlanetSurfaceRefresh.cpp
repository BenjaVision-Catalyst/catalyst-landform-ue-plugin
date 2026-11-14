#include "PCG/CLPlanetSurfacePCG.h"
#include "CLPlanet.h"
#include "PCGComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "Animation/TransformProviderData.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
FTSTicker::FDelegateHandle Handle;
struct FRegionWork
{
    FString Desired,Attempt;
    double ChangedAt=0;
    bool Cleaned=false,Generating=false;
};
TMap<TWeakObjectPtr<UCLPlanetSurfaceRegion>,FRegionWork> Work;
TWeakObjectPtr<UCLPlanetSurfaceRegion> Active;
UPCGComponent* Component(UCLPlanetSurfaceRegion* R)
{return IsValid(R)&&R->GetOwner()?R->GetOwner()->FindComponentByClass<UPCGComponent>():nullptr;}
void HideStale(UCLPlanetSurfaceRegion* R)
{
    TInlineComponentArray<UInstancedStaticMeshComponent*> Static(R->GetOwner());
    for(auto* C:Static)C->SetVisibility(false);
    TInlineComponentArray<UInstancedSkinnedMeshComponent*> Skinned(R->GetOwner());
    for(auto* C:Skinned)
    {
        if(C->GetTransformProvider())R->TransformProvider=C->GetTransformProvider();
        C->SetVisibility(false);
    }
}
bool Tick(float)
{
    if(!GEngine)return true;
    // A walk is a frozen duplicate of the editor world, never another generator.
    for(const auto& C:GEngine->GetWorldContexts())if(C.WorldType==EWorldType::PIE)return true;
    const double Now=FPlatformTime::Seconds();
    TArray<UCLPlanetSurfaceRegion*> Regions;
    for(const auto& C:GEngine->GetWorldContexts())
    {
        auto* W=C.World();if(!W||C.WorldType!=EWorldType::Editor||W->bIsTearingDown)continue;
        for(TActorIterator<AActor> It(W);It;++It)
            if(auto* R=It->FindComponentByClass<UCLPlanetSurfaceRegion>();R&&IsValid(R->Planet)&&!R->Planet->GetSurfaceGenerationKey().IsEmpty())Regions.Add(R);
    }
    TSet<TWeakObjectPtr<UCLPlanetSurfaceRegion>> Live;
    for(auto* R:Regions)
    {
        Live.Add(R);auto& S=Work.FindOrAdd(R);const FString Key=R->Planet->GetSurfaceGenerationKey();
        if(S.Desired!=Key)
        {
            S.Desired=Key;S.Attempt.Reset();S.ChangedAt=Now;S.Cleaned=false;S.Generating=false;
            if(R->GeneratedSurfaceKey!=Key)
            {
                HideStale(R);
                if(auto* C=Component(R);C&&C->IsGenerating())C->CancelGeneration();
            }
        }
    }
    for(auto It=Work.CreateIterator();It;++It)if(!Live.Contains(It.Key()))It.RemoveCurrent();
    if(auto* R=Active.Get())
    {
        auto* S=Work.Find(R);auto* C=Component(R);
        if(S&&C&&S->Generating)
        {
            if(C->IsGenerating()||C->IsCleaningUp())return true;
            S->Generating=false;
            if(C->bGenerated&&S->Attempt==R->Planet->GetSurfaceGenerationKey())
            {
                TInlineComponentArray<UInstancedSkinnedMeshComponent*> Skinned(R->GetOwner());
                for(auto* M:Skinned)if(R->TransformProvider)M->SetTransformProvider(R->TransformProvider);
                R->Modify();R->GeneratedSurfaceKey=S->Attempt;
                UE_LOG(LogTemp,Display,TEXT("LANDFORM_SURFACE_REGION_READY: %s | %s"),*R->GetPathName(),*S->Attempt);
            }
            else UE_LOG(LogTemp,Warning,TEXT("Landform surface PCG did not publish: %s"),*R->GetPathName());
        }
        Active.Reset();
    }
    Regions.Sort([](const auto& A,const auto& B){return A.GetPathName()<B.GetPathName();});
    for(auto* R:Regions)
    {
        auto& S=Work.FindChecked(R);auto* C=Component(R);
        if(!C||R->GeneratedSurfaceKey==S.Desired||S.Attempt==S.Desired||Now-S.ChangedAt<1||!R->Planet->IsVoxelWorldReady())continue;
        if(C->IsGenerating()||C->IsCleaningUp())continue;
        // Remove old products before any replacement. Yield between regions; only
        // one native graph is in flight, including after rapid successive edits.
        if(!S.Cleaned){C->CleanupLocalImmediate(true);S.Cleaned=true;return true;}
        S.Attempt=S.Desired;S.Generating=true;Active=R;C->DirtyGenerated();C->GenerateLocal(true);return true;
    }
    return true;
}
}
bool CLPlanetSurfaceRegionsReady(const ACLPlanet* Planet,FString& Status)
{
    if(!IsValid(Planet)||!Planet->GetWorld())return false;
    int32 Total=0,Pending=0;
    for(TActorIterator<AActor> It(Planet->GetWorld());It;++It)
        if(auto* R=It->FindComponentByClass<UCLPlanetSurfaceRegion>();R&&R->Planet==Planet)
        {++Total;auto* C=Component(R);if(!C||C->IsGenerating()||C->IsCleaningUp()||!C->bGenerated||Planet->GetSurfaceGenerationKey().IsEmpty()||R->GeneratedSurfaceKey!=Planet->GetSurfaceGenerationKey())++Pending;}
    if(Pending){Status=FString::Printf(TEXT("Updating planetary scenery: %d of %d regions remain."),Pending,Total);return false;}
    Status.Reset();return true;
}
void CLStartPlanetSurfaceRefresh()
{
#if WITH_EDITOR
    if(!Handle.IsValid())Handle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick),.1f);
#endif
}
void CLStopPlanetSurfaceRefresh()
{FTSTicker::RemoveTicker(Handle);Handle.Reset();Active.Reset();Work.Reset();}

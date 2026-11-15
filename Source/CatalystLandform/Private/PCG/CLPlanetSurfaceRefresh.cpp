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
#include "Components/PrimitiveComponent.h"

namespace
{
FCLPlanetSceneryEvent ReadyEvent,RetiringEvent;
TMap<TWeakObjectPtr<UWorld>,TFunction<bool(UPrimitiveComponent&,FString&)>> MaterialAdapters;
TMap<FName,TFunction<bool(const ACLPlanet&,FVector,FString&)>> Readiness;
}
FCLPlanetSceneryEvent& CLPlanetSceneryReady(){return ReadyEvent;}
FCLPlanetSceneryEvent& CLPlanetSceneryRetiring(){return RetiringEvent;}
void FCLSceneryMaterials::Register(UWorld* W,TFunction<bool(UPrimitiveComponent&,FString&)> F){MaterialAdapters.Add(W,MoveTemp(F));}
void FCLSceneryMaterials::Unregister(UWorld* W){MaterialAdapters.Remove(W);}
bool FCLSceneryMaterials::Prepare(UPrimitiveComponent& C,FString& Error)
{if(auto* F=MaterialAdapters.Find(C.GetWorld()))return (*F)(C,Error);Error.Reset();return true;}
void FCLSceneryReadiness::Register(FName N,TFunction<bool(const ACLPlanet&,FVector,FString&)> F){Readiness.Add(N,MoveTemp(F));}
void FCLSceneryReadiness::Unregister(FName N){Readiness.Remove(N);}
bool FCLSceneryReadiness::IsReady(const ACLPlanet& P,FVector At,FString& Status)
{for(auto& R:Readiness)if(!R.Value(P,At,Status))return false;Status.Reset();return true;}

namespace
{
FTSTicker::FDelegateHandle Handle;
struct FObserved {FDelegateHandle Ready,Cleaned;TWeakObjectPtr<ACLPlanet> Planet;};
TMap<TWeakObjectPtr<UPCGComponent>,FObserved> Observed;
void Detach(UPCGComponent* C,const FObserved& O)
{if(C){C->OnPCGGraphGeneratedDelegate.Remove(O.Ready);C->OnPCGGraphCleanedDelegate.Remove(O.Cleaned);}}
void Published(UPCGComponent* C)
{
    auto* Original=C?C->GetOriginalComponent():nullptr;auto* R=Original?Original->GetOwner()->FindComponentByClass<UCLPlanetSurfaceRegion>():nullptr;
    if(!R||!IsValid(R->Planet))return;
    TInlineComponentArray<UInstancedSkinnedMeshComponent*> Skinned(C->GetOwner());for(auto* M:Skinned)if(R->TransformProvider)M->SetTransformProvider(R->TransformProvider);
    // Native regeneration may reuse component objects with new instances/bounds.
    // Retire render caches before admitting the newly published native products.
    RetiringEvent.Broadcast(R->Planet,C->GetOwner());
    ReadyEvent.Broadcast(R->Planet,C->GetOwner());
}
bool Observe(float)
{
    if(!GEngine)return true;TSet<TWeakObjectPtr<UPCGComponent>> Live;
    for(const auto& Context:GEngine->GetWorldContexts())
    {
        auto* W=Context.World();if(!W||W->bIsTearingDown||(Context.WorldType!=EWorldType::Game&&Context.WorldType!=EWorldType::PIE))continue;
        for(TActorIterator<AActor> It(W);It;++It)
        {
            TInlineComponentArray<UPCGComponent*> Components(*It);
            for(auto* C:Components)
            {
                auto* Original=C->GetOriginalComponent();auto* R=Original?Original->GetOwner()->FindComponentByClass<UCLPlanetSurfaceRegion>():nullptr;
                if(C==Original||!R||!IsValid(R->Planet))continue;Live.Add(C);
                if(!Observed.Contains(C))
                {
                    FObserved O;O.Planet=R->Planet;O.Ready=C->OnPCGGraphGeneratedDelegate.AddStatic(&Published);
                    O.Cleaned=C->OnPCGGraphCleanedDelegate.AddLambda([Weak=O.Planet](UPCGComponent* P){if(auto* Planet=Weak.Get())RetiringEvent.Broadcast(Planet,P->GetOwner());});
                    Observed.Add(C,O);if(C->bGenerated)Published(C);
                }
            }
        }
    }
    for(auto It=Observed.CreateIterator();It;++It)if(!Live.Contains(It.Key())){Detach(It.Key().Get(),It.Value());It.RemoveCurrent();}
    return true;
}
}
void CLStartPlanetSceneryObservation(){if(!Handle.IsValid())Handle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Observe),.1f);}
void CLStopPlanetSceneryObservation(){FTSTicker::RemoveTicker(Handle);Handle.Reset();for(auto& Pair:Observed)Detach(Pair.Key.Get(),Pair.Value);Observed.Reset();}

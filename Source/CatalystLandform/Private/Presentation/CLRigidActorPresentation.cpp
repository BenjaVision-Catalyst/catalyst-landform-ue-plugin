#include "Presentation/CLRigidActorPresentation.h"
#include "Presentation/CLPlanetProjection.h"
#include "Presentation/CLPresentationTransform.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "GroomComponent.h"

struct FCLRigidActorPresentation::FImpl
{
    struct FVisual
    {
        TWeakObjectPtr<UMeshComponent> Source, Copy;
        bool MainPass=true, DepthPass=true, Shadow=true;
        EVisibilityBasedAnimTickOption AnimTick=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    };
    TWeakObjectPtr<AActor> Source, Actor;
    TArray<FVisual> Visuals;
    double NextDiscovery=0;
    void Restore(FVisual& V)
    {
        if(auto* C=V.Source.Get())
        {
            C->SetRenderInMainPass(V.MainPass);C->SetRenderInDepthPass(V.DepthPass);C->SetCastShadow(V.Shadow);
            if(auto* S=Cast<USkeletalMeshComponent>(C))S->VisibilityBasedAnimTickOption=V.AnimTick;
        }
        if(auto* C=V.Copy.Get())C->DestroyComponent();
    }
    bool Discover(FString& Error)
    {
        auto* RootSource=Source.Get();auto* Owner=Actor.Get();
        if(!RootSource||!Owner){Error=TEXT("Rigid presentation source retired");return false;}
        TArray<AActor*> Actors{RootSource};RootSource->GetAttachedActors(Actors,false,true);
        for(auto* A:Actors)
        {
            TInlineComponentArray<UMeshComponent*> Components(A);
            for(auto* C:Components)
            {
                if(!C->IsRegistered()||(!C->IsA<USkeletalMeshComponent>()&&!C->IsA<UStaticMeshComponent>()&&!C->IsA<UGroomComponent>()))continue;
                if(Visuals.ContainsByPredicate([&](const FVisual& V){return V.Source.Get()==C;}))continue;
                // Hidden animation/equipment drivers remain canonical and never acquire a visible copy.
                if(!C->IsVisible()||C->bHiddenInGame)continue;
                if(Visuals.Num()>=256){Error=TEXT("Rigid actor presentation exceeds 256 visual components");return false;}
                auto* Copy=DuplicateObject<UMeshComponent>(C,Owner,MakeUniqueObjectName(Owner,C->GetClass(),C->GetFName()));
                // Rebuild the copied follower map before any transform/bounds evaluation.
                if(auto* Skin=Cast<USkeletalMeshComponent>(Copy))
                {
                    Skin->SetAnimInstanceClass(nullptr);Skin->SetLeaderPoseComponent(CastChecked<USkeletalMeshComponent>(C),true,false);
                    Skin->SetComponentTickEnabled(false);
                }
                Copy->ClearFlags(RF_Transactional);Copy->SetFlags(RF_Transient);Copy->SetIsReplicated(false);
                Copy->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
                Copy->SetAbsolute(true,true,true);Copy->SetupAttachment(Owner->GetRootComponent());
                Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);Copy->SetGenerateOverlapEvents(false);
                Copy->SetSimulatePhysics(false);Copy->SetCanEverAffectNavigation(false);
                Owner->AddInstanceComponent(Copy);
                FVisual V;V.Source=C;V.Copy=Copy;V.MainPass=C->bRenderInMainPass;V.DepthPass=C->bRenderInDepthPass;V.Shadow=C->CastShadow;
                if(auto* Skin=Cast<USkeletalMeshComponent>(C))
                {V.AnimTick=Skin->VisibilityBasedAnimTickOption;Skin->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;}
                C->SetRenderInMainPass(false);C->SetRenderInDepthPass(false);C->SetCastShadow(false);
                Visuals.Add(V);
            }
        }
        // Groom binding finds its skeletal parent in this visual actor, never a simulation mesh.
        for(auto& V:Visuals)if(auto* C=V.Copy.Get();C&&!C->IsRegistered())
        {
            auto* Original=V.Source.Get();auto* Parent=Original?Original->GetAttachParent():nullptr;
            if(auto* Link=Visuals.FindByPredicate([&](const FVisual& P){return P.Source.Get()==Parent;}))
                C->SetupAttachment(Link->Copy.Get(),Original->GetAttachSocketName());
            C->RegisterComponent();
        }
        Error.Reset();return true;
    }
};
FCLRigidActorPresentation::FCLRigidActorPresentation():Impl(MakeUnique<FImpl>()){}
TUniquePtr<FCLRigidActorPresentation> FCLRigidActorPresentation::TryCreate(AActor& Source,FString& Error)
{
    if(!Source.GetWorld()||Source.IsActorBeingDestroyed()){Error=TEXT("Rigid presentation needs a live world actor");return nullptr;}
    TUniquePtr<FCLRigidActorPresentation> P(new FCLRigidActorPresentation());P->Impl->Source=&Source;
    FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Actor=Source.GetWorld()->SpawnActor<AActor>(AActor::StaticClass(),FTransform::Identity,Params);P->Impl->Actor=Actor;
    if(!Actor){Error=TEXT("Cannot create visual presentation actor");return nullptr;}
    Actor->SetReplicates(false);Actor->Tags.Add(TEXT("CL.RenderOnly"));
    auto* Root=NewObject<USceneComponent>(Actor);Actor->SetRootComponent(Root);Root->SetMobility(EComponentMobility::Movable);Root->RegisterComponent();
    if(!P->Impl->Discover(Error))return nullptr;
    return P;
}
FCLRigidActorPresentation::~FCLRigidActorPresentation()
{
    for(auto& V:Impl->Visuals)Impl->Restore(V);
    if(auto* A=Impl->Actor.Get())A->Destroy();
}
bool FCLRigidActorPresentation::Update(const FCLPlanetProjection& Projection,FString& Error)
{
    auto* Source=Impl->Source.Get();if(!Source||Source->IsActorBeingDestroyed()){Error=TEXT("Rigid presentation source retired");return false;}
    const double Now=Source->GetWorld()->GetTimeSeconds();
    for(int32 I=Impl->Visuals.Num()-1;I>=0;--I)if(!Impl->Visuals[I].Source.IsValid()||!Impl->Visuals[I].Copy.IsValid())
    {Impl->Restore(Impl->Visuals[I]);Impl->Visuals.RemoveAt(I);}
    if(Now>=Impl->NextDiscovery){if(!Impl->Discover(Error))return false;Impl->NextDiscovery=Now+.25;}
    const auto Canonical=Source->GetActorTransform();FVector Position=FVector::ZeroVector;FQuat Rotation=FQuat::Identity;
    const bool Inside=FCLPresentationTransform::TryProjectCamera(Projection,Canonical.GetLocation(),Canonical.GetRotation(),Position,Rotation);
    const FTransform Presented(Rotation,Position,Canonical.GetScale3D());
    if(Inside)Impl->Actor->SetActorTransform(Presented);
    for(auto& V:Impl->Visuals)
    {
        auto* C=V.Source.Get();auto* Copy=V.Copy.Get();if(!C||!Copy)continue;
        Copy->SetVisibility(C->IsVisible(),false);Copy->SetHiddenInGame(!Inside||C->bHiddenInGame||C->GetOwner()->IsHidden(),false);
        if(!Inside)continue;
        // One rigid frame for the whole assembly preserves body/clothing/hair/equipment relationships.
        Copy->SetWorldTransform(C->GetComponentTransform().GetRelativeTransform(Canonical)*Presented,false,nullptr,ETeleportType::TeleportPhysics);
        Copy->SetCastShadow(V.Shadow);Copy->SetRenderInMainPass(V.MainPass);Copy->SetRenderInDepthPass(V.DepthPass);
    }
    if(!Source->GetActorTransform().Equals(Canonical,0)){Error=TEXT("Presentation changed canonical actor pose");return false;}
    Error.Reset();return true;
}
int32 FCLRigidActorPresentation::NumVisuals()const{return Impl->Visuals.Num();}
AActor* FCLRigidActorPresentation::GetVisualActor()const{return Impl->Actor.Get();}

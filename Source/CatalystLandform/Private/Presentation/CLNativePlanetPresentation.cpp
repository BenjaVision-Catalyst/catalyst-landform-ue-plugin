#include "Presentation/CLNativePlanetPresentation.h"
#include "CLPlanet.h"
#include "CLNaniteProjection.h"
#include "Presentation/CLPlanetProjection.h"
#include "Presentation/CLSceneryRenderPolicy.h"
#include "Model/CLPlanetRecipe.h"
#include "Nanite/VoxelNaniteComponent.h"
#include "Collision/VoxelCollisionComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/PrimitiveComponent.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "EngineGlobals.h"
#include "Materials/MaterialInterface.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"


static TAutoConsoleVariable<float> CVarCLCurvature(
    TEXT("landform.Curvature"), 1, TEXT("Landform presentation curvature: 1 natural, 0 flat"));

// Qualification bypass: triangles keep their normal presentation for a same-pose voxel comparison.
static TAutoConsoleVariable<int32> CVarCLVoxelProjection(
    TEXT("landform.VoxelProjection"), 1, TEXT("Project Nanite voxel bricks into the walking chart (0: diagnostic bypass)"));

struct FCLNativePlanetPresentation::FImpl
{
    struct FMesh
    {
        TWeakObjectPtr<UVoxelNaniteComponent> Component;
        TWeakObjectPtr<UStaticMesh> Mesh;
        FTransform Transform;
        TArray<TStrongObjectPtr<UMaterialInterface>> Materials;
        TArray<float> Data;
        FVector Corners[8];
        double RadiusRatio = 1;
        bool bHeightValid = false, bWasShown = false, bHidden = false, bPreserveMaterials = false;
        EShadowCacheInvalidationBehavior ShadowBehavior;
        int32 WpoDistance = 0, SourceMaterialSlots = 0;
        uint64 BornFrame = 0;
        float BoundsScale = 1;
        FBox LocalBox{ForceInit};
    };
    struct FCVar
    {
        IConsoleVariable *Var;
        FString Before, Applied;
    };
    struct FPrimitive
    {
        TWeakObjectPtr<UPrimitiveComponent> Component;
        FBox Box{ForceInit}, SkinnedBounds{ForceInit};
        FTransform Transform;
        TArray<float> Data;
        float BoundsScale=1;
        bool Hidden=false,EvaluateWpo=true;
        int32 WpoDistance=0;
        EShadowCacheInvalidationBehavior ShadowBehavior;
        FCLSceneryRenderPolicy Budget;
        FBox RenderBounds{ForceInit};
        int32 CullStart=0,CullEnd=0;
        float AnimationScreenSize=0;
        bool CastShadow=true,TickEnabled=false,TickSuppressed=false;
    };
    TArray<FPrimitive> Primitives;
    TWeakObjectPtr<ACLPlanet> Planet;
    TStrongObjectPtr<UMaterialInterface> Material;
    TArray<FMesh> Meshes;
    TMap<uint32, int32> Index;
    FDelegateHandle SubmissionHandle, OriginOffsetHandle;
    int32 OriginOffsetEvents = 0, OriginTransformPublications = 0;
    TArray<FCVar> CVars;
    TArray<TPair<uint32, const void *>, TInlineAllocator<512>> Colliders;
    const void *Runtime = nullptr;
    const void *Sampling = nullptr;
    double MaxArcRadians = UE_PI / 3;
    double MinRadiusRatio = .6, MaxRadiusRatio = 1.3;
    double Radius = 0, InitialCurvature = 1, PreviousCurvature = 1, CurrentCurvature = 1;
    double CurrentRatio = 1, PreviousRatio = 1;
    FVector PreviousAnchor = FVector::ZAxisVector, CurrentAnchor = FVector::ZAxisVector;
    bool bFramed = false;
    FCLSceneWindow Window;
    bool bSweep = false, bInitialized = false, bFixed = false, bContractValid = true, bRuntimeStable = true,
         bNativeMaterials = false, bNativeControl = false;
    int32 MaterialOverrides = 0, MeshLimit = 512;
    bool bWorldAnchored = false, bConsoleCurvature = true;
    double RequestedCurvature = 1;
    bool bGameplay = false;
    double BlendFrom = 1, BlendTarget = 1, BlendStarted = 0;
    int32 Mode = 1, Visible = 0, CollisionChanges = 0, Frames = 0, ReadyEvents = 0, RetireEvents = 0,
          PeakMeshes = 0;
    double MinCurvature = 1, MaxCurvature = 0, MaxPendingTasks = 0, CallbackCpuMs = 0;
    FString LifecycleError;
    TArray<double> CpuMs;

    void SetProfile(const TCHAR *Name, const TCHAR *Value)
    {
        if (auto *Var = IConsoleManager::Get().FindConsoleVariable(Name); Var && Var->GetString() != Value)
        {
            CVars.Add({Var, Var->GetString(), Value});
            Var->Set(Value, ECVF_SetByCode);
        }
    }
    static void Restore(FMesh &M, bool bRestoreMaterials = true)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(CLProjection_Restore);
        auto *C = M.Component.Get();
        if (!C || !C->IsRegistered() || C->GetStaticMesh() != M.Mesh.Get())
            return;
        C->SetHiddenInGame(M.bHidden);
        C->ShadowCacheInvalidationBehavior = M.ShadowBehavior;
        C->SetWorldPositionOffsetDisableDistance(M.WpoDistance);
        C->SetBoundsScale(M.BoundsScale);
        C->ResetCustomPrimitiveData();
        if (!M.Data.IsEmpty())
            C->SetCustomPrimitiveDataFloatArray(0, M.Data);
        if (bRestoreMaterials && !M.bPreserveMaterials)
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(CLProjection_RestoreMaterialOverrides);
            for (int32 S = 0; S < M.Materials.Num(); ++S)
                C->SetMaterial(S, M.Materials[S].Get());
        }
    }
    void Retire(int32 K, bool bRestore)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(CLProjection_Retire);
        // The pinned native producer overwrites/clears every material slot immediately after this callback.
        // Avoid a redundant material/proxy update here; detach still uses the full Restore path.
        if (bRestore)
            Restore(Meshes[K], false);
        const auto *C = Meshes[K].Component.Get();
        if (C)
            Index.Remove(C->GetUniqueID());
        else
            for (auto It = Index.CreateIterator(); It; ++It)
                if (It.Value() == K)
                {
                    It.RemoveCurrent();
                    break;
                }
        Meshes.RemoveAtSwap(K, 1, EAllowShrinking::No);
        if (Meshes.IsValidIndex(K))
            if (const auto *Moved = Meshes[K].Component.Get())
                Index.FindChecked(Moved->GetUniqueID()) = K;
    }
    void Present(FMesh &M)
    {
        auto *C = M.Component.Get();
        const bool Natural = CurrentCurvature == 1 && PreviousCurvature == 1;
        bool bInside = bWorldAnchored && Natural;
        if (!bInside)
        {
            bInside = M.bHeightValid;
            for (const auto &U : M.Corners)
                bInside &= FVector::DotProduct(U, CurrentAnchor) >= FMath::Cos(MaxArcRadians)+5.e-6;
        }
        const bool bShown = bInside && !M.bHidden;
        if (C->bHiddenInGame == bShown)
            C->SetHiddenInGame(!bShown);
        if (bShown)
        {
            const bool bHistory = M.bWasShown && M.BornFrame < GFrameCounter;
            const auto Previous = bHistory ? PreviousAnchor : CurrentAnchor;
            const double OldC = bHistory ? PreviousCurvature : CurrentCurvature;
            const double L =
                FMath::Max(
                    1., FMath::Max((CurrentCurvature + (1 - CurrentCurvature) * M.RadiusRatio) * CurrentRatio,
                                   (OldC + (1 - OldC) * M.RadiusRatio) *
                                       (bHistory ? PreviousRatio : CurrentRatio))) +
                2.e-6;
            if (bWorldAnchored)
            {
                const FVector Center = Planet->BodyCenterWorld();
                const FVector Origin = Center + Radius * CurrentAnchor,
                              OldOrigin = Center + Radius * Previous;
                const FVector3f H(Origin), OH(OldOrigin);
                const FVector3f Lo(Origin - FVector(H)), OLo(OldOrigin - FVector(OH));
                const float Data[32] = {float(CurrentAnchor.X),
                                        float(CurrentAnchor.Y),
                                        float(CurrentAnchor.Z),
                                        float(CurrentCurvature),
                                        float(Previous.X),
                                        float(Previous.Y),
                                        float(Previous.Z),
                                        float(OldC),
                                        float(Radius),
                                        float(L),
                                        float(Mode),
                                        9150,
                                        H.X,
                                        H.Y,
                                        H.Z,
                                        0,
                                        Lo.X,
                                        Lo.Y,
                                        Lo.Z,
                                        0,
                                        OH.X,
                                        OH.Y,
                                        OH.Z,
                                        0,
                                        OLo.X,
                                        OLo.Y,
                                        OLo.Z,
                                        0,
                                        float(CVarCLVoxelProjection.GetValueOnGameThread()!=0),0,0,0};
                C->SetCustomPrimitiveDataFloatArray(0, MakeArrayView(Data));
                M.bWasShown = bShown;
                return;
            }
            if (bFramed)
            {
                const FVector Center = Window.Frame().BodyToNativeCentimeters(FVector::ZeroVector);
                const FVector Origin = Center + Radius * CurrentAnchor,
                              OldOrigin = Center + Radius * Previous;
                const FVector3f High(Window.AnchorMeters * 100);
                const FVector Low = Window.AnchorMeters * 100 - FVector(High);
                const FQuat Q = Window.BodyFromLocal;
                const float Data[32] = {float(CurrentAnchor.X),
                                        float(CurrentAnchor.Y),
                                        float(CurrentAnchor.Z),
                                        float(CurrentCurvature),
                                        float(Previous.X),
                                        float(Previous.Y),
                                        float(Previous.Z),
                                        float(OldC),
                                        float(Radius),
                                        2.5f,
                                        0,
                                        9140,
                                        float(Origin.X),
                                        float(Origin.Y),
                                        float(Origin.Z),
                                        0,
                                        float(OldOrigin.X),
                                        float(OldOrigin.Y),
                                        float(OldOrigin.Z),
                                        0,
                                        High.X,
                                        High.Y,
                                        High.Z,
                                        0,
                                        float(Low.X),
                                        float(Low.Y),
                                        float(Low.Z),
                                        0,
                                        float(Q.X),
                                        float(Q.Y),
                                        float(Q.Z),
                                        float(Q.W)};
                C->SetCustomPrimitiveDataFloatArray(0, MakeArrayView(Data));
                M.bWasShown = bShown;
                return;
            }
            const auto BC = M.LocalBox.GetCenter(), BE = M.LocalBox.GetExtent();
            const float Data[20] = {float(CurrentAnchor.X),
                                    float(CurrentAnchor.Y),
                                    float(CurrentAnchor.Z),
                                    float(CurrentCurvature),
                                    float(Previous.X),
                                    float(Previous.Y),
                                    float(Previous.Z),
                                    float(OldC),
                                    float(Radius),
                                    float(L),
                                    float(Mode),
                                    9137,
                                    float(BC.X),
                                    float(BC.Y),
                                    float(BC.Z),
                                    float(bNativeControl),
                                    float(BE.X),
                                    float(BE.Y),
                                    float(BE.Z),
                                    0};
            C->SetCustomPrimitiveDataFloatArray(0, MakeArrayView(Data));
        }
        M.bWasShown = bShown;
    }
    bool Bind(UVoxelNaniteComponent &C, FString &Error)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(CLProjection_Bind);
        if (!C.IsRegistered() || !C.GetStaticMesh())
        {
            Error = TEXT("Native ready event has no registered mesh");
            return false;
        }
        if (Index.Contains(C.GetUniqueID()))
        {
            Error = TEXT("Native producer overwrote a bound generation without retiring it");
            return false;
        }
        if (Meshes.Num() >= MeshLimit || C.GetNumMaterials() > 32)
        {
            Error = FString::Printf(
                TEXT("Nanite presentation budget exceeded (%d live meshes, 32 slots per mesh)"), MeshLimit);
            return false;
        }
        const FVector Scale = C.GetComponentTransform().GetScale3D();
        if (Scale.ContainsNaN() || Scale.GetMin() <= 0 ||
            !FMath::IsNearlyEqual(Scale.GetMin(), Scale.GetMax(), 1.e-8))
        {
            Error = TEXT("Prototype requires positive uniform source-component scale");
            return false;
        }
        TArray<UMaterialInterface *, TInlineAllocator<32>> SourceMaterials;
        for (int32 S = 0; S < C.GetNumMaterials(); ++S)
        {
            auto *Source = C.GetMaterial(S);
            if (Source == Material.Get())
            {
                Error = TEXT("Native producer retained a presentation material in its ready generation");
                return false;
            }
            SourceMaterials.Add(Source);
        }
        const int32 K = Meshes.AddDefaulted();
        Index.Add(C.GetUniqueID(), K);
        auto &M = Meshes[K];
        M.bPreserveMaterials = bNativeMaterials;
        M.BornFrame = GFrameCounter;
        M.Component = &C;
        M.Mesh = C.GetStaticMesh();
        M.Transform = C.GetComponentTransform();
        M.Data = C.GetCustomPrimitiveData().Data;
        M.bHidden = C.bHiddenInGame;
        M.SourceMaterialSlots = C.GetNumMaterials();
        M.ShadowBehavior = C.ShadowCacheInvalidationBehavior;
        M.WpoDistance = C.WorldPositionOffsetDisableDistance;
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(CLProjection_SaveMaterials);
            if (bNativeMaterials)
            {
                // Read the producer-owned bindings directly. GetMaterial resolves Nanite override
                // inheritance and recursion guards; repeating that for every slot every frame is unnecessary.
                if (C.OverrideMaterials.Num() < M.SourceMaterialSlots || C.OverrideMaterials.Num() > 32)
                {
                    Error = TEXT("Native producer has no bounded complete override array");
                    return false;
                }
                for (const auto &Source : C.OverrideMaterials)
                    M.Materials.Emplace(Source.Get());
            }
            else
                for (auto *Source : SourceMaterials)
                    M.Materials.Emplace(Source);
        }
        M.LocalBox = C.GetStaticMesh()->GetBoundingBox();
        const auto Box = M.LocalBox.TransformBy(M.Transform);
        M.BoundsScale = C.BoundsScale;
        const double MinExtent = C.GetStaticMesh()->GetBoundingBox().GetExtent().GetMin() * Scale.GetMin();
        // Public UE bounds scale supplies the coarse scene-culling envelope. The
        // versioned shader overlay retains tight projected Nanite node bounds.
        C.SetBoundsScale(M.BoundsScale + Radius * 4 / FMath::Max(1., MinExtent));
        const FVector Center =
            (bFramed || bWorldAnchored) && Planet.IsValid() ? Planet->BodyCenterWorld() : FVector::ZeroVector;
        const double MinRadius = FVector::Distance(Box.GetClosestPointTo(Center), Center);
        double MaxRadius = 0;
        for (int32 J = 0; J < 8; ++J)
        {
            const FVector Corner(J & 1 ? Box.Max.X : Box.Min.X, J & 2 ? Box.Max.Y : Box.Min.Y,
                                 J & 4 ? Box.Max.Z : Box.Min.Z);
            MaxRadius = FMath::Max(MaxRadius, (Corner - Center).Length());
            M.Corners[J] = (Corner - Center).GetSafeNormal();
        }
        M.bHeightValid =
            MinRadius >= Radius * MinRadiusRatio && MaxRadius <= Radius * MaxRadiusRatio;
        M.RadiusRatio = M.bHeightValid ? Radius / MinRadius : 1;
        C.ShadowCacheInvalidationBehavior = EShadowCacheInvalidationBehavior::Always;
        C.SetWorldPositionOffsetDisableDistance(0);
        if (!bNativeMaterials)
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(CLProjection_BindMaterialOverrides);
            for (int32 S = 0; S < C.GetNumMaterials(); ++S)
            {
                C.SetMaterial(S, Material.Get());
                ++MaterialOverrides;
            }
        }
        PeakMeshes = FMath::Max(PeakMeshes, Meshes.Num());
        // Native ready callbacks run before frame submission. New generations have no old mesh history.
        if (bInitialized)
            Present(M);
        Error.Reset();
        return true;
    }
    static void RestorePrimitive(FPrimitive& P)
    {
        if(auto* C=P.Component.Get())
        {
            C->SetHiddenInGame(P.Hidden); C->SetBoundsScale(P.BoundsScale);
            if(auto* Skinned=Cast<UInstancedSkinnedMeshComponent>(C))
            {if(P.TickSuppressed)Skinned->SetComponentTickEnabled(P.TickEnabled);Skinned->SetPrimitiveBoundsOverride(P.SkinnedBounds);Skinned->SetCullDistances(P.CullStart,P.CullEnd);Skinned->SetAnimationMinScreenSize(P.AnimationScreenSize);Skinned->UpdateBounds();Skinned->MarkRenderTransformDirty();}
            if(auto* Static=Cast<UInstancedStaticMeshComponent>(C))Static->SetCullDistances(P.CullStart,P.CullEnd);
            if(auto* Static=Cast<UStaticMeshComponent>(C)) {Static->SetEvaluateWorldPositionOffset(P.EvaluateWpo);Static->SetWorldPositionOffsetDisableDistance(P.WpoDistance);}
            if(auto* Skin=Cast<USkinnedMeshComponent>(C)) {Skin->SetEvaluateWorldPositionOffset(P.EvaluateWpo);Skin->SetWorldPositionOffsetDisableDistance(P.WpoDistance);}
            C->ShadowCacheInvalidationBehavior=P.ShadowBehavior;
            C->SetCastShadow(P.CastShadow);
            C->ResetCustomPrimitiveData();
            if(!P.Data.IsEmpty()) C->SetCustomPrimitiveDataFloatArray(0,P.Data);
        }
    }
    bool PresentPrimitives(FString& Error)
    {
        const FVector Center=Planet->BodyCenterWorld();
        const FVector Origin=Center+Radius*CurrentAnchor,OldOrigin=Center+Radius*PreviousAnchor;
        const FVector3f H(Origin),OH(OldOrigin),Lo(Origin-FVector(H)),OLo(OldOrigin-FVector(OH));
        FCLPlanetProjectionSettings Settings;Settings.Center=Center;Settings.AnchorDirection=CurrentAnchor;Settings.ReferenceRadiusCm=Radius;
        Settings.Curvature=CurrentCurvature;Settings.MaxArcRadians=MaxArcRadians;Settings.MinAltitudeCm=(MinRadiusRatio-1)*Radius;Settings.MaxAltitudeCm=(MaxRadiusRatio-1)*Radius;
        FCLPlanetProjection Projection,PreviousProjection;
        if(!FCLPlanetProjection::TryCreate(Settings,Projection)){Error=TEXT("Invalid scenery projection");return false;}
        Settings.AnchorDirection=PreviousAnchor;Settings.Curvature=PreviousCurvature;
        if(!FCLPlanetProjection::TryCreate(Settings,PreviousProjection)){Error=TEXT("Invalid scenery history");return false;}
        for(auto& P:Primitives)
        {
            auto* C=P.Component.Get(); if(!C) continue;
            if(!C->GetComponentTransform().Equals(P.Transform,1.e-8))
            { Error=TEXT("Canonical scenery component moved during inspection"); return false; }
            const double MinRadius=FVector::Distance(P.Box.GetClosestPointTo(Center),Center);
            bool Inside=MinRadius>=MinRadiusRatio*Radius;
            for(int32 J=0;J<8;++J)
            {
                FVector Corner(J&1?P.Box.Max.X:P.Box.Min.X,J&2?P.Box.Max.Y:P.Box.Min.Y,J&4?P.Box.Max.Z:P.Box.Min.Z);
                const FVector D=Corner-Center;
                Inside &= D.Length()<=MaxRadiusRatio*Radius && FVector::DotProduct(D.GetSafeNormal(),CurrentAnchor)>=FMath::Cos(MaxArcRadians)+5.e-6;
            }
            C->SetHiddenInGame(P.Hidden||!Inside);
            if(!Inside) continue;
            const float L=float(FMath::Max(1.,Radius/FMath::Max(1.,MinRadius))*FMath::Max(CurrentRatio,PreviousRatio)+2.e-6);
            const float Data[32]={float(CurrentAnchor.X),float(CurrentAnchor.Y),float(CurrentAnchor.Z),float(CurrentCurvature),
                float(PreviousAnchor.X),float(PreviousAnchor.Y),float(PreviousAnchor.Z),float(PreviousCurvature),
                float(Radius),L,float(Mode),9150,H.X,H.Y,H.Z,0,Lo.X,Lo.Y,Lo.Z,0,OH.X,OH.Y,OH.Z,0,OLo.X,OLo.Y,OLo.Z,0,float(CVarCLVoxelProjection.GetValueOnGameThread()!=0),0,0,0};
            C->SetCustomPrimitiveDataFloatArray(0,MakeArrayView(Data));
            FBox Bounds,OldBounds;
            if(!FCLSceneryRenderPolicy::ProjectBounds(P.Box,Projection,L,Bounds)){Error=TEXT("Scenery bounds left their admitted chart");return false;}
            if(FCLSceneryRenderPolicy::ProjectBounds(P.Box,PreviousProjection,L,OldBounds))Bounds+=OldBounds;
            if(!P.RenderBounds.IsValid||!P.RenderBounds.IsInside(Bounds)||(C->IsA<UInstancedSkinnedMeshComponent>()&&P.RenderBounds.GetVolume()>Bounds.GetVolume()*2))
            {
                const FBox Envelope=Bounds.ExpandBy(200.);
                if(auto* Skin=Cast<UInstancedSkinnedMeshComponent>(C))
                {Skin->SetPrimitiveBoundsOverride(Envelope);Skin->UpdateBounds();Skin->MarkRenderTransformDirty();P.RenderBounds=Envelope;}
                else
                {
                    const double Scale=FCLSceneryRenderPolicy::CenteredBoundsScale(P.Box,Envelope);
                    C->SetBoundsScale(P.BoundsScale*Scale);
                    P.RenderBounds=FBox(P.Box.GetCenter()-P.Box.GetExtent()*Scale,P.Box.GetCenter()+P.Box.GetExtent()*Scale);
                }
            }
            if(bGameplay)
            {
                const double Distance=FVector::Distance(P.Box.GetClosestPointTo(Origin),Origin);
                const bool Shadows=P.CastShadow && Distance<P.Budget.ShadowEndCm;
                C->SetCastShadow(Shadows);
                // Only local casters invalidate for changing projection and wind. Keep
                // this policy stable across multiple camera evaluations in one frame.
                const auto Behavior=Shadows?EShadowCacheInvalidationBehavior::Always:EShadowCacheInvalidationBehavior::Rigid;
                if(C->ShadowCacheInvalidationBehavior!=Behavior){C->ShadowCacheInvalidationBehavior=Behavior;C->MarkRenderStateDirty();}
            }
        }
        return true;
    }
    bool Synchronize(FString& Error)
    {
        // Stock VPP publishes and recycles components on the game thread. Observe
        // completed generations before the camera and again before render submission.
        // No producer hook, vendor patch, field rebuild or collision mutation.
        TInlineComponentArray<UVoxelNaniteComponent*,512> Components;
        Planet->GetComponents(Components);
        for (int32 K=Meshes.Num()-1; K>=0; --K)
        {
            auto* C=Meshes[K].Component.Get();
            if (!C || !C->IsRegistered() || C->GetStaticMesh()!=Meshes[K].Mesh.Get())
            {
                if (C)
                {
                    C->SetHiddenInGame(Meshes[K].bHidden);
                    C->SetBoundsScale(Meshes[K].BoundsScale);
                    C->ResetCustomPrimitiveData();
                    if (!Meshes[K].Data.IsEmpty()) C->SetCustomPrimitiveDataFloatArray(0,Meshes[K].Data);
                }
                Retire(K,false); ++RetireEvents;
            }
        }
        for (auto* C:Components)
            if (C->IsRegistered() && C->GetStaticMesh())
            {
                if (!Index.Contains(C->GetUniqueID()))
                { if(!Bind(*C,Error)) return false; ++ReadyEvents; }
                else if(!C->GetComponentTransform().Equals(Meshes[Index.FindChecked(C->GetUniqueID())].Transform,1.e-8))
                { Error=TEXT("Canonical native mesh transform changed during presentation"); return false; }
            }
        if(bFixed && bInitialized && (ReadyEvents || RetireEvents))
        { Error=TEXT("Fixed-pose canonical mesh generation changed"); return false; }
        return true;
    }

};
FCLNativePlanetPresentation::FCLNativePlanetPresentation() : Impl(MakeShared<FImpl>())
{
}
TUniquePtr<FCLNativePlanetPresentation> FCLNativePlanetPresentation::TryCreate(
    ACLPlanet &Planet, const FCLCompiledPlanetRecipe &Plan, UMaterialInterface &Material, FString &Error)
{
    FCLNanitePresentationOptions Options;
    Options.bGameplay = FParse::Param(FCommandLine::Get(), TEXT("CLGameplayPresentation"));
    Options.bNativeMaterials = FParse::Param(FCommandLine::Get(), TEXT("CLProjectionNativeMaterials"));
    Options.bNativeControl = FParse::Param(FCommandLine::Get(), TEXT("CLProjectionNativeControl"));
    Options.bSweep = FParse::Param(FCommandLine::Get(), TEXT("CLProjectionSweep"));
    Options.bFixed = FParse::Param(FCommandLine::Get(), TEXT("CLProjectionPose"));
    FParse::Value(FCommandLine::Get(), TEXT("CLCurvature="), Options.InitialCurvature);
    FParse::Value(FCommandLine::Get(), TEXT("CLProjectionBounds="), Options.BoundsMode);
    return TryCreate(Planet, Plan, Material, Options, Error);
}
TUniquePtr<FCLNativePlanetPresentation> FCLNativePlanetPresentation::TryCreate(
    ACLPlanet &Planet, const FCLCompiledPlanetRecipe &Plan, UMaterialInterface &Material,
    const FCLNanitePresentationOptions &Options, FString &Error)
{
    if (!CLNaniteProjectionShaderOverlayEnabled())
    {
        Error = TEXT("Projection needs the matching -CLNaniteProjectionShaders cook/runtime flag");
        return nullptr;
    }
    if (Options.bWorldAnchored &&
        (Planet.SamplingWindow.bEnabled ||
         !Planet.GetActorScale3D().Equals(FVector::OneVector, 1.e-12)))
    {
        Error = TEXT("World-anchored presentation requires a rigid canonical planet with "
                     "unit scale");
        return nullptr;
    }
    if (!Options.bWorldAnchored && !Planet.SamplingWindow.bEnabled &&
        !Planet.GetActorTransform().Equals(FTransform::Identity, 1.e-9))
    {
        Error = TEXT("Prototype requires a planet at world origin with identity transform");
        return nullptr;
    }
    if (Planet.SamplingWindow.bEnabled &&
        (!Planet.SamplingWindow.Validate(Error) || !Planet.GetActorQuat().Equals(FQuat::Identity, 1.e-12) ||
         !Planet.GetActorScale3D().Equals(FVector::OneVector, 1.e-12) ||
         Planet.GetActorLocation().GetAbsMax() > 100000))
    {
        Error = TEXT("Framed presentation requires a rigid native window within the qualified one-kilometre "
                     "local origin range");
        return nullptr;
    }
    auto Result = TUniquePtr<FCLNativePlanetPresentation>(new FCLNativePlanetPresentation());
    auto &I = *Result->Impl;
    I.bWorldAnchored = Options.bWorldAnchored;
    I.bConsoleCurvature = Options.bConsoleCurvature;
    I.RequestedCurvature = Options.InitialCurvature;
    I.MaxArcRadians = Options.MaxArcRadians;
    I.bFramed = Planet.SamplingWindow.bEnabled;
    I.MinRadiusRatio=I.bFramed?.55:Options.MinRadiusRatio;
    I.MaxRadiusRatio=I.bFramed?1.5:Options.MaxRadiusRatio;
    I.Window = Planet.SamplingWindow;
    I.bGameplay = Options.bGameplay;
    I.bNativeMaterials = Options.bNativeMaterials;
    I.bNativeControl = Options.bNativeControl;
    I.Planet = &Planet;
    I.Material.Reset(&Material);
    I.Runtime = Planet.GetRuntime().Get();
    I.Sampling = Planet.GetSamplingRuntime().Get();
    I.Radius = (Plan.RadiusMeters + Plan.BaseHeightMeters) * 100;
    if (!FMath::IsFinite(Options.ReferenceRadiusCm) || Options.ReferenceRadiusCm < 0)
    {
        Error = TEXT("Invalid explicit presentation reference radius");
        return nullptr;
    }
    if (Options.ReferenceRadiusCm > 0)
        I.Radius = Options.ReferenceRadiusCm;
    I.bSweep = Options.bSweep;
    I.bFixed = Options.bFixed;
    I.InitialCurvature = Options.InitialCurvature;
    I.Mode = Options.BoundsMode;
    if (I.bNativeControl && (!I.bNativeMaterials || I.bSweep || I.InitialCurvature != 1))
    {
        Error = TEXT("Native control requires native materials at fixed natural curvature");
        return nullptr;
    }
    if (!FMath::IsFinite(I.MinRadiusRatio) || !FMath::IsFinite(I.MaxRadiusRatio) ||
        I.MinRadiusRatio<=0 || I.MaxRadiusRatio<I.MinRadiusRatio ||
        !FMath::IsFinite(I.MaxArcRadians) || I.MaxArcRadians<=0 || I.MaxArcRadians>=UE_HALF_PI ||
        !FMath::IsFinite(I.InitialCurvature) || I.InitialCurvature < 0 || I.InitialCurvature > 1 ||
        (I.Mode != 1 && I.Mode != 2) || I.Radius <= 0)
    {
        Error = TEXT("Invalid prototype curvature/bounds/radius");
        return nullptr;
    }
    I.BlendFrom = I.BlendTarget = I.InitialCurvature;
    if (I.bConsoleCurvature)
        CVarCLCurvature->Set(float(I.InitialCurvature), ECVF_SetByCode);
    // The full native Mars body exceeds the old Lab chart's 512-mesh metadata budget.
    // This bounds binding metadata only; VPP still selects and generates every mesh.
    I.MeshLimit = I.bWorldAnchored ? 2048 : 512;
    I.Meshes.Reserve(I.MeshLimit);
    I.Index.Reserve(I.MeshLimit);
    TInlineComponentArray<UVoxelNaniteComponent *, 512> Components;
    Planet.GetComponents(Components);
    for (auto *C : Components)
        if (C->IsRegistered() && C->GetStaticMesh())
            if (!I.Bind(*C, Error))
                return nullptr;
    if (I.Meshes.IsEmpty())
    {
        Error = TEXT("No native Nanite meshes are ready");
        return nullptr;
    }
    TWeakPtr<FImpl> Weak = Result->Impl;
    I.SubmissionHandle = FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.AddLambda(
        [Weak](UWorld* World) {
            if (auto State = Weak.Pin(); State && State->Planet.IsValid() && State->Planet->GetWorld() == World && State->bInitialized)
            {
                FString Why;
                if (!State->Synchronize(Why)) { State->bContractValid = false; State->LifecycleError = Why; }
                else for (auto& Mesh : State->Meshes) State->Present(Mesh);
            }
        });
    TWeakPtr<FImpl> OriginWeak = Result->Impl;
    I.OriginOffsetHandle = FCoreDelegates::PostWorldOriginOffset.AddLambda(
        [OriginWeak](UWorld *World, FIntVector Before, FIntVector After) {
            const auto State = OriginWeak.Pin();
            if (!State || !State->Planet.IsValid() || State->Planet->GetWorld() != World)
                return;
            const FVector Offset = FVector(Before) - FVector(After);
            for (auto &Mesh : State->Meshes)
            {
                FTransform Expected = Mesh.Transform;
                Expected.AddToTranslation(Offset);
                auto *Component = Mesh.Component.Get();
                if (!Component || !Component->GetComponentTransform().Equals(Expected, 1.e-8))
                {
                    State->bContractValid = false;
                    State->LifecycleError =
                        TEXT("Native origin offset did not translate a retained mesh rigidly");
                    return;
                }
                Mesh.Transform = Expected;
                Component->MarkRenderTransformDirty();
                ++State->OriginTransformPublications;
            }
            ++State->OriginOffsetEvents;
        });
    I.SetProfile(TEXT("r.Lumen.DiffuseIndirect.Allow"), TEXT("0"));
    I.SetProfile(TEXT("r.Lumen.Reflections.Allow"), TEXT("0"));
    I.SetProfile(TEXT("r.DistanceFieldAO"), TEXT("0"));
    I.SetProfile(TEXT("r.DistanceFieldShadowing"), TEXT("0"));
    Error.Reset();
    return Result;
}
FCLNativePlanetPresentation::~FCLNativePlanetPresentation()
{
    auto &I = *Impl;
    FCoreDelegates::PostWorldOriginOffset.Remove(I.OriginOffsetHandle);
    FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.Remove(I.SubmissionHandle);
    for (auto &M : I.Meshes)
        FImpl::Restore(M);
    for(auto& P:I.Primitives) FImpl::RestorePrimitive(P);
    for (auto &V : I.CVars)
        if (V.Var->GetString() == V.Applied)
            V.Var->Set(*V.Before, ECVF_SetByCode);
}
bool FCLNativePlanetPresentation::AddPrimitive(UPrimitiveComponent& C,FString& Error)
{
    auto& I=*Impl;
    if(!I.bWorldAnchored || C.GetWorld()!=I.Planet->GetWorld() || !C.IsRegistered() || I.Primitives.Num()>=32768)
    { Error=TEXT("Scenery presentation requires a registered component in this planet world (maximum 32768)"); return false; }
    for(const auto& P:I.Primitives) if(P.Component.Get()==&C) return true;
    for(float Value:C.GetCustomPrimitiveData().Data) if(Value!=0)
    { Error=TEXT("Scenery uses the reserved projection custom-primitive-data range"); return false; }
    auto& P=I.Primitives.AddDefaulted_GetRef();
    P.Component=&C; P.Box=C.Bounds.GetBox(); P.Transform=C.GetComponentTransform();
    P.Data=C.GetCustomPrimitiveData().Data; P.BoundsScale=C.BoundsScale;
    P.Hidden=C.bHiddenInGame; P.ShadowBehavior=C.ShadowCacheInvalidationBehavior;
    P.CastShadow=C.CastShadow;
    double Size=0;
    if(const auto* Static=Cast<UStaticMeshComponent>(&C);Static&&Static->GetStaticMesh())Size=Static->GetStaticMesh()->GetBounds().BoxExtent.GetMax()*2;
    if(const auto* Skin=Cast<USkinnedMeshComponent>(&C);Skin&&Skin->GetSkinnedAsset())Size=Skin->GetSkinnedAsset()->GetBounds().BoxExtent.GetMax()*2;
    P.Budget=FCLSceneryRenderPolicy::ForSize(Size*C.GetComponentScale().GetAbsMax());
    if(auto* Static=Cast<UStaticMeshComponent>(&C)) {P.EvaluateWpo=Static->bEvaluateWorldPositionOffset;P.WpoDistance=Static->WorldPositionOffsetDisableDistance;Static->SetEvaluateWorldPositionOffset(true);Static->SetWorldPositionOffsetDisableDistance(I.bGameplay?P.Budget.WpoEndCm:P.WpoDistance);}
    if(auto* Skin=Cast<USkinnedMeshComponent>(&C)) {P.EvaluateWpo=Skin->bEvaluateWorldPositionOffset;P.WpoDistance=Skin->WorldPositionOffsetDisableDistance;Skin->SetEvaluateWorldPositionOffset(true);Skin->SetWorldPositionOffsetDisableDistance(I.bGameplay?P.Budget.WpoEndCm:P.WpoDistance);}
    if(auto* Skinned=Cast<UInstancedSkinnedMeshComponent>(&C))
    {
        P.SkinnedBounds=Skinned->GetPrimitiveBoundsOverride();
        if(const FFloatProperty* Property=FindFProperty<FFloatProperty>(Skinned->GetClass(),TEXT("AnimationMinScreenSize")))P.AnimationScreenSize=Property->GetPropertyValue_InContainer(Skinned);Skinned->GetCullDistances(P.CullStart,P.CullEnd);
        if(I.bGameplay){
            // GPU transform providers animate independently of the inherited CPU
            // bone-refresh tick, which otherwise republishes unchanged instance bounds.
            if(Skinned->GetTransformProvider()){P.TickEnabled=Skinned->IsComponentTickEnabled();P.TickSuppressed=true;Skinned->SetComponentTickEnabled(false);}
            Skinned->SetCullDistances(P.Budget.FadeStartCm,P.Budget.DrawEndCm);Skinned->SetAnimationMinScreenSize(FMath::Max(.1f,P.AnimationScreenSize));}
    }
    if(auto* Static=Cast<UInstancedStaticMeshComponent>(&C))
    {P.CullStart=Static->InstanceStartCullDistance;P.CullEnd=Static->InstanceEndCullDistance;if(I.bGameplay)Static->SetCullDistances(P.Budget.FadeStartCm,P.Budget.DrawEndCm);}
    Error.Reset(); return true;
}
bool FCLNativePlanetPresentation::Tick(const FVector &InAnchor, double Elapsed, bool bMeasure, FString &Error)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(CLProjection_Tick);
    const double Started = FPlatformTime::Seconds();
    auto &I = *Impl;
    auto *Planet = I.Planet.Get();
    if (!Planet || Planet->GetRuntime().Get() != I.Runtime ||
        Planet->GetSamplingRuntime().Get() != I.Sampling)
    {
        I.bRuntimeStable = false;
        Error = TEXT("Canonical runtime changed during render prototype");
        return false;
    }
    if (!I.bContractValid)
    {
        Error = I.LifecycleError;
        return false;
    }
    const FVector Anchor = InAnchor.GetSafeNormal();
    double C = I.bSweep ? .5 + .5 * FMath::Cos(2 * UE_PI * Elapsed / 12)
                        : (I.bConsoleCurvature ? double(CVarCLCurvature.GetValueOnGameThread())
                                               : I.RequestedCurvature);
    if (I.bGameplay && !I.bSweep && FMath::IsFinite(C) && C >= 0 && C <= 1)
    {
        if (C != I.BlendTarget)
        {
            I.BlendFrom = I.bInitialized ? I.CurrentCurvature : I.InitialCurvature;
            I.BlendTarget = C;
            I.BlendStarted = Elapsed;
        }
        const double T = FMath::Clamp((Elapsed - I.BlendStarted) / 2., 0., 1.);
        C = FMath::Lerp(I.BlendFrom, I.BlendTarget, T * T * (3 - 2 * T));
    }
    if (I.bNativeControl && C != 1)
    {
        Error = TEXT("Native control curvature changed from natural");
        return false;
    }
    if (Anchor.IsNearlyZero() || Anchor.ContainsNaN() || !FMath::IsFinite(C) || C < 0 || C > 1)
    {
        Error = TEXT("Invalid dynamic projection state");
        return false;
    }
    if (!I.bInitialized)
    {
        I.PreviousAnchor = Anchor;
        I.PreviousCurvature = C;
        I.bInitialized = true;
    }
    else { I.PreviousAnchor=I.CurrentAnchor;I.PreviousCurvature=I.CurrentCurvature; }
    I.CurrentAnchor = Anchor;
    I.CurrentCurvature = C;
    auto Ratio = [&I](double Curvature) {
        const double Arc=I.MaxArcRadians;
        return (Curvature == 0 ? 1 : FMath::Sin(Curvature * Arc) / (Curvature * Arc)) /
               (FMath::Sin(Arc) / Arc);
    };
    I.CurrentRatio = Ratio(C);
    I.PreviousRatio = Ratio(I.PreviousCurvature);
    if (!I.Synchronize(Error)) { I.bContractValid=false; I.LifecycleError=Error; return false; }
    const int32 Active=I.Meshes.Num();
    if (I.bNativeMaterials)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(CLProjection_AuditNativeSlots);
        for (const auto &Bound : I.Meshes)
        {
            const auto *Comp = Bound.Component.Get();
            if (!Comp || Comp->GetNumMaterials() != Bound.SourceMaterialSlots ||
                Comp->OverrideMaterials.Num() != Bound.Materials.Num())
            {
                Error = TEXT("Native material slot count changed outside lifecycle");
                I.bContractValid = false;
                return false;
            }
            for (int32 S = 0; S < Bound.Materials.Num(); ++S)
                if (Comp->OverrideMaterials[S].Get() != Bound.Materials[S].Get())
                {
                    Error = TEXT("Native material identity changed outside lifecycle");
                    I.bContractValid = false;
                    return false;
                }
        }
    }
    if (Active != I.Meshes.Num())
    {
        I.bContractValid = false;
        Error = TEXT("Native mesh retired without a presentation lifecycle notification");
        return false;
    }
    I.Visible = 0;
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(CLProjection_PresentAll);
        for (auto &M : I.Meshes)
        {
            I.Present(M);
            I.Visible += M.bWasShown;
        }
    }
    TInlineComponentArray<UVoxelCollisionComponent *, 128> Collision;
    Planet->GetComponents(Collision);
    TArray<TPair<uint32, const void *>, TInlineAllocator<512>> Current;
    for (auto *Comp : Collision)
        if (Comp->IsRegistered() && Comp->GetCollider())
            Current.Emplace(Comp->GetUniqueID(), Comp->GetCollider().Get());
    Current.Sort([](const auto &A, const auto &B) { return A.Key < B.Key; });
    if (bMeasure)
    {
        int32 A = 0, B = 0;
        while (A < Current.Num() || B < I.Colliders.Num())
        {
            if (B >= I.Colliders.Num() || (A < Current.Num() && Current[A].Key < I.Colliders[B].Key))
            {
                ++I.CollisionChanges;
                ++A;
            }
            else if (A >= Current.Num() || Current[A].Key > I.Colliders[B].Key)
            {
                ++I.CollisionChanges;
                ++B;
            }
            else
            {
                I.CollisionChanges += Current[A].Value != I.Colliders[B].Value;
                ++A;
                ++B;
            }
        }
    }
    I.Colliders = MoveTemp(Current);
    I.MaxPendingTasks = FMath::Max(I.MaxPendingTasks, double(Planet->GetNumPendingTasks()));
    if (I.bFixed && bMeasure && I.CollisionChanges)
    {
        Error = TEXT("Stationary render sweep changed active canonical collider identities");
        return false;
    }
    if(!I.PresentPrimitives(Error)) return false;
    I.MinCurvature = FMath::Min(I.MinCurvature, C);
    I.MaxCurvature = FMath::Max(I.MaxCurvature, C);
    ++I.Frames;
    const double TotalCpuMs = (FPlatformTime::Seconds() - Started) * 1000 + I.CallbackCpuMs;
    I.CallbackCpuMs = 0;
    if (bMeasure && I.CpuMs.Num() < 100000)
        I.CpuMs.Add(TotalCpuMs);
    Error.Reset();
    return true;
}
void FCLNativePlanetPresentation::WriteReport(FJsonObject &Report) const
{
    const auto &I = *Impl;
    auto O = MakeShared<FJsonObject>();
    O->SetStringField(TEXT("profile"), I.bNativeMaterials
                                           ? TEXT("Native VPP and scenery material presentation; direct "
                                                  "lighting and sky ambient")
                                           : TEXT("Experimental direct-light diagnostic material; Lumen and "
                                                  "distance-field lighting disabled"));
    int32 SceneryPackets=0,TerrainPackets=0;
    for(const auto& P:I.Primitives) if(const auto* C=P.Component.Get())
    {
        const auto& D=C->GetCustomPrimitiveData().Data;
        SceneryPackets+=D.Num()>=28 && D[3]==I.CurrentCurvature && D[11]==9150;
    }
    for(const auto& M:I.Meshes) if(const auto* C=M.Component.Get();C && M.bWasShown)
    {
        const auto& D=C->GetCustomPrimitiveData().Data;
        TerrainPackets+=D.Num()>=28 && D[3]==I.CurrentCurvature && D[11]==9150;
    }
    O->SetNumberField(TEXT("sceneryProjectionPackets"),SceneryPackets);
    O->SetNumberField(TEXT("visibleTerrainProjectionPackets"),TerrainPackets);
    O->SetNumberField(TEXT("sceneryComponents"),I.Primitives.Num());
    O->SetBoolField(TEXT("voxelProjectionEnabled"),CVarCLVoxelProjection.GetValueOnGameThread()!=0);
    O->SetBoolField(TEXT("stockVPP"),true);
    O->SetBoolField(TEXT("worldAnchored"), I.bWorldAnchored);
    O->SetBoolField(TEXT("nativeWindow"), I.bFramed);
    O->SetNumberField(TEXT("originOffsetEvents"), I.OriginOffsetEvents);
    O->SetNumberField(TEXT("originTransformPublications"), I.OriginTransformPublications);
    O->SetBoolField(TEXT("nativeMaterialsPreserved"), I.bNativeMaterials && I.bContractValid);
    O->SetBoolField(TEXT("nativeMaterialControl"), I.bNativeControl);
    O->SetNumberField(TEXT("presentationMaterialSlotOverrides"), I.MaterialOverrides);
    O->SetNumberField(TEXT("boundsMode"), I.Mode);
    O->SetNumberField(TEXT("meshBindingLimit"), I.MeshLimit);
    O->SetNumberField(TEXT("trackedNaniteMeshes"), I.Meshes.Num());
    O->SetNumberField(TEXT("peakTrackedNaniteMeshes"), I.PeakMeshes);
    O->SetNumberField(TEXT("visibleNaniteMeshes"), I.Visible);
    O->SetNumberField(TEXT("nativeMeshReadyEvents"), I.ReadyEvents);
    O->SetNumberField(TEXT("nativeMeshRetireEvents"), I.RetireEvents);
    O->SetNumberField(TEXT("frames"), I.Frames);
    O->SetNumberField(TEXT("chartMaxArcDegrees"), FMath::RadiansToDegrees(I.MaxArcRadians));
    O->SetNumberField(TEXT("chartMinRadiusRatio"),I.MinRadiusRatio);
    O->SetNumberField(TEXT("chartMaxRadiusRatio"),I.MaxRadiusRatio);
    O->SetNumberField(TEXT("physicalReferenceRadiusCm"), I.Radius);
    if(I.CurrentCurvature>0) O->SetNumberField(TEXT("visualReferenceRadiusCm"), I.Radius/I.CurrentCurvature);
    O->SetNumberField(TEXT("minCurvature"), I.MinCurvature);
    O->SetNumberField(TEXT("maxCurvature"), I.MaxCurvature);
    O->SetNumberField(TEXT("activeCanonicalColliders"), I.Colliders.Num());
    O->SetNumberField(TEXT("collisionIdentityChangesAfterWarmup"), I.CollisionChanges);
    O->SetNumberField(TEXT("maxPendingNativeTasks"), I.MaxPendingTasks);
    O->SetBoolField(TEXT("canonicalRuntimeIdentitiesUnchanged"), I.bRuntimeStable);
    O->SetBoolField(TEXT("bindingContractValid"), I.bContractValid);
    O->SetBoolField(TEXT("canonicalMeshAndRuntimeIdentitiesUnchanged"),
                    I.bRuntimeStable && I.bContractValid && I.ReadyEvents == 0 && I.RetireEvents == 0);
    O->SetBoolField(TEXT("sweep"), I.bSweep);
    O->SetStringField(TEXT("cpuTimingScope"),
                      TEXT("Game-thread tick plus native lifecycle callback work since the preceding tick"));
    if (!I.CpuMs.IsEmpty())
    {
        auto Values = I.CpuMs;
        Values.Sort();
        O->SetNumberField(
            TEXT("gameThreadUpdateP99Ms"),
            Values[FMath::Clamp(FMath::CeilToInt(.99 * Values.Num()) - 1, 0, Values.Num() - 1)]);
        O->SetNumberField(TEXT("gameThreadUpdateMaxMs"), Values.Last());
    }
    Report.SetObjectField(TEXT("projectionPrototype"), O);
}

bool FCLNativePlanetPresentation::GetCurrentProjection(FCLPlanetProjection &Out) const
{
    const auto &I = *Impl;
    if (!I.bInitialized || !I.bContractValid)
        return false;
    FCLPlanetProjectionSettings S;
    S.ReferenceRadiusCm = I.Radius;
    S.AnchorDirection = I.CurrentAnchor;
    S.Curvature = I.CurrentCurvature;
    S.MaxArcRadians = I.MaxArcRadians;
    if ((I.bFramed || I.bWorldAnchored) && I.Planet.IsValid())
        S.Center = I.Planet->BodyCenterWorld();
    S.MinAltitudeCm = (I.MinRadiusRatio-1) * I.Radius;
    S.MaxAltitudeCm = I.bFramed ? 50000 : (I.MaxRadiusRatio-1) * I.Radius;
    return FCLPlanetProjection::TryCreate(S, Out);
}

bool FCLNativePlanetPresentation::SetCurvature(double Value)
{
    if (!FMath::IsFinite(Value) || Value < 0 || Value > 1)
        return false;
    Impl->RequestedCurvature = Value;
    return true;
}

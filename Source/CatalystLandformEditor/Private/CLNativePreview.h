#pragma once
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "VoxelWorld.h"
#include "VoxelStampTransform.h"
class UVoxelStampComponent;
struct FCLCompiledPlanetRecipe;
/** One persistent native VPP producer/presenter for each terrain or inspection role.
 * Pending recipe input coalesces; generation, materials, collision and LOD stay native. */
class FCLNativePreview
{
public:
    // Read-only inspection data extracted from the published native mesh. Never rendered.
    struct FBuffers {TArray<FVector> Vertices,SourcePoints;TArray<int32> Triangles;};
    struct FMaterialCoordinates{FLinearColor High,Low;};
    static FMaterialCoordinates MaterialCoordinates(const FVoxelVolumeTransform&,bool Section,double ReferenceMeters);
    explicit FCLNativePreview(bool InspectCaves=false,FGuid Network={}):CaveInspection(InspectCaves),CaveDomain(Network){}
    ~FCLNativePreview();
    void SetVisible(bool Visible);
    bool Start(UWorld&,TSharedPtr<const FCLCompiledPlanetRecipe>,bool Section,double Width,FString&,bool Draft=false);
    void Reset();
    bool Poll(bool Grid=false);
    bool IsUpdating() const;
    bool IsApplyingInput()const;
    TOptional<float> InputProgress()const;
    FString InputProgressText()const;
    bool IsActive() const{return IsCaveOverview()?!CaveDomains.IsEmpty():Native.IsValid();}
    FString Status()const;
    FBox DisplayBounds()const;
    /** Explicit read-only inspection, outside normal publication/navigation. */
    const FBuffers& Published()const;
    TSharedPtr<const FCLCompiledPlanetRecipe> PublishedPlan()const{return VisiblePlan;}
    bool IsDraft()const{return AppliedDraft;}
    uint32 WorldIdentity()const{return Native?Native->GetUniqueID():0;}
    const void* RuntimeIdentity()const;
    bool UsesNativePresentation()const;
#if WITH_DEV_AUTOMATION_TESTS
    TArray<uint32> CaveWorldIdentities()const;
    TMap<FGuid,uint32> CaveDomainIdentities()const;
    struct FMetrics{uint64 MeshCopies=0,VerticesCopied=0,Starts=0;double MaxPollMs=0,TotalCopyMs=0;};
    mutable FMetrics Metrics;
    bool CheckMaterialCoordinates(FString& Error)const;
    void SetMaterialCoordinatesEnabledForTesting(bool Enabled);
#endif
    bool PickGlobe(const FVector&,const FVector&,FVector&)const;
    bool PickSection(const FVector&,const FVector&,FVector&)const;
private:
    TStrongObjectPtr<AVoxelWorld> Native;
    TWeakObjectPtr<UVoxelStampComponent> Stamp;
    TSharedPtr<const FCLCompiledPlanetRecipe> Plan,Pending,VisiblePlan;
    bool CaveInspection=false;
    FGuid CaveDomain;
    // All static diagnostic domains are active together. Native VPP owns each
    // domain's field, mesh, LOD and lifecycle; this is not camera-based streaming.
    TArray<TUniquePtr<FCLNativePreview>> CaveDomains;
    bool IsCaveOverview()const{return CaveInspection&&!CaveDomain.IsValid();}
    bool Section=false,PendingSection=false,PendingDraft=false,AppliedDraft=false,AwaitingPublication=false;
#if WITH_DEV_AUTOMATION_TESTS
    bool MaterialCoordinatesEnabled=true;
#endif
    double Width=500,PendingWidth=500;
    uint32 PublishedHash=0;
    mutable FBuffers Last;
    mutable uint32 InspectionHash=0;
    FString Failure;
    FVoxelVolumeTransform StampToDisplay()const;
    bool ApplyPending();
    bool Pick(const FVector&,const FVector&,FVector&)const;
};

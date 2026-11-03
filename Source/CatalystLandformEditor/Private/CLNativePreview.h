#pragma once
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "VoxelWorld.h"
#include "VoxelStampTransform.h"
class UVoxelStampComponent;
struct FCLCompiledPlanetRecipe;
/** One persistent native VPP producer and presenter per Designer viewport.
 * Pending recipe input coalesces; generation, materials, collision and LOD stay native. */
class FCLNativePreview
{
public:
    // Read-only inspection data extracted from the published native mesh. Never rendered.
    struct FBuffers {TArray<FVector> Vertices,SourcePoints;TArray<int32> Triangles;};
    struct FMaterialCoordinates{FLinearColor High,Low;};
    static FMaterialCoordinates MaterialCoordinates(const FVoxelVolumeTransform&,bool Section,double ReferenceMeters);
    ~FCLNativePreview();
    bool Start(UWorld&,TSharedPtr<const FCLCompiledPlanetRecipe>,bool Section,double Width,FString&,bool Draft=false);
    void Reset();
    bool Poll(bool Grid=false);
    bool IsUpdating() const;
    bool IsActive() const{return Native.IsValid();}
    FString Status()const;
    FBox DisplayBounds()const;
    const FBuffers& Published()const{return Last;}
    TSharedPtr<const FCLCompiledPlanetRecipe> PublishedPlan()const{return VisiblePlan;}
    bool IsDraft()const{return AppliedDraft;}
    uint32 WorldIdentity()const{return Native?Native->GetUniqueID():0;}
    const void* RuntimeIdentity()const;
    bool UsesNativePresentation()const;
#if WITH_DEV_AUTOMATION_TESTS
    bool CheckMaterialCoordinates(FString& Error)const;
    void SetMaterialCoordinatesEnabledForTesting(bool Enabled);
#endif
    bool PickGlobe(const FVector&,const FVector&,FVector&)const;
    bool PickSection(const FVector&,const FVector&,FVector&)const;
private:
    TStrongObjectPtr<AVoxelWorld> Native;
    TWeakObjectPtr<UVoxelStampComponent> Stamp;
    TSharedPtr<const FCLCompiledPlanetRecipe> Plan,Pending,VisiblePlan;
    bool Section=false,PendingSection=false,PendingDraft=false,AppliedDraft=false,AwaitingPublication=false;
#if WITH_DEV_AUTOMATION_TESTS
    bool MaterialCoordinatesEnabled=true;
#endif
    double Width=500,PendingWidth=500;
    uint32 PublishedHash=0;
    FBuffers Last;
    FString Failure;
    FVoxelVolumeTransform StampToDisplay()const;
    bool ApplyPending();
    bool Pick(const FVector&,const FVector&,FVector&)const;
};

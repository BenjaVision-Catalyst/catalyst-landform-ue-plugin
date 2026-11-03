#pragma once
#include "CoreMinimal.h"
#include "Async/Future.h"
#include "UObject/StrongObjectPtr.h"
#include "CLPreviewMesh.h"
#include "VoxelWorld.h"
#include "VoxelChunkKey.h"
class UWorld;
class FCLScene;
class FVoxelMesh;
struct FCLCompiledPlanetRecipe;

/** One native VPP producer per viewport, never a mesher per operation. Its
 * immutable native triangles are presented with the existing editor materials. */
class FCLVolumePreview
{
public:
    struct FMeshSource {TSharedPtr<const FVoxelMesh> Mesh;FTransform ToBodyCm;FVoxelChunkNeighborInfo Neighbors;};
    struct FBuffers
    {
        TArray<FVector> Vertices,Normals,SourcePoints;
        TArray<int32> Triangles;
        TArray<FVector2D> UV,UV1,UV2,UV3;
        TArray<FLinearColor> Colors;
        TArray<FProcMeshTangent> Tangents;
        bool Cancelled=false;
    };
    ~FCLVolumePreview();
    bool Start(UWorld& World,TSharedPtr<const FCLCompiledPlanetRecipe> Plan,bool Section,double Width,FString& Error);
    void Reset();
    bool Poll(UDynamicMeshComponent& Mesh,bool Grid);
    bool IsUpdating() const;
    bool IsActive() const {return Native.IsValid();}
    FString Status() const;
    const FBuffers& Published()const{return Last;}
    bool PickGlobe(const FVector& Start,const FVector& Direction,FVector& BodyMeters)const;
    bool PickSection(const FVector& Start,const FVector& Direction,FVector& BodyMeters)const;
private:
    static FBuffers Build(TArray<FMeshSource> Sources,TSharedPtr<const FCLCompiledPlanetRecipe> Plan,bool Section,double Width,TSharedPtr<TAtomic<bool>,ESPMode::ThreadSafe> Stop);
    TStrongObjectPtr<AVoxelWorld> Native;
    TSharedPtr<const FCLCompiledPlanetRecipe> Plan;
    bool Section=false;
    double Width=500,LastPoll=0;
    uint32 BuiltHash=0,TaskHash=0;
    TFuture<FBuffers> Task;
    TSharedPtr<TAtomic<bool>,ESPMode::ThreadSafe> Stop;
    FBuffers Last;
    FString Failure;
};

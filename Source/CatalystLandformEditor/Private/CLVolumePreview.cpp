#include "CLVolumePreview.h"
#include "CLSectionGeometry.h"
#include "CLSurfacePreview.h"
#include "Model/CLPlanetRecipe.h"
#include "Scene/CLScene.h"
#include "CLPlanetStamp.h"
#include "VoxelWorld.h"
#include "VoxelLayer.h"
#include "VoxelStampComponent.h"
#include "VoxelMesh.h"
#include "Render/VoxelMeshComponent.h"
// Qualified read-only adapter to VPP 2.0p8's retained native mesh. No vendor
// meshing code is copied, changed or reimplemented here.
#include "Render/VoxelMeshRenderProxy.h"
#include "Surface/CLSurfaceLibrary.h"
#include "MegaMaterial/VoxelMegaMaterial.h"
#include "Async/Async.h"
#include "Engine/World.h"

FCLVolumePreview::~FCLVolumePreview(){Reset();}
void FCLVolumePreview::Reset()
{
    if(Stop)Stop->Store(true);if(Task.IsValid()){Task.Wait();Task.Get();Task=TFuture<FBuffers>();}Stop.Reset();
    if(Native){Native->DestroyRuntime();Native->Destroy();Native.Reset();}
    Plan.Reset();BuiltHash=TaskHash=0;Last=FBuffers();Failure.Reset();
}
bool FCLVolumePreview::Start(UWorld& World,TSharedPtr<const FCLCompiledPlanetRecipe> Input,bool Flat,double Area,FString& Error)
{
    Reset();if(!Input||!Input->Volumes||!Input->VolumeRecipe){Error=TEXT("Native volume preview requires a complete immutable volume recipe");return false;}
    Plan=MoveTemp(Input);Section=Flat;Width=FMath::Clamp(Area,100.,1000.);
    FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Native.Reset(World.SpawnActor<AVoxelWorld>(Params));if(!Native){Error=TEXT("Native preview world could not be created");Reset();return false;}
    Native->DestroyRuntime();Native->SetupForPreview();Native->bEnableNanite=false;Native->bEnableTessellation=false;Native->bEnableNavigation=false;Native->bWaitForPCG=false;
    Native->VoxelSize=50;Native->LODQuality.EditorQuality=FFloatInterval(4,4);Native->bCreateRuntimeOnBeginPlay=false;Native->bDoubleSidedCollision=true;
    // Match the fixed display frame while keeping the field in physical metres.
    Native->SetActorScale3D(FVector(1000./Plan->RadiusMeters));
    auto* Material=GetDefault<UCLSurfaceSettings>()->PlanetMaterial.LoadSynchronous();if(Material)Native->MegaMaterial=Material;
    auto* StampComponent=NewObject<UVoxelStampComponent>(Native.Get());StampComponent->SetupAttachment(Native->GetRootComponent());StampComponent->RegisterComponent();
    FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Radius=Plan->RadiusMeters*100;Stamp.Recipe=*Plan->VolumeRecipe;Stamp.Layer=UVoxelVolumeLayer::Default();
    Stamp.Behavior=EVoxelStampBehavior::AffectAll;Stamp.Smoothness=0;Stamp.BoundsExtension=.05;
    Stamp.bSectionPreview=Section;Stamp.SectionWidthMeters=Width;Stamp.SectionReferenceRadiusMeters=FCLSectionGeometry::ReferenceSurfaceRadius(*Plan);
    StampComponent->SetStamp(Stamp);Native->CreateRuntime();
    // Actor visibility also controls VPP stamp admission. Hide only the produced
    // render components, after native publication, keeping the source enabled.
    const TWeakObjectPtr<AVoxelWorld> Weak=Native.Get();const auto Pending=MakeShared<bool>(true);
    const auto Hide=FSimpleDelegate::CreateLambda([Weak,Pending]
    {if(auto* W=Weak.Get()){TInlineComponentArray<UVoxelMeshComponent*> C;W->GetComponents(C);for(auto* M:C)M->SetVisibility(false);}*Pending=false;});
    Native->OnNextStateRendered(Hide);Native->OnInvalidated(FSimpleDelegate::CreateLambda([Weak,Pending,Hide]
    {if(auto* W=Weak.Get())if(!*Pending){*Pending=true;W->OnNextStateRendered(Hide);}}));
    Error.Reset();return true;
}
FString FCLVolumePreview::Status()const
{
    if(!Failure.IsEmpty())return Failure;if(!Native)return TEXT("No native producer");
    TInlineComponentArray<UVoxelMeshComponent*> C;Native->GetComponents(C);
    return FString::Printf(TEXT("runtime %d, ready %d, processing %d, tasks %d, mesh components %d, published vertices %d"),Native->IsRuntimeCreated(),Native->IsVoxelWorldReady(),Native->IsProcessingNewState(),Native->GetNumPendingTasks(),C.Num(),Last.Vertices.Num());
}
bool FCLVolumePreview::IsUpdating()const
{return Native&&Failure.IsEmpty()&&(Task.IsValid()||BuiltHash==0||Native->IsProcessingNewState());}

FCLVolumePreview::FBuffers FCLVolumePreview::Build(TArray<FMeshSource> Sources,TSharedPtr<const FCLCompiledPlanetRecipe> P,bool Flat,double Area,TSharedPtr<TAtomic<bool>,ESPMode::ThreadSafe> Cancel)
{
    FBuffers B;FString Error;const auto Scene=FCLScene::FromPlanet(P,Error);if(!Scene){B.Cancelled=true;return B;}
    const double Reference=FCLSectionGeometry::ReferenceSurfaceRadius(*P);
    auto Display=[&](const FVector& Source)->FVector
    {
        if(!Flat)return Source*(100000./P->RadiusMeters);
        return FVector(Source.X/Area,Source.Y/Area,Source.Z/P->RadiusMeters)*100000.;
    };
    for(const auto& Source:Sources)
    {
        if(Cancel->Load()){B.Cancelled=true;return B;}
        const auto& M=*Source.Mesh;const int32 Offset=B.Vertices.Num();TArray<FVector3d> Points,NativePoints;
        const auto NativeVertices=M.GetDisplacedVertices(Source.Neighbors);
        for(const auto& V:NativeVertices)
        {const FVector Q=Source.ToBodyCm.TransformPosition(FVector(V))*.01;NativePoints.Add(Q);Points.Add(Flat?FCLPlanetStampRuntime::SectionToBodyMeters(Q,Reference):Q);}
        for(int32 Start=0;Start<Points.Num();Start+=512)
        {
            if(Cancel->Load()){B.Cancelled=true;return B;}
            const int32 Count=FMath::Min(512,Points.Num()-Start);TArray<FCLSceneSurfaceSample> Surfaces;
            if(!Scene->SampleSurfaces({MakeArrayView(Points).Slice(Start,Count)},Surfaces,Error)){B.Cancelled=true;return B;}
            for(int32 I=0;I<Count;++I)
            {
                const int32 J=Start+I;const auto& Q=Points[J];const FVector D=Display(NativePoints[J]);B.SourcePoints.Add(Q*100);B.Vertices.Add(D);
                FVector N=Source.ToBodyCm.TransformVectorNoScale(FVector(M.Normals[J].GetUnitVector())).GetSafeNormal();
                if(Flat)
                {
                    N=(N*FVector(Area,Area,P->RadiusMeters)).GetSafeNormal();
                }
                if(N.IsNearlyZero())N=FVector::ZAxisVector;B.Normals.Add(N);FVector X,Y;N.FindBestAxisVectors(X,Y);B.Tangents.Emplace(X,false);
                FCLSurfaceWeights W;for(const auto& S:Surfaces[I].Weights){const int32 K=P->Surfaces->Find(S.Id,S.Revision);if(K!=INDEX_NONE)W.Values[K]=S.Weight;}
                B.Colors.Add(P->Surfaces?CLPackSurfaceWeights(W):FLinearColor(.38,.32,.23));B.UV.Add(FVector2D(Q.X,Q.Y)*100);B.UV1.Add(FVector2D(Q.Z*100,1));
                B.UV2.Add(FVector2D(W.Values[4],W.Values[5]));B.UV3.Add(FVector2D(W.Values[6],W.Values[7]));
            }
        }
        for(int32 I:M.Indices)B.Triangles.Add(Offset+I);
    }
    return B;
}
bool FCLVolumePreview::Poll(UDynamicMeshComponent& Target,bool Grid)
{
    if(!Native||!Failure.IsEmpty())return false;
    if(Task.IsValid()&&Task.IsReady())
    {
        auto B=Task.Get();Task=TFuture<FBuffers>();Stop.Reset();
        if(!B.Cancelled&&!B.Vertices.IsEmpty())
        {
            Last=MoveTemp(B);BuiltHash=TaskHash;
            if(Plan->Surfaces)Target.SetMaterial(0,CLMakeSurfacePreviewMaterial(&Target,*Plan->Surfaces,Grid));
            CLSetPreviewMesh(Target,Last.Vertices,Last.Triangles,Last.Normals,Last.UV,Last.UV1,Last.UV2,Last.UV3,Last.Colors,Last.Tangents,true);
            return true;
        }
    }
    const double Now=FPlatformTime::Seconds();if(Task.IsValid()||Now-LastPoll<.25||!Native->IsVoxelWorldReady()||Native->IsProcessingNewState())return false;LastPoll=Now;
    TInlineComponentArray<UVoxelMeshComponent*> Components;Native->GetComponents(Components);
    TArray<FMeshSource> Sources;uint32 Hash=0;int32 Vertices=0;
    for(auto* C:Components)
    {
        C->SetVisibility(false);const auto& Proxy=C->GetRenderProxy();if(!Proxy)continue;
        const auto M=Proxy->Mesh;Vertices+=M->Vertices.Num();Hash=HashCombine(Hash,PointerHash(Proxy.Get()));
        Sources.Add({M,C->GetComponentTransform().GetRelativeTransform(Native->GetActorTransform()),{}});
    }
    if(Vertices>1000000){Failure=TEXT("Native volume preview exceeds its one-million-vertex publication budget");return false;}
    if(Sources.IsEmpty()||Hash==BuiltHash)return false;
    // Use VPP's own transition displacement. Neighbor resolution follows its
    // chunk-key ancestry; missing/finer neighbors require no coarse displacement.
    TSet<FVoxelChunkKey> Keys;
    auto Key=[](const FVoxelMesh& M){return FVoxelChunkKey(M.ChunkLOD,FIntVector(M.ChunkOffset.X/M.ChunkSize,M.ChunkOffset.Y/M.ChunkSize,M.ChunkOffset.Z/M.ChunkSize));};
    for(const auto& S:Sources)Keys.Add(Key(*S.Mesh));
    for(auto& S:Sources)
    {
        const auto K=Key(*S.Mesh);
        for(int32 X=-1;X<=1;++X)for(int32 Y=-1;Y<=1;++Y)for(int32 Z=-1;Z<=1;++Z)
        {
            auto N=K;N.ChunkKey+=FIntVector(X,Y,Z)*(1<<K.LOD);auto Parent=K;
            while(N.LOD<29)
            {
                if(Keys.Contains(N)){S.Neighbors.SetLOD(X,Y,Z,N.LOD);break;}
                if(N==Parent)break;N=N.GetParent();Parent=Parent.GetParent();
            }
        }
    }
    TaskHash=Hash;Stop=MakeShared<TAtomic<bool>,ESPMode::ThreadSafe>(false);
    Task=Async(EAsyncExecution::ThreadPool,[Sources=MoveTemp(Sources),P=Plan,Flat=Section,Area=Width,Cancel=Stop]()mutable{return Build(MoveTemp(Sources),P,Flat,Area,Cancel);});
    return false;
}
bool FCLVolumePreview::PickGlobe(const FVector& Start,const FVector& Direction,FVector& BodyMeters)const
{
    if(!Native||Section||BuiltHash==0)return false;FHitResult Hit;
    if(!Native->GetWorld()->LineTraceSingleByChannel(Hit,Start,Start+Direction*10000000,ECC_Visibility)||Hit.GetActor()!=Native.Get())return false;
    BodyMeters=Native->GetActorTransform().InverseTransformPosition(Hit.ImpactPoint)*.01;return true;
}
bool FCLVolumePreview::PickSection(const FVector& Start,const FVector& Direction,FVector& BodyMeters)const
{
    if(!Section||Last.Triangles.IsEmpty())return false;double Nearest=MAX_dbl;FVector Best;
    for(int32 I=0;I<Last.Triangles.Num();I+=3)
    {
        const int32 A=Last.Triangles[I],B=Last.Triangles[I+1],C=Last.Triangles[I+2];
        const FVector E1=Last.Vertices[B]-Last.Vertices[A],E2=Last.Vertices[C]-Last.Vertices[A],H=FVector::CrossProduct(Direction,E2);
        const double Det=FVector::DotProduct(E1,H);if(FMath::Abs(Det)<1.e-9)continue;
        const FVector S=Start-Last.Vertices[A];const double U=FVector::DotProduct(S,H)/Det;if(U<0||U>1)continue;
        const FVector Q=FVector::CrossProduct(S,E1);const double V=FVector::DotProduct(Direction,Q)/Det;if(V<0||U+V>1)continue;
        const double T=FVector::DotProduct(E2,Q)/Det;if(T<0||T>=Nearest)continue;
        const FVector P=(Last.SourcePoints[A]*(1-U-V)+Last.SourcePoints[B]*U+Last.SourcePoints[C]*V)*.01;
        FString Error;const auto Scene=FCLScene::FromPlanet(Plan,Error);TArray<FCLSceneShapeSample> Shape;
        // The presentation cut wall is not a physical walking surface.
        if(!Scene||!Scene->SampleShape({MakeArrayView(&P,1)},Shape,Error)||FMath::Abs(Shape[0].FieldMeters)>2)continue;
        Nearest=T;Best=P;
    }
    if(Nearest==MAX_dbl)return false;BodyMeters=Best;return true;
}

#include "CLRockQueryLibrary.h"
#include "CLPlanet.h"
#include "VoxelLinearColorMetadata.h"
#include "VoxelQueryBlueprintLibrary.h"
#include "VoxelStackLayer.h"
#include "Scene/CLScene.h"
#include "Surface/CLSurfaceLibrary.h"
UVoxelLinearColorMetadata* UCLRockQueryLibrary::GetPropertiesMetadata()
{return LoadObject<UVoxelLinearColorMetadata>(nullptr,FCLCompiledRock::PropertiesAssetPath());}
FLinearColor UCLRockQueryLibrary::MakeFillProperties(double R,double E){return FCLCompiledRock::FillProperties(R,E);}
TVoxelFuture<FCLRockSample> UCLRockQueryLibrary::QueryRock(ACLPlanet* Planet,const FVector& WorldPosition)
{
    check(IsInGameThread());FCLRockSample Empty;
    if(!IsValid(Planet)||WorldPosition.ContainsNaN()||!Planet->IsRuntimeCreated()||Planet->IsProcessingNewState()||!Planet->IsVoxelWorldReady())return Empty;
    const auto Snapshot=Planet->GetSamplingRuntime();if(!Snapshot||!Snapshot->RecipePlan||(!Snapshot->RecipePlan->Rock&&!Snapshot->RecipePlan->MaterialLayers))return Empty;
    auto* Metadata=GetPropertiesMetadata();if(!Metadata)return Empty;
    const auto* Runtime=Planet->GetRuntime().Get();const FTransform Pose=Planet->GetActorTransform();const TWeakObjectPtr<ACLPlanet> Weak(Planet);
    const FVector Body=Snapshot->SamplingFrame.NativeToBodyMeters(Planet->GetActorTransform().InverseTransformPosition(WorldPosition));
    FVoxelStackLayer Layer;if(Planet->LayerStack)Layer.Stack=Planet->LayerStack;if(Planet->TerrainLayer)Layer.Layer=Planet->TerrainLayer;
    return UVoxelQueryBlueprintLibrary::MultiQueryVoxelLayer(Planet->GetWorld(),Layer,{WorldPosition},{Metadata},5.f).Then_GameThread([Weak,Snapshot,Runtime,Pose,Body,Metadata](const TSharedRef<TOptional<TArray<FVoxelQueryResult>>>& Data)
    {
        FCLRockSample S;auto* P=Weak.Get();
        if(!P||!P->GetActorTransform().Equals(Pose)||P->GetSamplingRuntime()!=Snapshot||P->GetRuntime().Get()!=Runtime||P->IsProcessingNewState()||!P->IsVoxelWorldReady()||!Data->IsSet()||Data->GetValue().Num()!=1)return S;
        const auto& Q=Data->GetValue()[0];if(!FMath::IsFinite(Q.Value)||Q.Value>2)return S;
        const auto* V=Q.MetadataToValue.Find(Metadata);if(!V)return S;
        const FLinearColor Props=V->Get<FLinearColor>();if(!FMath::IsFinite(Props.R)||!FMath::IsFinite(Props.G)||!FMath::IsFinite(Props.B)||!FMath::IsFinite(Props.A)||Props.B<.99f)return S;
        FString Error;const auto Scene=FCLScene::FromPlanet(Snapshot->RecipePlan,Error);TArray<FCLMaterialSample> Materials;
        if(!Scene||!Scene->SampleMaterials({MakeArrayView(&Body,1)},Materials,Error,false)||Materials.Num()!=1)return S;
        S=Materials[0].Material;if(!S.bValid&&Props.A<.5f)return S;
        S.MiningResistance=FMath::Clamp(double(Props.R),0.,1.);S.Erodibility=FMath::Clamp(double(Props.G),0.,1.);S.PlacedFraction=FMath::Clamp(double(Props.A),0.,1.);
        if(S.PlacedFraction>=.5)
        {S.bValid=true;S.Origin=ECLMaterialOrigin::PlacedFill;S.SourceId.Invalidate();S.DepositId.Invalidate();S.DepositDepthMeters=0;S.DepositThicknessMeters=0;S.UnitId=FGuid(0x434c4649,1,0,1);S.StratumId=S.UnitId;S.StratumIndex=0;S.Name=TEXT("Placed fill");}
        S.VisibleSurfaceId=NAME_None;S.VisibleSurfaceRevision=0;const auto& Library=*Snapshot->RecipePlan->Surfaces;
        for(int32 I=0;I<Library.Ids.Num();++I)if(Library.NativeTypes[I]==FVoxelSurfaceType(Q.SurfaceType)){S.VisibleSurfaceId=Library.Ids[I];S.VisibleSurfaceRevision=Library.Revisions[I];break;}
        if(S.Origin==ECLMaterialOrigin::PlacedFill){S.SurfaceId=S.VisibleSurfaceId;S.SurfaceRevision=S.VisibleSurfaceRevision;}
        return S;
    });
}
FVoxelFuture UCLRockQueryLibrary::QueryExposedRock(FCLRockSample& Result,ACLPlanet* Planet,FVector Position)
{
    Result=FCLRockSample();return QueryRock(Planet,Position).Then_GameThread([&Result](const TSharedRef<FCLRockSample>& Value){Result=*Value;});
}

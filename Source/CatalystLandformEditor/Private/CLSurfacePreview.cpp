#include "CLSurfacePreview.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
UMaterialInterface* CLMakeSurfacePreviewMaterial(UObject* Outer,const FCLCompiledSurfaceLibrary& Library,bool bGlobeGrid)
{
    auto* Material=UMaterialInstanceDynamic::Create(Library.Owner->Library.PreviewMaterial,Outer);
    if(!Material)return nullptr;
    Material->SetVectorParameterValue(TEXT("CLGrid"),FLinearColor(bGlobeGrid?1:0,10,0,0));
    for(int32 I=0;I<8;++I)
    {
        const FCLSurfaceAppearance A=Library.Appearances.IsValidIndex(I)?Library.Appearances[I]:FCLSurfaceAppearance();
        Material->SetVectorParameterValue(FName(*FString::Printf(TEXT("CLTint%d"),I)),FLinearColor(A.Color.R,A.Color.G,A.Color.B,A.Roughness));
        Material->SetVectorParameterValue(FName(*FString::Printf(TEXT("CLDetail%d"),I)),FLinearColor(A.GrainMeters*100,A.GrainStrength,A.NormalStrength,0));
        const auto T=Library.Textures.IsValidIndex(I)?Library.Textures[I]:FCLSurfaceTextures();
        Material->SetVectorParameterValue(FName(*FString::Printf(TEXT("CLTextureDetail%d"),I)),FLinearColor(T.TileMeters*100,T.IsActive()?1:0,0,0));
        if(T.IsActive())for(const auto& Pair:TArray<TPair<FString,UTexture2D*>>{{TEXT("CLBaseColor"),T.BaseColor},{TEXT("CLNormal"),T.Normal},{TEXT("CLRoughness"),T.Roughness}})
            Material->SetTextureParameterValue(FName(*(Pair.Key+LexToString(I))),Pair.Value);
    }return Material;
}
FLinearColor CLPackSurfaceWeights(const FCLSurfaceWeights& W){return FLinearColor(W.Values[0],W.Values[1],W.Values[2],W.Values[3]);}

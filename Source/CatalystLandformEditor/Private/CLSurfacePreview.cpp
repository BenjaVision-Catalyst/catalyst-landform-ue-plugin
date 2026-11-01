#include "CLSurfacePreview.h"
#include "Materials/MaterialInstanceDynamic.h"
UMaterialInterface* CLMakeSurfacePreviewMaterial(UObject* Outer,const FCLCompiledSurfaceLibrary& Library)
{
    auto* Material=UMaterialInstanceDynamic::Create(Library.Owner->Library.PreviewMaterial,Outer);
    if(!Material)return nullptr;
    for(int32 I=0;I<8;++I)
    {
        const FCLSurfaceAppearance A=Library.Appearances.IsValidIndex(I)?Library.Appearances[I]:FCLSurfaceAppearance();
        Material->SetVectorParameterValue(FName(*FString::Printf(TEXT("CLTint%d"),I)),FLinearColor(A.Color.R,A.Color.G,A.Color.B,A.Roughness));
        Material->SetVectorParameterValue(FName(*FString::Printf(TEXT("CLDetail%d"),I)),FLinearColor(A.GrainMeters*100,A.GrainStrength,A.NormalStrength,0));
    }return Material;
}
FLinearColor CLPackSurfaceWeights(const FCLSurfaceWeights& W){return FLinearColor(W.Values[0],W.Values[1],W.Values[2],W.Values[3]);}

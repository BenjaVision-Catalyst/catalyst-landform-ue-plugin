#include "Surface/CLSurfaceCompiler.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLRegionGeneration.h"
#include "Model/CLSphericalPatches.h"
#include "Surface/VoxelSurfaceTypeAsset.h"
#include "Materials/MaterialInstanceConstant.h"

namespace
{
double SmoothSurface(double T){T=FMath::Clamp(T,0.,1.);return T*T*T*(T*(T*6-15)+10);}
double SurfaceBand(double V,double Low,double High,double Feather)
{return SmoothSurface(1+(V-Low)/Feather)*SmoothSurface(1+(High-V)/Feather);}
}
bool FCLSurfaceStack::Validate(FString& Error) const
{
    if(!IsActive())return true;
    if(AlgorithmVersion!=1||DefaultSurfaceId.IsNone()||DefaultSurfaceRevision<1||Layers.Num()>16){Error=TEXT("Surface stack requires algorithm 1, a default surface/revision and at most 16 overlays");return false;}
    for(const auto& L:Layers)
    {
        if(L.Name.IsNone()||L.SurfaceId.IsNone()||L.SurfaceRevision<1||L.SlopeDegrees.ContainsNaN()||L.SlopeDegrees.X<0||L.SlopeDegrees.Y>90||L.SlopeDegrees.X>L.SlopeDegrees.Y||!FMath::IsFinite(L.SlopeFeatherDegrees)||L.SlopeFeatherDegrees<.1||L.SlopeFeatherDegrees>45||!FMath::IsFinite(L.HeightFeatherRadiusFraction)||L.HeightFeatherRadiusFraction<.00001||L.HeightFeatherRadiusFraction>1||!FMath::IsFinite(L.Strength)||L.Strength<0||L.Strength>1||!FMath::IsFinite(L.NoiseAmount)||L.NoiseAmount<0||L.NoiseAmount>1||!FMath::IsFinite(L.NoiseWavelengthMeters)||L.NoiseWavelengthMeters<.1||L.NoiseWavelengthMeters>1.e7||
           (L.bUseHeightRange&&(!L.LowerHeight.Validate(Error)||!L.UpperHeight.Validate(Error)||L.LowerHeight.Mode==ECLHeightReferenceMode::IntervalDelta||L.UpperHeight.Mode==ECLHeightReferenceMode::IntervalDelta)))
        {Error=TEXT("Invalid surface layer: ")+L.Name.ToString();return false;}
    }return true;
}
void FCLSurfaceStack::RenameGuide(FName Old,FName New)
{for(auto& L:Layers)for(auto* H:{&L.LowerHeight,&L.UpperHeight}){if(H->Guide==Old)H->Guide=New;if(H->UpperGuide==Old)H->UpperGuide=New;}}
UCLSurfaceSettings::UCLSurfaceSettings(){SurfaceLibrary=TSoftObjectPtr<UCLSurfaceLibraryAsset>(FSoftObjectPath(TEXT("/CatalystLandform/Surfaces/DA_SurfaceLibrary.DA_SurfaceLibrary")));}
UCLSurfaceLibraryAsset* UCLSurfaceLibraryAsset::LoadConfigured(){return GetDefault<UCLSurfaceSettings>()->SurfaceLibrary.LoadSynchronous();}
bool UCLSurfaceLibraryAsset::ValidateLibrary(FString& Error) const
{
    const auto& Entries=Library.Entries;TSet<FName> Ids;TSet<const UVoxelSurfaceTypeAsset*> Types;if(!Library.PreviewMaterial||Library.SchemaVersion!=1||Entries.IsEmpty()||Entries.Num()>8){Error=TEXT("Client surface library requires 1..8 surfaces");return false;}
    for(const auto& E:Entries)
    {
        const auto& A=E.Appearance;
        if(E.Id.IsNone()||Ids.Contains(E.Id)||E.Revision<1||!E.SurfaceType||Types.Contains(E.SurfaceType)||!E.SurfaceType->Material||!FMath::IsFinite(A.Color.R)||!FMath::IsFinite(A.Color.G)||!FMath::IsFinite(A.Color.B)||A.Color.GetMin()<0||A.Color.GetMax()>1||!FMath::IsFinite(A.Roughness)||A.Roughness<0||A.Roughness>1||!FMath::IsFinite(A.GrainMeters)||A.GrainMeters<.01||!FMath::IsFinite(A.GrainStrength)||A.GrainStrength<0||A.GrainStrength>1||!FMath::IsFinite(A.NormalStrength)||A.NormalStrength<0||A.NormalStrength>1)
        {Error=TEXT("Invalid client surface appearance or missing native surface/material: ")+E.Id.ToString();return false;}
        FLinearColor Tint,Detail;const auto* M=E.SurfaceType->Material.Get();
        if(!M->GetVectorParameterValue(FMaterialParameterInfo(TEXT("CLTint")),Tint)||!M->GetVectorParameterValue(FMaterialParameterInfo(TEXT("CLDetail")),Detail)||
            !Tint.Equals(FLinearColor(A.Color.R,A.Color.G,A.Color.B,A.Roughness),.00001)||!Detail.Equals(FLinearColor(A.GrainMeters*100,A.GrainStrength,A.NormalStrength,0),.00001))
        {Error=TEXT("Native appearance parameters differ from the surface library; Bake Appearance: ")+E.Id.ToString();return false;}
        Ids.Add(E.Id);Types.Add(E.SurfaceType);
    }return true;
}
#if WITH_EDITOR
bool UCLSurfaceLibraryAsset::BakeAppearance(FString& Error)
{
    for(const auto& Entry:Library.Entries)
    {
        auto* M=Entry.SurfaceType?Cast<UMaterialInstanceConstant>(Entry.SurfaceType->Material):nullptr;
        if(!M){Error=TEXT("Appearance profile 1 requires a material instance: ")+Entry.Id.ToString();return false;}
    }
    for(const auto& Entry:Library.Entries)
    {
        auto* M=CastChecked<UMaterialInstanceConstant>(Entry.SurfaceType->Material);const auto& A=Entry.Appearance;M->Modify();
        M->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("CLTint")),FLinearColor(A.Color.R,A.Color.G,A.Color.B,A.Roughness));
        M->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("CLDetail")),FLinearColor(A.GrainMeters*100,A.GrainStrength,A.NormalStrength,0));
        M->PostEditChange();M->MarkPackageDirty();
    }return true;
}
#endif
TSharedPtr<const FCLCompiledSurfaceLibrary> FCLCompiledSurfaceLibrary::Resolve(FString& Error)
{
    if(!IsInGameThread()){Error=TEXT("Surface assets must be resolved on the game thread");return nullptr;}
    auto* Source=UCLSurfaceLibraryAsset::LoadConfigured();if(!Source||!Source->ValidateLibrary(Error)){if(Error.IsEmpty())Error=TEXT("Configured surface library is missing");return nullptr;}
    auto Out=MakeShared<FCLCompiledSurfaceLibrary>();Out->Owner.Reset(Source);auto Entries=Source->Library.Entries;Entries.Sort([](const auto& A,const auto& B){return A.Id.LexicalLess(B.Id);});
    for(const auto& E:Entries){Out->Ids.Add(E.Id);Out->Revisions.Add(E.Revision);Out->Appearances.Add(E.Appearance);Out->NativeTypes.Add(FVoxelSurfaceType(E.SurfaceType));}return Out;
}
int32 FCLCompiledSurfaceLibrary::Find(FName Id,int32 Revision) const
{const int32 I=Ids.IndexOfByKey(Id);return I!=INDEX_NONE&&Revisions[I]==Revision?I:INDEX_NONE;}
void FCLSurfaceWeights::Lerp(const FCLSurfaceWeights& Other,double Alpha)
{for(int32 I=0;I<8;++I)Values[I]=FMath::Lerp(Values[I],Other.Values[I],Alpha);}
bool FCLCompiledSurfaceStack::Compile(const FCLSurfaceStack& S,const FCLCompiledSurfaceLibrary& Library,TConstArrayView<FCLHeightGuide> Guides,double Radius,int32 Seed,FCLCompiledSurfaceStack& Out,FString& Error)
{
    if(!S.Validate(Error))return false;if(!S.IsActive())return true;
    auto Find=[&](FName Id,int32 Revision,int32& Index){Index=Library.Find(Id,Revision);if(Index==INDEX_NONE){Error=TEXT("Missing client surface revision: ")+Id.ToString();return false;}return true;};
    if(!Find(S.DefaultSurfaceId,S.DefaultSurfaceRevision,Out.DefaultIndex))return false;
    auto Height=[&](const FCLHeightReference& H,double& V)
    {
        V=H.OffsetMeters;const auto* Low=Guides.FindByPredicate([&](const auto& G){return G.Name==H.Guide;});
        if(!H.Guide.IsNone()&&!Low){Error=TEXT("Missing surface height guide: ")+H.Guide.ToString();return false;}
        if(H.Mode==ECLHeightReferenceMode::Height){if(Low)V+=Low->HeightMeters;return true;}
        const auto* High=Guides.FindByPredicate([&](const auto& G){return G.Name==H.UpperGuide;});if(!Low||!High||Low->HeightMeters>=High->HeightMeters){Error=TEXT("Surface height interval must be ordered and present");return false;}V+=FMath::Lerp(Low->HeightMeters,High->HeightMeters,H.Fraction);return FMath::IsFinite(V);
    };
    for(const auto& L:S.Layers)
    {
        FLayer R;if(!Find(L.SurfaceId,L.SurfaceRevision,R.Index))return false;
        if(L.bUseHeightRange&&(!Height(L.LowerHeight,R.Low)||!Height(L.UpperHeight,R.High)||R.Low>R.High)){if(Error.IsEmpty())Error=TEXT("Surface height range is reversed");return false;}
        if(!L.bEnabled)continue;R.Slope=L.SlopeDegrees;R.Feather=L.SlopeFeatherDegrees;R.HeightFeather=L.HeightFeatherRadiusFraction*Radius;R.Strength=L.Strength;R.Noise=L.NoiseAmount;R.Wavelength=L.NoiseWavelengthMeters;
        FRandomStream Random(HashCombineFast(uint32(Seed),uint32(L.NoiseSeed)));R.Offset=FVector(Random.FRandRange(-5000,5000),Random.FRandRange(-5000,5000),Random.FRandRange(-5000,5000));Out.Layers.Add(R);
    }return true;
}
FCLSurfaceWeights FCLCompiledSurfaceStack::Evaluate(const FVector& Position,double Height,double Slope) const
{
    FCLSurfaceWeights W;if(DefaultIndex==INDEX_NONE)return W;W.Values[DefaultIndex]=1;
    for(const auto& L:Layers)
    {
        const double Noise=.5+.5*FMath::PerlinNoise3D((Position+L.Offset)/L.Wavelength);
        const double Alpha=L.Strength*SurfaceBand(Slope,L.Slope.X,L.Slope.Y,L.Feather)*SurfaceBand(Height,L.Low,L.High,L.HeightFeather)*FMath::Lerp(1.,Noise,L.Noise);
        for(double& V:W.Values)V*=1-Alpha;W.Values[L.Index]+=Alpha;
    }return W;
}

FCLSurfaceWeights FCLCompiledPlanetRecipe::SurfaceWeightsAt(const FVector& Direction,double Height,double Slope) const
{
    if(!SurfacePatches.IsEmpty())return SurfacePatches[FCLSphericalPatches::Owner(Direction)]->SurfaceWeightsAt(Direction,Height,Slope);
    const FVector D=Direction.GetSafeNormal(),Position=D*(RadiusMeters+Height);auto W=SurfaceStack.Evaluate(Position,Height,Slope);
    if(GeneratedRegions)
    {
        TArray<double> Weights;GeneratedRegions->PaletteWeights(D,Weights);FCLSurfaceWeights Next;
        for(int32 I=0;I<Weights.Num();++I)if(Weights[I]>0){const auto& Field=GeneratedRegions->Fields[I];const auto V=Field->SurfaceStack.DefaultIndex==INDEX_NONE?W:Field->SurfaceStack.Evaluate(Position,Height,Slope);for(int32 K=0;K<8;++K)Next.Values[K]+=Weights[I]*V.Values[K];}W=Next;
    }
    for(const auto& R:Regions)
    {
        const double Alpha=R.Weight(D,RadiusMeters+BaseHeightMeters);if(Alpha<=0||R.Field->SurfaceStack.DefaultIndex==INDEX_NONE)continue;
        const auto V=R.Field->SurfaceStack.Evaluate(R.SourceDirection(D)*(RadiusMeters+Height),Height,Slope);W.Lerp(V,Alpha);
    }return W;
}
void CLSurfaceProbe(const FVector& Direction,TArray<FVector>& D)
{
    const FVector P=Direction.GetSafeNormal(),X=FVector::CrossProduct(P,FMath::Abs(P.Z)<.8?FVector::ZAxisVector:FVector::XAxisVector).GetSafeNormal(),Y=FVector::CrossProduct(P,X);
    D.Append({P,(P+X*.0001).GetSafeNormal(),(P-X*.0001).GetSafeNormal(),(P+Y*.0001).GetSafeNormal(),(P-Y*.0001).GetSafeNormal()});
}
FVector CLSurfaceNormal(TConstArrayView<FVector> D,TConstArrayView<double> H,double Radius,int32 K)
{
    auto Point=[&](int32 J){return D[K+J]*(Radius+H[K+J]);};auto N=FVector::CrossProduct(Point(1)-Point(2),Point(3)-Point(4)).GetSafeNormal();
    if(FVector::DotProduct(N,D[K])<0)N*=-1;return N;
}
void FCLCompiledPlanetRecipe::SampleSurfaceWeights(TConstArrayView<FVector> Directions,TArray<FCLSurfaceWeights>& Out) const
{
    Out.SetNumZeroed(Directions.Num());if(!Surfaces)return;
    for(int32 Start=0;Start<Directions.Num();Start+=512)
    {
        const int32 N=FMath::Min(512,Directions.Num()-Start);TArray<FVector> D;
        for(int32 I=0;I<N;++I)CLSurfaceProbe(Directions[Start+I],D);
        TArray<double> H;SampleHeightsMeters(D,H);
        for(int32 I=0;I<N;++I){const int32 K=5*I;const auto Normal=CLSurfaceNormal(D,H,RadiusMeters,K);const double Slope=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Normal,D[K]),0.,1.)));Out[Start+I]=SurfaceWeightsAt(D[K],H[K],Slope);}
    }
}

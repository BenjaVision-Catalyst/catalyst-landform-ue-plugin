#include "Model/CLCaves.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLCompileProgress.h"
#include "Model/CLRegionGeneration.h"
#include "Model/CLProvinceDefinition.h"
#include "Model/CLTectonics.h"
#include "Model/CLWatershed.h"
#include "CLCaveKarst.h"
#include "Model/CLSpatialCave.h"
#include "CLCaveMorphology.h"
#include "VoxelAABBTree.h"
#include "Util/SizedDisjointSet.h"

void FCLCaveSettings::AddDefaultGuides(FCLRadialGuideSet& Set,double Radius,double BaseSurface)const
{
    if(!FMath::IsFinite(Radius)||Radius<=0)return;
    auto Height=[&](FName Name,double Default){const auto* G=Set.Guides.FindByPredicate([&](const auto& V){return V.Name==Name;});return G?G->NormalizedHeight:Default;};
    const double Low=Height(TEXT("SurfaceMin"),BaseSurface),High=Height(TEXT("SurfaceMax"),BaseSurface);
    auto Add=[&](FName Name,const TCHAR* Canonical,double Value,FLinearColor Color)
    {
        if(Name!=Canonical||Set.Guides.ContainsByPredicate([&](const auto& G){return G.Name==Name;}))return;
        FCLRadialGuide G;G.Name=Name;G.NormalizedHeight=FMath::Clamp(Value,0.,1.);G.Color=Color;Set.Guides.Add(G);
    };
    Add(MinimumGuide,TEXT("CavesMin"),FMath::Min(Low,High)*.5,FLinearColor(.65,.35,.85));
    Add(MaximumGuide,TEXT("CavesMax"),High-FMath::Min(5./Radius,High*.02),FLinearColor(.2,.85,.85));
}
bool FCLCaveSettings::ResolvePlacement(const FCLCompiledPlanetRecipe& Base,double& Low,double& High,double& Cover,FString& Error)const
{
    FCLRadialGuideSet Set;
    for(const auto& G:Base.Guides){FCLRadialGuide V;V.Name=G.Name;V.NormalizedHeight=(Base.RadiusMeters+G.HeightMeters)/Base.RadiusMeters;Set.Guides.Add(V);}
    AddDefaultGuides(Set,Base.RadiusMeters,1+Base.MinimumHeightMeters/Base.RadiusMeters);
    auto Radius=[&](FName Name,double& Value){const auto* G=Set.Guides.FindByPredicate([&](const auto& V){return V.Name==Name;});if(!G)return false;Value=G->NormalizedHeight*Base.RadiusMeters;return true;};
    double SurfaceMax=Base.RadiusMeters+Base.MaximumHeightMeters;Radius(TEXT("SurfaceMax"),SurfaceMax);
    if(!Radius(MinimumGuide,Low)||!Radius(MaximumGuide,High)||!FMath::IsFinite(Low)||!FMath::IsFinite(High)||Low<0||High<=Low)
    {Error=FString::Printf(TEXT("Caves require ordered radial guides %s < %s"),*MinimumGuide.ToString(),*MaximumGuide.ToString());return false;}
    Cover=FMath::Max(0.,SurfaceMax-High);return true;
}

void FCLCaveSettings::UpgradeAbundanceProfile()
{
    if(AlgorithmVersion!=1)return;
    if(FMath::IsNearlyEqual(NetworksPerSquareKilometer,.8*Character,1.e-8))NetworksPerSquareKilometer=6*Character*Character;
    AlgorithmVersion=2;
}

void FCLCaveSettings::ApplyProgression(double Value)
{
    Character=FMath::Clamp(Value,0.,1.);
    const bool Spatial=AlgorithmVersion>=8;AlgorithmVersion=Spatial?9:7;NetworksPerSquareKilometer=6*Character*Character;
    LengthMeters=FMath::Lerp(35.,140.,Character);
    RadiusMeters=FMath::Lerp(1.1,3.2,Character);
    DepthMeters=FMath::Lerp(10.,42.,Character);
    Branching=FMath::Lerp(.15,1.,Character);
    if(Spatial){LengthMeters=FMath::Lerp(150.,500.,Character);RadiusMeters=FMath::Lerp(2.5,5.5,Character);}
}
bool FCLCaveSettings::Validate(FString& Error)const
{
    auto In=[](double V,double A,double B){return FMath::IsFinite(V)&&V>=A&&V<=B;};
    if((AlgorithmVersion<1||AlgorithmVersion>9)||MinimumGuide.IsNone()||MaximumGuide.IsNone()||MinimumGuide==MaximumGuide||!In(Character,0,1)||!In(NetworksPerSquareKilometer,0,10)||!In(LengthMeters,1,500)||!In(RadiusMeters,.1,10)||!In(MinimumCoverMeters,1,100)||!In(DepthMeters,1,500)||!In(Branching,0,1))
    {Error=TEXT("Caves require algorithm 1..9 and finite bounded physical density, dimensions, cover and branching");return false;}return true;
}
TSharedPtr<const FCLCompiledCaves> FCLCompiledCaves::Compile(const FCLCaveSettings& Settings,const FCLCompiledPlanetRecipe& Base,int32 Seed,FString& Error)
{
    Error.Reset();
    if(!Settings.Validate(Error)||!FCLCompileProgress::Check(Error))return nullptr;
    if(!Settings.bEnabled)return MakeShared<FCLCompiledCaves>();
    // Profile 8 remains the exact accepted review fixture. Every other saved
    // selector now places that same anatomy; none can select the retired ring.
    if(Settings.AlgorithmVersion==8)
    {
        const auto Reference=FCLSpatialCave::BuildPrototype(Base,Seed^Settings.Seed,Error);
        if(Reference)return Reference;
        if(!FCLCompileProgress::Check(Error))return nullptr;
        Error.Reset();
    }
    return FCLSpatialCave::BuildFamily(Base,Settings,Seed^Settings.Seed,Error);
}

void FCLCompiledCaves::ResolveInspectionDomains()
{
    InspectionDomains.Reset();if(!Cutters||NetworkInfo.IsEmpty())return;
    FVoxelAABBTree::FElementArray Elements;TArray<FBox> Supports;
    for(int32 I=0;I<NetworkInfo.Num();++I)
    {
        FBox B=Cutters->Bounds(NetworkInfo[I].Id);if(NetworkInfo[I].InspectionBounds.IsValid)B+=NetworkInfo[I].InspectionBounds;
        Supports.Add(B);
        TArray<FBox> Joints;if(Spatial)Spatial->GetInspectionSupportBounds(Joints,NetworkInfo[I].Id);
        if(Joints.IsEmpty())Joints.Add(B);
        for(const auto& Joint:Joints)Elements.Add(FVoxelBox(Joint),I);
    }
    const auto Index=FVoxelAABBTree::Create(MoveTemp(Elements));UE::Geometry::FSizedDisjointSet Sets(NetworkInfo.Num());
    for(int32 I=0;I<Supports.Num();++I)
    {
        TArray<FBox> Joints;if(Spatial)Spatial->GetInspectionSupportBounds(Joints,NetworkInfo[I].Id);if(Joints.IsEmpty())Joints.Add(Supports[I]);
        for(const auto& Joint:Joints)Index->Intersects(FVoxelFastBox(FVoxelBox(Joint)),[&](int32 J){Sets.Union(I,J);return false;});
    }
    TMap<int32,int32> GroupIndices;
    for(int32 I=0;I<NetworkInfo.Num();++I)
    {
        const int32 Root=Sets.Find(I);int32* Found=GroupIndices.Find(Root);int32 Group;
        if(Found)Group=*Found;else{Group=InspectionDomains.AddDefaulted();GroupIndices.Add(Root,Group);}
        InspectionDomains[Group].Networks.Add(NetworkInfo[I].Id);
    }
    for(auto& D:InspectionDomains)
    {
        D.Networks.Sort([](const auto& A,const auto& B){return A.ToString(EGuidFormats::Digits)<B.ToString(EGuidFormats::Digits);});D.Id=D.Networks[0];D.Bounds=Cutters->Select(D.Networks)->Bounds();
    }
    InspectionDomains.Sort([](const auto& A,const auto& B){return A.Id.ToString(EGuidFormats::Digits)<B.Id.ToString(EGuidFormats::Digits);});
}

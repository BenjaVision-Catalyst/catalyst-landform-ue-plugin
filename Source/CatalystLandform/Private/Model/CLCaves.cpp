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
    if((AlgorithmVersion<1||AlgorithmVersion>9)||!In(Character,0,1)||!In(NetworksPerSquareKilometer,0,10)||!In(LengthMeters,1,500)||!In(RadiusMeters,.1,10)||!In(MinimumCoverMeters,1,100)||!In(DepthMeters,1,500)||!In(Branching,0,1))
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
        Supports.Add(B);Elements.Add(FVoxelBox(B),I);
    }
    const auto Index=FVoxelAABBTree::Create(MoveTemp(Elements));UE::Geometry::FSizedDisjointSet Sets(NetworkInfo.Num());
    for(int32 I=0;I<Supports.Num();++I)Index->Intersects(FVoxelFastBox(FVoxelBox(Supports[I])),[&](int32 J){Sets.Union(I,J);return false;});
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

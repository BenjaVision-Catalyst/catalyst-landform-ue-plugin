#include "CLRecipeAssetTools.h"
#include "CLTerrainDesigner.h"
#include "Model/CLTerrainType.h"
#include "Toolkits/CFModelEditorToolkit.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

UCLTerrainTypeFactory::UCLTerrainTypeFactory()
{ SupportedClass = UCLTerrainTypeAsset::StaticClass(); bCreateNew = true; bEditAfterNew = true; }
UObject* UCLTerrainTypeFactory::FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{
    auto* Asset = NewObject<UCLTerrainTypeAsset>(Parent, Class, Name, Flags);
    Asset->TerrainType.Id = FGuid::NewGuid();
    Asset->TerrainType.Coordinates = ECLTerrainCoordinates::LocalPatch;
    Asset->TerrainType.AlgorithmVersion = 2;
    Asset->TerrainType.NoiseLayers = FCLPlanetCompiler::RollingPlains().NoiseLayers; return Asset;
}
TSoftClassPtr<UObject> UCLTerrainTypeAssetDefinition::GetAssetClass() const { return UCLTerrainTypeAsset::StaticClass(); }
EAssetCommandResult UCLTerrainTypeAssetDefinition::OpenAssets(const FAssetOpenArgs& Args) const
{
    for (auto* Asset : Args.LoadObjects<UCLTerrainTypeAsset>())
        CLOpenTerrainDesigner(Asset, nullptr, Args.ToolkitHost.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone, Args.ToolkitHost);
    return EAssetCommandResult::Handled;
}

#include "Stamps/CLStampLibrary.h"
UCLStampLibraryFactory::UCLStampLibraryFactory()
{SupportedClass=UCLStampLibraryAsset::StaticClass();bCreateNew=true;bEditAfterNew=true;}
UObject* UCLStampLibraryFactory::FactoryCreateNew(UClass* Class,UObject* Parent,FName Name,EObjectFlags Flags,UObject*,FFeedbackContext*)
{return NewObject<UCLStampLibraryAsset>(Parent,Class,Name,Flags);}
TSoftClassPtr<UObject> UCLStampLibraryAssetDefinition::GetAssetClass()const{return UCLStampLibraryAsset::StaticClass();}

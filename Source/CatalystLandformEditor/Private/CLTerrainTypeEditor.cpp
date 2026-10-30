#include "CLRecipeAssetTools.h"
#include "Model/CLTerrainType.h"
#include "Toolkits/CFModelEditorToolkit.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

namespace
{
class FCLTerrainTypeToolkit : public FCFModelEditorToolkit
{
public:
    void Open(UCLTerrainTypeAsset* Asset, EToolkitMode::Type Mode, TSharedPtr<IToolkitHost> Host)
    {
        EditingAsset = Asset; BuildDetailsPanel();
        const auto Layout = FTabManager::NewLayout("CLTerrainType_v1")->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
            ->Split(FTabManager::NewStack()->AddTab(DetailsTabId, ETabState::OpenedTab)->AddTab(ValidationTabId, ETabState::OpenedTab)));
        InitAssetEditor(Mode, Host, GetToolkitFName(), Layout, true, true, Asset);
        const auto Extender = MakeShared<FExtender>();
        Extender->AddToolBarExtension(TEXT("Asset"), EExtensionHook::After, nullptr,
            FToolBarExtensionDelegate::CreateLambda([this](FToolBarBuilder& Builder)
            { Builder.AddToolBarButton(FUIAction(FExecuteAction::CreateSP(this, &FCLTerrainTypeToolkit::Export)), NAME_None,
                FText::FromString(TEXT("Export terrain type")), FText::FromString(TEXT("Export local authoring JSON through Foundation"))); }));
        AddToolbarExtender(Extender); RegenerateMenusAndToolbars(); RefreshAllUI(Asset, false);
    }
    void Export() { HandleExportJson(); }
    virtual FName GetToolkitFName() const override { return TEXT("CLTerrainType"); }
    virtual FText GetBaseToolkitName() const override { return NSLOCTEXT("CL", "TerrainTypeToolkit", "Landform Terrain Type"); }
    virtual FString GetWorldCentricTabPrefix() const override { return TEXT("Terrain Type"); }
};
}
UCLTerrainTypeFactory::UCLTerrainTypeFactory()
{ SupportedClass = UCLTerrainTypeAsset::StaticClass(); bCreateNew = true; bEditAfterNew = true; }
UObject* UCLTerrainTypeFactory::FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{
    auto* Asset = NewObject<UCLTerrainTypeAsset>(Parent, Class, Name, Flags);
    Asset->TerrainType.Id = FGuid::NewGuid(); return Asset;
}
TSoftClassPtr<UObject> UCLTerrainTypeAssetDefinition::GetAssetClass() const { return UCLTerrainTypeAsset::StaticClass(); }
EAssetCommandResult UCLTerrainTypeAssetDefinition::OpenAssets(const FAssetOpenArgs& Args) const
{
    for (auto* Asset : Args.LoadObjects<UCLTerrainTypeAsset>())
        MakeShared<FCLTerrainTypeToolkit>()->Open(Asset, Args.ToolkitHost.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone, Args.ToolkitHost);
    return EAssetCommandResult::Handled;
}

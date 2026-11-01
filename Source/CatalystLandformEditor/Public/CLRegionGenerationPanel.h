#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Model/CLRegionGeneration.h"
#include "UObject/StrongObjectPtr.h"
DECLARE_DELEGATE_OneParam(FOnCLRegionInspection,bool);
DECLARE_DELEGATE_OneParam(FOnCLGeneratedRegionSelection,FGuid);
/** Planet-owned palette/generator controls. Rendering and source authoring retain their own owners. */
class CATALYSTLANDFORMEDITOR_API SCLRegionGenerationPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SCLRegionGenerationPanel){} SLATE_ARGUMENT(UCLPlanetRecipeAsset*,Asset) SLATE_ATTRIBUTE(bool,Inspecting)
        SLATE_EVENT(FOnCLRegionInspection,OnInspection) SLATE_EVENT(FOnCLGeneratedRegionSelection,OnSelectRegion) SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    void Refresh(TSharedPtr<const FCLCompiledRegionGeneration> Plan);
    void SelectRegion(FGuid Id);
private:
    TStrongObjectPtr<UCLPlanetRecipeAsset> Asset;
    TSharedPtr<const FCLCompiledRegionGeneration> Plan;
    TSharedPtr<SVerticalBox> PaletteRows;
    FGuid Selected;
    FString Error;
    TAttribute<bool> Inspecting;
    FOnCLRegionInspection OnInspection;
    FOnCLGeneratedRegionSelection OnSelectRegion;
    bool Commit(FCLRegionGenerationSettings Settings,const FText& Description,TFunction<void()> EditLinks={});
    TSharedRef<SWidget> PaletteMenu();
    TSharedRef<SWidget> RegionMenu();
    TSharedRef<SWidget> OverrideMenu();
    const FCLGeneratedRegionCell* Cell() const;
};

#include "CLRegionGenerationPanel.h"
#include "CLTerrainDesigner.h"
#include "Model/CLTerrainType.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

bool SCLRegionGenerationPanel::Commit(FCLRegionGenerationSettings Settings,const FText& Description,TFunction<void()> EditLinks)
{
    auto Candidate=Asset->Recipe;Candidate.SchemaVersion=FMath::Max(12,Candidate.SchemaVersion);Candidate.RegionGeneration=MoveTemp(Settings);
    Error.Reset();if(!FCLPlanetCompiler::Compile(Candidate,Error))return false;
    const FScopedTransaction Tx(Description);Asset->Modify();Asset->Recipe=MoveTemp(Candidate);if(EditLinks)EditLinks();Asset->PostEditChange();Asset->MarkPackageDirty();return true;
}
const FCLGeneratedRegionCell* SCLRegionGenerationPanel::Cell() const
{return Plan?Plan->Cells.FindByPredicate([&](const auto& C){return C.Id==Selected;}):nullptr;}
void SCLRegionGenerationPanel::SelectRegion(FGuid Id){Selected=Id;}
void SCLRegionGenerationPanel::Construct(const FArguments& Args)
{
    Asset.Reset(Args._Asset);Inspecting=Args._Inspecting;OnInspection=Args._OnInspection;OnSelectRegion=Args._OnSelectRegion;
    auto Controls=SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight()[SNew(SCheckBox).IsChecked_Lambda([this]{return Asset->Recipe.RegionGeneration.bEnabled?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this](ECheckBoxState V){auto S=Asset->Recipe.RegionGeneration;S.bEnabled=V==ECheckBoxState::Checked;Commit(S,FText::FromString(TEXT("Enable structure-guided regions")));})
            [SNew(STextBlock).Text(FText::FromString(TEXT("Generate regions from structure")))]]
        +SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(SComboButton).OnGetMenuContent(this,&SCLRegionGenerationPanel::PaletteMenu)
            .ButtonContent()[SNew(STextBlock).Text(FText::FromString(TEXT("Choose Landform palette…")))]]
        +SVerticalBox::Slot().AutoHeight()[SAssignNew(PaletteRows,SVerticalBox)]
        +SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(FText::FromString(TEXT("Region count")))]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SNumericEntryBox<int32>).MinValue(8).MaxValue(128).AllowSpin(true)
                .Value_Lambda([this]{return Asset->Recipe.RegionGeneration.RegionCount;})
                .OnValueCommitted_Lambda([this](int32 V,ETextCommit::Type){auto S=Asset->Recipe.RegionGeneration;S.RegionCount=V;S.Overrides.RemoveAll([&](const auto& O){return O.RegionId.D>uint32(V);});Commit(S,FText::FromString(TEXT("Change generated region count")));})]]
        +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(FText::FromString(TEXT("Region seed")))]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SNumericEntryBox<int32>).AllowSpin(true).Value_Lambda([this]{return Asset->Recipe.RegionGeneration.Seed;})
                .OnValueCommitted_Lambda([this](int32 V,ETextCommit::Type){auto S=Asset->Recipe.RegionGeneration;S.Seed=V;S.Overrides.Reset();Commit(S,FText::FromString(TEXT("Reseed regions and clear assignment overrides")));})]];
    auto AddNumber=[&](const TCHAR* Label,double FCLRegionGenerationSettings::*Member,double Minimum,double Maximum)
    {
        Controls->AddSlot().AutoHeight().Padding(0,6,0,0)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(FText::FromString(Label))]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SNumericEntryBox<double>).MinValue(Minimum).MaxValue(Maximum).AllowSpin(true)
                .Value_Lambda([this,Member]{return Asset->Recipe.RegionGeneration.*Member;})
                .OnValueCommitted_Lambda([this,Member](double V,ETextCommit::Type){auto S=Asset->Recipe.RegionGeneration;S.*Member=V;Commit(S,FText::FromString(TEXT("Edit region generation")));})]];
    };
    AddNumber(TEXT("Structure guidance"),&FCLRegionGenerationSettings::StructureGuidance,0,1);
    AddNumber(TEXT("Boundary blending"),&FCLRegionGenerationSettings::BlendFraction,.05,1);
    AddNumber(TEXT("Regional relief strength"),&FCLRegionGenerationSettings::ReliefStrength,0,1);
    Controls->AddSlot().AutoHeight().Padding(0,8)[SNew(SCheckBox).IsChecked_Lambda([this]{return Inspecting.Get()?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
        .OnCheckStateChanged_Lambda([this](ECheckBoxState V){OnInspection.ExecuteIfBound(V==ECheckBoxState::Checked);})[SNew(STextBlock).Text(FText::FromString(TEXT("Inspect generated regions on globe")))]];
    Controls->AddSlot().AutoHeight()[SNew(SComboButton).OnGetMenuContent(this,&SCLRegionGenerationPanel::RegionMenu)
        .ButtonContent()[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]{const auto* C=Cell();return FText::FromString(C?FString::Printf(TEXT("Region %d — %s"),Plan->Cells.IndexOfByPredicate([&](const auto& V){return V.Id==Selected;})+1,*Plan->Palette[C->PaletteIndex].Name):TEXT("Choose a region or click its surface"));})]];
    Controls->AddSlot().AutoHeight().Padding(0,6)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]{const auto* C=Cell();return FText::FromString(C?C->Reason:TEXT("One selected Landform fills every region. Multiple selections are matched to the base structure using their placement preferences."));})];
    Controls->AddSlot().AutoHeight()[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4,4))
        +SWrapBox::Slot()[SNew(SComboButton).IsEnabled_Lambda([this]{return Cell()!=nullptr;}).OnGetMenuContent(this,&SCLRegionGenerationPanel::OverrideMenu)
            .ButtonContent()[SNew(STextBlock).Text(FText::FromString(TEXT("Assign this region…")))]]
        +SWrapBox::Slot()[SNew(SButton).Text(FText::FromString(TEXT("Clear assignment overrides"))).IsEnabled_Lambda([this]{return !Asset->Recipe.RegionGeneration.Overrides.IsEmpty();})
            .OnClicked_Lambda([this]{auto S=Asset->Recipe.RegionGeneration;S.Overrides.Reset();Commit(S,FText::FromString(TEXT("Clear region assignments")));return FReply::Handled();})]];
    Controls->AddSlot().AutoHeight().Padding(0,6)[SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FLinearColor(1,.5,.25)).Text_Lambda([this]{return FText::FromString(Error);})];
    ChildSlot[Controls];Refresh(nullptr);
}
void SCLRegionGenerationPanel::Refresh(TSharedPtr<const FCLCompiledRegionGeneration> InPlan)
{
    Plan=MoveTemp(InPlan);if(!PaletteRows)return;PaletteRows->ClearChildren();
    for(const auto& Entry:Asset->Recipe.RegionGeneration.Palette)
    {
        const auto Id=Entry.Id;int32 Count=0;if(Plan)for(const auto& C:Plan->Cells)if(Plan->Palette[C.PaletteIndex].Id==Id)++Count;
        PaletteRows->AddSlot().AutoHeight().Padding(0,3)[SNew(SBorder).Padding(6)[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[SNew(SCheckBox).IsChecked(Entry.bEnabled?ECheckBoxState::Checked:ECheckBoxState::Unchecked)
                .OnCheckStateChanged_Lambda([this,Id](ECheckBoxState V){auto S=Asset->Recipe.RegionGeneration;auto* P=S.Palette.FindByPredicate([&](const auto& P){return P.Id==Id;});if(!P)return;P->bEnabled=V==ECheckBoxState::Checked;if(!P->bEnabled)S.Overrides.RemoveAll([&](const auto& O){return O.LandformId==Id;});if(!S.Palette.ContainsByPredicate([](const auto& P){return P.bEnabled;}))S.bEnabled=false;Commit(S,FText::FromString(TEXT("Select regional Landforms")));})
                [SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(FString::Printf(TEXT("%s · %d regions"),*Entry.Name,Count)))]]
            +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4,4))
                +SWrapBox::Slot()[SNew(SButton).Text(FText::FromString(TEXT("Edit Landform"))).IsEnabled(Asset->PaletteSources.Contains(Id))
                    .OnClicked_Lambda([this,Id]{if(const auto* Source=Asset->PaletteSources.Find(Id))CLOpenTerrainDesigner(Source->Get(),&Asset->Recipe.RadialGuides);return FReply::Handled();})]
                +SWrapBox::Slot()[SNew(SButton).Text(FText::FromString(TEXT("Refresh source"))).IsEnabled(Asset->PaletteSources.Contains(Id))
                    .OnClicked_Lambda([this,Id]{const FScopedTransaction Tx(FText::FromString(TEXT("Refresh regional Landform")));Error.Reset();Asset->RefreshPaletteSource(Id,Error);return FReply::Handled();})]
                +SWrapBox::Slot()[SNew(SButton).Text(FText::FromString(TEXT("Remove"))).OnClicked_Lambda([this,Id]
                    {auto S=Asset->Recipe.RegionGeneration;S.Palette.RemoveAll([&](const auto& P){return P.Id==Id;});S.Overrides.RemoveAll([&](const auto& O){return O.LandformId==Id;});if(!S.Palette.ContainsByPredicate([](const auto& P){return P.bEnabled;}))S.bEnabled=false;Commit(S,FText::FromString(TEXT("Remove palette Landform")),[this,Id]{Asset->PaletteSources.Remove(Id);});return FReply::Handled();})]]]];
    }
}
TSharedRef<SWidget> SCLRegionGenerationPanel::PaletteMenu()
{
    FMenuBuilder Menu(false,nullptr);TArray<FAssetData> Assets;FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByClass(UCLTerrainTypeAsset::StaticClass()->GetClassPathName(),Assets,true);Assets.Sort([](const auto& A,const auto& B){return A.AssetName.LexicalLess(B.AssetName);});TSet<FGuid> Seen;
    for(const auto& Data:Assets)
    {
        auto* Source=Cast<UCLTerrainTypeAsset>(Data.GetAsset());if(!Source||Source->TerrainType.Coordinates!=ECLTerrainCoordinates::SphericalSurface||Seen.Contains(Source->TerrainType.Id))continue;Seen.Add(Source->TerrainType.Id);
        const auto Id=Source->TerrainType.Id;const TWeakObjectPtr<UCLTerrainTypeAsset> Weak(Source);
        Menu.AddMenuEntry(FText::FromString(Source->TerrainType.Name),FText::FromString(Source->GetPathName()),FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([this,Weak,Id]
        {
            if(!Weak.IsValid())return;
            const auto* Existing=Asset->Recipe.RegionGeneration.Palette.FindByPredicate([&](const auto& P){return P.Id==Id;});
            if(Existing){auto S=Asset->Recipe.RegionGeneration;S.Palette.RemoveAll([&](const auto& P){return P.Id==Id;});S.Overrides.RemoveAll([&](const auto& O){return O.LandformId==Id;});if(!S.Palette.ContainsByPredicate([](const auto& P){return P.bEnabled;}))S.bEnabled=false;Commit(S,FText::FromString(TEXT("Remove palette Landform")),[this,Id]{Asset->PaletteSources.Remove(Id);});}
            else {const FScopedTransaction Tx(FText::FromString(TEXT("Add regional Landform")));Error.Reset();Asset->AddPaletteSource(Weak.Get(),Error);}
        }),FCanExecuteAction(),FIsActionChecked::CreateLambda([this,Id]{return Asset->Recipe.RegionGeneration.Palette.ContainsByPredicate([&](const auto& P){return P.Id==Id;});})),NAME_None,EUserInterfaceActionType::Check);
    }
    return Menu.MakeWidget();
}
TSharedRef<SWidget> SCLRegionGenerationPanel::RegionMenu()
{
    FMenuBuilder Menu(true,nullptr);if(Plan)for(int32 I=0;I<Plan->Cells.Num();++I){const auto& C=Plan->Cells[I];Menu.AddMenuEntry(FText::FromString(FString::Printf(TEXT("Region %d — %s"),I+1,*Plan->Palette[C.PaletteIndex].Name)),FText::FromString(C.Reason),FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([this,Id=C.Id]{SelectRegion(Id);OnSelectRegion.ExecuteIfBound(Id);})));}return Menu.MakeWidget();
}
TSharedRef<SWidget> SCLRegionGenerationPanel::OverrideMenu()
{
    FMenuBuilder Menu(true,nullptr);auto Add=[&](FGuid Id,const FString& Name){Menu.AddMenuEntry(FText::FromString(Name),FText(),FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([this,Id]{auto S=Asset->Recipe.RegionGeneration;S.Overrides.RemoveAll([&](const auto& O){return O.RegionId==Selected;});if(Id.IsValid()){FCLRegionAssignmentOverride O;O.RegionId=Selected;O.LandformId=Id;S.Overrides.Add(O);}Commit(S,FText::FromString(TEXT("Assign generated region")));})));};
    Add(FGuid(),TEXT("Automatic assignment"));if(Plan)for(const auto& P:Plan->Palette)Add(P.Id,P.Name);return Menu.MakeWidget();
}

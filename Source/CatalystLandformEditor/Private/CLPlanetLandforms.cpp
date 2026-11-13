#include "CLPlanetLandforms.h"
#include "Surface/CLSurfaceLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"
#include "PropertyEditorModule.h"
#include "IStructureDetailsView.h"
#include "IDetailsView.h"
#include "PropertyPath.h"
#include "UObject/StructOnScope.h"

void FCLPlanetLandforms::AddSource(UCLTerrainTypeAsset* Source)
{if(Source&&Source->TerrainType.Coordinates==ECLTerrainCoordinates::SphericalSurface)Sources.Add(Source->TerrainType.Id,TStrongObjectPtr<UCLTerrainTypeAsset>(Source));}
void FCLPlanetLandforms::Discover()
{
    TArray<FAssetData> Assets;FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByClass(UCLTerrainTypeAsset::StaticClass()->GetClassPathName(),Assets,true);
    Assets.Sort([](const auto& A,const auto& B){return A.PackageName.LexicalLess(B.PackageName);});
    for(const auto& A:Assets)if(A.PackagePath.ToString()==TEXT("/CatalystLandform/TerrainTypes/Provinces"))AddSource(Cast<UCLTerrainTypeAsset>(A.GetAsset()));
    // The planet's explicit source link wins over a catalogue definition with the same ID.
    for(const auto& Pair:Asset->PaletteSources)AddSource(Pair.Value.Get());
}
TArray<FGuid> FCLPlanetLandforms::Rows()const
{
    TArray<FGuid> Ids;Sources.GetKeys(Ids);for(const auto& P:Asset->Recipe.RegionGeneration.Palette)Ids.AddUnique(P.Id);
    Ids.Sort([this](FGuid A,FGuid B){return Name(A)<Name(B);});return Ids;
}
const FCLLandformPaletteEntry* FCLPlanetLandforms::Entry(FGuid Id)const
{return Asset->Recipe.RegionGeneration.Palette.FindByPredicate([&](const auto& P){return P.Id==Id;});}
FString FCLPlanetLandforms::Name(FGuid Id)const
{if(auto* P=Entry(Id))return P->Name;if(const auto* S=Sources.Find(Id))return (*S)->TerrainType.Name;return TEXT("Landform");}
FString FCLPlanetLandforms::Description(FGuid Id)const
{
    const auto* Applied=Entry(Id);const auto* Source=Sources.Find(Id);
    const auto* Province=Applied?&Applied->Suitability.Province:Source?&(*Source)->TerrainType.PlacementSuitability.Province:nullptr;
    if(Province&&Province->IsActive()&&!Province->RegionalForms.IsEmpty())
    {TArray<FString> Forms;for(FName Form:Province->RegionalForms)Forms.Add(Form.ToString());return FString::Join(Forms,TEXT(" · "))+(Province->bPlaceholder?TEXT(" (planned)"):TEXT(""));}
    if(Applied&&!Applied->Description.IsEmpty())return Applied->Description;
    if(Source)return (*Source)->TerrainType.Description;
    return TEXT("Applied Landform snapshot");
}
bool FCLPlanetLandforms::Included(FGuid Id)const
{const auto* P=Entry(Id);return Asset->Recipe.RegionGeneration.bEnabled&&P&&P->bEnabled;}
bool FCLPlanetLandforms::Visible(FGuid Id)const{return Included(Id)&&(Solo==Id||!Hidden.Contains(Id));}
double FCLPlanetLandforms::Character(FGuid Id)const
{if(auto* P=Entry(Id))return P->Rift.IsActive()?P->Rift.Character:P->Transform.IsActive()?P->Transform.Character:P->VolcanicArc.IsActive()?P->VolcanicArc.Character:P->Character;if(const auto* S=Sources.Find(Id))return (*S)->TerrainType.Rift.IsActive()?(*S)->TerrainType.Rift.Character:(*S)->TerrainType.Transform.IsActive()?(*S)->TerrainType.Transform.Character:(*S)->TerrainType.VolcanicArc.IsActive()?(*S)->TerrainType.VolcanicArc.Character:.5;return .5;}
bool FCLPlanetLandforms::Commit(FCLPlanetRecipe R,const FText& Label,UCLTerrainTypeAsset* Source)
{
    Error.Reset();if(!FCLPlanetCompiler::PrepareBackground(R,Error))return false;
    TUniquePtr<FScopedTransaction> Tx;if(!Gesture)Tx=MakeUnique<FScopedTransaction>(Label);
    Asset->Modify();Asset->Recipe=MoveTemp(R);if(Source)Asset->PaletteSources.Add(Source->TerrainType.Id,Source);
    Asset->MarkPackageDirty();Asset->PostEditChange();return true;
}
bool FCLPlanetLandforms::Include(FGuid Id,bool Enabled)
{
    if(Included(Id)==Enabled)return true;
    auto R=Asset->Recipe;auto& S=R.RegionGeneration;UCLTerrainTypeAsset* Source=nullptr;
    auto* P=S.Palette.FindByPredicate([&](const auto& V){return V.Id==Id;});
    if(!P)
    {
        const auto* Found=Sources.Find(Id);if(!Enabled||!Found)return false;Source=Found->Get();if(!Source->TerrainType.Validate(Error))return false;
        if(S.Palette.Num()>=32){Error=TEXT("The planet supports at most 32 Landforms");return false;}
        const auto& T=Source->TerrainType;P=&S.Palette.AddDefaulted_GetRef();P->Id=T.Id;P->Name=T.Name;P->Description=T.Description;P->AlgorithmVersion=T.AlgorithmVersion;P->Suitability=T.PlacementSuitability;P->Rift=T.Rift;P->Transform=T.Transform;P->VolcanicArc=T.VolcanicArc;P->NoiseLayers=T.NoiseLayers;P->SurfaceStack=T.SurfaceStack;P->Deposits=T.Deposits;
        if(!P->Deposits.IsEmpty()&&!R.SurfaceStack.IsActive())R.SurfaceStack=FCLSurfaceStack::MakeDefault();
        if(P->SurfaceStack.IsActive()&&!R.SurfaceStack.IsActive())R.SurfaceStack=P->SurfaceStack;
    }
    if(Enabled&&!S.bEnabled)for(auto& Existing:S.Palette)Existing.bEnabled=false;
    P->bEnabled=Enabled;R.SchemaVersion=FMath::Max(R.SchemaVersion,(P->SurfaceStack.IsActive()||!P->Deposits.IsEmpty())?13:12);
    S.AlgorithmVersion=FMath::Max(S.AlgorithmVersion,(P->Rift.IsActive()||P->Transform.IsActive()||P->VolcanicArc.IsActive())?4:P->Suitability.Province.IsActive()?3:2);
    S.bEnabled=S.Palette.ContainsByPredicate([](const auto& V){return V.bEnabled;});
    if(!Enabled)S.Overrides.RemoveAll([&](const auto& O){return O.LandformId==Id;});
    const bool Ok=Commit(MoveTemp(R),FText::FromString(TEXT("Include Landform in generation")),Source);
    if(Ok&&!Enabled&&Highlighted.Remove(Id))HighlightsChanged.ExecuteIfBound();
    if(Ok&&!Enabled&&Solo==Id){Solo.Invalidate();PreviewChanged.ExecuteIfBound();}return Ok;
}
bool FCLPlanetLandforms::SetCharacter(FGuid Id,double Value)
{
    if(!Included(Id)||!FMath::IsFinite(Value))return false;auto R=Asset->Recipe;
    auto* P=R.RegionGeneration.Palette.FindByPredicate([&](const auto& V){return V.Id==Id;});
    Value=FMath::Clamp(Value,0.,1.);if(P->Rift.IsActive())P->Rift.ApplyCuratedCharacter(Value);else if(P->Transform.IsActive())P->Transform.ApplyProgression(Value);else if(P->VolcanicArc.IsActive())P->VolcanicArc.ApplyProgression(Value);else P->Character=Value;
    return Commit(MoveTemp(R),FText::FromString(TEXT("Change Landform Character")));
}
bool FCLPlanetLandforms::SetValues(FGuid Id,const FCLLandformPaletteEntry& Values)
{
    if(!Included(Id))return false;auto R=Asset->Recipe;auto* P=R.RegionGeneration.Palette.FindByPredicate([&](const auto& V){return V.Id==Id;});
    P->Rift=Values.Rift;P->Transform=Values.Transform;P->VolcanicArc=Values.VolcanicArc;P->NoiseLayers=Values.NoiseLayers;P->Suitability=Values.Suitability;P->SurfaceStack=Values.SurfaceStack;P->Seed=Values.Seed;
    P->Deposits=Values.Deposits;
    TSet<FGuid> Seen;const auto* Library=UCLSurfaceLibraryAsset::LoadConfigured();
    for(auto& Deposit:P->Deposits)
    {
        if(!Deposit.Unit.Id.IsValid()||Seen.Contains(Deposit.Unit.Id))Deposit.Unit.Id=FGuid::NewGuid();Seen.Add(Deposit.Unit.Id);
        const auto* Previous=Entry(Id)->Deposits.FindByPredicate([&](const auto& D){return D.Unit.Id==Deposit.Unit.Id;});
        if(Library&&(!Previous||Previous->Unit.SurfaceId!=Deposit.Unit.SurfaceId))
            if(const auto* Material=Library->Library.Entries.FindByPredicate([&](const auto& M){return M.Id==Deposit.Unit.SurfaceId;}))Deposit.Unit.SurfaceRevision=Material->Revision;
    }
    if(!P->Deposits.IsEmpty()){R.SchemaVersion=FMath::Max(13,R.SchemaVersion);if(!R.SurfaceStack.IsActive())R.SurfaceStack=FCLSurfaceStack::MakeDefault();}
    // A deliberate physical edit adopts additive delivery while retaining the
    // user's ranges. The knob's curated defaults are a separate action.
    if(P->Rift.IsActive())P->Rift.AlgorithmVersion=3;
    return Commit(MoveTemp(R),FText::FromString(TEXT("Edit Landform values")));
}
bool FCLPlanetLandforms::Reseed(FGuid Id)
{
    if(!Included(Id))return false;auto R=Asset->Recipe;auto* P=R.RegionGeneration.Palette.FindByPredicate([&](const auto& V){return V.Id==Id;});
    int32& Seed=P->Rift.IsActive()?P->Rift.Seed:P->Transform.IsActive()?P->Transform.Seed:P->VolcanicArc.IsActive()?P->VolcanicArc.Seed:P->Seed;Seed=int32(uint32(Seed)*1664525U+1013904223U);
    return Commit(MoveTemp(R),FText::FromString(TEXT("Reseed Landform")));
}
bool FCLPlanetLandforms::ToggleEye(FGuid Id)
{if(!Included(Id))return false;if(Solo==Id){Solo.Invalidate();Hidden.Add(Id);}else if(Hidden.Contains(Id))Hidden.Remove(Id);else Hidden.Add(Id);PreviewChanged.ExecuteIfBound();return true;}
bool FCLPlanetLandforms::ToggleHighlight(FGuid Id)
{if(!Included(Id))return false;if(Highlighted.Contains(Id))Highlighted.Remove(Id);else Highlighted.Add(Id);HighlightsChanged.ExecuteIfBound();return true;}
bool FCLPlanetLandforms::ToggleSolo(FGuid Id)
{if(!Included(Id))return false;if(Solo==Id)Solo.Invalidate();else Solo=Id;PreviewChanged.ExecuteIfBound();return true;}
void FCLPlanetLandforms::BeginGesture(FGuid Id)
{if(Included(Id)&&!Gesture){Gesture=MakeUnique<FScopedTransaction>(FText::FromString(TEXT("Change Landform Character")));Asset->Modify();}}
void FCLPlanetLandforms::EndGesture(){Gesture.Reset();}
FGuid FCLPlanetLandforms::PreviewSolo()const{return Included(Solo)?Solo:FGuid();}
FCLPlanetRecipe FCLPlanetLandforms::PreviewRecipe()const
{
    auto R=Asset->Recipe;const FGuid Only=PreviewSolo();
    for(auto& P:R.RegionGeneration.Palette)P.bEnabled=P.bEnabled&&(Only.IsValid()?P.Id==Only:!Hidden.Contains(P.Id));
    R.RegionGeneration.Overrides.RemoveAll([&](const auto& O){return !R.RegionGeneration.Palette.ContainsByPredicate([&](const auto& P){return P.Id==O.LandformId&&P.bEnabled;});});
    if(!R.RegionGeneration.Palette.ContainsByPredicate([](const auto& P){return P.bEnabled;}))R.RegionGeneration.bEnabled=false;
    if(Only.IsValid()){R.TerrainRegions.Reset();R.RegionGeneration.bEnabled=true;R.RegionGeneration.Overrides.Reset();}
    return R;
}
void SCLPlanetLandforms::Construct(const FArguments& A)
{
    Session=A._Session;ChildSlot[SNew(SVerticalBox)
        +SVerticalBox::Slot().FillHeight(1)[SAssignNew(Scroll,SScrollBox)+SScrollBox::Slot()[SAssignNew(Rows,SVerticalBox)]]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FLinearColor(1,.4,.2)).Text_Lambda([this]{return FText::FromString(Session->Error);})]];Refresh();
}
void SCLPlanetLandforms::Refresh()
{
    Rows->ClearChildren();Controls.Reset();
    for(FGuid Id:Session->Rows())
    {
        auto& C=Controls.Add(Id);C.Name=Session->Name(Id);C.Description=Session->Description(Id);const auto State=Session;auto Enabled=[State,Id]{return State->Included(Id);};
        FString Description=Session->Description(Id);int32 End;if(Description.FindChar('.',End))Description=Description.Left(End+1);if(Description.Len()>95)Description=Description.Left(92)+TEXT("…");
        Rows->AddSlot().AutoHeight().Padding(4,1)[SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.055,.055,.055)).Padding(6,7)[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)[SAssignNew(C.Disclosure,SButton).IsEnabled_Lambda(Enabled).ButtonStyle(FAppStyle::Get(),"NoBorder").ContentPadding(0).ToolTipText(FText::FromString(TEXT("Edit underlying values"))).OnClicked_Lambda([this,Id]{ToggleDetails(Id);return FReply::Handled();})[SNew(SImage).DesiredSizeOverride(FVector2D(10,10)).Image_Lambda([this,Id]{return FAppStyle::GetBrush(Expanded.Contains(Id)?TEXT("TreeArrow_Expanded"):TEXT("TreeArrow_Collapsed"));})]]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,6,0)[SAssignNew(C.Include,SCheckBox).ToolTipText(FText::FromString(TEXT("Include in planet generation"))).IsChecked_Lambda([Enabled]{return Enabled()?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([State,Id](ECheckBoxState V){State->Include(Id,V==ECheckBoxState::Checked);})]
            +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0,0,8,0)[SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(Session->Name(Id))).Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"))]
                +SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(Description)).ToolTipText(FText::FromString(Session->Description(Id))).Font(FAppStyle::GetFontStyle("SmallFont")).ColorAndOpacity(FLinearColor(.65,.65,.65))]]
            +SHorizontalBox::Slot().FillWidth(.55).VAlign(VAlign_Center)[SAssignNew(C.Knob,SSlider).IsEnabled_Lambda(Enabled).ToolTipText_Lambda([State,Id]{const auto* P=State->Entry(Id);return FText::FromString(P&&P->Rift.IsActive()?TEXT("Curated Character. Moving this restores the physical ranges; expand the row for manual overrides."):TEXT("Character. Expand the row to edit underlying values."));}).Value_Lambda([State,Id]{return float(State->Character(Id));}).OnMouseCaptureBegin_Lambda([State,Id]{State->BeginGesture(Id);}).OnValueChanged_Lambda([State,Id](float V){State->SetCharacter(Id,V);}).OnMouseCaptureEnd_Lambda([State]{State->EndGesture();})]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5,0)[SNew(SBox).WidthOverride(30)[SNew(STextBlock).IsEnabled_Lambda(Enabled).Text_Lambda([State,Id]{FNumberFormattingOptions Format;Format.SetMaximumFractionalDigits(1);return FText::AsNumber(State->Character(Id)*100,&Format);})]]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SAssignNew(C.Eye,SButton).IsEnabled_Lambda(Enabled).ButtonStyle(FAppStyle::Get(),"SimpleButton").ContentPadding(3).ToolTipText(FText::FromString(TEXT("Include in preview"))).OnClicked_Lambda([State,Id]{State->ToggleEye(Id);return FReply::Handled();})[SNew(SImage).DesiredSizeOverride(FVector2D(16,16)).Image_Lambda([State,Id]{return FAppStyle::GetBrush(State->Visible(Id)?TEXT("Level.VisibleIcon16x"):TEXT("Level.NotVisibleIcon16x"));})]]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SAssignNew(C.Info,SButton).IsEnabled_Lambda(Enabled).ButtonStyle(FAppStyle::Get(),"SimpleButton").ContentPadding(3).ToolTipText(FText::FromString(TEXT("Highlight assigned province cells"))).OnClicked_Lambda([State,Id]{State->ToggleHighlight(Id);return FReply::Handled();})[SNew(SImage).DesiredSizeOverride(FVector2D(16,16)).Image(FAppStyle::GetBrush("Icons.Info")).ColorAndOpacity_Lambda([State,Id]{return State->Highlighted.Contains(Id)?FSlateColor(FLinearColor(1,.65,.1)):FSlateColor::UseForeground();})]]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SAssignNew(C.Reseed,SButton).IsEnabled_Lambda(Enabled).ButtonStyle(FAppStyle::Get(),"SimpleButton").ContentPadding(3).ToolTipText(FText::FromString(TEXT("Reseed"))).OnClicked_Lambda([State,Id]{State->Reseed(Id);return FReply::Handled();})[SNew(SImage).DesiredSizeOverride(FVector2D(16,16)).Image(FAppStyle::GetBrush("Icons.Refresh"))]]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5,0,0,0)[SNew(SBox).WidthOverride(50)[SAssignNew(C.Solo,SButton).IsEnabled_Lambda(Enabled).ContentPadding(FMargin(3,2)).ToolTipText(FText::FromString(TEXT("Preview this Landform across the whole planet"))).OnClicked_Lambda([State,Id]{State->ToggleSolo(Id);return FReply::Handled();})[SNew(STextBlock).Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont")).Text_Lambda([State,Id]{return FText::FromString(State->PreviewSolo()==Id?TEXT("Unsolo"):TEXT("Solo"));})]]]]
            +SVerticalBox::Slot().AutoHeight().Padding(16,8,0,0)[SAssignNew(C.DetailsBox,SBox).MaxDesiredHeight(420).IsEnabled_Lambda(Enabled).Visibility_Lambda([this,Id]{return Expanded.Contains(Id)?EVisibility::Visible:EVisibility::Collapsed;})]]];
        if(Expanded.Contains(Id)){Expanded.Remove(Id);ToggleDetails(Id);}
    }
    SyncControls();
}

void SCLPlanetLandforms::SyncControls()
{
    // Rebuild only when the list/captions change; ordinary knob updates retain
    // the captured slider and its gesture transaction.
    for(const auto& Link:Session->Asset->PaletteSources)
        if(const auto* Existing=Session->Sources.Find(Link.Key);!Existing||Existing->Get()!=Link.Value.Get())Session->AddSource(Link.Value.Get());
    const auto Ids=Session->Rows();bool Rebuild=Ids.Num()!=Controls.Num();
    for(FGuid Id:Ids){const auto* C=Controls.Find(Id);Rebuild|=!C||C->Name!=Session->Name(Id)||C->Description!=Session->Description(Id);}
    if(Rebuild){Refresh();return;}
    for(auto& Pair:Controls)
    {const bool Enabled=Session->Included(Pair.Key);auto& C=Pair.Value;C.Include->SetIsChecked(Enabled?ECheckBoxState::Checked:ECheckBoxState::Unchecked);C.Disclosure->SetEnabled(Enabled);C.DetailsBox->SetEnabled(Enabled);C.Knob->SetEnabled(Enabled);C.Eye->SetEnabled(Enabled);C.Info->SetEnabled(Enabled);C.Reseed->SetEnabled(Enabled);C.Solo->SetEnabled(Enabled);
        if(C.Draft&&!ApplyingDetails)if(const auto* P=Session->Entry(Pair.Key);P&&!FCLLandformPaletteEntry::StaticStruct()->CompareScriptStruct(P,C.Draft->GetStructMemory(),0))
        {*reinterpret_cast<FCLLandformPaletteEntry*>(C.Draft->GetStructMemory())=*P;C.Details->GetDetailsView()->ForceRefresh();}
    }
}

void SCLPlanetLandforms::ToggleDetails(FGuid Id)
{
    if(!Session->Included(Id))return;auto* C=Controls.Find(Id);if(!C)return;
    if(Expanded.Contains(Id)){Expanded.Remove(Id);return;}Expanded.Add(Id);
    if(C->Details)return;
    C->Draft=MakeShared<FStructOnScope>(FCLLandformPaletteEntry::StaticStruct());
    *reinterpret_cast<FCLLandformPaletteEntry*>(C->Draft->GetStructMemory())=*Session->Entry(Id);
    FDetailsViewArgs Args;Args.bAllowSearch=false;Args.bHideSelectionTip=true;Args.NameAreaSettings=FDetailsViewArgs::HideNameArea;
    C->Details=FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateStructureDetailView(Args,FStructureDetailsViewArgs(),C->Draft);
    C->Details->SetCustomName(FText::FromString(Session->Entry(Id)->Rift.IsActive()?TEXT("Continental Rift"):Session->Name(Id)));
    C->Details->GetDetailsView()->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([RiftActive=Session->Entry(Id)->Rift.IsActive(),TransformActive=Session->Entry(Id)->Transform.IsActive(),ArcActive=Session->Entry(Id)->VolcanicArc.IsActive()](const FPropertyAndParent& P)
    {
        const FName N=P.Property.GetFName();if((N==TEXT("Rift")&&(TransformActive||ArcActive))||(N==TEXT("Transform")&&!TransformActive)||(N==TEXT("VolcanicArc")&&!ArcActive))return false;if((TransformActive||ArcActive)&&P.ParentProperties.IsEmpty()&&(N==TEXT("Seed")||N==TEXT("SurfaceStack")))return false;if(RiftActive&&P.ParentProperties.IsEmpty()&&(N==TEXT("Seed")||N==TEXT("SurfaceStack")))return false;if(N==TEXT("AlgorithmVersion")||N==TEXT("Character")||N==TEXT("Id")||N==TEXT("Type")||N==TEXT("bPlaceholder"))return false;
        auto Allowed=[](FName Name){return Name==TEXT("VolcanicArc")||Name==TEXT("Transform")||Name==TEXT("Rift")||Name==TEXT("NoiseLayers")||Name==TEXT("Suitability")||Name==TEXT("SurfaceStack")||Name==TEXT("Deposits")||Name==TEXT("Seed");};
        if(Allowed(N))return true;for(const auto* Parent:P.ParentProperties)if(Allowed(Parent->GetFName()))return true;return false;
    }));
    const auto Draft=C->Draft;const auto State=Session;
    C->Details->GetOnFinishedChangingPropertiesDelegate().AddLambda([this,State,Draft,Id](const FPropertyChangedEvent&)
    {
        ApplyingDetails=true;State->SetValues(Id,*reinterpret_cast<FCLLandformPaletteEntry*>(Draft->GetStructMemory()));ApplyingDetails=false;
        // Keep generated deposit IDs and resolved revisions in the editing draft.
        // Rejected edits restore the last accepted values through the same path.
        SyncControls();
    });
    C->DetailsBox->SetContent(C->Details->GetWidget().ToSharedRef());
    // Visibility is bound after structure creation; rebuild the initial tree too.
    auto View=C->Details->GetDetailsView();View->ForceRefresh();View->SetRootExpansionStates(true,false);
    FPropertyPath Path;Path.AddProperty(FPropertyInfo(FindFProperty<FProperty>(FCLLandformPaletteEntry::StaticStruct(),Session->Entry(Id)->VolcanicArc.IsActive()?TEXT("VolcanicArc"):Session->Entry(Id)->Transform.IsActive()?TEXT("Transform"):TEXT("Rift"))));View->ScrollPropertyIntoView(Path,true);
}

void SCLPlanetLandforms::Focus(FGuid Id)
{if(const auto* Row=Controls.Find(Id))Scroll->ScrollDescendantIntoView(Row->Include,false,EDescendantScrollDestination::Center);}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Model/CLRegionGeneration.h"
#include "Editor.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPlanetLandformNoiseTest,"Catalyst.Landform.Planet.LandformRows.GenericNoiseAndSeed",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPlanetLandformNoiseTest::RunTest(const FString&)
{
    TStrongObjectPtr<UCLTerrainTypeAsset> Source(NewObject<UCLTerrainTypeAsset>());
    auto& T=Source->TerrainType;T.Id=FGuid(9,8,7,6);T.Name=TEXT("Local noise family");T.Description=TEXT("Test local relief");T.SchemaVersion=6;T.AlgorithmVersion=2;T.Coordinates=ECLTerrainCoordinates::SphericalSurface;T.NoiseLayers=FCLPlanetCompiler::RollingPlains().NoiseLayers;
    T.NoiseLayers[0].Seed=195;T.NoiseLayers[0].WavelengthMeters=130;
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());A->Recipe=FCLPlanetCompiler::RollingPlains();A->Recipe.AlgorithmVersion=2;A->Recipe.Coordinates=ECLTerrainCoordinates::SphericalSurface;A->Recipe.RadiusMeters=2000;
    auto State=MakeShared<FCLPlanetLandforms>(A.Get());State->AddSource(Source.Get());if(!State->Include(T.Id,true)){AddError(State->Error);return false;}
    FString Error;State->SetCharacter(T.Id,0);const auto Low=FCLPlanetCompiler::Compile(A->Recipe,Error);State->SetCharacter(T.Id,1);const auto High=FCLPlanetCompiler::Compile(A->Recipe,Error);State->Reseed(T.Id);const auto Seeded=FCLPlanetCompiler::Compile(A->Recipe,Error);
    if(!Low||!High||!Seeded){AddError(Error);return false;}double CharacterDelta=0,SeedDelta=0;
    for(int32 I=0;I<256;++I){const double Z=1-2*(I+.5)/256,Angle=I*2.399963229728653;const FVector D(FMath::Cos(Angle)*FMath::Sqrt(1-Z*Z),FMath::Sin(Angle)*FMath::Sqrt(1-Z*Z),Z);CharacterDelta+=FMath::Abs(Low->SampleHeightMeters(D)-High->SampleHeightMeters(D));SeedDelta+=FMath::Abs(High->SampleHeightMeters(D)-Seeded->SampleHeightMeters(D));}
    TestTrue(TEXT("Generic Character changes actual local relief"),CharacterDelta>1);TestTrue(TEXT("Generic reseed changes actual terrain"),SeedDelta>1);
    TestEqual(TEXT("Generic controls retain original source Character semantics"),T.NoiseLayers[0].Seed,195);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPlanetDepositEditorTest,"Catalyst.Landform.MaterialLayers.PlanetDetailsAndUndo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPlanetDepositEditorTest::RunTest(const FString&)
{
    auto* Source=LoadObject<UCLTerrainTypeAsset>(nullptr,TEXT("/CatalystLandform/TerrainTypes/Provinces/DA_Province_ContinentalRift.DA_Province_ContinentalRift"));
    if(!Source){AddError(TEXT("Missing packaged Rift"));return false;}const auto OriginalSource=Source->TerrainType;
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>(GetTransientPackage(),NAME_None,RF_Transactional));A->Recipe=FCLPlanetCompiler::ContinentalBase();A->Recipe.UseGuidedGeology();A->Recipe.Caves.bEnabled=false;
    auto State=MakeShared<FCLPlanetLandforms>(A.Get());State->AddSource(Source);const FGuid Id=Source->TerrainType.Id;
    if(!State->Include(Id,true)){AddError(State->Error);return false;}
    const auto Panel=SNew(SCLPlanetLandforms).Session(State);Panel->ToggleDetails(Id);auto& Row=Panel->Controls.FindChecked(Id);
    auto Edit=[&](TFunction<void(FCLLandformPaletteEntry&)> Change)
    {Change(*reinterpret_cast<FCLLandformPaletteEntry*>(Row.Draft->GetStructMemory()));Row.Details->GetOnFinishedChangingPropertiesDelegate().Broadcast(FPropertyChangedEvent(nullptr));};
    const auto Before=A->Recipe;Edit([](auto& P){auto& D=P.Deposits.AddDefaulted_GetRef();D.Unit.SurfaceId=TEXT("StrongRock");});
    if(!TestEqual(TEXT("Native row details commit a new deposit"),State->Entry(Id)->Deposits.Num(),Before.RegionGeneration.Palette[0].Deposits.Num()+1))return false;
    const FGuid First=State->Entry(Id)->Deposits.Last().Unit.Id;
    TestTrue(TEXT("Explicit addition assigns a stable physical identity"),First.IsValid());
    TestEqual(TEXT("New material resolves its revision atomically"),State->Entry(Id)->Deposits.Last().Unit.SurfaceRevision,1);
    TestEqual(TEXT("Editing draft retains the accepted stable identity"),reinterpret_cast<FCLLandformPaletteEntry*>(Row.Draft->GetStructMemory())->Deposits.Last().Unit.Id,First);
    Edit([](auto& P){const auto Copy=P.Deposits.Last();P.Deposits.Add(Copy);});
    TestTrue(TEXT("Duplicating creates a distinct physical deposit"),State->Entry(Id)->Deposits.Last().Unit.Id!=First);
    GEditor->UndoTransaction();Panel->SyncControls();TestEqual(TEXT("One Undo removes one duplicated deposit"),State->Entry(Id)->Deposits.Last().Unit.Id,First);
    const auto Good=A->Recipe;Edit([](auto& P){P.Deposits.Last().ThicknessMeters=0;});
    TestTrue(TEXT("Rejected details preserve the accepted recipe"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Good,&A->Recipe,0));
    TestTrue(TEXT("Rejected draft is restored"),reinterpret_cast<FCLLandformPaletteEntry*>(Row.Draft->GetStructMemory())->Deposits.Last().ThicknessMeters>0);
    State->ToggleSolo(Id);FString E;const auto Plan=FCLPlanetCompiler::Compile(State->PreviewRecipe(),E);if(!Plan){AddError(E);return false;}
    TestTrue(TEXT("Solo retains the actual shared deposit program"),Plan->MaterialLayers&&Plan->MaterialLayers->HasDeposits());
    TestTrue(TEXT("Editing the planet never rewrites its source definition"),FCLTerrainType::StaticStruct()->CompareScriptStruct(&OriginalSource,&Source->TerrainType,0));
    State->Include(Id,false);Panel->SyncControls();TestFalse(TEXT("Excluded row disables all deposit details"),Row.DetailsBox->IsEnabled());
    return !HasAnyErrors();
}
#endif

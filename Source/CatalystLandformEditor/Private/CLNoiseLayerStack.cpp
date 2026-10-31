#include "CLNoiseLayerStack.h"
#include "CLTerrainEditing.h"
#include "Stamps/CLStampLibrary.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyHandle.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Misc/AutomationTest.h"
#include "Editor.h"

namespace
{
// Contextual guide pickers stay local to this layer editor, not global property layouts.
class FCLGuideReferenceDetails : public IPropertyTypeCustomization
{
    TWeakPtr<FCLTerrainEditing> Session;
public:
    explicit FCLGuideReferenceDetails(TSharedPtr<FCLTerrainEditing> InSession) : Session(InSession) {}
    virtual void CustomizeHeader(TSharedRef<IPropertyHandle> Handle, FDetailWidgetRow& Row, IPropertyTypeCustomizationUtils&) override
    {
        Row.NameContent()[Handle->CreatePropertyNameWidget()];
        Row.ValueContent().MinDesiredWidth(150)[SNew(STextBlock).Font(IPropertyTypeCustomizationUtils::GetRegularFont())
            .Text_Lambda([Handle]
            {
                uint8 Mode = 0; FName Lower, Upper; double Fraction = 0, Offset = 0;
                Handle->GetChildHandle(TEXT("Mode"))->GetValue(Mode);
                Handle->GetChildHandle(TEXT("Guide"))->GetValue(Lower);
                Handle->GetChildHandle(TEXT("UpperGuide"))->GetValue(Upper);
                Handle->GetChildHandle(TEXT("Fraction"))->GetValue(Fraction);
                Handle->GetChildHandle(TEXT("OffsetMeters"))->GetValue(Offset);
                return FText::FromString(Mode == 0 ? FString::Printf(TEXT("%s %+.2f m"), Lower.IsNone() ? TEXT("Sky") : *Lower.ToString(), Offset) :
                    FString::Printf(TEXT("%s → %s · %.1f%%%s"), *Lower.ToString(), *Upper.ToString(), Fraction * 100, Mode == 2 ? TEXT(" delta") : TEXT("")));
            })];
    }
    virtual void CustomizeChildren(TSharedRef<IPropertyHandle> Handle, IDetailChildrenBuilder& Children, IPropertyTypeCustomizationUtils&) override
    {
        const auto Mode = Handle->GetChildHandle(TEXT("Mode")).ToSharedRef();
        Mode->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([WeakHandle = TWeakPtr<IPropertyHandle>(Handle), Weak = Session]
        {
            const auto Handle = WeakHandle.Pin(); if (!Handle) return;
            uint8 Value = 0; Handle->GetChildHandle(TEXT("Mode"))->GetValue(Value);
            if (Value == 0)
            {
                Handle->GetChildHandle(TEXT("UpperGuide"))->SetValue(NAME_None);
                Handle->GetChildHandle(TEXT("Fraction"))->SetValue(0.0);
            }
            else if (auto S = Weak.Pin())
            {
                FName Guide, Upper;
                Handle->GetChildHandle(TEXT("Guide"))->GetValue(Guide); Handle->GetChildHandle(TEXT("UpperGuide"))->GetValue(Upper);
                if (Guide.IsNone() && !S->Context.Strata.Stratum.IsEmpty()) Handle->GetChildHandle(TEXT("Guide"))->SetValue(S->Context.Strata.Stratum[0].Name);
                if (Upper.IsNone() && !S->Context.Strata.Stratum.IsEmpty()) Handle->GetChildHandle(TEXT("UpperGuide"))->SetValue(S->Context.Strata.Stratum.Last().Name);
            }
        }));
        for (const FName Field : {FName(TEXT("Mode")), FName(TEXT("Guide")), FName(TEXT("UpperGuide")), FName(TEXT("Fraction")), FName(TEXT("OffsetMeters"))})
        {
            auto Property = Handle->GetChildHandle(Field).ToSharedRef();
            auto Visible = TAttribute<EVisibility>::CreateLambda([Mode, Field]
            { uint8 Value = 0; Mode->GetValue(Value); return Value == 0 && (Field == TEXT("UpperGuide") || Field == TEXT("Fraction")) ? EVisibility::Collapsed : EVisibility::Visible; });
            if (Field != TEXT("Guide") && Field != TEXT("UpperGuide")) { Children.AddProperty(Property).Visibility(Visible); continue; }
            Children.AddCustomRow(Property->GetPropertyDisplayName()).NameContent()[Property->CreatePropertyNameWidget()]
                .Visibility(Visible).ValueContent().MinDesiredWidth(130)[SNew(SComboButton)
                    .ButtonContent()[SNew(STextBlock).Font(IPropertyTypeCustomizationUtils::GetRegularFont()).Text_Lambda([Property]
                    { FName Value; Property->GetValue(Value); return FText::FromName(Value); })]
                    .OnGetMenuContent_Lambda([Weak = Session, Property]
                    {
                        FMenuBuilder Menu(true, nullptr);
                        auto Entry = [&](FName Name)
                        { Menu.AddMenuEntry(FText::FromName(Name), FText(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([Property, Name] { Property->SetValue(Name); }))); };
                        Entry(NAME_None);
                        if (auto S = Weak.Pin()) for (const auto& Guide : S->Context.Strata.Stratum) Entry(Guide.Name);
                        return Menu.MakeWidget();
                    })];
        }
    }
};
class FCLDistributionDetails : public IPropertyTypeCustomization
{
public:
    void CustomizeHeader(TSharedRef<IPropertyHandle> H,FDetailWidgetRow& Row,IPropertyTypeCustomizationUtils&)override
    {Row.NameContent()[H->CreatePropertyNameWidget()];Row.ValueContent()[H->CreatePropertyValueWidget()];}
    void CustomizeChildren(TSharedRef<IPropertyHandle> H,IDetailChildrenBuilder& Children,IPropertyTypeCustomizationUtils&)override
    {
        uint32 Count=0;H->GetNumChildren(Count);
        for(uint32 I=0;I<Count;++I)
        {
            const auto P=H->GetChildHandle(I).ToSharedRef();
            if(P->GetProperty()->GetFName()!=TEXT("CollectionId")){Children.AddProperty(P);continue;}
            Children.AddCustomRow(FText::FromString(TEXT("Collection"))).NameContent()[P->CreatePropertyNameWidget()]
                .ValueContent().MinDesiredWidth(150)[SNew(SComboButton)
                    .ButtonContent()[SNew(STextBlock).Font(IPropertyTypeCustomizationUtils::GetRegularFont()).Text_Lambda([P]{FName V;P->GetValue(V);return FText::FromName(V);})]
                    .OnGetMenuContent_Lambda([P,H]
                    {
                        FMenuBuilder Menu(true,nullptr);
                        if(auto* Library=UCLStampLibraryAsset::LoadConfigured())for(const auto& C:Library->Library.Collections)
                        {
                            const FName Id=C.Id;const int32 Revision=C.Revision;
                            Menu.AddMenuEntry(FText::FromString(C.Name+TEXT(" / v")+LexToString(Revision)),FText::FromString(C.Description),FSlateIcon(),
                                FUIAction(FExecuteAction::CreateLambda([H,Id,Revision]
                                {
                                    TArray<void*> Raw;H->AccessRawData(Raw);if(Raw.Num()!=1||!Raw[0])return;
                                    auto Updated=*static_cast<FCLStampDistributionSettings*>(Raw[0]);
                                    Updated.CollectionId=Id;Updated.CollectionRevision=Revision;
                                    FString Text;FCLStampDistributionSettings::StaticStruct()->ExportText(Text,&Updated,nullptr,nullptr,PPF_None,nullptr);
                                    // One property notification/undo; never compile an ID with the old revision.
                                    H->SetValueFromFormattedString(Text);
                                })));
                        }
                        return Menu.MakeWidget();
                    })];
        }
    }
};

}

void SCLNoiseLayerStack::Construct(const FArguments& Args)
{
    Session = Args._Session; OnPreviewChanged = Args._OnPreviewChanged;
    FDetailsViewArgs ViewArgs; ViewArgs.bAllowSearch = false; ViewArgs.bHideSelectionTip = true;
    ViewArgs.bUpdatesFromSelection = false; ViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    FStructureDetailsViewArgs StructArgs;
    Details = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateStructureDetailView(ViewArgs, StructArgs, TSharedPtr<FStructOnScope>(), FText::FromString(TEXT("Layer settings")));
    Details->GetDetailsView()->RegisterInstancedCustomPropertyTypeLayout(TEXT("CLHeightReference"), FOnGetPropertyTypeCustomizationInstance::CreateLambda([S = Session]
        { return MakeShared<FCLGuideReferenceDetails>(S); }));
    Details->GetDetailsView()->RegisterInstancedCustomPropertyTypeLayout(TEXT("CLStampDistributionSettings"),FOnGetPropertyTypeCustomizationInstance::CreateLambda([]{return MakeShared<FCLDistributionDetails>();}));
    Details->GetOnFinishedChangingPropertiesDelegate().AddSPLambda(this, [this](const FPropertyChangedEvent& Event)
    {
        if (!Draft || Event.ChangeType & EPropertyChangeType::Interactive) return;
        // Interactive property changes touch only the draft; one finished edit is one undo.
        if (!Session->SetLayer(*reinterpret_cast<const FCLNoiseLayer*>(Draft->GetStructMemory())))
            Session->Error = TEXT("Edit not applied: ") + Session->Error;
    });
    auto Action = [this](const TCHAR* Text, TFunction<void()> Run, TFunction<bool()> Enabled)
    {
        return SNew(SButton).ContentPadding(FMargin(7, 3)).Text(FText::FromString(Text))
            .IsEnabled_Lambda(MoveTemp(Enabled)).OnClicked_Lambda([Run = MoveTemp(Run)] { Run(); return FReply::Handled(); });
    };
    auto HasSelection = [this] { return Session->Asset->TerrainType.NoiseLayers.IsValidIndex(Session->Selected); };
    ChildSlot[SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(TEXT("Terrain layers"))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 0)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("Layers run from top to bottom. Select a layer to edit it. Solo isolates noise or stamps; terracing keeps its upstream terrain.")))]
        + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()[SNew(SComboButton).IsEnabled_Lambda([this]{return Session->Asset->TerrainType.NoiseLayers.Num()<32;})
                .ButtonContent()[SNew(STextBlock).Text(FText::FromString(TEXT("Add layer")))]
                .OnGetMenuContent_Lambda([this]
                {
                    FMenuBuilder Menu(true,nullptr);
                    for(const auto Op:{ECLTerrainOperation::Noise,ECLTerrainOperation::Terrace,ECLTerrainOperation::StampDistribution})
                    {
                        const TCHAR* Name=Op==ECLTerrainOperation::Noise?TEXT("Noise"):Op==ECLTerrainOperation::Terrace?TEXT("Terracing"):TEXT("Stamp distribution");
                        Menu.AddMenuEntry(FText::FromString(Name),FText(),FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([this,Op]{Session->Add(Op);})));
                    }
                    return Menu.MakeWidget();
                })]
            + SHorizontalBox::Slot().AutoWidth().Padding(3, 0)[Action(TEXT("Duplicate"), [this] { Session->Duplicate(); }, [this, HasSelection] { return HasSelection() && Session->Asset->TerrainType.NoiseLayers.Num() < 32; })]
            + SHorizontalBox::Slot().AutoWidth()[Action(TEXT("Remove"), [this] { Session->Remove(); }, HasSelection)]
            + SHorizontalBox::Slot().AutoWidth().Padding(3, 0)[Action(TEXT("↑"), [this] { Session->Move(-1); }, [this] { return Session->Selected > 0; })]
            + SHorizontalBox::Slot().AutoWidth()[Action(TEXT("↓"), [this] { Session->Move(1); }, [this, HasSelection] { return HasSelection() && Session->Selected + 1 < Session->Asset->TerrainType.NoiseLayers.Num(); })]]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 0)[SNew(SBox).HeightOverride(150)[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(Rows, SVerticalBox)]]]
        + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SSeparator)]
        + SVerticalBox::Slot().FillHeight(1)[Details->GetWidget().ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FLinearColor(1, .55, .25))
            .Text_Lambda([this] { return FText::FromString(Session->Error); })]];
}
void SCLNoiseLayerStack::Tick(const FGeometry& G, double T, float D)
{
    SCompoundWidget::Tick(G, T, D);
    if (bRefresh) { bRefresh = false; Rebuild(); }
}
void SCLNoiseLayerStack::Rebuild()
{
    Session->Reconcile(); Rows->ClearChildren();
    const auto& Layers = Session->Asset->TerrainType.NoiseLayers;
    for (int32 I = 0; I < Layers.Num(); ++I)
        Rows->AddSlot().AutoHeight().Padding(0, 2)[SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SCheckBox)
                .ToolTipText(FText::FromString(TEXT("Include this layer in the saved terrain definition")))
                .IsChecked_Lambda([this, I] { return Session->Asset->TerrainType.NoiseLayers.IsValidIndex(I) && Session->Asset->TerrainType.NoiseLayers[I].bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this, I](ECheckBoxState State) { Session->Enable(I, State == ECheckBoxState::Checked); })]
            + SHorizontalBox::Slot().FillWidth(1).Padding(5, 0)[SNew(SButton)
                .ButtonColorAndOpacity_Lambda([this, I] { return Session->Selected == I ? FLinearColor(.12, .34, .5) : FLinearColor(.12, .13, .15); })
                .Text(FText::FromString(FString::Printf(TEXT("%d. %s"), I + 1, *Layers[I].Name.ToString())))
                .OnClicked_Lambda([this, I] { Session->Selected = I; Session->Error.Reset(); Refresh(); return FReply::Handled(); })]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SCheckBox)
                .ToolTipText(FText::FromString(TEXT("Noise: isolate over centre height. Terrace: include upstream terrain and omit downstream layers. Saved settings and planet are unchanged")))
                .IsChecked_Lambda([this, I] { return Session->Solo == I ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this, I](ECheckBoxState State)
                { Session->SetSolo(State == ECheckBoxState::Checked ? I : INDEX_NONE); OnPreviewChanged.ExecuteIfBound(); })
                [SNew(STextBlock).Text(FText::FromString(TEXT("Solo")))]]];
    if (Layers.IsEmpty()) Rows->AddSlot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("Add a layer to begin.")))];
    Draft.Reset();
    if (Layers.IsValidIndex(Session->Selected))
    {
        Draft = MakeShared<FStructOnScope>(FCLNoiseLayer::StaticStruct());
        *reinterpret_cast<FCLNoiseLayer*>(Draft->GetStructMemory()) = Layers[Session->Selected];
    }
    Details->SetStructureData(Draft);
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLNoiseLayerDraftTest, "Catalyst.Landform.Designer.LayerPropertyDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLNoiseLayerDraftTest::RunTest(const FString&)
{
    TStrongObjectPtr<UCLTerrainTypeAsset> Asset(NewObject<UCLTerrainTypeAsset>(GetTransientPackage(), NAME_None, RF_Transactional));
    Asset->TerrainType.Id = FGuid::NewGuid(); Asset->TerrainType.NoiseLayers = FCLPlanetCompiler::RollingPlains().NoiseLayers;
    auto Session = MakeShared<FCLTerrainEditing>(); Session->Open(Asset.Get());
    const auto Stack = SNew(SCLNoiseLayerStack).Session(Session); Stack->Rebuild();
    auto& Draft = *reinterpret_cast<FCLNoiseLayer*>(Stack->Draft->GetStructMemory());
    const double Original = Draft.Persistence;
    auto* Property = FCLNoiseLayer::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FCLNoiseLayer, Persistence));
    Draft.Persistence = .63;
    FPropertyChangedEvent Interactive(Property, EPropertyChangeType::Interactive);
    Stack->Details->GetOnFinishedChangingPropertiesDelegate().Broadcast(Interactive);
    TestEqual(TEXT("Interactive draft changes do not mutate source or trigger preview generation"), Asset->TerrainType.NoiseLayers[0].Persistence, Original);
    FPropertyChangedEvent Finished(Property, EPropertyChangeType::ValueSet);
    Stack->Details->GetOnFinishedChangingPropertiesDelegate().Broadcast(Finished);
    TestEqual(TEXT("Finished property event commits the selected layer"), Asset->TerrainType.NoiseLayers[0].Persistence, .63);
    GEditor->UndoTransaction();
    TestEqual(TEXT("One undo reverses the complete property gesture"), Asset->TerrainType.NoiseLayers[0].Persistence, Original);
    Draft.Persistence = 2;
    Stack->Details->GetOnFinishedChangingPropertiesDelegate().Broadcast(Finished);
    TestEqual(TEXT("Rejected draft preserves the definition"), Asset->TerrainType.NoiseLayers[0].Persistence, Original);
    TestTrue(TEXT("Draft error explicitly says edit was not applied"), Session->Error.StartsWith(TEXT("Edit not applied:")));
    Draft.Persistence = Original;
    Stack->Details->GetOnFinishedChangingPropertiesDelegate().Broadcast(Finished);
    TestTrue(TEXT("Correcting to the stored value clears the error"), Session->Error.IsEmpty());
    TestTrue(TEXT("Terracing can be added from the stack session"), Session->Add(ECLTerrainOperation::Terrace));
    Stack->Rebuild();
    auto& TerraceDraft = *reinterpret_cast<FCLNoiseLayer*>(Stack->Draft->GetStructMemory());
    const double OriginalSoftness = TerraceDraft.Terrace.Softness;
    TerraceDraft.Terrace.Softness = .61;
    auto* SoftnessProperty = FCLTerraceSettings::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FCLTerraceSettings, Softness));
    FPropertyChangedEvent TerraceInteractive(SoftnessProperty, EPropertyChangeType::Interactive);
    Stack->Details->GetOnFinishedChangingPropertiesDelegate().Broadcast(TerraceInteractive);
    TestEqual(TEXT("Terrace slider gesture remains a draft"), Asset->TerrainType.NoiseLayers.Last().Terrace.Softness, OriginalSoftness);
    FPropertyChangedEvent TerraceFinished(SoftnessProperty, EPropertyChangeType::ValueSet);
    Stack->Details->GetOnFinishedChangingPropertiesDelegate().Broadcast(TerraceFinished);
    TestEqual(TEXT("Terrace softness commits through the real Details delegate"), Asset->TerrainType.NoiseLayers.Last().Terrace.Softness, .61);
    GEditor->UndoTransaction();
    TestEqual(TEXT("Terrace property edit is one undo"), Asset->TerrainType.NoiseLayers.Last().Terrace.Softness, OriginalSoftness);
    TerraceDraft.Terrace.Softness = 2;
    Stack->Details->GetOnFinishedChangingPropertiesDelegate().Broadcast(TerraceFinished);
    TestEqual(TEXT("Invalid terrace draft leaves source intact"), Asset->TerrainType.NoiseLayers.Last().Terrace.Softness, OriginalSoftness);
    Session->Add(ECLTerrainOperation::StampDistribution);Stack->Rebuild();
    TestEqual(TEXT("New stamp layers select the published Motion Forge collection"),Asset->TerrainType.NoiseLayers.Last().Distribution.CollectionRevision,2);
    auto& DistributionDraft=*reinterpret_cast<FCLNoiseLayer*>(Stack->Draft->GetStructMemory());
    auto* DensityProperty=FCLStampDistributionSettings::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FCLStampDistributionSettings,DensityPerSquareKilometer));
    DistributionDraft.Distribution.DensityPerSquareKilometer=31;
    FPropertyChangedEvent DistributionFinished(DensityProperty,EPropertyChangeType::ValueSet);
    Stack->Details->GetOnFinishedChangingPropertiesDelegate().Broadcast(DistributionFinished);
    TestEqual(TEXT("Distribution density commits through actual Details"),Asset->TerrainType.NoiseLayers.Last().Distribution.DensityPerSquareKilometer,31.);
    GEditor->UndoTransaction();
    TestEqual(TEXT("Distribution property edit is one undo"),Asset->TerrainType.NoiseLayers.Last().Distribution.DensityPerSquareKilometer,24.);

    return true;
}
#endif

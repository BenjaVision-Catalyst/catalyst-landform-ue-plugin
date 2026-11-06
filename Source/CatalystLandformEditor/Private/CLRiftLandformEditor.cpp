#include "CLRiftLandformEditor.h"
#include "Model/CLTerrainType.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Styling/AppStyle.h"
class FCLRiftCharacterDetails:public IPropertyTypeCustomization
{
    TUniquePtr<FScopedTransaction> Gesture;
public:
    void CustomizeHeader(TSharedRef<IPropertyHandle> H,FDetailWidgetRow& Row,IPropertyTypeCustomizationUtils&)override
    {
        TArray<UObject*> Objects;H->GetOuterObjects(Objects);TWeakObjectPtr<UCLTerrainTypeAsset> Source=Objects.Num()==1?Cast<UCLTerrainTypeAsset>(Objects[0]):nullptr;
        auto C=H->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCLRiftLandform,Character)).ToSharedRef();
        auto Seed=H->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCLRiftLandform,Seed)).ToSharedRef();
        Row.NameContent()[H->CreatePropertyNameWidget()];
        Row.ValueContent().MinDesiredWidth(220)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SSlider).Value_Lambda([C]{double V=0;C->GetValue(V);return float(V);})
                .OnMouseCaptureBegin_Lambda([this]{Gesture=MakeUnique<FScopedTransaction>(NSLOCTEXT("CL","RiftCharacter","Change rift Character"));})
                .OnValueChanged_Lambda([C,Source](float V){C->SetValue(double(V),EPropertyValueSetFlags::InteractiveChange);if(Source.IsValid())Source->OnTerrainChanged.Broadcast();})
                .OnMouseCaptureEnd_Lambda([this,C]{double V=0;C->GetValue(V);C->NotifyPostChange(EPropertyChangeType::ValueSet);C->NotifyFinishedChangingProperties();Gesture.Reset();})]
            +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SBox).WidthOverride(28)[SNew(STextBlock).Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont")).Text_Lambda([C]{double V=0;C->GetValue(V);return FText::AsNumber(FMath::RoundToInt(V*100));})]]
            +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(FAppStyle::Get(),"NoBorder").ToolTipText(NSLOCTEXT("CL","RiftReseed","Reseed"))
                .OnClicked_Lambda([Seed]{int32 V=0;Seed->GetValue(V);Seed->SetValue(int32(uint32(V)*1664525U+1013904223U));return FReply::Handled();})
                [SNew(SImage).Image(FAppStyle::GetBrush("Icons.Refresh"))]]];
    }
    void CustomizeChildren(TSharedRef<IPropertyHandle> H,IDetailChildrenBuilder& Children,IPropertyTypeCustomizationUtils&)override
    {
        uint32 Count=0;H->GetNumChildren(Count);for(uint32 I=0;I<Count;++I)
        {auto P=H->GetChildHandle(I).ToSharedRef();const FName N=P->GetProperty()->GetFName();if(N!=TEXT("AlgorithmVersion")&&N!=TEXT("Character"))Children.AddProperty(P);}
    }
};
class FCLRiftAssetDetails:public IDetailCustomization
{
public:
    void CustomizeDetails(IDetailLayoutBuilder& B)override
    {
        auto T=B.GetProperty(GET_MEMBER_NAME_CHECKED(UCLTerrainTypeAsset,TerrainType));B.HideProperty(T);
        B.EditCategory(TEXT("Landform")).AddProperty(T->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCLTerrainType,Rift)));
    }
};
TSharedRef<SWidget> CLMakeRiftLandformEditor(UCLTerrainTypeAsset* Asset)
{
    FDetailsViewArgs Args;Args.bAllowSearch=false;Args.bHideSelectionTip=true;Args.NameAreaSettings=FDetailsViewArgs::HideNameArea;
    auto View=FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(Args);
    View->RegisterInstancedCustomPropertyTypeLayout(TEXT("CLRiftLandform"),FOnGetPropertyTypeCustomizationInstance::CreateLambda([]{return MakeShared<FCLRiftCharacterDetails>();}));
    View->RegisterInstancedCustomPropertyLayout(UCLTerrainTypeAsset::StaticClass(),FOnGetDetailCustomizationInstance::CreateLambda([]{return MakeShared<FCLRiftAssetDetails>();}));
    View->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& P){if(P.Property.GetFName()==TEXT("Rift"))return true;for(auto* Parent:P.ParentProperties)if(Parent->GetFName()==TEXT("Rift"))return true;return false;}));
    View->SetObject(Asset);return View;
}

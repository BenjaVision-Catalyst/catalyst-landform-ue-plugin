#pragma once
#include "CoreMinimal.h"
#include "ScopedTransaction.h"
#include "Model/CLTerrainType.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h"
class FScopedTransaction;

/** Planet-owned authoring, with eye/Solo kept outside the saved recipe. */
class FCLPlanetLandforms : public TSharedFromThis<FCLPlanetLandforms>
{
public:
    explicit FCLPlanetLandforms(UCLPlanetRecipeAsset* InAsset):Asset(InAsset){}
    UCLPlanetRecipeAsset* Asset;
    TMap<FGuid,TStrongObjectPtr<UCLTerrainTypeAsset>> Sources;
    TSet<FGuid> Hidden;
    FGuid Solo;
    FString Error;
    FSimpleDelegate PreviewChanged;
    void Discover();
    void AddSource(UCLTerrainTypeAsset*);
    TArray<FGuid> Rows()const;
    const FCLLandformPaletteEntry* Entry(FGuid)const;
    FString Name(FGuid)const;
    FString Description(FGuid)const;
    bool Included(FGuid)const;
    bool Visible(FGuid)const;
    double Character(FGuid)const;
    bool Include(FGuid,bool);
    bool SetCharacter(FGuid,double);
    bool Reseed(FGuid);
    bool SetValues(FGuid,const FCLLandformPaletteEntry&);
    bool ToggleEye(FGuid);
    bool ToggleSolo(FGuid);
    void BeginGesture(FGuid);
    void EndGesture();
    FCLPlanetRecipe PreviewRecipe()const;
    FGuid PreviewSolo()const;
private:
    TUniquePtr<FScopedTransaction> Gesture;
    bool Commit(FCLPlanetRecipe,const FText&,UCLTerrainTypeAsset* Source=nullptr);
};
class SCLPlanetLandforms : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SCLPlanetLandforms){} SLATE_ARGUMENT(TSharedPtr<FCLPlanetLandforms>,Session) SLATE_END_ARGS()
    void Construct(const FArguments&);
    void Refresh();
    void Focus(FGuid);
    void SyncControls();
    void ToggleDetails(FGuid);
    struct FRowControls
    {
        FString Name,Description;
        TSharedPtr<class SCheckBox> Include;
        TSharedPtr<class SSlider> Knob;
        TSharedPtr<class SButton> Disclosure,Eye,Reseed,Solo;
        TSharedPtr<class SBox> DetailsBox;
        TSharedPtr<class IStructureDetailsView> Details;
        TSharedPtr<class FStructOnScope> Draft;
    };
    TMap<FGuid,FRowControls> Controls;
private:
    TSharedPtr<FCLPlanetLandforms> Session;
    TSet<FGuid> Expanded;
    bool ApplyingDetails=false;
    TSharedPtr<class SVerticalBox> Rows;
    TSharedPtr<class SScrollBox> Scroll;
};

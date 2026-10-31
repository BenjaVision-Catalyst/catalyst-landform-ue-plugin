#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
class FCLTerrainEditing;
class IStructureDetailsView;
class FStructOnScope;
class SVerticalBox;
class FCLNoiseLayerDraftTest;
/** Ordered terrain layers with a copied, validated property draft for the selection. */
class CATALYSTLANDFORMEDITOR_API SCLNoiseLayerStack : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SCLNoiseLayerStack) {} SLATE_ARGUMENT(TSharedPtr<FCLTerrainEditing>, Session)
        SLATE_EVENT(FSimpleDelegate, OnPreviewChanged)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    void Refresh() { bRefresh = true; }
    virtual void Tick(const FGeometry&, double, float) override;
private:
    friend class FCLNoiseLayerDraftTest;
    TSharedPtr<FCLTerrainEditing> Session;
    FSimpleDelegate OnPreviewChanged;
    TSharedPtr<SVerticalBox> Rows;
    TSharedPtr<IStructureDetailsView> Details;
    TSharedPtr<FStructOnScope> Draft;
    bool bRefresh = true;
    void Rebuild();
};

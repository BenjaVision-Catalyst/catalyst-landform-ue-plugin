#pragma once
#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "ProceduralMeshComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "Model/CLPlanetRecipe.h"
#include "CLRadialGuideEditing.h"
#include "CLGlobePlacement.h"
#include "Model/CLRegionGeneration.h"
#include "UnrealWidgetFwd.h"
void CLRegisterGlobePlacementMode();
void CLUnregisterGlobePlacementMode();
DECLARE_DELEGATE_TwoParams(FOnCLGlobePlacementCommitted,const FCLGlobePlacementTarget&,bool);
DECLARE_DELEGATE_OneParam(FOnCLGlobeRegionSelected,FGuid);
class FCLGlobeClient;
class FPreviewScene;
struct CATALYSTLANDFORMEDITOR_API FCLGlobeBuffers
{
    TArray<FVector> Vertices,Normals,SourceDirections;
    TArray<int32> Triangles;
    TArray<FVector2D> UV,UV1,UV2,UV3;
    TSharedPtr<const FCLCompiledSurfaceLibrary> Surfaces;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    TArray<FCLGeneratedRegionBoundary> RegionBoundaries;
    TArray<FVector> RegionCentres;
    double DisplayRadius=50000;
    bool bCancelled=false;
    static constexpr int32 PreviewResolution=512;
    /** Bound worker scratch to 20,480 field samples, regardless of mesh resolution. */
    static constexpr int32 SampleVertexBatch=4096;
    static FCLGlobeBuffers Build(const FCLCompiledPlanetRecipe& Plan,int32 Resolution=PreviewResolution,const TAtomic<bool>* Stop=nullptr);
};
/** Coalesced worker-built globe. Shared field; separate navigation from the cutaway. */
class CATALYSTLANDFORMEDITOR_API SCLPlanetPreview : public SEditorViewport
{
public:
    SLATE_BEGIN_ARGS(SCLPlanetPreview){} SLATE_EVENT(FOnCLGlobePlacementCommitted,OnPlacementCommitted) SLATE_EVENT(FOnCLGlobeRegionSelected,OnRegionSelected) SLATE_END_ARGS()
    void Construct(const FArguments&);
    virtual ~SCLPlanetPreview();
    void Rebuild(TSharedPtr<const FCLCompiledPlanetRecipe> Plan);
    void Cancel();
    void SetAuthoringRecipe(const FCLPlanetRecipe& Recipe);
    void Select(FCLGlobeSelection Key);
    FCLGlobeSelection Selection() const;
    FString SelectionName() const;
    FString PlacementHint() const;
    void SetPlacementMode(ECLGlobePlacementMode Mode);
    ECLGlobePlacementMode PlacementMode() const;
    void SetRegionInspection(bool Inspect);
    bool IsInspectingRegions() const;
    void SelectGeneratedRegion(FGuid Id);
    FGuid GeneratedSelection() const;
    void SetWalkPicking(bool Enabled);
    bool IsWalkPicking() const;
    void SetWalkLocation(FVector Direction);
    FVector WalkLocation() const;
    void SetDraftMode(bool Enabled){if(bDraft!=Enabled){++Epoch;if(Stop)Stop->Store(true);}bDraft=Enabled;}
    uint64 PublishedBuilds() const{return AppliedBuilds;}
    bool IsDraftMode() const{return bDraft;}
    bool HasDraftMesh() const{return bLastPublishedDraft;}
    void Frame();
    void Dolly(double Steps);
    void SetGuides(const FCLRadialGuideSet& Guides,bool Visible);
    double NormalizedAtScreenY(double Y) const;
    const TArray<FCLGuideIndicator>& Indicators() const;
    const TArray<TArray<FVector2D>>& Rings() const;
    bool IsUpdating() const;
protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
    virtual void Tick(const FGeometry&,double,float) override;
    virtual FReply OnPreviewMouseButtonDown(const FGeometry&,const FPointerEvent&) override;
    virtual FReply OnMouseMove(const FGeometry&,const FPointerEvent&) override;
    virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent&) override;
    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override;
    virtual FReply OnPreviewKeyDown(const FGeometry&,const FKeyEvent&) override;
    virtual FReply OnMouseWheel(const FGeometry&,const FPointerEvent&) override;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FCLGlobeWalkPickingTest;
    friend class FCLTectonicGlobeTest;
    friend class FCLGlobeGuidesTest;
    friend class FCLGlobePointerTest;
    friend class FCLGlobePlacementPointerTest;
    friend class FCLGlobeLightingTest;
    friend class FCLGlobeGeneratedInspectionTest;
    friend class FCLSurfaceGlobeTest;
#endif
    void Pump();
    void CancelPlacementDrag();
    FVector2D PointerPosition(const FGeometry& Geometry,const FPointerEvent& Event) const;
    FOnCLGlobePlacementCommitted OnPlacementCommitted;
    FOnCLGlobeRegionSelected OnRegionSelected;
    FGuid ClickGeneratedRegion;
    EAxisList::Type DragAxis=EAxisList::None;
    FCLGlobePlacementTarget DragStart;
    FVector DragStartPoint,DragOrigin;
    double DragScale=1;
    FCLGlobeSelection ClickSelection;
    FVector2D MouseDown;
    bool bOrbitDrag=false,bPlacementMoved=false,bWalkPicking=false;
    FVector ClickWalkLocation=FVector::ZeroVector;
    TSharedPtr<const FCLCompiledPlanetRecipe> BuildingPlan;
    TUniquePtr<FPreviewScene> Scene;
    TSharedPtr<FCLGlobeClient> Client;
    TStrongObjectPtr<UProceduralMeshComponent> Mesh;
    TSharedPtr<const FCLCompiledPlanetRecipe> Pending;
    TFuture<FCLGlobeBuffers> Task;
    TSharedPtr<TAtomic<bool>,ESPMode::ThreadSafe> Stop;
    uint64 Revision=0,TaskRevision=0;
    bool Framed=false,bDraft=false,bTaskDraft=false,bLastPublishedDraft=false;
    uint64 AppliedBuilds=0,Epoch=0,TaskEpoch=0;
    FVector2D Previous;
    FKey Button;
};

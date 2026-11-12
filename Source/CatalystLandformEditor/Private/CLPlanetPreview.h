#pragma once
#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "CLPreviewMesh.h"
#include "UObject/StrongObjectPtr.h"
#include "Model/CLPlanetRecipe.h"
#include "CLRadialGuideEditing.h"
#include "CLGlobePlacement.h"
#include "Model/CLRegionGeneration.h"
#include "UnrealWidgetFwd.h"
struct FCLWatershedDebugGeometry;
void CLRegisterGlobePlacementMode();
void CLUnregisterGlobePlacementMode();
DECLARE_DELEGATE_TwoParams(FOnCLGlobePlacementCommitted,const FCLGlobePlacementTarget&,bool);
DECLARE_DELEGATE_OneParam(FOnCLGlobeRegionSelected,FGuid);
class FCLGlobeClient;
class FPreviewScene;
class UStaticMeshComponent;
class FCLNativePreview;
// Independent reference tessellation exists only in automation fixtures.
#if WITH_DEV_AUTOMATION_TESTS
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
#endif
/** Native VPP globe with application navigation and authoring overlays. */
class CATALYSTLANDFORMEDITOR_API SCLPlanetPreview : public SEditorViewport
{
public:
    SLATE_BEGIN_ARGS(SCLPlanetPreview){} SLATE_EVENT(FOnCLGlobePlacementCommitted,OnPlacementCommitted) SLATE_EVENT(FOnCLGlobeRegionSelected,OnRegionSelected) SLATE_END_ARGS()
    void Construct(const FArguments&);
    virtual ~SCLPlanetPreview();
    void Rebuild(TSharedPtr<const FCLCompiledPlanetRecipe> Plan,TSharedPtr<const FCLWatershedDebugGeometry> Debug=nullptr,bool PreserveWalk=false);
    bool IsSurfaceReady(const TSharedPtr<const FCLCompiledPlanetRecipe>& Plan)const;
    void SetGlobeGrid(bool Visible);
    bool IsGlobeGridVisible()const;
    void Cancel();
    void SetEvaluationPending(bool Pending);
    void SetAuthoringValid(bool Valid);
    bool CanPick()const{return !bAuthoringInvalid&&!IsUpdating();}
    void SetAuthoringRecipe(const FCLPlanetRecipe& Recipe);
    void Select(FCLGlobeSelection Key);
    FCLGlobeSelection Selection() const;
    FString SelectionName() const;
    FString PlacementHint() const;
    void SetPlacementMode(ECLGlobePlacementMode Mode);
    ECLGlobePlacementMode PlacementMode() const;
    void SetCaveOverlay(bool Enabled);
    bool IsCaveOverlayEnabled()const;
    void SetWatershedOverlay(bool Enabled);
    bool IsWatershedOverlayEnabled() const;
    bool FocusWatershed();
    void SetRegionInspection(bool Inspect);
    bool IsInspectingRegions() const;
    void SelectGeneratedRegion(FGuid Id);
    FGuid GeneratedSelection() const;
    void SetWalkPicking(bool Enabled);
    bool IsWalkPicking() const;
    void SetWalkLocation(FVector Direction);
    FVector WalkLocation() const;
    void SetWalkPoint(FVector BodyMeters);
    TOptional<FVector> WalkPoint()const{return WalkSurfacePoint;}
    void SetDraftMode(bool Enabled){bDraft=Enabled;}
    uint64 PublishedBuilds() const{return AppliedBuilds;}
    bool IsDraftMode() const{return bDraft;}
    bool HasDraftMesh() const{return bLastPublishedDraft;}
#if WITH_DEV_AUTOMATION_TESTS
    void CreateReferenceMeshForTesting();
    FCLNativePreview* NativeForTesting()const{return NativePreview.Get();}
    void OrbitForTesting(FVector2D Delta);
    void FrameDirectionForTesting(FVector Direction,double Zoom=1);
    void FrameSurfaceForTesting(FVector Direction);
    void FrameLandformForTesting(FVector Direction,FVector Along,double ExtentMeters,FVector2D CameraSlope=FVector2D(.90,.45));
#endif
    void SetOceanVisible(bool Visible);
    bool IsOceanVisible() const{return bOceanVisible;}
    void Frame();
    void Dolly(double Steps);
    void SetGuides(const FCLRadialGuideSet& Guides,bool Visible);
    double NormalizedAtScreenY(double Y) const;
    const TArray<FCLGuideIndicator>& Indicators() const;
    const TArray<TArray<FVector2D>>& Rings() const;
    bool IsUpdating() const;
    bool IsApplyingInput()const;
    TOptional<float> InputProgress()const;
    FString InputProgressText()const;
    FString NativePreviewStatus()const;
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
    bool bEvaluationPending=false,bAuthoringInvalid=false,bPressCanPick=true;
#if WITH_DEV_AUTOMATION_TESTS
    friend class FCLWatershedDebugRenderedTest;
    friend class FCLCaveDebugRenderedTest;
    friend class FCLSpatialFamilyEditorTest;
    friend class FCLNativePreviewTest;
    friend class FCLPreviewMaterialRenderedTest;
    friend class FCLNativePreviewRadiiTest;
    friend class FCLTectonicRenderedPortfolioTest;
    friend class FCLContinentalRenderedPortfolioTest;
    friend class FCLGlobePublicationTest;
    friend class FCLGeologyRenderedTest;
    friend class FCLGlobeOceanTest;
    friend class FCLGlobeWalkPickingTest;
    friend class FCLTectonicGlobeTest;
    friend class FCLContinentalGlobeTest;
    friend class FCLGlobeGuidesTest;
    friend class FCLGlobePointerTest;
    friend class FCLGlobePlacementPointerTest;
    friend class FCLGlobeLightingTest;
    friend class FCLGlobeGeneratedInspectionTest;
    friend class FCLSurfaceGlobeTest;
    friend class FCLPreviewPrecisionTest;
#endif
    void Pump();
    void UpdateCaveVolume();
    TSharedPtr<const FCLCompiledPlanetRecipe> CaveInput;
    TUniquePtr<FCLNativePreview> CavePreview;
    void UpdateOcean();
    void UpdateWatershedDebug();
    TSharedPtr<const FCLCompiledPlanetRecipe> PendingDebugPlan,DisplayedDebugPlan;
    TSharedPtr<const FCLWatershedDebugGeometry> PendingWatershedDebug;
    TStrongObjectPtr<UDynamicMeshComponent> DebugWater;
    bool bOceanVisible=false;
    TStrongObjectPtr<UStaticMeshComponent> Ocean;
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
    TOptional<FVector> ClickWalkPoint,WalkSurfacePoint;
    TUniquePtr<FPreviewScene> Scene;
    TUniquePtr<FCLNativePreview> NativePreview;
    TSharedPtr<FCLGlobeClient> Client;
#if WITH_DEV_AUTOMATION_TESTS
    TStrongObjectPtr<UDynamicMeshComponent> Mesh; // Explicit test oracle only.
#endif
    bool Framed=false,bDraft=false,bLastPublishedDraft=false;
    uint64 AppliedBuilds=0;
    FVector2D Previous;
    FKey Button;
};

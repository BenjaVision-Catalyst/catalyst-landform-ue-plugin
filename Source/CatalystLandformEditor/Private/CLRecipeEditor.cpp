#include "CLRecipeAssetTools.h"
#include "CLSectionGeometry.h"
#include "CLSectionNavigation.h"
#include "Framework/Application/IInputProcessor.h"
#include "Layout/WidgetPath.h"
#include "InputKeyEventArgs.h"
#include "CLStrataEditing.h"
#include "CLPlanet.h"
#include "Engine/Selection.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "Widgets/SCanvas.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "SceneView.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLTerrainType.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Toolkits/CFModelEditorToolkit.h"
#include "AdvancedPreviewScene.h"
#include "SEditorViewport.h"
#include "EditorViewportClient.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/Material.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Misc/AutomationTest.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Rendering/DrawElements.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "HAL/IConsoleManager.h"

namespace
{
const FLinearColor GuideLabelColor(.72, .72, .72);
class SCLSectionViewport;
class FCLSectionClient : public FEditorViewportClient
{
public:
    TSharedPtr<const FCLCompiledPlanetRecipe> Plan;
    FCLSectionNavigation Navigation;
    FIntPoint NavigationSize = FIntPoint::ZeroValue;
    void ApplyNavigation()
    {
        FIntPoint Size = Viewport ? Viewport->GetSizeXY() : FIntPoint(1600, 1000);
        if (Size.X < 1 || Size.Y < 1) Size = FIntPoint(1600, 1000);
        NavigationSize = Size; AspectRatio = double(Size.X) / Size.Y;
        const double TanH = FMath::Tan(FMath::DegreesToRadians(double(ViewFOV) * .5));
        const auto Camera = Navigation.Camera(TanH, TanH / AspectRatio);
        SetViewLocation(Camera.Eye); SetViewRotation(Camera.Rotation); SetLookAtLocation(Camera.Target);
        Invalidate();
    }
    virtual void Tick(float) override
    {
        if (Viewport && Viewport->GetSizeXY().X > 0 && Viewport->GetSizeXY().Y > 0 && Viewport->GetSizeXY() != NavigationSize) ApplyNavigation();
    }
    // All camera gestures are object navigation. Never enter editor flight mode.
    virtual bool InputKey(const FInputKeyEventArgs& E) override
    {
        if (E.Event == IE_Pressed && (E.Key == EKeys::MouseScrollUp || E.Key == EKeys::MouseScrollDown))
        { Navigation.Dolly(E.Key == EKeys::MouseScrollUp ? 1 : -1); ApplyNavigation(); }
        return true;
    }
    virtual bool InputAxis(const FInputKeyEventArgs&) override { return true; }
    virtual void MouseMove(FViewport*, int32, int32) override {}
    virtual void CapturedMouseMove(FViewport*, int32, int32) override {}
    virtual bool InputGesture(FViewport*, FInputDeviceId, EGestureEvent Type, const FVector2D& Delta, bool Inverted, uint64) override
    {
        if (Type == EGestureEvent::Magnify) Navigation.Dolly(Delta.X * 6);
        else if (Type == EGestureEvent::Scroll) Navigation.PanBy(Delta * (Inverted ? -10 : 10));
        ApplyNavigation(); return true;
    }
    double Heading = 0;
    double AreaWidthMeters = 0;
    bool bGuides = true;
    FCLStrata Strata;
    FVector2D RightEdge;
    FMatrix ViewProjection;
    FIntRect ViewRect;
    bool bHasProjection = false;
    double StratumHeightMeters(double Normalized) const { return (Normalized - 1) * Plan->RadiusMeters; }
    double NormalizedAtScreenY(double Y) const
    {
        return bHasProjection && Plan ? FCLSectionGeometry::StratumAtScreenY(RightEdge, ViewProjection, Y) : 0;
    }
    struct FIndicator { FString Text; FLinearColor Color; FVector2D Position; int32 Index = INDEX_NONE; };
    TArray<FIndicator> Indicators;
    FCLSectionClient(FPreviewScene* Scene, const TSharedRef<SEditorViewport>& Viewport)
        : FEditorViewportClient(nullptr, Scene, Viewport)
    {
        SetViewMode(VMI_Lit); SetRealtime(false); ViewFOV = FOVAngle = 60;
        ExposureSettings.bFixed = true; ExposureSettings.FixedEV100 = 0;
        SetViewLocation(FVector(8500, -10500, 8000));
        SetViewRotation((FVector(0, 0, -1500) - GetViewLocation()).Rotation());
        SetLookAtLocation(FVector(0, 0, -1500));
        EngineShowFlags.SetGrid(false); EngineShowFlags.SetSelectionOutline(false); EngineShowFlags.SetEyeAdaptation(false);
    }
    double TopHeight() const
    {
        return FCLSectionGeometry::DisplaySize;
    }
    bool GuidePoint(double X, double Y, double Height, FVector& Point) const
    {
        Point = FCLSectionGeometry::StratumPoint(FVector2D(X, Y), 1 + Height / Plan->RadiusMeters);
        return true;
    }
    virtual FLinearColor GetBackgroundColor() const override { return FLinearColor(.075, .09, .11); }
    virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override
    {
        FEditorViewportClient::Draw(View, PDI);
        if (!Plan) return;
        const double W = FCLSectionGeometry::DisplaySize * .5;
        const FLinearColor Frame(.24, .34, .44);
        for (double X : {-W, W}) for (double Y : {-W, W})
        { FVector P; GuidePoint(X, Y, 0, P); PDI->DrawLine(FVector(X, Y, 0) * 100, P, Frame, SDPG_World, 1); }
        for (double Side : {-W, W}) for (int32 I = 0; I < 32; ++I)
        {
            const double A = W * (2.0 * I / 32 - 1), B = W * (2.0 * (I + 1) / 32 - 1);
            FVector P, Q;
            GuidePoint(A, Side, 0, P); GuidePoint(B, Side, 0, Q); PDI->DrawLine(P, Q, Frame, SDPG_World, 1);
            GuidePoint(Side, A, 0, P); GuidePoint(Side, B, 0, Q); PDI->DrawLine(P, Q, Frame, SDPG_World, 1);
        }
        Indicators.Reset();
        // Use the visible right silhouette, rather than the front corner or the
        // viewport border. Every tick stays at its exact projected radial height.
        FVector2D Edge(W, -W);
        double Rightmost = -TNumericLimits<double>::Max();
        for (double X : {-W, W}) for (double Y : {-W, W})
        {
            FVector2D Screen;
            if (View->WorldToPixel(FVector(X, Y, TopHeight()) * 100, Screen) && Screen.X > Rightmost)
            { Rightmost = Screen.X; Edge = FVector2D(X, Y); }
        }
        RightEdge = Edge; ViewProjection = View->ViewMatrices.GetViewProjectionMatrix();
        ViewRect = View->UnscaledViewRect; bHasProjection = true;
        UpdateIndicators();
    }
    void UpdateIndicators()
    {
        if (!Plan || !bHasProjection) return;
        Indicators.Reset();
        auto Indicator = [&](const FVector& P, const FString& Text, FLinearColor Color, int32 Index = INDEX_NONE)
        {
            FVector2D Screen;
            if (FSceneView::ProjectWorldToScreen(P, ViewRect, ViewProjection, Screen)) Indicators.Add({Text, GuideLabelColor,
                FVector2D((Screen.X - ViewRect.Min.X) / FMath::Max(1, ViewRect.Width()),
                    (Screen.Y - ViewRect.Min.Y) / FMath::Max(1, ViewRect.Height())), Index});
        };
        Indicator(FVector(RightEdge.X, RightEdge.Y, 0) * 100, TEXT("Core"), FLinearColor(.9, .7, .5), -2);
        FVector Sky; GuidePoint(RightEdge.X, RightEdge.Y, 0, Sky);
        Indicator(Sky, TEXT("Sky boundary"), FLinearColor(.65, .8, 1), -3);
        if (bGuides) for (int32 I = 0; I < Strata.Stratum.Num(); ++I)
        {
            const auto& G = Strata.Stratum[I]; FVector P;
            if (GuidePoint(RightEdge.X, RightEdge.Y, StratumHeightMeters(G.NormalizedHeight), P))
                Indicator(P, G.Name.ToString(), G.Color, I);
        }
        Indicators.Sort([](const FIndicator& A, const FIndicator& B) { return A.Position.Y < B.Position.Y; });
    }
};

/** A tick or label is a direct manipulator, with one undo transaction per gesture. */
class SCLStratumHandle : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SCLStratumHandle) : _Index(INDEX_NONE), _Label(false) {}
        SLATE_ARGUMENT(TSharedPtr<FCLStrataSession>, Session)
        SLATE_ARGUMENT(TSharedPtr<FCLSectionClient>, Client)
        SLATE_ARGUMENT(TWeakPtr<SWidget>, Overlay)
        SLATE_ARGUMENT(int32, Index)
        SLATE_ARGUMENT(bool, Label)
    SLATE_END_ARGS()
    TSharedPtr<FCLStrataSession> Session;
    TSharedPtr<FCLSectionClient> Client;
    TWeakPtr<SWidget> Overlay;
    TSharedPtr<SInlineEditableTextBlock> Name;
    int32 Index = INDEX_NONE;
    bool bDragging = false;
    FVector2D MouseStart;
    void Construct(const FArguments& Args)
    {
        Session = Args._Session; Client = Args._Client; Overlay = Args._Overlay;
        Index = Args._Index;
        SetToolTipText(FText::FromString(TEXT("Click to select; drag to move; Delete to remove; double-click a label or press F2 to rename")));
        if (Args._Label)
            ChildSlot[SNew(SBorder).Padding(FMargin(3, 1)).BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([this] { return Session->Selected == Index ? FLinearColor(.1, .3, .5, .65) : FLinearColor::Transparent; })
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth()[SAssignNew(Name, SInlineEditableTextBlock)
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10)).DelayedLeftClickEntersEditMode(false)
                        .Text_Lambda([this] { return FText::FromName(Session->Asset->Recipe.Strata.Stratum.IsValidIndex(Index) ?
                            Session->Asset->Recipe.Strata.Stratum[Index].Name : NAME_None); })
                        .ColorAndOpacity(GuideLabelColor)
                        .IsSelected_Lambda([this] { return Session->Selected == Index; })
                        .OnVerifyTextChanged_Lambda([this](const FText& Text, FText& Error)
                        { FString E; const bool bOK = FCLStrataEditing::CanRename(Session->Asset->Recipe.Strata, Index, Text.ToString(), E);
                          Error = FText::FromString(E); return bOK; })
                        .OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type Type)
                        {
                            if (Type == ETextCommit::OnCleared) return;
                            FString Error;
                            if (!FCLStrataEditing::CanRename(Session->Asset->Recipe.Strata, Index, Text.ToString(), Error)) return;
                            Session->Begin(NSLOCTEXT("CL", "RenameStratum", "Rename Stratum"));
                            FCLStrataEditing::Rename(Session->Asset->Recipe, Index, Text.ToString(), Error); Session->Changed(); Session->End();
                        })]
                ]];
        else ChildSlot[SNew(SBorder).Padding(0).BorderImage(FAppStyle::GetBrush("WhiteBrush"))
            .BorderBackgroundColor_Lambda([this] { return Session->Selected == Index ? FLinearColor(.2, .6, 1, .4) : FLinearColor::Transparent; })];
    }
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& E) override
    {
        if (E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
        MouseStart = E.GetScreenSpacePosition(); bDragging = false;
        Session->Selected = Index;
        return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));
    }
    virtual FReply OnMouseButtonDoubleClick(const FGeometry&, const FPointerEvent&) override
    { if (Name) { Session->Selected = Index; Name->EnterEditingMode(); } return FReply::Handled(); }
    virtual FReply OnMouseMove(const FGeometry&, const FPointerEvent& E) override
    {
        if (!HasMouseCapture()) return FReply::Unhandled();
        const auto Layer = Overlay.Pin(); if (!Layer) return FReply::Handled();
        const auto& G = Layer->GetCachedGeometry();
        const FVector2D Local = G.AbsoluteToLocal(E.GetScreenSpacePosition());
        if (!bDragging && FVector2D::Distance(MouseStart, E.GetScreenSpacePosition()) > 3)
        {
            Session->Begin(NSLOCTEXT("CL", "MoveStratum", "Move Stratum"));
            bDragging = true;
        }
        if (bDragging) Session->Height(Index, Client->NormalizedAtScreenY(Local.Y / FMath::Max(1.0, G.GetLocalSize().Y)));
        return FReply::Handled();
    }
    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& E) override
    {
        if (E.GetEffectingButton() != EKeys::LeftMouseButton || !HasMouseCapture()) return FReply::Unhandled();
        Session->End(); bDragging = false;
        return FReply::Handled().ReleaseMouseCapture();
    }
    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override { Session->End(true); bDragging = false; }
    virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& E) override
    {
        if ((E.GetKey() == EKeys::Delete || E.GetKey() == EKeys::BackSpace) && Session->Selected == Index)
        { Session->Remove(Index); return FReply::Handled(); }
        if (E.GetKey() == EKeys::F2 && Name && Session->Selected == Index) { Name->EnterEditingMode(); return FReply::Handled(); }
        if (E.GetKey() == EKeys::Escape && bDragging)
        { Session->End(true); bDragging = false; return FReply::Handled().ReleaseMouseCapture(); }
        return FReply::Unhandled();
    }
};

/** Observe actual Slate paths, including editable label children, without consuming input. */
class FCLSelectionInput : public IInputProcessor
{
public:
    TWeakPtr<FCLStrataSession> Session;
    explicit FCLSelectionInput(TSharedPtr<FCLStrataSession> InSession) : Session(InSession) {}
    virtual void Tick(float, FSlateApplication&, TSharedRef<ICursor>) override {}
    void ObservePath(const FWidgetPath& Path)
    {
        const auto S = Session.Pin(); if (!S || S->Selected == INDEX_NONE) return;
        for (const FArrangedWidget& W : Path.Widgets.GetInternalArray())
            if (W.Widget->GetType() == FName(TEXT("SCLStratumHandle")) &&
                StaticCastSharedRef<SCLStratumHandle>(W.Widget)->Session == S) return;
        S->Selected = INDEX_NONE;
        if (S->OnPreviewChanged) S->OnPreviewChanged();
    }
    virtual bool HandleMouseButtonDownEvent(FSlateApplication& App, const FPointerEvent& E) override
    {
        ObservePath(App.LocateWindowUnderMouse(E.GetScreenSpacePosition(), App.GetInteractiveTopLevelWindows(), false, E.GetUserIndex()));
        return false;
    }
    virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& App, const FPointerEvent& E) override
    { return HandleMouseButtonDownEvent(App, E); }
};

/** Interactive annotations share the render's projection; empty space passes through to orbit. */
class SCLHeightAnnotations : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SCLHeightAnnotations) {}
        SLATE_ARGUMENT(TSharedPtr<FCLSectionClient>, Client)
        SLATE_ARGUMENT(TSharedPtr<FCLStrataSession>, Session)
    SLATE_END_ARGS()
    TSharedPtr<FCLSectionClient> Client;
    TSharedPtr<FCLStrataSession> Session;
    TSharedPtr<SCanvas> Canvas;
    int32 Rows = -1;
    void Construct(const FArguments& Args)
    {
        Client = Args._Client; Session = Args._Session; ForceVolatile(true);
        SetVisibility(EVisibility::SelfHitTestInvisible);
        ChildSlot[SAssignNew(Canvas, SCanvas).Visibility(EVisibility::SelfHitTestInvisible)];
    }
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& E) override
    {
        if (E.GetKey() == EKeys::Delete || E.GetKey() == EKeys::BackSpace)
        { Session->Remove(Session->Selected); return FReply::Handled(); }
        return FReply::Unhandled();
    }
    virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent& E) override
    { Client->Navigation.Dolly(E.GetWheelDelta()); Client->ApplyNavigation(); return FReply::Handled(); }
    const FCLSectionClient::FIndicator* Indicator(int32 Index) const
    { return Client->Indicators.FindByPredicate([&](const auto& I) { return I.Index == Index; }); }
    FVector2D Anchor(int32 Index) const
    { const auto* I = Indicator(Index); return I ? I->Position * GetCachedGeometry().GetLocalSize() : FVector2D(-1000, -1000); }
    FVector2D Label(int32 Index) const
    {
        const FVector2D Size = GetCachedGeometry().GetLocalSize();
        double X = 0, Previous = 8 - 24; TArray<double> Ys;
        int32 Found = INDEX_NONE;
        for (int32 K = 0; K < Client->Indicators.Num(); ++K)
        {
            const auto& I = Client->Indicators[K];
            X = FMath::Max(X, I.Position.X * Size.X + 28);
            Previous = FMath::Max(8.0, FMath::Max(I.Position.Y * Size.Y - 9, Previous + 24)); Ys.Add(Previous);
            if (I.Index == Index) Found = K;
        }
        if (Found == INDEX_NONE) return FVector2D(-1000, -1000);
        const double Overflow = FMath::Max(0.0, Ys.Last() - Size.Y + 24);
        return FVector2D(X, Ys[Found] - Overflow);
    }
    virtual void Tick(const FGeometry& Geometry, double Time, float Delta) override
    {
        SCompoundWidget::Tick(Geometry, Time, Delta);
        const int32 Count = Session->Asset->Recipe.Strata.Stratum.Num();
        if (Count == Rows) return;
        Rows = Count; Canvas->ClearChildren();
        for (int32 I = 0; I < Count; ++I)
        {
            Canvas->AddSlot().Position_Lambda([this, I] { return Anchor(I) - FVector2D(5, 9); }).Size(FVector2D(24, 18))
                [SNew(SCLStratumHandle).Session(Session).Client(Client).Overlay(SharedThis(this)).Index(I)];
            const auto LabelHandle = SNew(SCLStratumHandle).Session(Session).Client(Client).Overlay(SharedThis(this)).Index(I).Label(true);
            Canvas->AddSlot().Position_Lambda([this, I] { return Label(I); })
                .Size_Lambda([LabelHandle] { return FVector2D(FMath::Min(300.0, double(LabelHandle->GetDesiredSize().X)), 22); })[LabelHandle];
        }
        Canvas->AddSlot().Position_Lambda([this] { return Label(-3); }).Size(FVector2D(150, 22))
            [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 10)).Visibility(EVisibility::HitTestInvisible)
                .Text(FText::FromString(TEXT("Sky boundary"))).ColorAndOpacity(GuideLabelColor)];
        Canvas->AddSlot().Position_Lambda([this] { return Label(-2); }).Size(FVector2D(80, 22))
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 10)).Text(FText::FromString(TEXT("Core"))).ColorAndOpacity(GuideLabelColor)]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)
                    [SNew(SBox).WidthOverride(16).HeightOverride(16).VAlign(VAlign_Center)
                        [SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton").ContentPadding(0)
                        .ToolTipText(FText::FromString(TEXT("Add a Stratum just above Core, then drag it to the desired height")))
                        .IsEnabled_Lambda([this] { return Session->Asset->Recipe.Strata.Stratum.Num() < 128; })
                        .OnClicked_Lambda([this]
                        {
                            Client->bGuides = true; Session->Add();
                            return FReply::Handled().SetUserFocus(SharedThis(this));
                        })[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                            .Text(FText::FromString(TEXT("+"))).ColorAndOpacity(GuideLabelColor)]]]];
    }
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Clip, FSlateWindowElementList& Elements,
        int32 Layer, const FWidgetStyle& Style, bool Enabled) const override
    {
        for (const auto& I : Client->Indicators)
        {
            const FVector2D A = I.Position * Geometry.GetLocalSize(), L = Label(I.Index);
            const FLinearColor Color = I.Color * Style.GetColorAndOpacityTint();
            TArray<FVector2D> Tick{A - FVector2D(4, 0), A + FVector2D(12, 0)};
            FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Tick, ESlateDrawEffect::None, Color, true,
                I.Index >= 0 && Session->Selected == I.Index ? 4 : 2);
            TArray<FVector2D> Leader{A + FVector2D(12, 0), FVector2D(L.X - 8, L.Y + 9), FVector2D(L.X - 3, L.Y + 9)};
            FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Leader, ESlateDrawEffect::None, Color, true, 1);
        }
        return SCompoundWidget::OnPaint(Args, Geometry, Clip, Elements, Layer + 1, Style, Enabled);
    }
};

class SCLSectionViewport : public SEditorViewport
{
public:
    SLATE_BEGIN_ARGS(SCLSectionViewport) {} SLATE_END_ARGS()
    FAdvancedPreviewScene Scene{FPreviewScene::ConstructionValues()};
    TStrongObjectPtr<UProceduralMeshComponent> Mesh;
    TSharedPtr<FCLSectionClient> Client;
    bool bCameraFramed = false;
    FKey NavigationButton;
    FVector2D PreviousMouse;
    virtual FReply OnPreviewMouseButtonDown(const FGeometry&, const FPointerEvent& E) override
    {
        const FKey Button = E.GetEffectingButton();
        if (Button != EKeys::LeftMouseButton && Button != EKeys::MiddleMouseButton && Button != EKeys::RightMouseButton) return FReply::Unhandled();
        NavigationButton = Button; PreviousMouse = E.GetScreenSpacePosition();
        return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));
    }
    virtual FReply OnMouseMove(const FGeometry&, const FPointerEvent& E) override
    {
        if (!HasMouseCapture()) return FReply::Unhandled();
        const FVector2D Delta = E.GetScreenSpacePosition() - PreviousMouse; PreviousMouse = E.GetScreenSpacePosition();
        if (NavigationButton == EKeys::LeftMouseButton) Client->Navigation.Orbit(Delta);
        else if (NavigationButton == EKeys::MiddleMouseButton) Client->Navigation.PanBy(Delta);
        else if (NavigationButton == EKeys::RightMouseButton) Client->Navigation.Dolly(-Delta.Y * .05);
        Client->ApplyNavigation(); return FReply::Handled();
    }
    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& E) override
    {
        if (!HasMouseCapture() || E.GetEffectingButton() != NavigationButton) return FReply::Unhandled();
        NavigationButton = FKey(); return FReply::Handled().ReleaseMouseCapture();
    }
    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override { NavigationButton = FKey(); }
    virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent& E) override
    { Client->Navigation.Dolly(E.GetWheelDelta()); Client->ApplyNavigation(); return FReply::Handled(); }
    virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent&) override { return FReply::Unhandled(); }
    void Construct(const FArguments& Args)
    {
        Scene.SetFloorVisibility(false); Scene.SetEnvironmentVisibility(false);
        Scene.SetLightDirection(FRotator(-35, 135, 0)); Scene.SetLightBrightness(4); Scene.SetSkyBrightness(1);
        Mesh.Reset(NewObject<UProceduralMeshComponent>(GetTransientPackage()));
        Scene.AddComponent(Mesh.Get(), FTransform::Identity);
        Mesh->SetMaterial(0, LoadObject<UMaterial>(nullptr, TEXT("/CatalystLandform/Designer/M_Section.M_Section")));
        SEditorViewport::Construct(SEditorViewport::FArguments());
    }
    virtual ~SCLSectionViewport()
    {
        if (Client) Client->Viewport = nullptr;
        if (Mesh) Scene.RemoveComponent(Mesh.Get());
    }
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override
    { Client = MakeShared<FCLSectionClient>(&Scene, SharedThis(this)); return Client.ToSharedRef(); }
    void Rebuild(TSharedPtr<const FCLCompiledPlanetRecipe> Plan)
    {
        Client->Plan = Plan;
        if (!Plan) return;
        if (Client->AreaWidthMeters <= 0) Client->AreaWidthMeters = FCLSectionGeometry::DefaultWidth(*Plan);
        const FCLSectionGeometry G = FCLSectionGeometry::Build(*Plan, Client->Heading, Client->AreaWidthMeters);
        TArray<FVector> N; TArray<FVector2D> UV; TArray<FProcMeshTangent> Tangents;
        UV.Init(FVector2D::ZeroVector, G.Vertices.Num());
        UKismetProceduralMeshLibrary::CalculateTangentsForMesh(G.Vertices, G.Triangles, {}, N, Tangents);
        Mesh->CreateMeshSection_LinearColor(0, G.Vertices, G.Triangles, N, UV, G.Colors, Tangents, false);
        if (!bCameraFramed)
        {
            bCameraFramed = true;
            Client->ApplyNavigation();
        }
        Client->Invalidate();
    }
};

class FCLRecipeToolkit : public FCFModelEditorToolkit
{
    TSharedPtr<SCLSectionViewport> Preview;
    TSharedPtr<SCLHeightAnnotations> Annotations;
    TSharedPtr<FCLStrataSession> StrataSession;
    TSharedPtr<FCLSelectionInput> SelectionInput;
    TStrongObjectPtr<UCLTerrainTypeAsset> TerrainSource;
    FString Status;
    double PendingAreaWidth = 0;
    bool bAreaDragging = false;
    void ApplyPreviewArea()
    {
        if (!Preview || !Preview->Client->Plan) return;
        Preview->Client->AreaWidthMeters = PendingAreaWidth;
        Preview->Rebuild(Preview->Client->Plan);
    }
    FText AreaCaption() const
    {
        const double Width = Preview ? Preview->Client->AreaWidthMeters : 0;
        return FText::FromString(FString::Printf(TEXT("Viewing %.0f m × %.0f m"), Width, Width));
    }
    FDelegateHandle Changed;
    static const FName SectionTab;
public:
    virtual ~FCLRecipeToolkit()
    {
        if (SelectionInput && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(SelectionInput);
        if (auto* Asset = Cast<UCLPlanetRecipeAsset>(EditingAsset)) Asset->OnRecipeChanged.Remove(Changed);
    }
    void Open(UCLPlanetRecipeAsset* Asset, EToolkitMode::Type Mode, TSharedPtr<IToolkitHost> Host)
    {
        EditingAsset = Asset;
        StrataSession = MakeShared<FCLStrataSession>(); StrataSession->Open(Asset);
        SelectionInput = MakeShared<FCLSelectionInput>(StrataSession);
        FSlateApplication::Get().RegisterInputPreProcessor(SelectionInput);
        StrataSession->OnPreviewChanged = [this]
        {
            if (!Preview) return;
            Preview->Client->Strata = StrataSession->Asset->Recipe.Strata;
            Preview->Client->UpdateIndicators();
            if (Annotations) Annotations->Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
        };
        BuildDetailsPanel();
        Changed = Asset->OnRecipeChanged.AddSP(this, &FCLRecipeToolkit::Refresh);
        const auto Layout = FTabManager::NewLayout("CLRecipeDesigner_v2")->AddArea(
            FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
            ->Split(FTabManager::NewStack()->SetSizeCoefficient(.32)->AddTab(DetailsTabId, ETabState::OpenedTab)->AddTab(ValidationTabId, ETabState::OpenedTab)->SetForegroundTab(DetailsTabId))
            ->Split(FTabManager::NewStack()->SetSizeCoefficient(.68)->AddTab(SectionTab, ETabState::OpenedTab)->SetHideTabWell(true)));
        InitAssetEditor(Mode, Host, GetToolkitFName(), Layout, true, true, Asset);
        const auto Extender = MakeShared<FExtender>();
        Extender->AddToolBarExtension(TEXT("Asset"), EExtensionHook::After, nullptr,
            FToolBarExtensionDelegate::CreateLambda([this](FToolBarBuilder& Builder)
            {
                Builder.AddToolBarButton(FUIAction(FExecuteAction::CreateSP(this, &FCLRecipeToolkit::Export)), NAME_None,
                    FText::FromString(TEXT("Export recipe")), FText::FromString(TEXT("Export through Foundation to Saved/CatalystLandform/Model.json")));
            }));
        AddToolbarExtender(Extender); RegenerateMenusAndToolbars();
        // Explicit asset target; don't use Foundation's legacy first-PIE-service lookup.
        Refresh();
    }
    virtual void GetSaveableObjects(TArray<UObject*>& Objects) const override
    { FCFModelEditorToolkit::GetSaveableObjects(Objects); if (StrataSession) Objects.AddUnique(StrataSession->Library.Get()); }
    TSharedRef<SWidget> TerrainMenu()
    {
        FMenuBuilder Menu(true, nullptr); TArray<FAssetData> Assets;
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByClass(UCLTerrainTypeAsset::StaticClass()->GetClassPathName(), Assets, true);
        Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
        if (Assets.IsEmpty()) Menu.AddMenuEntry(FText::FromString(TEXT("No terrain types found")), FText(), FSlateIcon(), FUIAction());
        for (const FAssetData& Data : Assets)
        {
            auto* Source = Cast<UCLTerrainTypeAsset>(Data.GetAsset()); if (!Source) continue;
            const TWeakObjectPtr<UCLTerrainTypeAsset> Weak(Source);
            Menu.AddMenuEntry(FText::FromString(Source->TerrainType.Name), FText::FromString(Source->TerrainType.Description), FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([this, Weak]
                {
                    if (!Weak.IsValid()) return;
                    const FScopedTransaction Tx(NSLOCTEXT("CL", "ApplyTerrainType", "Apply terrain type")); FString Error;
                    if (!Weak->ApplyToRecipe(CastChecked<UCLPlanetRecipeAsset>(EditingAsset), Error)) Status = TEXT("Terrain type was not applied: ") + Error;
                })));
        }
        return Menu.MakeWidget();
    }
    TSharedRef<SWidget> StrataMenu()
    {
        FMenuBuilder Menu(true, nullptr);
        for (const FCLStrata& S : StrataSession->Library->Library.Sets)
            Menu.AddMenuEntry(FText::FromString(S.Name), FText::FromString(TEXT("Apply this Strata to the contextual planet recipe")), FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([Session = StrataSession, Id = S.Id] { Session->Apply(Id); })));
        return Menu.MakeWidget();
    }
    virtual FName GetToolkitFName() const override { return TEXT("CLRecipeDesigner"); }
    virtual FText GetBaseToolkitName() const override { return NSLOCTEXT("CL", "Designer", "Landform Designer"); }
    virtual FString GetWorldCentricTabPrefix() const override { return TEXT("Landform"); }
    void Export() { HandleExportJson(); }
    virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& Manager) override
    {
        FCFModelEditorToolkit::RegisterTabSpawners(Manager);
        Manager->RegisterTabSpawner(SectionTab, FOnSpawnTab::CreateSP(this, &FCLRecipeToolkit::SpawnSection));
    }
    virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& Manager) override
    { Manager->UnregisterTabSpawner(SectionTab); FCFModelEditorToolkit::UnregisterTabSpawners(Manager); }
    void Refresh()
    {
        auto* Asset = CastChecked<UCLPlanetRecipeAsset>(EditingAsset);
        if (!Asset->Recipe.TerrainTypeId.IsValid()) TerrainSource.Reset();
        else if (!TerrainSource || TerrainSource->TerrainType.Id != Asset->Recipe.TerrainTypeId)
        {
            TerrainSource.Reset(); TArray<FAssetData> Assets;
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByClass(UCLTerrainTypeAsset::StaticClass()->GetClassPathName(), Assets, true);
            for (const auto& Data : Assets) if (auto* Source = Cast<UCLTerrainTypeAsset>(Data.GetAsset()); Source && Source->TerrainType.Id == Asset->Recipe.TerrainTypeId)
            { TerrainSource.Reset(Source); break; }
        }
        if (Preview) Preview->Client->Strata = Asset->Recipe.Strata;
        const auto Plan = FCLPlanetCompiler::Compile(Asset->Recipe, Status);
        if (Plan)
        {
            Status = FString::Printf(TEXT("%s | radius %.0f m | %d layers | preview curvature 1/3"), *Asset->Recipe.Name, Plan->RadiusMeters, Plan->Layers.Num());
            if (Preview)
            {
                Preview->Rebuild(Plan);
                if (PendingAreaWidth <= 0) PendingAreaWidth = Preview->Client->AreaWidthMeters;
            }
        }
        else Status = TEXT("Keeping last valid preview: ") + Status;
        RefreshAllUI(EditingAsset, false);
    }
    TSharedRef<SDockTab> SpawnSection(const FSpawnTabArgs&)
    {
        return SNew(SDockTab)[SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Status); })]
            + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()[SNew(SComboButton).OnGetMenuContent(this, &FCLRecipeToolkit::TerrainMenu)
                    .ToolTipText(FText::FromString(TEXT("Apply a terrain type's noise layers using this planet's Strata, radius and seed")))
                    .ButtonContent()[SNew(STextBlock).Text_Lambda([this]
                    { const FString& Name = CastChecked<UCLPlanetRecipeAsset>(EditingAsset)->Recipe.TerrainTypeName;
                      return FText::FromString(TEXT("Terrain: ") + (Name.IsEmpty() ? TEXT("Custom") : Name)); })]]
                + SHorizontalBox::Slot().AutoWidth().Padding(8, 0)[SNew(SButton).Text(FText::FromString(TEXT("Edit source")))
                    .IsEnabled_Lambda([this] { return TerrainSource.IsValid(); })
                    .ToolTipText(FText::FromString(TEXT("Edit the reusable terrain type. Reapply it to update this planet's copy.")))
                    .OnClicked_Lambda([this]
                    { if (TerrainSource) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(TerrainSource.Get()); return FReply::Handled(); })]
                + SHorizontalBox::Slot().FillWidth(1).Padding(8, 0)[SNew(STextBlock).AutoWrapText(true)
                    .Text(FText::FromString(TEXT("Edit this planet's base noise layers in Details")))]]
            + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()[SNew(SComboButton).OnGetMenuContent(this, &FCLRecipeToolkit::StrataMenu)
                    .ButtonContent()[SNew(STextBlock).Text(FText::FromString(TEXT("Strata")))]]
                + SHorizontalBox::Slot().FillWidth(1).Padding(8, 0)[SNew(SInlineEditableTextBlock)
                    .Text_Lambda([this] { return FText::FromString(StrataSession->Asset->Recipe.Strata.Name); })
                    .OnVerifyTextChanged_Lambda([this](const FText& Text, FText& Error)
                    {
                        const FString N = Text.ToString().TrimStartAndEnd();
                        if (N.IsEmpty() || StrataSession->Library->Library.Sets.ContainsByPredicate([&](const FCLStrata& S)
                            { return S.Id != StrataSession->Asset->Recipe.Strata.Id && S.Name.Equals(N, ESearchCase::IgnoreCase); }))
                        { Error = FText::FromString(TEXT("Use a unique, nonempty Strata name")); return false; }
                        return true;
                    })
                    .OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type Type)
                    {
                        if (Type == ETextCommit::OnCleared || Text.ToString().TrimStartAndEnd().IsEmpty()) return;
                        StrataSession->Begin(NSLOCTEXT("CL", "RenameStrata", "Rename Strata"));
                        StrataSession->Asset->Recipe.Strata.Name = Text.ToString().TrimStartAndEnd(); StrataSession->Changed(); StrataSession->End();
                    })]
                + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("New Strata")))
                    .OnClicked_Lambda([this] { StrataSession->NewSet(); return FReply::Handled(); })]
                + SHorizontalBox::Slot().AutoWidth().Padding(8, 0)[SNew(SButton).Text(FText::FromString(TEXT("Remove Strata")))
                    .OnClicked_Lambda([this] { StrataSession->RemoveSet(); return FReply::Handled(); })]]
            + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()[SNew(SCheckBox).IsChecked_Lambda([this] { return !Preview || Preview->Client->bGuides ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }).OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                    { Preview->Client->bGuides = State == ECheckBoxState::Checked; Preview->Client->Invalidate(); })[SNew(STextBlock).Text(FText::FromString(TEXT("Height Guides")))]]
                + SHorizontalBox::Slot().AutoWidth().Padding(12, 0)[SNew(STextBlock).Text(FText::FromString(TEXT("Section heading")))]
                + SHorizontalBox::Slot().FillWidth(1)[SNew(SSlider).OnValueChanged_Lambda([this](float Value)
                    { Preview->Client->Heading = Value * 2 * UE_PI; Preview->Rebuild(Preview->Client->Plan); })]]
            + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(200)[SNew(STextBlock).Text(this, &FCLRecipeToolkit::AreaCaption)]]
                + SHorizontalBox::Slot().AutoWidth().Padding(12, 0)[SNew(STextBlock).Text(FText::FromString(TEXT("100 m")))]
                + SHorizontalBox::Slot().FillWidth(1)[SNew(SSlider)
                    .ToolTipText(FText::FromString(TEXT("Terrain area fitted into the fixed cutaway. Release to resample; camera and guide scale stay fixed.")))
                    .Value_Lambda([this] { return float((PendingAreaWidth - 100) / 900); })
                    .OnMouseCaptureBegin_Lambda([this] { bAreaDragging = true; })
                    .OnMouseCaptureEnd_Lambda([this] { bAreaDragging = false; ApplyPreviewArea(); })
                    .OnControllerCaptureBegin_Lambda([this] { bAreaDragging = true; })
                    .OnControllerCaptureEnd_Lambda([this] { bAreaDragging = false; ApplyPreviewArea(); })
                    .OnValueChanged_Lambda([this](float Value)
                    { PendingAreaWidth = FMath::Clamp(FMath::RoundToDouble((100 + 900 * Value) / 10) * 10, 100.0, 1000.0);
                      if (!bAreaDragging) ApplyPreviewArea(); })]
                + SHorizontalBox::Slot().AutoWidth().Padding(12, 0)[SNew(STextBlock).Text(FText::FromString(TEXT("1 km")))]]
            + SVerticalBox::Slot().AutoHeight().Padding(8, 0)[SNew(SBox).HeightOverride(18)[SNew(STextBlock).Text_Lambda([this]
                { return bAreaDragging ? FText::FromString(FString::Printf(TEXT("Release to view %.0f m × %.0f m"), PendingAreaWidth, PendingAreaWidth)) : FText(); })]]
            + SVerticalBox::Slot().FillHeight(1)[SNew(SOverlay)
                + SOverlay::Slot()[SAssignNew(Preview, SCLSectionViewport)]
                + SOverlay::Slot()[SAssignNew(Annotations, SCLHeightAnnotations).Client(Preview->Client).Session(StrataSession)]]
            + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SBox).HeightOverride(42)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]
                {
                    FString Legend = StrataSession->Gesture ? TEXT("Release to update terrain | Esc to cancel") :
                        TEXT("Left drag: orbit | Middle drag: pan cropped view | Right drag or wheel: zoom | + beside Core: add Stratum");
                    if (Preview && Preview->Client->Plan && Preview->Client->AreaWidthMeters / FMath::Sqrt(2.0) >=
                        UE_PI * FCLSectionGeometry::ReferenceSurfaceRadius(*Preview->Client->Plan))
                        Legend += TEXT(" | Area passes the far side of this small planet: terrain repeats");
                    return FText::FromString(Legend);
                })]]];
    }
};
const FName FCLRecipeToolkit::SectionTab(TEXT("CLRecipeSection"));

void OpenExample()
{
    if (!GEditor) return;
    const auto* Planet = GEditor->GetSelectedActors()->GetTop<ACLPlanet>();
    auto* Asset = Planet && Planet->RecipeAsset ? Planet->RecipeAsset.Get() :
        LoadObject<UCLPlanetRecipeAsset>(nullptr, TEXT("/Game/LandformDesigner/DA_RollingPlains.DA_RollingPlains"));
    if (Asset) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
}
class FCLDesignerModule : public IModuleInterface
{
    TUniquePtr<FAutoConsoleCommand> Command;
    FDelegateHandle MenuHandle;
public:
    virtual void StartupModule() override
    {
        Command = MakeUnique<FAutoConsoleCommand>(TEXT("Landform.Designer"), TEXT("Open selected planet recipe or rolling-plains example"), FConsoleCommandDelegate::CreateStatic(&OpenExample));
        MenuHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]
        {
            FToolMenuOwnerScoped Owner(TEXT("CatalystLandformDesigner"));
            auto* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
            Menu->AddSection(TEXT("CatalystLandform"), FText::FromString(TEXT("Catalyst"))).AddMenuEntry(TEXT("LandformDesigner"),
                FText::FromString(TEXT("Landform Designer")), FText::FromString(TEXT("Open the selected planet recipe, or the rolling-plains example")),
                FSlateIcon(), FUIAction(FExecuteAction::CreateStatic(&OpenExample)));
        }));
    }
    virtual void ShutdownModule() override
    { Command.Reset(); UToolMenus::UnRegisterStartupCallback(MenuHandle); UToolMenus::UnregisterOwner(TEXT("CatalystLandformDesigner")); }
};
}

UCLPlanetRecipeFactory::UCLPlanetRecipeFactory()
{ SupportedClass = UCLPlanetRecipeAsset::StaticClass(); bCreateNew = true; bEditAfterNew = true; }
UObject* UCLPlanetRecipeFactory::FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{ return NewObject<UCLPlanetRecipeAsset>(Parent, Class, Name, Flags); }
TSoftClassPtr<UObject> UCLRecipeAssetDefinition::GetAssetClass() const { return UCLPlanetRecipeAsset::StaticClass(); }
EAssetCommandResult UCLRecipeAssetDefinition::OpenAssets(const FAssetOpenArgs& Args) const
{
    for (auto* Asset : Args.LoadObjects<UCLPlanetRecipeAsset>())
        MakeShared<FCLRecipeToolkit>()->Open(Asset, Args.ToolkitHost.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone, Args.ToolkitHost);
    return EAssetCommandResult::Handled;
}
IMPLEMENT_MODULE(FCLDesignerModule, CatalystLandformEditor)

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLFixedDesignerFrameTest, "Catalyst.Landform.Designer.FixedDisplayFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLFixedDesignerFrameTest::RunTest(const FString&)
{
    FString Error;
    FCLPlanetRecipe Recipe; Recipe.RadiusMeters = 1000;
    const auto Plan = FCLPlanetCompiler::Compile(Recipe, Error);
    const TSharedRef<SCLSectionViewport> Preview = SNew(SCLSectionViewport);
    Preview->Rebuild(Plan);
    // Changing coverage must also preserve a user's manually adjusted camera.
    const FVector Eye = Preview->Client->GetViewLocation() + FVector(200, 300, 400);
    const FRotator Rotation = Preview->Client->GetViewRotation() + FRotator(2, 3, 0);
    Preview->Client->SetViewLocation(Eye); Preview->Client->SetViewRotation(Rotation);
    const FVector Target = Preview->Client->GetLookAtLocation();
    const FBox InitialBounds = Preview->Mesh->GetProcMeshSection(0)->SectionLocalBox;
    FVector PreviousCorner; bool bPrevious = false;
    for (double Width : {100.0, 500.0, 1000.0})
    {
        Preview->Client->AreaWidthMeters = Width;
        Preview->Rebuild(Plan);
        TestTrue(TEXT("Coverage preserves the user's camera position"), Preview->Client->GetViewLocation().Equals(Eye, .001));
        TestTrue(TEXT("Coverage preserves the user's camera rotation"), Preview->Client->GetViewRotation().Equals(Rotation, .001));
        TestTrue(TEXT("Coverage preserves the orbit target"), Preview->Client->GetLookAtLocation().Equals(Target, .001));
        const FBox Bounds = Preview->Mesh->GetProcMeshSection(0)->SectionLocalBox;
        TestTrue(TEXT("Coverage leaves the displayed block bounds unchanged"), Bounds.Min.Equals(InitialBounds.Min, .01) && Bounds.Max.Equals(InitialBounds.Max, .01));
        const FVector Corner = FCLSectionGeometry::SourceSurfacePoint(*Plan, Width / 2, Width / 2, 0);
        if (bPrevious) TestFalse(TEXT("Larger coverage samples different data at the same displayed corner"), Corner.Equals(PreviousCorner, .01));
        PreviousCorner = Corner; bPrevious = true;
    }
    Recipe.RadiusMeters = 100;
    Preview->Rebuild(FCLPlanetCompiler::Compile(Recipe, Error));
    TestTrue(TEXT("Resizing a planet preserves the display camera"), Preview->Client->GetViewLocation().Equals(Eye, .001));
    const FBox ResizedBounds = Preview->Mesh->GetProcMeshSection(0)->SectionLocalBox;
    TestTrue(TEXT("Normalized display bounds are independent of planet radius"), ResizedBounds.Min.Equals(InitialBounds.Min, .01) && ResizedBounds.Max.Equals(InitialBounds.Max, .01));
    const FVector BeforeKeys = Preview->Client->GetViewLocation();
    Preview->Client->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Pressed, 1));
    Preview->Client->InputAxis(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX, IE_Axis, 200));
    Preview->Client->Tick(1);
    TestTrue(TEXT("Editor flight keys and raw mouse axes cannot move the camera"), Preview->Client->GetViewLocation().Equals(BeforeKeys, .001));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLDesignerSelectionTest, "Catalyst.Landform.Designer.SelectionAndCoreAdd",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLDesignerSelectionTest::RunTest(const FString&)
{
    const auto Session = MakeShared<FCLStrataSession>();
    Session->Asset = NewObject<UCLPlanetRecipeAsset>(GetTransientPackage(), NAME_None, RF_Transactional);
    Session->Library.Reset(NewObject<UCLStrataLibraryAsset>(GetTransientPackage(), NAME_None, RF_Transactional));
    Session->Library->Library.Sets.Add(Session->Asset->Recipe.Strata);
    const int32 Before = Session->Asset->Recipe.Strata.Stratum.Num();
    int32 Changes = 0; Session->Asset->OnRecipeChanged.AddLambda([&] { ++Changes; });
    const int32 New = Session->Add();
    TestEqual(TEXT("Core + creates one guide"), Session->Asset->Recipe.Strata.Stratum.Num(), Before + 1);
    TestEqual(TEXT("Core + selects the new guide"), Session->Selected, New);
    TestEqual(TEXT("New guide starts just above Core"), Session->Asset->Recipe.Strata.Stratum[New].NormalizedHeight, .05);
    TestEqual(TEXT("Core + commits one authoring change"), Changes, 1);
    const TSharedRef<SCLStratumHandle> Handle = SNew(SCLStratumHandle).Session(Session).Index(New).Label(true);
    FWidgetPath StratumPath; StratumPath.Widgets.AddWidget(FArrangedWidget(Handle, FGeometry()));
    StratumPath.Widgets.AddWidget(FArrangedWidget(Handle->Name.ToSharedRef(), FGeometry()));
    FCLSelectionInput Observer(Session); Observer.ObservePath(StratumPath);
    TestEqual(TEXT("Clicking a Stratum or its editable text preserves selection"), Session->Selected, New);
    FWidgetPath OtherPath; OtherPath.Widgets.AddWidget(FArrangedWidget(SNew(SButton), FGeometry()));
    Observer.ObservePath(OtherPath);
    TestEqual(TEXT("Clicking another control deselects"), Session->Selected, INDEX_NONE);
    Handle->OnKeyDown(FGeometry(), FKeyEvent(EKeys::Delete, FModifierKeysState(), 0, false, 0, 0));
    TestEqual(TEXT("Delete cannot remove a previously selected guide after deselection"), Session->Asset->Recipe.Strata.Stratum.Num(), Before + 1);
    Session->Selected = New; Observer.ObservePath(FWidgetPath());
    TestEqual(TEXT("Clicking empty space deselects"), Session->Selected, INDEX_NONE);
    TestEqual(TEXT("Selection changes do not notify terrain generation"), Changes, 1);
    GEditor->UndoTransaction();
    TestEqual(TEXT("One undo removes the added planet guide"), Session->Asset->Recipe.Strata.Stratum.Num(), Before);
    TestEqual(TEXT("One undo restores the catalog"), Session->Library->Library.Sets[0].Stratum.Num(), Before);
    Session->Asset->OnRecipeChanged.Clear();
    return true;
}
#endif

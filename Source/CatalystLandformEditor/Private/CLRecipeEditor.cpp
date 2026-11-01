#include "CLRecipeAssetTools.h"
#include "CLTerrainDesigner.h"
#include "CLTerrainEditing.h"
#include "CLNoiseLayerStack.h"
#include "IDetailsView.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/NumericUnitTypeInterface.inl"
#include "Async/Async.h"
#include "CLSectionGeometry.h"
#include "CLSectionNavigation.h"
#include "Framework/Application/IInputProcessor.h"
#include "Layout/WidgetPath.h"
#include "InputKeyEventArgs.h"
#include "CLRadialGuideEditing.h"
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
#include "PreviewScene.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/TextureCube.h"
#include "SEditorViewport.h"
#include "EditorViewportClient.h"
#include "ProceduralMeshComponent.h"
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
const FLinearColor GuideLabelColor(.72, .72, .72, .3);
constexpr double GuideTickWidth = 16;
constexpr double GuideLabelHeight = 20;
FLinearColor GuideTint(int32 Index, int32 Selected)
{
    return GuideLabelColor.CopyWithNewOpacity(Index >= 0 && Selected == Index ? 1.f : .3f);
}
// A private studio rig: preview lighting must not inherit another asset editor's
// environment profile or animate continuously while the designer is idle.
class FCLSectionScene : public FPreviewScene
{
    UDirectionalLightComponent* Fill = nullptr;
    UDirectionalLightComponent* Rim = nullptr;
public:
    FCLSectionScene() : FPreviewScene(ConstructionValues())
    {
        SetSkyCubemap(LoadObject<UTextureCube>(nullptr, TEXT("/Engine/MapTemplates/Sky/SunsetAmbientCubemap.SunsetAmbientCubemap")));
        SetSkyBrightness(.12f);
        DirectionalLight->SetMobility(EComponentMobility::Movable);
        DirectionalLight->SetIntensity(1.9f);
        DirectionalLight->SetLightColor(FLinearColor(1, .94, .86));
        DirectionalLight->SetForwardShadingPriority(1);
        DirectionalLight->SetLightSourceAngle(4);
        // Presentation dimensions are fixed at 1 km, independently of coverage.
        DirectionalLight->SetDynamicShadowDistanceMovableLight(FCLSectionGeometry::DisplaySize * 100 * 6);
        DirectionalLight->DynamicShadowCascades = 4;
        DirectionalLight->SetShadowBias(.35f);
        auto AddSoftLight = [this](float Intensity, FLinearColor Color)
        {
            auto* Light = NewObject<UDirectionalLightComponent>(GetTransientPackage(), NAME_None, RF_Transient);
            Light->SetMobility(EComponentMobility::Movable);
            Light->SetIntensity(Intensity); Light->SetLightColor(Color);
            Light->SetCastShadows(false);
            AddComponent(Light, FTransform::Identity);
            return Light;
        };
        Fill = AddSoftLight(.55f, FLinearColor(.65, .79, 1));
        Rim = AddSoftLight(1.2f, FLinearColor(.58, .79, 1));
    }
    void AimLights(const FRotator& ViewRotation)
    {
        // Keep lights on their camera side throughout an orbit. World-up keeps
        // the key and rear strip above the landscape even at steep view angles.
        const FVector Back = -FVector(ViewRotation.Vector().X, ViewRotation.Vector().Y, 0).GetSafeNormal();
        const FVector Right = FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y);
        auto Aim = [](UDirectionalLightComponent* Light, FVector ToSource)
        { Light->SetWorldRotation((-ToSource).Rotation()); };
        Aim(DirectionalLight, Back * .5 - Right * .95 + FVector::UpVector * .65);
        Aim(Fill, Back * .4 + Right + FVector::UpVector * .35);
        Aim(Rim, -Back * .9 + FVector::UpVector * .9);
    }
};
class SCLSectionViewport;
class FCLSectionClient : public FEditorViewportClient
{
    FCLSectionScene* SectionScene;
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
        SectionScene->AimLights(Camera.Rotation);
        Invalidate();
    }
    virtual void Tick(float) override
    {
        SectionScene->UpdateCaptureContents();
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
    double AreaWidthMeters = 0;
    bool bGuides = true;
    FCLRadialGuideSet RadialGuides;
    FVector2D RightEdge;
    FMatrix ViewProjection;
    FIntRect ViewRect;
    bool bHasProjection = false;
    double RadialGuideHeightMeters(double Normalized) const { return (Normalized - 1) * Plan->RadiusMeters; }
    double NormalizedAtScreenY(double Y) const
    {
        return bHasProjection && Plan ? FCLSectionGeometry::RadialGuideAtScreenY(RightEdge, ViewProjection, Y) : 0;
    }
    struct FIndicator { FString Text; FLinearColor Color; FVector2D Position; int32 Index = INDEX_NONE; };
    TArray<FIndicator> Indicators;
    FCLSectionClient(FCLSectionScene* Scene, const TSharedRef<SEditorViewport>& Viewport)
        : FEditorViewportClient(nullptr, Scene, Viewport), SectionScene(Scene)
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
        Point = FCLSectionGeometry::RadialGuidePoint(FVector2D(X, Y), 1 + Height / Plan->RadiusMeters);
        return true;
    }
    virtual FLinearColor GetBackgroundColor() const override { return FLinearColor(.012, .018, .028); }
    virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override
    {
        FEditorViewportClient::Draw(View, PDI);
        if (!Plan) return;
        const double W = FCLSectionGeometry::DisplaySize * .5;
        const FLinearColor Frame(.16, .23, .29);
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
        if (bGuides) for (int32 I = 0; I < RadialGuides.Guides.Num(); ++I)
        {
            const auto& G = RadialGuides.Guides[I]; FVector P;
            if (GuidePoint(RightEdge.X, RightEdge.Y, RadialGuideHeightMeters(G.NormalizedHeight), P))
                Indicator(P, G.Name.ToString(), G.Color, I);
        }
        Indicators.Sort([](const FIndicator& A, const FIndicator& B) { return A.Position.Y < B.Position.Y; });
    }
};

/** A tick or label is a direct manipulator, with one undo transaction per gesture. */
class SCLRadialGuideHandle : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SCLRadialGuideHandle) : _Index(INDEX_NONE), _Label(false) {}
        SLATE_ARGUMENT(TSharedPtr<FCLRadialGuideSession>, Session)
        SLATE_ARGUMENT(TSharedPtr<FCLSectionClient>, Client)
        SLATE_ARGUMENT(TWeakPtr<SWidget>, Overlay)
        SLATE_ARGUMENT(int32, Index)
        SLATE_ARGUMENT(bool, Label)
    SLATE_END_ARGS()
    TSharedPtr<FCLRadialGuideSession> Session;
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
            ChildSlot[SNew(SBorder).Padding(FMargin(3, 0)).VAlign(VAlign_Center).BorderImage(FAppStyle::GetBrush("NoBorder"))
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SAssignNew(Name, SInlineEditableTextBlock)
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)).DelayedLeftClickEntersEditMode(false)
                        .Text_Lambda([this] { return FText::FromName(Session->Asset->Recipe.RadialGuides.Guides.IsValidIndex(Index) ?
                            Session->Asset->Recipe.RadialGuides.Guides[Index].Name : NAME_None); })
                        .ColorAndOpacity_Lambda([this] { return GuideTint(Index, Session->Selected); })
                        .IsSelected_Lambda([this] { return Session->Selected == Index; })
                        .OnVerifyTextChanged_Lambda([this](const FText& Text, FText& Error)
                        { FString E; const bool bOK = FCLRadialGuideEditing::CanRename(Session->Asset->Recipe.RadialGuides, Index, Text.ToString(), E);
                          Error = FText::FromString(E); return bOK; })
                        .OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type Type)
                        {
                            if (Type == ETextCommit::OnCleared) return;
                            FString Error;
                            if (!FCLRadialGuideEditing::CanRename(Session->Asset->Recipe.RadialGuides, Index, Text.ToString(), Error)) return;
                            Session->Begin(NSLOCTEXT("CL", "RenameRadialGuide", "Rename Radial Guide"));
                            FCLRadialGuideEditing::Rename(Session->Asset->Recipe, Index, Text.ToString(), Error); Session->Changed(); Session->End();
                        })]
                ]];
        else ChildSlot[SNew(SBorder).Padding(0).BorderImage(FAppStyle::GetBrush("NoBorder"))];
    }
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual TOptional<bool> OnQueryShowFocus(EFocusCause) const override { return false; }
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
            Session->Begin(NSLOCTEXT("CL", "MoveRadialGuide", "Move Radial Guide"));
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
    TWeakPtr<FCLRadialGuideSession> Session;
    explicit FCLSelectionInput(TSharedPtr<FCLRadialGuideSession> InSession) : Session(InSession) {}
    virtual void Tick(float, FSlateApplication&, TSharedRef<ICursor>) override {}
    void ObservePath(const FWidgetPath& Path)
    {
        const auto S = Session.Pin(); if (!S || S->Selected == INDEX_NONE) return;
        for (const FArrangedWidget& W : Path.Widgets.GetInternalArray())
            if (W.Widget->GetType() == FName(TEXT("SCLRadialGuideHandle")) &&
                StaticCastSharedRef<SCLRadialGuideHandle>(W.Widget)->Session == S) return;
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
        SLATE_ARGUMENT(TSharedPtr<FCLRadialGuideSession>, Session)
    SLATE_END_ARGS()
    TSharedPtr<FCLSectionClient> Client;
    TSharedPtr<FCLRadialGuideSession> Session;
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
            X = FMath::Max(X, I.Position.X * Size.X + GuideTickWidth * 2 + 16);
            Previous = FMath::Max(8.0, FMath::Max(I.Position.Y * Size.Y - GuideLabelHeight * .5, Previous + 24)); Ys.Add(Previous);
            if (I.Index == Index) Found = K;
        }
        if (Found == INDEX_NONE) return FVector2D(-1000, -1000);
        const double Overflow = FMath::Max(0.0, Ys.Last() - Size.Y + GuideLabelHeight + 2);
        return FVector2D(X, Ys[Found] - Overflow);
    }
    virtual void Tick(const FGeometry& Geometry, double Time, float Delta) override
    {
        SCompoundWidget::Tick(Geometry, Time, Delta);
        const int32 Count = Session->Asset->Recipe.RadialGuides.Guides.Num();
        if (Count == Rows) return;
        Rows = Count; Canvas->ClearChildren();
        for (int32 I = 0; I < Count; ++I)
        {
            Canvas->AddSlot().Position_Lambda([this, I] { return Anchor(I) + FVector2D(GuideTickWidth - 4, -9); }).Size(FVector2D(GuideTickWidth + 8, 18))
                [SNew(SCLRadialGuideHandle).Session(Session).Client(Client).Overlay(SharedThis(this)).Index(I)];
            const auto LabelHandle = SNew(SCLRadialGuideHandle).Session(Session).Client(Client).Overlay(SharedThis(this)).Index(I).Label(true);
            Canvas->AddSlot().Position_Lambda([this, I] { return Label(I); })
                .Size_Lambda([LabelHandle] { return FVector2D(FMath::Min(300.0, double(LabelHandle->GetDesiredSize().X)), GuideLabelHeight); })[LabelHandle];
        }
        Canvas->AddSlot().Position_Lambda([this] { return Label(-3); }).Size(FVector2D(150, GuideLabelHeight))
            [SNew(SBox).VAlign(VAlign_Center).Visibility(EVisibility::HitTestInvisible)
                [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    .Text(FText::FromString(TEXT("Sky boundary"))).ColorAndOpacity(GuideLabelColor)]];
        Canvas->AddSlot().Position_Lambda([this] { return Label(-2); }).Size(FVector2D(80, GuideLabelHeight))
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)).Text(FText::FromString(TEXT("Core"))).ColorAndOpacity(GuideLabelColor)]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)
                    [SNew(SBox).WidthOverride(16).HeightOverride(16).VAlign(VAlign_Center)
                        .Visibility_Lambda([this] { return Session->bReadOnly ? EVisibility::Collapsed : EVisibility::Visible; })
                        [SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton").ContentPadding(0)
                        .ToolTipText(FText::FromString(TEXT("Add a Radial Guide just above Core, then drag it to the desired height")))
                        .IsEnabled_Lambda([this] { return Session->Asset->Recipe.RadialGuides.Guides.Num() < 128; })
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
            // Leave one full tick width clear between the projected edge and tick.
            const FVector2D A = I.Position * Geometry.GetLocalSize() + FVector2D(GuideTickWidth, 0), L = Label(I.Index);
            const FLinearColor Color = GuideTint(I.Index, Session->Selected) * Style.GetColorAndOpacityTint();
            TArray<FVector2D> Tick{A, A + FVector2D(GuideTickWidth, 0)};
            FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Tick, ESlateDrawEffect::None, Color, true, 2);
            const double LabelCentreY = L.Y + GuideLabelHeight * .5;
            TArray<FVector2D> Leader{A + FVector2D(GuideTickWidth, 0), FVector2D(L.X - 8, LabelCentreY), FVector2D(L.X - 3, LabelCentreY)};
            FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Leader, ESlateDrawEffect::None, Color, true, 1);
        }
        return SCompoundWidget::OnPaint(Args, Geometry, Clip, Elements, Layer + 1, Style, Enabled);
    }
};

struct FCLSectionBuffers
{
    FCLSectionGeometry Geometry;
    TArray<FVector> Normals;
    TArray<FVector2D> UV;
    TArray<FProcMeshTangent> Tangents;
    static FCLSectionBuffers Build(const FCLCompiledPlanetRecipe& Plan, double Width)
    {
        FCLSectionBuffers B; B.Geometry = FCLSectionGeometry::Build(Plan, 0, Width);
        B.Finish(); return B;
    }
    void Finish()
    {
        auto& B = *this;
        B.UV.Init(FVector2D::ZeroVector, B.Geometry.Vertices.Num());
        // Pure array math: no scene components or other UObject state on the worker.
        // The generic helper searches every vertex for overlaps for every corner
        // (quadratic at detailed preview sizes). Our topology already shares top
        // vertices and splits cut faces: accumulate normals by index in one pass.
        B.Normals.Init(FVector::ZeroVector, B.Geometry.Vertices.Num());
        for (int32 I = 0; I < B.Geometry.Triangles.Num(); I += 3)
        {
            const int32 A = B.Geometry.Triangles[I], C = B.Geometry.Triangles[I + 1], D = B.Geometry.Triangles[I + 2];
            const FVector Normal = FVector::CrossProduct(B.Geometry.Vertices[D] - B.Geometry.Vertices[A],
                B.Geometry.Vertices[C] - B.Geometry.Vertices[A]).GetSafeNormal();
            B.Normals[A] += Normal; B.Normals[C] += Normal; B.Normals[D] += Normal;
        }
        B.Tangents.Reserve(B.Normals.Num());
        for (FVector& Normal : B.Normals)
        {
            Normal = Normal.GetSafeNormal();
            const FVector Axis = FMath::Abs(Normal.X) < .9 ? FVector::XAxisVector : FVector::YAxisVector;
            B.Tangents.Emplace((Axis - Normal * FVector::DotProduct(Axis, Normal)).GetSafeNormal(), false);
        }
    }
};
class SCLSectionViewport : public SEditorViewport
{
public:
    SLATE_BEGIN_ARGS(SCLSectionViewport) {} SLATE_END_ARGS()
    FCLSectionScene Scene;
    TStrongObjectPtr<UProceduralMeshComponent> Mesh;
    TSharedPtr<FCLSectionClient> Client;
    bool bCameraFramed = false;
    TFuture<FCLSectionBuffers> BuildTask;
    TSharedPtr<const FCLCompiledPlanetRecipe> PendingPlan;
    double PendingWidth = 500;
    uint64 Revision = 0, TaskRevision = 0;
    int32 StartedBuilds = 0, AppliedBuilds = 0;
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
    bool IsUpdating() const { return BuildTask.IsValid() || PendingPlan.IsValid(); }
    void CancelPendingBuild()
    { ++Revision; PendingPlan.Reset(); }
    void PumpBuild()
    {
        if (BuildTask.IsValid() && BuildTask.IsReady())
        {
            auto Buffers = BuildTask.Get(); BuildTask = TFuture<FCLSectionBuffers>();
            if (TaskRevision == Revision) ApplyBuffers(Buffers);
        }
        if (!BuildTask.IsValid() && PendingPlan)
        {
            const auto Plan = PendingPlan; const double Width = PendingWidth;
            PendingPlan.Reset(); TaskRevision = Revision; ++StartedBuilds;
            BuildTask = Async(EAsyncExecution::ThreadPool, [Plan, Width] { return FCLSectionBuffers::Build(*Plan, Width); });
        }
    }
    virtual void Tick(const FGeometry& G, double T, float D) override
    { SEditorViewport::Tick(G, T, D); PumpBuild(); }
    void RebuildAsync(TSharedPtr<const FCLCompiledPlanetRecipe> Plan)
    {
        if (!Plan) { CancelPendingBuild(); return; }
        Client->Plan = Plan;
        if (Client->AreaWidthMeters <= 0) Client->AreaWidthMeters = FCLSectionGeometry::DefaultWidth(*Plan);
        ++Revision; PendingPlan = MoveTemp(Plan); PendingWidth = Client->AreaWidthMeters;
        PumpBuild();
    }
    void Rebuild(TSharedPtr<const FCLCompiledPlanetRecipe> Plan)
    {
        CancelPendingBuild();
        Client->Plan = Plan;
        if (!Plan) return;
        if (Client->AreaWidthMeters <= 0) Client->AreaWidthMeters = FCLSectionGeometry::DefaultWidth(*Plan);
        ApplyBuffers(FCLSectionBuffers::Build(*Plan, Client->AreaWidthMeters));
    }
    void ApplyBuffers(const FCLSectionBuffers& B)
    {
        ++AppliedBuilds; const auto& G = B.Geometry;
        Mesh->CreateMeshSection_LinearColor(0, G.Vertices, G.Triangles, B.Normals, B.UV, G.Colors, B.Tangents, false);
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
#if WITH_DEV_AUTOMATION_TESTS
    friend class FCLTerrainWorkspaceTest;
#endif
    TSharedPtr<SCLSectionViewport> Preview;
    TSharedPtr<SCLHeightAnnotations> Annotations;
    TSharedPtr<FCLRadialGuideSession> RadialGuidesSession;
    TSharedPtr<FCLSelectionInput> SelectionInput;
    TStrongObjectPtr<UCLTerrainTypeAsset> TerrainSource;
    TSharedPtr<FCLTerrainEditing> TerrainEditing;
    TStrongObjectPtr<UCLPlanetRecipeAsset> PreviewContext;
    TSharedPtr<SCLNoiseLayerStack> NoiseStack;
    TSharedPtr<SVerticalBox> RegionControls;
    FDelegateHandle TerrainChanged;
    UCLPlanetRecipeAsset* RecipeAsset() const
    { return PreviewContext ? PreviewContext.Get() : CastChecked<UCLPlanetRecipeAsset>(EditingAsset); }
    FString Status;
    double PendingAreaWidth = 0;
    bool bAreaDragging = false;
    bool bPreviewInvalid = false;
    void ApplyPreviewArea()
    {
        if (!Preview || !Preview->Client->Plan || bPreviewInvalid) return;
        Preview->Client->AreaWidthMeters = PendingAreaWidth;
        if (TerrainEditing) Refresh();
        else if(!Preview->Client->Plan->Regions.IsEmpty() || !Preview->Client->Plan->SurfacePatches.IsEmpty()) Preview->RebuildAsync(Preview->Client->Plan);
        else Preview->Rebuild(Preview->Client->Plan);
    }
    FText AreaCaption() const
    {
        const double Width = Preview ? Preview->Client->AreaWidthMeters : 0;
        return FText::FromString(FString::Printf(TEXT("Viewing %.0f m × %.0f m"), Width, Width));
    }
    FDelegateHandle Changed;
    static const FName SectionTab;
    static const FName NoiseTab;
public:
    virtual ~FCLRecipeToolkit()
    {
        if (SelectionInput && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(SelectionInput);
        if (EditingAsset) RecipeAsset()->OnRecipeChanged.Remove(Changed);
        if (TerrainEditing) TerrainEditing->Asset->OnTerrainChanged.Remove(TerrainChanged);
    }
    void Open(UCLPlanetRecipeAsset* Asset, EToolkitMode::Type Mode, TSharedPtr<IToolkitHost> Host)
    {
        EditingAsset = Asset;
        OpenDesigner(Mode, Host);
    }
    void OpenTerrain(UCLTerrainTypeAsset* Asset, const FCLRadialGuideSet* InitialRadialGuides, EToolkitMode::Type Mode, TSharedPtr<IToolkitHost> Host)
    {
        EditingAsset = Asset;
        // Reference scale and seed belong to the disposable evaluator, never a
        // contextual planet. Only the caller's guide snapshot can cross here.
        TerrainEditing = MakeShared<FCLTerrainEditing>(); TerrainEditing->Open(Asset);
        if (InitialRadialGuides) TerrainEditing->Context.RadialGuides = *InitialRadialGuides;
        PreviewContext.Reset(NewObject<UCLPlanetRecipeAsset>(GetTransientPackage(), NAME_None, RF_Transactional));
        PreviewContext->Recipe = TerrainEditing->Context;
        TerrainChanged = Asset->OnTerrainChanged.AddSP(this, &FCLRecipeToolkit::RefreshTerrain);
        OpenDesigner(Mode, Host);
    }
    void RefreshTerrain()
    {
        TerrainEditing->Reconcile();
        if (NoiseStack) NoiseStack->Refresh();
        Refresh();
    }
    void UsePreviewRadialGuides(const FCLRadialGuideSet& RadialGuides)
    {
        if (!TerrainEditing || !RadialGuidesSession) return;
        RadialGuidesSession->UsePreviewRadialGuides(RadialGuides);
        if (NoiseStack) NoiseStack->Refresh();
    }
    void OpenDesigner(EToolkitMode::Type Mode, TSharedPtr<IToolkitHost> Host)
    {
        auto* Asset = RecipeAsset();
        RadialGuidesSession = MakeShared<FCLRadialGuideSession>();
        if (TerrainEditing)
        {
            const bool bOpened = RadialGuidesSession->OpenPreview(Asset);
            check(bOpened);
        }
        else RadialGuidesSession->Open(Asset);
        SelectionInput = MakeShared<FCLSelectionInput>(RadialGuidesSession);
        FSlateApplication::Get().RegisterInputPreProcessor(SelectionInput);
        RadialGuidesSession->OnPreviewChanged = [this]
        {
            if (!Preview) return;
            Preview->Client->RadialGuides = RadialGuidesSession->Asset->Recipe.RadialGuides;
            Preview->Client->UpdateIndicators();
            if (Annotations) Annotations->Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
        };
        BuildDetailsPanel();
        if (TerrainEditing) DetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& P)
        { return P.Property.GetFName() != GET_MEMBER_NAME_CHECKED(FCLTerrainType, NoiseLayers); }));
        Changed = Asset->OnRecipeChanged.AddSP(this, &FCLRecipeToolkit::Refresh);
        const auto Layout = FTabManager::NewLayout(TerrainEditing ? "CLTerrainDesigner_v1" : "CLRecipeDesigner_v2")->AddArea(
            FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
            ->Split(FTabManager::NewStack()->SetSizeCoefficient(.34)->AddTab(TerrainEditing ? NoiseTab : DetailsTabId, ETabState::OpenedTab)
                ->AddTab(TerrainEditing ? DetailsTabId : NoiseTab, TerrainEditing ? ETabState::OpenedTab : ETabState::ClosedTab)
                ->AddTab(ValidationTabId, ETabState::OpenedTab)->SetForegroundTab(TerrainEditing ? NoiseTab : DetailsTabId))
            ->Split(FTabManager::NewStack()->SetSizeCoefficient(.68)->AddTab(SectionTab, ETabState::OpenedTab)->SetHideTabWell(true)));
        InitAssetEditor(Mode, Host, GetToolkitFName(), Layout, true, true, EditingAsset.Get());
        const auto Extender = MakeShared<FExtender>();
        Extender->AddToolBarExtension(TEXT("Asset"), EExtensionHook::After, nullptr,
            FToolBarExtensionDelegate::CreateLambda([this](FToolBarBuilder& Builder)
            {
                Builder.AddToolBarButton(FUIAction(FExecuteAction::CreateSP(this, &FCLRecipeToolkit::Export)), NAME_None,
                    FText::FromString(TerrainEditing ? TEXT("Export terrain type") : TEXT("Export recipe")), FText::FromString(TEXT("Export the authored definition through Foundation")));
            }));
        AddToolbarExtender(Extender); RegenerateMenusAndToolbars();
        // Explicit asset target; don't use Foundation's legacy first-PIE-service lookup.
        Refresh();
    }
    virtual void GetSaveableObjects(TArray<UObject*>& Objects) const override
    { FCFModelEditorToolkit::GetSaveableObjects(Objects); if (RadialGuidesSession && !TerrainEditing) Objects.AddUnique(RadialGuidesSession->Library.Get()); }
    TSharedRef<SWidget> TerrainMenu() { return TerrainSourceMenu(FGuid(), false); }
    TSharedRef<SWidget> TerrainSourceMenu(FGuid RegionId, bool bNewRegion)
    {
        FMenuBuilder Menu(true, nullptr); TArray<FAssetData> Assets;
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByClass(UCLTerrainTypeAsset::StaticClass()->GetClassPathName(), Assets, true);
        Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
        if (Assets.IsEmpty()) Menu.AddMenuEntry(FText::FromString(TEXT("No terrain types found")), FText(), FSlateIcon(), FUIAction());
        for (const FAssetData& Data : Assets)
        {
            auto* Source = Cast<UCLTerrainTypeAsset>(Data.GetAsset()); if (!Source) continue;
            if ((bNewRegion || RegionId.IsValid()) && Source->TerrainType.Coordinates != RecipeAsset()->Recipe.Coordinates) continue;
            const TWeakObjectPtr<UCLTerrainTypeAsset> Weak(Source);
            const bool bTemplate = Data.PackagePath.ToString().StartsWith(TEXT("/CatalystLandform/"));
            Menu.AddMenuEntry(FText::FromString(Source->TerrainType.Name + (bTemplate ? TEXT(" (template)") : TEXT(""))),
                FText::FromString(Source->TerrainType.Description + TEXT("\n") + Source->GetPathName()), FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([this, Weak, RegionId, bNewRegion]
                {
                    if (!Weak.IsValid()) return;
                    if (TerrainEditing) { CLOpenTerrainDesigner(Weak.Get(), &RecipeAsset()->Recipe.RadialGuides); return; }
                    if(bNewRegion || RegionId.IsValid())
                    {
                        auto Candidate=RecipeAsset()->Recipe;FGuid Target=RegionId;
                        if(bNewRegion)
                        {
                            FCLTerrainRegion Region;Region.Id=FGuid::NewGuid();Region.Name=Weak->TerrainType.Name;
                            Target=Region.Id;Candidate.TerrainRegions.Add(Region);
                        }
                        FString Error;
                        if(!Weak->TerrainType.ApplyToRegion(Candidate,Target,Error)){Status=TEXT("Region was not applied: ")+Error;return;}
                        const FScopedTransaction Tx(NSLOCTEXT("CL", "ApplyRegion", "Apply terrain to planet region"));
                        RecipeAsset()->Modify();RecipeAsset()->Recipe=MoveTemp(Candidate);RecipeAsset()->PostEditChange();RecipeAsset()->MarkPackageDirty();
                        return;
                    }
                    const FScopedTransaction Tx(NSLOCTEXT("CL", "ApplyTerrainType", "Apply terrain type")); FString Error;
                    if (!Weak->ApplyToRecipe(RecipeAsset(), Error)) Status = TEXT("Terrain type was not applied: ") + Error;
                })));
        }
        return Menu.MakeWidget();
    }
    void RefreshRegionControls()
    {
        if(!RegionControls)return;
        RegionControls->ClearChildren();
        if(TerrainEditing)return;
        RegionControls->AddSlot().AutoHeight()[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(FText::FromString(TEXT("Terrain regions")))]
            +SHorizontalBox::Slot().AutoWidth().Padding(12,0)[SNew(SComboButton)
                .IsEnabled_Lambda([this]{return RecipeAsset()->Recipe.Coordinates!=ECLTerrainCoordinates::Sphere && RecipeAsset()->Recipe.TerrainRegions.Num()<8;})
                .OnGetMenuContent_Lambda([this]{return TerrainSourceMenu(FGuid(),true);})
                .ButtonContent()[SNew(STextBlock).Text(FText::FromString(TEXT("Add region")))]]
            +SHorizontalBox::Slot().FillWidth(1).Padding(12,0)[SNew(STextBlock).AutoWrapText(true)
                .Text(FText::FromString(TEXT("Edit region masks and transition widths in Details")))]];
        for(const auto& Region:RecipeAsset()->Recipe.TerrainRegions)
        {
            const FGuid Id=Region.Id;
            RegionControls->AddSlot().AutoHeight().Padding(0,4)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(FText::FromString(Region.Name+TEXT(": "))) ]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SComboButton).OnGetMenuContent_Lambda([this,Id]{return TerrainSourceMenu(Id,false);})
                    .ButtonContent()[SNew(STextBlock).Text(FText::FromString(Region.TerrainTypeName))]]
                +SHorizontalBox::Slot().AutoWidth().Padding(12,0)[SNew(SButton).Text(FText::FromString(TEXT("Remove")))
                    .OnClicked_Lambda([this,Id]
                    {
                        const FScopedTransaction Tx(NSLOCTEXT("CL","RemoveRegion","Remove terrain region"));
                        RecipeAsset()->Modify();RecipeAsset()->Recipe.TerrainRegions.RemoveAll([&](const auto& R){return R.Id==Id;});
                        RecipeAsset()->PostEditChange();RecipeAsset()->MarkPackageDirty();return FReply::Handled();
                    })]];
        }
    }
    TSharedRef<SWidget> RadialGuidesMenu()
    {
        FMenuBuilder Menu(true, nullptr);
        for (const FCLRadialGuideSet& S : RadialGuidesSession->Library->Library.Sets)
            Menu.AddMenuEntry(FText::FromString(S.Name), FText::FromString(TerrainEditing ? TEXT("Preview this guide set without saving it to the terrain or changing a planet") : TEXT("Apply this Radial Guides to this planet recipe")), FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([Session = RadialGuidesSession, Id = S.Id] { Session->Apply(Id); })));
        return Menu.MakeWidget();
    }
    virtual FName GetToolkitFName() const override { return TerrainEditing ? TEXT("CLTerrainType") : TEXT("CLRecipeDesigner"); }
    virtual FText GetBaseToolkitName() const override { return TerrainEditing ? NSLOCTEXT("CL", "TerrainDesigner", "Landform Designer") : NSLOCTEXT("CL", "PlanetRecipeEditor", "Planet Recipe"); }
    virtual FString GetWorldCentricTabPrefix() const override { return TEXT("Landform"); }
    void Export() { HandleExportJson(); }
    virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& Manager) override
    {
        FCFModelEditorToolkit::RegisterTabSpawners(Manager);
        Manager->RegisterTabSpawner(SectionTab, FOnSpawnTab::CreateSP(this, &FCLRecipeToolkit::SpawnSection));
        Manager->RegisterTabSpawner(NoiseTab, FOnSpawnTab::CreateSP(this, &FCLRecipeToolkit::SpawnNoise)).SetDisplayName(FText::FromString(TEXT("Terrain layers")));
    }
    virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& Manager) override
    { Manager->UnregisterTabSpawner(SectionTab); Manager->UnregisterTabSpawner(NoiseTab); FCFModelEditorToolkit::UnregisterTabSpawners(Manager); }
    TSharedRef<SDockTab> SpawnNoise(const FSpawnTabArgs&)
    {
        if (!TerrainEditing) return SNew(SDockTab)[SNew(STextBlock).Text(FText::FromString(TEXT("Use Edit source to author a reusable terrain type.")))];
        return SNew(SDockTab)[SAssignNew(NoiseStack, SCLNoiseLayerStack).Session(TerrainEditing)
            .OnPreviewChanged(FSimpleDelegate::CreateSP(this, &FCLRecipeToolkit::Refresh))];
    }
    void CreateTerrain(bool bDuplicate)
    {
        auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
        UObject* New = bDuplicate ? Tools.DuplicateAssetWithDialog(TEXT("DA_TerrainVariant"), TEXT("/Game/LandformDesigner/TerrainTypes"), TerrainEditing->Asset) :
            Tools.CreateAssetWithDialog(TEXT("DA_NewTerrain"), TEXT("/Game/LandformDesigner/TerrainTypes"), UCLTerrainTypeAsset::StaticClass(), NewObject<UCLTerrainTypeFactory>());
        if (auto* Terrain = Cast<UCLTerrainTypeAsset>(New))
        {
            Terrain->TerrainType.Name = bDuplicate ? TerrainEditing->Asset->TerrainType.Name + TEXT(" variant") : TEXT("New terrain");
            Terrain->MarkPackageDirty(); Terrain->PostEditChange();
            TArray<UPackage*> Packages{Terrain->GetOutermost()};
            FEditorFileUtils::PromptForCheckoutAndSave(Packages, false, false);
            CLOpenTerrainDesigner(Terrain, &RecipeAsset()->Recipe.RadialGuides);
        }
    }
    void Refresh()
    {
        auto* Asset = RecipeAsset();
        if (TerrainEditing) TerrainSource.Reset(TerrainEditing->Asset);
        else if (!Asset->Recipe.TerrainTypeId.IsValid()) TerrainSource.Reset();
        else if (!TerrainSource || TerrainSource->TerrainType.Id != Asset->Recipe.TerrainTypeId)
        {
            TerrainSource.Reset(); TArray<FAssetData> Assets;
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByClass(UCLTerrainTypeAsset::StaticClass()->GetClassPathName(), Assets, true);
            for (const auto& Data : Assets) if (auto* Source = Cast<UCLTerrainTypeAsset>(Data.GetAsset()); Source && Source->TerrainType.Id == Asset->Recipe.TerrainTypeId)
            { TerrainSource.Reset(Source); break; }
        }
        if (Preview) Preview->Client->RadialGuides = Asset->Recipe.RadialGuides;
        if (TerrainEditing) TerrainEditing->Context = Asset->Recipe;
        const auto Plan = TerrainEditing ? TerrainEditing->CompilePreview(Status, PendingAreaWidth > 0 ? PendingAreaWidth : 500) : FCLPlanetCompiler::Compile(Asset->Recipe, Status);
        bPreviewInvalid = !Plan.IsValid();
        if (Plan)
        {
            Status = TerrainEditing ? FString::Printf(TEXT("%s | %d layers | %s"), *TerrainEditing->Asset->TerrainType.Name,
                Plan->SurfacePatches.IsEmpty()?Plan->Layers.Num():Plan->SurfacePatches[0]->Layers.Num(), TerrainEditing->Solo != INDEX_NONE ? TEXT("Solo preview") : TEXT("Terrain preview")) :
                FString::Printf(TEXT("%s | radius %.0f m | %d base layers | %d regions"), *Asset->Recipe.Name, Plan->RadiusMeters, Asset->Recipe.NoiseLayers.Num(), Asset->Recipe.TerrainRegions.Num());
            if (Preview)
            {
                if (TerrainEditing || !Plan->Regions.IsEmpty() || !Plan->SurfacePatches.IsEmpty()) Preview->RebuildAsync(Plan); else Preview->Rebuild(Plan);
                if (PendingAreaWidth <= 0) PendingAreaWidth = Preview->Client->AreaWidthMeters;
            }
        }
        else { if (Preview) Preview->CancelPendingBuild(); Status = TEXT("Preview out of date: ") + Status; }
        RefreshRegionControls();
        RefreshAllUI(EditingAsset, false);
    }
    TSharedRef<SDockTab> SpawnSection(const FSpawnTabArgs&)
    {
        auto Tab=SNew(SDockTab)[SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] { return FText::FromString(Status); })]
            + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()[SNew(SComboButton).OnGetMenuContent(this, &FCLRecipeToolkit::TerrainMenu)
                    .ToolTipText(FText::FromString(TerrainEditing ? TEXT("Open a reusable terrain definition using the current preview guides") : TEXT("Apply a terrain type's layers using this planet's Radial Guides, radius and seed")))
                    .ButtonContent()[SNew(STextBlock).Text_Lambda([this]
                    { const FString& Name = TerrainEditing ? TerrainEditing->Asset->TerrainType.Name : RecipeAsset()->Recipe.TerrainTypeName;
                      return FText::FromString(TEXT("Terrain: ") + (Name.IsEmpty() ? TEXT("Custom") : Name)); })]]
                + SHorizontalBox::Slot().AutoWidth().Padding(8, 0)[SNew(SButton).Text(FText::FromString(TEXT("Edit source")))
                    .Visibility_Lambda([this] { return TerrainEditing ? EVisibility::Collapsed : EVisibility::Visible; })
                    .IsEnabled_Lambda([this] { return TerrainSource.IsValid(); })
                    .ToolTipText(FText::FromString(TEXT("Edit the reusable terrain type. Reapply it to update this planet's copy.")))
                    .OnClicked_Lambda([this]
                    { if (TerrainSource) CLOpenTerrainDesigner(TerrainSource.Get(), &RecipeAsset()->Recipe.RadialGuides); return FReply::Handled(); })]
                + SHorizontalBox::Slot().AutoWidth().Padding(8, 0)[SNew(SButton).Text(FText::FromString(TEXT("New")))
                    .Visibility_Lambda([this] { return TerrainEditing ? EVisibility::Visible : EVisibility::Collapsed; })
                    .OnClicked_Lambda([this] { CreateTerrain(false); return FReply::Handled(); })]
                + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Save as variant…")))
                    .Visibility_Lambda([this] { return TerrainEditing ? EVisibility::Visible : EVisibility::Collapsed; })
                    .OnClicked_Lambda([this] { CreateTerrain(true); return FReply::Handled(); })]
                + SHorizontalBox::Slot().FillWidth(1).Padding(8, 0)[SNew(STextBlock).AutoWrapText(true)
                    .Text_Lambda([this] { return FText::FromString(TerrainEditing ? TEXT("Save edits to this reusable terrain type") : TEXT("Edit this planet's terrain layers in Details")); })]]
            + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()[SNew(SComboButton).OnGetMenuContent(this, &FCLRecipeToolkit::RadialGuidesMenu)
                    .ToolTipText(FText::FromString(TerrainEditing ? TEXT("Temporary preview guides; the terrain stores named references, not these guide values") : TEXT("Choose this planet's Radial Guides")))
                    .ButtonContent()[SNew(STextBlock).Text(FText::FromString(TerrainEditing ? TEXT("Preview Radial Guides") : TEXT("Radial Guides")))]]
                + SHorizontalBox::Slot().FillWidth(1).Padding(8, 0)[SNew(SInlineEditableTextBlock)
                    .IsReadOnly_Lambda([this] { return TerrainEditing.IsValid(); })
                    .Text_Lambda([this] { return FText::FromString(RadialGuidesSession->Asset->Recipe.RadialGuides.Name); })
                    .OnVerifyTextChanged_Lambda([this](const FText& Text, FText& Error)
                    {
                        const FString N = Text.ToString().TrimStartAndEnd();
                        if (N.IsEmpty() || RadialGuidesSession->Library->Library.Sets.ContainsByPredicate([&](const FCLRadialGuideSet& S)
                            { return S.Id != RadialGuidesSession->Asset->Recipe.RadialGuides.Id && S.Name.Equals(N, ESearchCase::IgnoreCase); }))
                        { Error = FText::FromString(TEXT("Use a unique, nonempty Radial Guides name")); return false; }
                        return true;
                    })
                    .OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type Type)
                    {
                        if (Type == ETextCommit::OnCleared || Text.ToString().TrimStartAndEnd().IsEmpty()) return;
                        RadialGuidesSession->Begin(NSLOCTEXT("CL", "RenameRadial Guides", "Rename Radial Guides"));
                        RadialGuidesSession->Asset->Recipe.RadialGuides.Name = Text.ToString().TrimStartAndEnd(); RadialGuidesSession->Changed(); RadialGuidesSession->End();
                    })]
                + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("New guide set")))
                    .Visibility_Lambda([this] { return TerrainEditing ? EVisibility::Collapsed : EVisibility::Visible; })
                    .OnClicked_Lambda([this] { RadialGuidesSession->NewSet(); return FReply::Handled(); })]
                + SHorizontalBox::Slot().AutoWidth().Padding(8, 0)[SNew(SButton).Text(FText::FromString(TEXT("Remove guide set")))
                    .Visibility_Lambda([this] { return TerrainEditing ? EVisibility::Collapsed : EVisibility::Visible; })
                    .OnClicked_Lambda([this] { RadialGuidesSession->RemoveSet(); return FReply::Handled(); })]]
            + SVerticalBox::Slot().AutoHeight().Padding(8,0)[SAssignNew(RegionControls,SVerticalBox)
                .Visibility_Lambda([this]{return TerrainEditing?EVisibility::Collapsed:EVisibility::Visible;})]
            + SVerticalBox::Slot().AutoHeight().Padding(8, 0)[SNew(STextBlock).AutoWrapText(true)
                .Visibility_Lambda([this] { return TerrainEditing ? EVisibility::Visible : EVisibility::Collapsed; })
                .Text(FText::FromString(TEXT("Preview guides only. Guide values are supplied when this terrain is assembled onto a planet."))) ]
            + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()[SNew(SCheckBox).IsChecked_Lambda([this] { return !Preview || Preview->Client->bGuides ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }).OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                    { Preview->Client->bGuides = State == ECheckBoxState::Checked; Preview->Client->Invalidate(); })[SNew(STextBlock).Text(FText::FromString(TEXT("Radial Guides")))]]]
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
                + SOverlay::Slot()[SAssignNew(Annotations, SCLHeightAnnotations).Client(Preview->Client).Session(RadialGuidesSession)
                    .Visibility_Lambda([this] { return TerrainEditing ? EVisibility::HitTestInvisible : EVisibility::SelfHitTestInvisible; })]
                + SOverlay::Slot().VAlign(VAlign_Top).Padding(12)[SNew(SBorder).Padding(10)
                    .Visibility_Lambda([this] { return bPreviewInvalid ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
                    .BorderBackgroundColor(FLinearColor(.12, .06, .025, .95))
                    [SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FLinearColor(1, .65, .3))
                        .Text_Lambda([this] { return FText::FromString(Status + TEXT("\nThe displayed terrain does not reflect the current layer settings.")); })]]]
            + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SBox).HeightOverride(42)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]
                {
                    FString Legend = RadialGuidesSession->Gesture ? TEXT("Release to update terrain | Esc to cancel") :
                        TEXT("Left drag: orbit | Middle drag: pan cropped view | Right drag or wheel: zoom");
                    if (!TerrainEditing) Legend += TEXT(" | + beside Core: add Radial Guide");
                    if (Preview && Preview->IsUpdating()) Legend = TEXT("Updating terrain preview… | ") + Legend;
                    if (Preview && Preview->Client->Plan && Preview->Client->Plan->Coordinates == ECLTerrainCoordinates::Sphere && Preview->Client->AreaWidthMeters / FMath::Sqrt(2.0) >=
                        UE_PI * FCLSectionGeometry::ReferenceSurfaceRadius(*Preview->Client->Plan))
                        Legend += TEXT(" | Area passes the far side of this small planet: terrain repeats");
                    return FText::FromString(Legend);
                })]]];
        RefreshRegionControls();
        return Tab;
    }
};
const FName FCLRecipeToolkit::SectionTab(TEXT("CLRecipeSection"));
const FName FCLRecipeToolkit::NoiseTab(TEXT("CLNoiseLayers"));

void OpenPlanetRecipe()
{
    if (!GEditor) return;
    const auto* Planet = GEditor->GetSelectedActors()->GetTop<ACLPlanet>();
    auto* Asset = Planet && Planet->RecipeAsset ? Planet->RecipeAsset.Get() :
        LoadObject<UCLPlanetRecipeAsset>(nullptr, TEXT("/Game/LandformDesigner/DA_RollingPlains.DA_RollingPlains"));
    if (Asset) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
}
void OpenLandformDesigner()
{
    if (!GEditor) return;
    const auto* Planet = GEditor->GetSelectedActors()->GetTop<ACLPlanet>();
    const auto* Recipe = Planet && Planet->RecipeAsset ? &Planet->RecipeAsset->Recipe : nullptr;
    UCLTerrainTypeAsset* Terrain = nullptr;
    if (Recipe && Recipe->TerrainTypeId.IsValid())
    {
        TArray<FAssetData> Assets;
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByClass(UCLTerrainTypeAsset::StaticClass()->GetClassPathName(), Assets, true);
        Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });
        for (const auto& Data : Assets)
            if (auto* Source = Cast<UCLTerrainTypeAsset>(Data.GetAsset()); Source && Source->TerrainType.Id == Recipe->TerrainTypeId)
            {
                Terrain = Source;
                if (Data.PackagePath.ToString().StartsWith(TEXT("/Game/"))) break;
            }
    }
    if (!Terrain) Terrain = LoadObject<UCLTerrainTypeAsset>(nullptr, TEXT("/CatalystLandform/TerrainTypes/DA_MountainRidges.DA_MountainRidges"));
    if (Terrain) CLOpenTerrainDesigner(Terrain, Recipe ? &Recipe->RadialGuides : nullptr);
}
class FCLDesignerModule : public IModuleInterface
{
    TUniquePtr<FAutoConsoleCommand> Command;
    TUniquePtr<FAutoConsoleCommand> TerrainCommand;
    TUniquePtr<FAutoConsoleCommand> PlanetCommand;
    FDelegateHandle MenuHandle;
public:
    virtual void StartupModule() override
    {
        Command = MakeUnique<FAutoConsoleCommand>(TEXT("Landform.Designer"), TEXT("Open a terrain definition; selected planet supplies preview guides only"), FConsoleCommandDelegate::CreateStatic(&OpenLandformDesigner));
        PlanetCommand = MakeUnique<FAutoConsoleCommand>(TEXT("Landform.PlanetRecipe"), TEXT("Open selected planet recipe or rolling-plains recipe"), FConsoleCommandDelegate::CreateStatic(&OpenPlanetRecipe));
        TerrainCommand = MakeUnique<FAutoConsoleCommand>(TEXT("Landform.TerrainDesigner"), TEXT("Open terrain type [asset path] [optional recipe path supplying preview Radial Guides only]"),
            FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
            {
                auto* Terrain = LoadObject<UCLTerrainTypeAsset>(nullptr, Args.IsEmpty() ? TEXT("/CatalystLandform/TerrainTypes/DA_MountainRidges.DA_MountainRidges") : *Args[0]);
                auto* Context = Args.Num() > 1 ? LoadObject<UCLPlanetRecipeAsset>(nullptr, *Args[1]) : nullptr;
                if (Terrain) CLOpenTerrainDesigner(Terrain, Context ? &Context->Recipe.RadialGuides : nullptr);
            }));
        MenuHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]
        {
            FToolMenuOwnerScoped Owner(TEXT("CatalystLandformDesigner"));
            auto* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
            Menu->AddSection(TEXT("CatalystLandform"), FText::FromString(TEXT("Catalyst"))).AddMenuEntry(TEXT("LandformDesigner"),
                FText::FromString(TEXT("Landform Designer")), FText::FromString(TEXT("Edit a terrain definition; selected planet supplies preview Radial Guides only")),
                FSlateIcon(), FUIAction(FExecuteAction::CreateStatic(&OpenLandformDesigner)));
            Menu->FindOrAddSection(TEXT("CatalystLandform")).AddMenuEntry(TEXT("LandformPlanetRecipe"),
                FText::FromString(TEXT("Planet Recipe")), FText::FromString(TEXT("Edit the selected planet recipe, or the rolling-plains recipe")),
                FSlateIcon(), FUIAction(FExecuteAction::CreateStatic(&OpenPlanetRecipe)));
        }));
    }
    virtual void ShutdownModule() override
    { Command.Reset(); TerrainCommand.Reset(); PlanetCommand.Reset(); UToolMenus::UnRegisterStartupCallback(MenuHandle); UToolMenus::UnregisterOwner(TEXT("CatalystLandformDesigner")); }
};
}

void CLOpenTerrainDesigner(UCLTerrainTypeAsset* Terrain, const FCLRadialGuideSet* PreviewRadialGuides, EToolkitMode::Type Mode, TSharedPtr<IToolkitHost> Host)
{
    if (!GEditor || !Terrain) return;
    TOptional<FCLRadialGuideSet> Guides;
    if (PreviewRadialGuides) Guides = *PreviewRadialGuides;
    if (Terrain->GetOutermost()->GetName().StartsWith(TEXT("/CatalystLandform/")))
    {
        // The plugin is a replaceable staged copy in development hosts. Author
        // under /Game so saved work survives restaging, and seed each slot once.
        const FString Folder = TEXT("/Game/LandformDesigner/TerrainTypes");
        const FString Package = Folder / Terrain->GetName();
        const FString Path = Package + TEXT(".") + Terrain->GetName();
        auto* Existing = FindObject<UObject>(nullptr, *Path);
        const bool bPackageExists = FPackageName::DoesPackageExist(Package);
        auto* Working = Existing ? Cast<UCLTerrainTypeAsset>(Existing) : bPackageExists ? LoadObject<UCLTerrainTypeAsset>(nullptr, *Path) : nullptr;
        if ((Existing || bPackageExists) && !Working)
        {
            UE_LOG(LogTemp, Error, TEXT("Cannot open terrain working copy: %s is not a readable terrain asset; existing content was preserved"), *Path);
            return;
        }
        if (!Working)
        {
            auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
            Working = Cast<UCLTerrainTypeAsset>(Tools.DuplicateAsset(Terrain->GetName(), Folder, Terrain));
            if (Working)
            {
                TArray<UPackage*> Packages{Working->GetOutermost()};
                FEditorFileUtils::PromptForCheckoutAndSave(Packages, false, false);
            }
        }
        if (!Working) return;
        Terrain = Working;
    }
    if (auto* Existing = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Terrain, true))
    {
        if (Guides.IsSet() && Existing->GetEditorName() == FName(TEXT("CLTerrainType")))
            static_cast<FCLRecipeToolkit*>(Existing)->UsePreviewRadialGuides(Guides.GetValue());
        return;
    }
    MakeShared<FCLRecipeToolkit>()->OpenTerrain(Terrain, Guides.IsSet() ? &Guides.GetValue() : nullptr, Mode, Host);
    UE_LOG(LogTemp, Display, TEXT("CL_TERRAIN_DESIGNER_READY: %s"), *Terrain->GetPathName());
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLDistanceInputUnitsTest, "Catalyst.Landform.Designer.DistanceInputUnits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLDistanceInputUnitsTest::RunTest(const FString&)
{
    auto Input = MakeShared<TNumericUnitTypeInterface<double>>(EUnit::Meters);
    Input->SetupFixedDisplay(1000);
    Input->UserDisplayUnits = EUnit::Centimeters;
    for (const FString Text : {TEXT("25000"), TEXT("25000 cm"), TEXT("250 m"), TEXT("0.25 km")})
    {
        const auto Value = Input->FromString(Text, 1000);
        TestTrue(*FString::Printf(TEXT("%s resolves to 250 stored metres"), *Text), Value.IsSet() && FMath::IsNearlyEqual(Value.GetValue(), 250.0));
    }
    TestTrue(TEXT("Distance field shows its input unit"), Input->ToString(250).EndsWith(TEXT(" cm")));
    const auto Offset = Input->FromString(TEXT("-25 cm"), 0);
    if (!TestTrue(TEXT("Signed offset converts into stored metres"), Offset.IsSet() && FMath::IsNearlyEqual(Offset.GetValue(), -.25))) return false;
    TStrongObjectPtr<UCLPlanetRecipeAsset> Asset(NewObject<UCLPlanetRecipeAsset>());
    Asset->Recipe = FCLPlanetCompiler::RollingPlains(); Asset->Recipe.NoiseLayers[1].Minimum.OffsetMeters = Offset.GetValue();
    FString Json, Error;
    TestTrue(TEXT("Recipe exports with unit-neutral editor labels"), Asset->ExportJsonString(Json, Error));
    TestTrue(TEXT("Existing serialized metre field name is preserved"), Json.Contains(TEXT("offsetMeters")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> Copy(NewObject<UCLPlanetRecipeAsset>());
    if (!TestTrue(TEXT("Recipe reloads"), Copy->ImportRecipeJson(Json, Error))) return false;
    TestEqual(TEXT("Changing display units cannot rescale saved terrain"), Copy->Recipe.NoiseLayers[1].Minimum.OffsetMeters, -.25);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLDeferredTerrainPreviewTest, "Catalyst.Landform.Designer.DeferredTerrainPreview",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLDeferredTerrainPreviewTest::RunTest(const FString&)
{
    FString Error; auto Recipe = FCLPlanetCompiler::RollingPlains(); Recipe.RadiusMeters = 1000;
    const auto First = FCLPlanetCompiler::Compile(Recipe, Error);
    Recipe.NoiseLayers[0].Minimum.OffsetMeters = 30;
    const auto Latest = FCLPlanetCompiler::Compile(Recipe, Error);
    const TSharedRef<SCLSectionViewport> Preview = SNew(SCLSectionViewport);
    Preview->Client->AreaWidthMeters = 100;
    Preview->RebuildAsync(First);
    for (int32 I = 0; I < 50; ++I) Preview->RebuildAsync(Latest);
    TestEqual(TEXT("Only one worker starts while changes coalesce"), Preview->StartedBuilds, 1);
    while (Preview->IsUpdating())
    { if (Preview->BuildTask.IsValid()) Preview->BuildTask.Wait(); Preview->PumpBuild(); }
    TestEqual(TEXT("Only the final pending snapshot starts a second worker"), Preview->StartedBuilds, 2);
    TestEqual(TEXT("Obsolete geometry never reaches the scene"), Preview->AppliedBuilds, 1);
    const auto Expected = FCLSectionGeometry::Build(*Latest, 0, 100);
    TestTrue(TEXT("Displayed mesh matches latest settings"), Preview->Mesh->GetProcMeshSection(0)->ProcVertexBuffer[0].Position.Equals(Expected.Vertices[0], .01));
    // Exercise the complete render-buffer path at the detailed grid cap, not
    // just height sampling. This catches accidental generic overlap searches.
    auto Dense = Expected;
    constexpr int32 DenseSide = 513;
    Dense.Vertices.Reset(); Dense.Triangles.Reset();
    for (int32 Y = 0; Y < DenseSide; ++Y)
        for (int32 X = 0; X < DenseSide; ++X) Dense.Vertices.Add(FVector(X, Y, .1 * X));
    for (int32 Y = 0; Y < DenseSide - 1; ++Y)
        for (int32 X = 0; X < DenseSide - 1; ++X)
        {
            const int32 A = Y * DenseSide + X;
            Dense.Triangles.Append({A, A + DenseSide + 1, A + 1, A, A + DenseSide, A + DenseSide + 1});
        }
    FCLSectionBuffers DenseBuffers; DenseBuffers.Geometry = MoveTemp(Dense); DenseBuffers.Finish();
    const FVector ExpectedNormal = FVector(-.1, 0, 1).GetSafeNormal();
    bool bFramesValid = DenseBuffers.Normals.Num() == DenseSide * DenseSide;
    for (int32 I = 0; I < DenseBuffers.Normals.Num(); ++I)
        bFramesValid &= DenseBuffers.Normals[I].Equals(ExpectedNormal, 1.e-6)
            && FMath::IsNearlyEqual(DenseBuffers.Tangents[I].TangentX.Size(), 1.0, 1.e-6)
            && FMath::Abs(FVector::DotProduct(DenseBuffers.Normals[I], DenseBuffers.Tangents[I].TangentX)) < 1.e-6;
    TestTrue(TEXT("Maximum-size preview produces outward unit normals and orthogonal unit tangents"), bFramesValid);
    const auto Eye = Preview->Client->GetViewLocation();
    Preview->RebuildAsync(First); Preview->CancelPendingBuild();
    while (Preview->IsUpdating())
    { if (Preview->BuildTask.IsValid()) Preview->BuildTask.Wait(); Preview->PumpBuild(); }
    TestEqual(TEXT("Invalid settings can cancel pending geometry without replacing the valid scene"), Preview->AppliedBuilds, 1);
    TestTrue(TEXT("Background completion does not reset camera framing"), Preview->Client->GetViewLocation().Equals(Eye));
    return true;
}

namespace
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTerrainWorkspaceTest, "Catalyst.Landform.Designer.TerrainWorkspace",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLTerrainWorkspaceTest::RunTest(const FString&)
{
    auto* Package = CreatePackage(*(TEXT("/Temp/CLTerrainWorkspace_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    TStrongObjectPtr<UCLTerrainTypeAsset> Terrain(NewObject<UCLTerrainTypeAsset>(Package, TEXT("Terrain"), RF_Public | RF_Standalone | RF_Transactional));
    Terrain->TerrainType.Id = FGuid::NewGuid(); Terrain->TerrainType.Name = TEXT("Authoring workspace test");
    Terrain->TerrainType.NoiseLayers = FCLPlanetCompiler::RollingPlains().NoiseLayers;
    const auto BeforeTerrain = Terrain->TerrainType;
    TStrongObjectPtr<UCLPlanetRecipeAsset> Planet(NewObject<UCLPlanetRecipeAsset>(GetTransientPackage(), NAME_None, RF_Transactional));
    Planet->Recipe = FCLPlanetCompiler::RollingPlains(); Planet->Recipe.RadiusMeters = 1400; Planet->Recipe.Seed = 829;
    Planet->Recipe.Name = TEXT("Private planet identity"); Planet->Recipe.BaseSurfaceNormalized = .71;
    const auto BeforePlanet = Planet->Recipe;
    const auto* SharedCatalog = LoadObject<UCLRadialGuideLibraryAsset>(nullptr, TEXT("/Game/LandformDesigner/DA_StrataLibrary.DA_StrataLibrary"));
    const auto BeforeCatalog = SharedCatalog ? SharedCatalog->Library : FCLRadialGuideLibrary(); const bool CatalogDirty = SharedCatalog && SharedCatalog->GetOutermost()->IsDirty();
    auto Editor = MakeShared<FCLRecipeToolkit>(); Editor->OpenTerrain(Terrain.Get(), &Planet->Recipe.RadialGuides, EToolkitMode::Standalone, nullptr);
    const auto Layers = Editor->GetTabManager()->FindExistingLiveTab(FName(TEXT("CLNoiseLayers")));
    const auto Section = Editor->GetTabManager()->FindExistingLiveTab(FName(TEXT("CLRecipeSection")));
    TestTrue(TEXT("Layer stack and cross-section are in the same editor"), Layers.IsValid() && Section.IsValid());
    TestTrue(TEXT("Layer tab contains the actual stack widget"), Layers && Layers->GetContent()->GetType() == FName(TEXT("SCLNoiseLayerStack")));
    TestEqual(TEXT("Preview uses its fixed reference scale, not the planet radius"), Editor->PreviewContext->Recipe.RadiusMeters, 1000.0);
    TestEqual(TEXT("Preview does not inherit the planet seed"), Editor->PreviewContext->Recipe.Seed, 1729);
    TestEqual(TEXT("Preview does not inherit the planet base"), Editor->PreviewContext->Recipe.BaseSurfaceNormalized, 1.0);
    TestTrue(TEXT("Only initial Radial Guides are copied from the caller"), FCLRadialGuideSet::StaticStruct()->CompareScriptStruct(&Editor->PreviewContext->Recipe.RadialGuides, &Planet->Recipe.RadialGuides, 0));
    TestFalse(TEXT("Planet identity is not copied"), Editor->PreviewContext->Recipe.Name == Planet->Recipe.Name);
    TArray<FString> Texts;
    TFunction<void(const TSharedRef<SWidget>&)> CollectText = [&](const TSharedRef<SWidget>& Widget)
    {
        if (!Widget->GetVisibility().IsVisible()) return;
        if (Widget->GetType() == FName(TEXT("STextBlock"))) Texts.Add(StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString());
        FChildren* Children = Widget->GetChildren();
        for (int32 I = 0; I < Children->Num(); ++I) CollectText(Children->GetChildAt(I));
    };
    CollectText(Section->GetContent());
    TestTrue(TEXT("The guide selector is explicitly Preview Radial Guides"), Texts.Contains(TEXT("Preview Radial Guides")));
    for (const FString Forbidden : {TEXT("Preview radius"), TEXT("Seed"), TEXT("Apply to planet")})
        TestFalse(*FString::Printf(TEXT("No planet control: %s"),*Forbidden), Texts.Contains(Forbidden));
    TestFalse(TEXT("Preview header contains no planet radius or identity"), Texts.ContainsByPredicate([](const FString& T){return T.Contains(TEXT("Context:"))||T.Contains(TEXT("Private planet identity"))||T.Contains(TEXT("radius 1400"));}));
    TArray<UObject*> Saved; Editor->GetSaveableObjects(Saved);
    TestTrue(TEXT("Save owns only the terrain definition"), Saved.Num()==1&&Saved.Contains(Terrain.Get()));
    TestFalse(TEXT("Save cannot write the preview recipe"), Saved.Contains(Editor->PreviewContext.Get()));
    TestFalse(TEXT("Save cannot write the guide library"), Saved.Contains(Editor->RadialGuidesSession->Library.Get())||Saved.Contains(SharedCatalog));
    const auto Alternate = FCLRadialGuidePresets::Defaults()[1];
    Editor->RadialGuidesSession->UsePreviewRadialGuides(Alternate);
    TestEqual(TEXT("Preview guide selection changes the scratch evaluator"), Editor->PreviewContext->Recipe.RadialGuides.Id, Alternate.Id);
    TestTrue(TEXT("The guide library is an independent snapshot"), Editor->RadialGuidesSession->Library.Get()!=SharedCatalog);
    const auto PreviewGuides = Editor->PreviewContext->Recipe.RadialGuides;
    const auto PreviewCatalog = Editor->RadialGuidesSession->Library->Library;
    Editor->RadialGuidesSession->Height(0,.01); Editor->RadialGuidesSession->Remove(0);
    TestEqual(TEXT("Preview cannot add a guide"),Editor->RadialGuidesSession->Add(),INDEX_NONE);
    Editor->RadialGuidesSession->NewSet(); Editor->RadialGuidesSession->RemoveSet();
    TestTrue(TEXT("Preview cannot edit guide values"),FCLRadialGuideSet::StaticStruct()->CompareScriptStruct(&PreviewGuides,&Editor->PreviewContext->Recipe.RadialGuides,0));
    TestTrue(TEXT("Preview cannot author guide sets"),FCLRadialGuideLibrary::StaticStruct()->CompareScriptStruct(&PreviewCatalog,&Editor->RadialGuidesSession->Library->Library,0));
    // Menu selection uses the same preview-only Apply path, with no transaction.
    Editor->RadialGuidesSession->Apply(BeforePlanet.RadialGuides.Id);
    TestEqual(TEXT("Preview menu applies only the selected guide snapshot"),Editor->PreviewContext->Recipe.RadialGuides.Id,BeforePlanet.RadialGuides.Id);
    TStrongObjectPtr<UCLPlanetRecipeAsset> OtherPlanet(NewObject<UCLPlanetRecipeAsset>());
    OtherPlanet->Recipe = Planet->Recipe; OtherPlanet->Recipe.Name = TEXT("Second private planet");
    OtherPlanet->Recipe.RadiusMeters = 10000; OtherPlanet->Recipe.Seed = 319;
    OtherPlanet->Recipe.RadialGuides.Id = FGuid::NewGuid(); OtherPlanet->Recipe.RadialGuides.Name = TEXT("Second preview guides");
    const auto BeforeOther = OtherPlanet->Recipe;
    CLOpenTerrainDesigner(Terrain.Get(), &OtherPlanet->Recipe.RadialGuides);
    TestEqual(TEXT("Reopening an editor replaces only the preview guides"),Editor->PreviewContext->Recipe.RadialGuides.Name,FString(TEXT("Second preview guides")));
    TestEqual(TEXT("Switching guides retains the reference scale"),Editor->PreviewContext->Recipe.RadiusMeters,1000.0);
    TestEqual(TEXT("Switching guides retains the evaluator seed"),Editor->PreviewContext->Recipe.Seed,1729);
    // Changes to the supplier after opening cannot alter the copied guide values.
    const auto Snapshot = Editor->PreviewContext->Recipe.RadialGuides;
    OtherPlanet->Recipe.RadialGuides.Guides[0].NormalizedHeight += .01;
    TestTrue(TEXT("Preview does not follow its supplier through a live binding"),FCLRadialGuideSet::StaticStruct()->CompareScriptStruct(&Snapshot,&Editor->PreviewContext->Recipe.RadialGuides,0));
    OtherPlanet->Recipe = BeforeOther;
    TestTrue(TEXT("Opening and changing preview guides never write the planet"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&BeforePlanet,&Planet->Recipe,0));
    TestTrue(TEXT("The second planet is unchanged"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&BeforeOther,&OtherPlanet->Recipe,0));
    TestTrue(TEXT("Preview choices never change the terrain definition"),FCLTerrainType::StaticStruct()->CompareScriptStruct(&BeforeTerrain,&Terrain->TerrainType,0));
    if (SharedCatalog)
    {
        TestTrue(TEXT("Preview never modifies the shared guide catalog"),FCLRadialGuideLibrary::StaticStruct()->CompareScriptStruct(&BeforeCatalog,&SharedCatalog->Library,0));
        TestEqual(TEXT("Preview never dirties the shared catalog"),SharedCatalog->GetOutermost()->IsDirty(),CatalogDirty);
    }
    // A preview session cannot accidentally attach to a persistent recipe.
    auto* PersistentPackage = CreatePackage(*(TEXT("/Game/CLPreviewTarget_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    TStrongObjectPtr<UCLPlanetRecipeAsset> Persistent(NewObject<UCLPlanetRecipeAsset>(PersistentPackage, TEXT("PersistentRecipe")));
    FCLRadialGuideSession Refused;
    TestFalse(TEXT("Preview session refuses a persistent asset target"),Refused.OpenPreview(Persistent.Get()));
    TestTrue(TEXT("Editor closes cleanly with pending preview work"),Editor->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed));
    return true;
}

}

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
    const auto Session = MakeShared<FCLRadialGuideSession>();
    Session->Asset = NewObject<UCLPlanetRecipeAsset>(GetTransientPackage(), NAME_None, RF_Transactional);
    Session->Library.Reset(NewObject<UCLRadialGuideLibraryAsset>(GetTransientPackage(), NAME_None, RF_Transactional));
    Session->Library->Library.Sets.Add(Session->Asset->Recipe.RadialGuides);
    const int32 Before = Session->Asset->Recipe.RadialGuides.Guides.Num();
    int32 Changes = 0; Session->Asset->OnRecipeChanged.AddLambda([&] { ++Changes; });
    const int32 New = Session->Add();
    TestEqual(TEXT("Core + creates one guide"), Session->Asset->Recipe.RadialGuides.Guides.Num(), Before + 1);
    TestEqual(TEXT("Core + selects the new guide"), Session->Selected, New);
    TestEqual(TEXT("New guide starts just above Core"), Session->Asset->Recipe.RadialGuides.Guides[New].NormalizedHeight, .05);
    TestEqual(TEXT("Core + commits one authoring change"), Changes, 1);
    const TSharedRef<SCLRadialGuideHandle> Handle = SNew(SCLRadialGuideHandle).Session(Session).Index(New).Label(true);
    FWidgetPath RadialGuidePath; RadialGuidePath.Widgets.AddWidget(FArrangedWidget(Handle, FGeometry()));
    RadialGuidePath.Widgets.AddWidget(FArrangedWidget(Handle->Name.ToSharedRef(), FGeometry()));
    FCLSelectionInput Observer(Session); Observer.ObservePath(RadialGuidePath);
    TestEqual(TEXT("Clicking a Radial Guide or its editable text preserves selection"), Session->Selected, New);
    FWidgetPath OtherPath; OtherPath.Widgets.AddWidget(FArrangedWidget(SNew(SButton), FGeometry()));
    Observer.ObservePath(OtherPath);
    TestEqual(TEXT("Clicking another control deselects"), Session->Selected, INDEX_NONE);
    Handle->OnKeyDown(FGeometry(), FKeyEvent(EKeys::Delete, FModifierKeysState(), 0, false, 0, 0));
    TestEqual(TEXT("Delete cannot remove a previously selected guide after deselection"), Session->Asset->Recipe.RadialGuides.Guides.Num(), Before + 1);
    Session->Selected = New; Observer.ObservePath(FWidgetPath());
    TestEqual(TEXT("Clicking empty space deselects"), Session->Selected, INDEX_NONE);
    TestEqual(TEXT("Selection changes do not notify terrain generation"), Changes, 1);
    GEditor->UndoTransaction();
    TestEqual(TEXT("One undo removes the added planet guide"), Session->Asset->Recipe.RadialGuides.Guides.Num(), Before);
    TestEqual(TEXT("One undo restores the catalog"), Session->Library->Library.Sets[0].Guides.Num(), Before);
    Session->Asset->OnRecipeChanged.Clear();
    return true;
}
#endif

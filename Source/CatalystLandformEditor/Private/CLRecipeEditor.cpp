#include "CLPreviewMesh.h"
#include "CLRecipeEvaluation.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "CLSurfaceDistributionPlot.h"
#include "CLRockThicknessPlot.h"
#include "Model/CLSurfaceRange.h"
#include "CLRecipeAssetTools.h"
#include "CLTerrainDesigner.h"
#include "CLSurfacePreview.h"
#include "CLTerrainEditing.h"
#include "CLNoiseLayerStack.h"
#include "CLRegionGenerationPanel.h"
#include "CLPlanetLandforms.h"
#include "IStructureDetailsView.h"
#include "IDetailsView.h"
#include "IDetailCustomization.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "PropertyEditorModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/NumericUnitTypeInterface.inl"
#include "Async/Async.h"
#include "CLSectionGeometry.h"
#include "CLPlanetPreview.h"
#include "CLWatershedDebug.h"
#include "CLNativePreview.h"
#include "CLPlanetInspection.h"
#include "Containers/Ticker.h"
#include "PropertyPath.h"
#include "Interfaces/IPluginManager.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "CLSectionNavigation.h"
#include "Framework/Application/IInputProcessor.h"
#include "Layout/WidgetPath.h"
#include "InputKeyEventArgs.h"
#include "CLRadialGuideEditing.h"
#include "CLRadialGuideMeasurements.h"
#include "CLPlanet.h"
#include "Engine/Selection.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "Widgets/SCanvas.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "SceneView.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLTerrainType.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Toolkits/CFModelEditorToolkit.h"
#include "Toolkits/IToolkitHost.h"
#include "PreviewScene.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/TextureCube.h"
#include "SEditorViewport.h"
#include "EditorViewportClient.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "ShaderCompiler.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Misc/AutomationTest.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
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
    virtual void Tick(float Delta) override
    {
        if(Plan)FEditorViewportClient::Tick(Delta);SectionScene->UpdateCaptureContents();
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
    using FIndicator=FCLGuideIndicator;
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
        SLATE_ARGUMENT(TFunction<double(double)>, HeightAtScreenY)
    SLATE_END_ARGS()
    TSharedPtr<FCLRadialGuideSession> Session;
    TSharedPtr<FCLSectionClient> Client;
    TWeakPtr<SWidget> Overlay;
    TSharedPtr<SInlineEditableTextBlock> Name;
    int32 Index = INDEX_NONE;
    TFunction<double(double)> HeightAtScreenY;
    bool bDragging = false;
    FVector2D MouseStart;
    void Construct(const FArguments& Args)
    {
        Session = Args._Session; Client = Args._Client; Overlay = Args._Overlay;
        Index = Args._Index;HeightAtScreenY=Args._HeightAtScreenY;
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
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(7,0)
                        [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",9))
                            .Text_Lambda([this]{return FCLRadialGuideMeasurements::Height(Session->Asset->Recipe,Index);})
                            .ColorAndOpacity_Lambda([this]{return GuideTint(Index,Session->Selected);})]
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
        if (bDragging) Session->Height(Index, HeightAtScreenY?HeightAtScreenY(Local.Y / FMath::Max(1.0, G.GetLocalSize().Y)):Client->NormalizedAtScreenY(Local.Y / FMath::Max(1.0, G.GetLocalSize().Y)));
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
        SLATE_ARGUMENT(TSharedPtr<SCLPlanetPreview>, Globe)
        SLATE_ATTRIBUTE(bool, UseGlobe)
    SLATE_END_ARGS()
    TSharedPtr<FCLSectionClient> Client;
    TSharedPtr<FCLRadialGuideSession> Session;
    TSharedPtr<SCanvas> Canvas;
    TSharedPtr<SCLPlanetPreview> Globe;
    TAttribute<bool> UseGlobe;
    bool IsGlobe()const{return Globe&&UseGlobe.Get(false);}
    const TArray<FCLGuideIndicator>& Indicators()const{return IsGlobe()?Globe->Indicators():Client->Indicators;}
    double NormalizedAtScreenY(double Y)const{return IsGlobe()?Globe->NormalizedAtScreenY(Y):Client->NormalizedAtScreenY(Y);}
    int32 Rows = -1;
    TArray<TSharedPtr<SCLRadialGuideHandle>> LabelHandles;
    double LabelWidth() const
    {
        double Width=150;
        for(const auto& Handle:LabelHandles)Width=FMath::Max(Width,FMath::Min(300.,double(Handle->GetDesiredSize().X)));
        return Width;
    }
    void Construct(const FArguments& Args)
    {
        Client = Args._Client; Session = Args._Session;Globe=Args._Globe;UseGlobe=Args._UseGlobe; ForceVolatile(true);
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
    { if(IsGlobe())Globe->Dolly(E.GetWheelDelta());else {Client->Navigation.Dolly(E.GetWheelDelta());Client->ApplyNavigation();}return FReply::Handled(); }
    const FCLSectionClient::FIndicator* Indicator(int32 Index) const
    { return Indicators().FindByPredicate([&](const auto& I) { return I.Index == Index; }); }
    double TickGap()const{return IsGlobe()?0:GuideTickWidth;}
    FVector2D Anchor(int32 Index) const
    { const auto* I = Indicator(Index); return I ? I->Position * GetCachedGeometry().GetLocalSize() : FVector2D(-1000, -1000); }
    FVector2D TickStart(int32 Index)const{return Anchor(Index)+FVector2D(TickGap(),0);}
    FVector2D Label(int32 Index) const
    {
        const FVector2D Size = GetCachedGeometry().GetLocalSize();
        double X = 0, Previous = 8 - 24; TArray<double> Ys;
        int32 Found = INDEX_NONE;
        for (int32 K = 0; K < Indicators().Num(); ++K)
        {
            const auto& I = Indicators()[K];
            X = FMath::Max(X, I.Position.X * Size.X + GuideTickWidth * 2 + 16);
            Previous = FMath::Max(8.0, FMath::Max(I.Position.Y * Size.Y - GuideLabelHeight * .5, Previous + 24)); Ys.Add(Previous);
            if (I.Index == Index) Found = K;
        }
        if (Found == INDEX_NONE) return FVector2D(-1000, -1000);
        const double Overflow = FMath::Max(0.0, Ys.Last() - Size.Y + GuideLabelHeight + 2);
        const bool bDimensions=!FCLRadialGuideMeasurements::SurfaceSpans(Session->Asset->Recipe).IsEmpty();
        X=FMath::Min(X,FMath::Max(8.,Size.X-LabelWidth()-(bDimensions?105:12)));
        return FVector2D(X, Ys[Found] - Overflow);
    }
    virtual void Tick(const FGeometry& Geometry, double Time, float Delta) override
    {
        SCompoundWidget::Tick(Geometry, Time, Delta);
        const int32 Count = Session->Asset->Recipe.RadialGuides.Guides.Num();
        if (Count == Rows) return;
        Rows = Count; Canvas->ClearChildren();LabelHandles.Reset();
        for (int32 I = 0; I < Count; ++I)
        {
            Canvas->AddSlot().Position_Lambda([this, I] { return TickStart(I) + FVector2D(-4, -9); }).Size(FVector2D(GuideTickWidth + 8, 18))
                [SNew(SCLRadialGuideHandle).Session(Session).Client(Client).Overlay(SharedThis(this)).HeightAtScreenY([this](double Y){return NormalizedAtScreenY(Y);}).Index(I)];
            const auto LabelHandle = SNew(SCLRadialGuideHandle).Session(Session).Client(Client).Overlay(SharedThis(this)).HeightAtScreenY([this](double Y){return NormalizedAtScreenY(Y);}).Index(I).Label(true);
            LabelHandles.Add(LabelHandle);
            Canvas->AddSlot().Position_Lambda([this, I] { return Label(I); })
                .Size_Lambda([LabelHandle] { return FVector2D(FMath::Min(300.0, double(LabelHandle->GetDesiredSize().X)), GuideLabelHeight); })[LabelHandle];
        }
        Canvas->AddSlot().Position_Lambda([this] { return Label(-3); }).Size(FVector2D(200, GuideLabelHeight))
            [SNew(SBox).VAlign(VAlign_Center).Visibility(EVisibility::HitTestInvisible)
                [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    .Text_Lambda([this]{return FText::Format(NSLOCTEXT("CL","SkyHeight","Sky boundary  {0}"),FCLRadialGuideMeasurements::Height(Session->Asset->Recipe,-3));}).ColorAndOpacity(GuideLabelColor)]];
        Canvas->AddSlot().Position_Lambda([this] { return Label(-2); }).Size(FVector2D(110, GuideLabelHeight))
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)).Text(FText::FromString(TEXT("Core  0 m"))).ColorAndOpacity(GuideLabelColor)]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)
                    [SNew(SBox).WidthOverride(16).HeightOverride(16).VAlign(VAlign_Center)
                        .Visibility_Lambda([this] { return Session->bReadOnly ? EVisibility::Collapsed : EVisibility::Visible; })
                        [SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton").ContentPadding(0)
                        .ToolTipText(FText::FromString(TEXT("Add a Radial Guide just above Core, then drag it to the desired height")))
                        .IsEnabled_Lambda([this] { return Session->Asset->Recipe.RadialGuides.Guides.Num() < 128; })
                        .OnClicked_Lambda([this]
                        {
                            Client->bGuides = true;if(Globe)Globe->SetGuides(Session->Asset->Recipe.RadialGuides,true);Session->Add();
                            return FReply::Handled().SetUserFocus(SharedThis(this));
                        })[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                            .Text(FText::FromString(TEXT("+"))).ColorAndOpacity(GuideLabelColor)]]]];
    }
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Clip, FSlateWindowElementList& Elements,
        int32 Layer, const FWidgetStyle& Style, bool Enabled) const override
    {
        if(IsGlobe())
        {
            const auto& Rings=Globe->Rings();
            for(int32 I=0;I<Rings.Num();++I)
            {
                TArray<FVector2D> Points;for(const auto& P:Rings[I])Points.Add(P*Geometry.GetLocalSize());
                const auto Color=GuideTint(I==0?-3:I-1,Session->Selected)*Style.GetColorAndOpacityTint();
                FSlateDrawElement::MakeLines(Elements,Layer,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,1);
            }
        }
        for (const auto& I : Indicators())
        {
            // Globe ticks meet their rings; cutaway ticks retain the requested margin.
            const FVector2D A = TickStart(I.Index), L = Label(I.Index);
            const FLinearColor Color = GuideTint(I.Index, Session->Selected) * Style.GetColorAndOpacityTint();
            TArray<FVector2D> Tick{A, A + FVector2D(GuideTickWidth, 0)};
            FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Tick, ESlateDrawEffect::None, Color, true, 2);
            const double LabelCentreY = L.Y + GuideLabelHeight * .5;
            TArray<FVector2D> Leader{A + FVector2D(GuideTickWidth, 0), FVector2D(L.X - 8, LabelCentreY), FVector2D(L.X - 3, LabelCentreY)};
            FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Leader, ESlateDrawEffect::None, Color, true, 1);
        }
        const auto Spans=FCLRadialGuideMeasurements::SurfaceSpans(Session->Asset->Recipe);
        if(!Spans.IsEmpty()&&Indicator(Spans[0].Low)&&Indicator(Spans[0].High))
        {
            // Dimensions use the aligned callout rows. Their leaders still locate the
            // exact heights on the render; a short projected interval remains readable.
            const auto& Total=Spans[0];
            const double X=Label(Total.High).X+LabelWidth()+12;
            const auto Y=[&](int32 Index){return Label(Index).Y+GuideLabelHeight*.5;};
            const bool Selected=Session->Selected==Total.Low||Session->Selected==Total.High||
                (Spans.Num()==3&&Session->Selected==Spans[1].High);
            FLinearColor Color=GuideLabelColor;if(Selected)Color.A=1;Color*=Style.GetColorAndOpacityTint();
            const auto Line=[&](TArray<FVector2D> Points){FSlateDrawElement::MakeLines(Elements,Layer,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,1);};
            Line({FVector2D(X,Y(Total.Low)),FVector2D(X,Y(Total.High))});
            TArray<int32> Ends{Total.Low,Total.High};if(Spans.Num()==3)Ends.Add(Spans[1].High);
            for(int32 Index:Ends)Line({FVector2D(X-5,Y(Index)),FVector2D(X+5,Y(Index))});
            for(const auto& Span:Spans)
            {
                const bool bSplitTotal=Span.bTotal&&Spans.Num()==3;
                const FText Text=bSplitTotal?FText::Format(NSLOCTEXT("CL","TotalRadialSpan","Total {0}"),FCLRadialGuideMeasurements::Meters(Span.Meters)):FCLRadialGuideMeasurements::Meters(Span.Meters);
                const double TextY=bSplitTotal?Y(Total.High)-22:(Y(Span.Low)+Y(Span.High))*.5-7;
                FSlateDrawElement::MakeText(Elements,Layer,Geometry.ToPaintGeometry(FVector2D(95,20),FSlateLayoutTransform(FVector2D(X+9,TextY))),Text,FCoreStyle::GetDefaultFontStyle("Regular",9),ESlateDrawEffect::None,Color);
            }
        }
        return SCompoundWidget::OnPaint(Args, Geometry, Clip, Elements, Layer + 1, Style, Enabled);
    }
};

class SCLSectionViewport : public SEditorViewport
{
public:
    SLATE_BEGIN_ARGS(SCLSectionViewport) {} SLATE_END_ARGS()
    FCLSectionScene Scene;
    TUniquePtr<FCLNativePreview> NativePreview;
    TSharedPtr<FCLSectionClient> Client;
    bool bCameraFramed = false;
    bool bDraft=false;
    void SetDraftMode(bool Enabled){bDraft=Enabled;}
    int32 StartedBuilds = 0, AppliedBuilds = 0;
    FKey NavigationButton;
    FVector2D PreviousMouse;
    bool bWalkPicking=false;
    double PointerTravel=0;
    TFunction<void(FVector)> OnWalkPoint;
    virtual FReply OnPreviewMouseButtonDown(const FGeometry&, const FPointerEvent& E) override
    {
        const FKey Button = E.GetEffectingButton();
        if (Button != EKeys::LeftMouseButton && Button != EKeys::MiddleMouseButton && Button != EKeys::RightMouseButton) return FReply::Unhandled();
        NavigationButton = Button; PreviousMouse = E.GetScreenSpacePosition();PointerTravel=0;
        return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));
    }
    virtual FReply OnMouseMove(const FGeometry&, const FPointerEvent& E) override
    {
        if (!HasMouseCapture()) return FReply::Unhandled();
        const FVector2D Delta = E.GetScreenSpacePosition() - PreviousMouse; PreviousMouse = E.GetScreenSpacePosition();
        PointerTravel+=Delta.Size();
        if (NavigationButton == EKeys::LeftMouseButton) Client->Navigation.Orbit(Delta);
        else if (NavigationButton == EKeys::MiddleMouseButton) Client->Navigation.PanBy(Delta);
        else if (NavigationButton == EKeys::RightMouseButton) Client->Navigation.Dolly(-Delta.Y * .05);
        Client->ApplyNavigation(); return FReply::Handled();
    }
    virtual FReply OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E) override
    {
        if (!HasMouseCapture() || E.GetEffectingButton() != NavigationButton) return FReply::Unhandled();
        if(NavigationButton==EKeys::LeftMouseButton&&PointerTravel<4&&bWalkPicking&&NativePreview&&Client->bHasProjection)
        {
            const FVector2D Local=G.AbsoluteToLocal(E.GetScreenSpacePosition()),Size=G.GetLocalSize();FVector O,D;
            const FVector2D Pixel=FVector2D(Client->ViewRect.Min)+Local/FVector2D(FMath::Max(1.,Size.X),FMath::Max(1.,Size.Y))*FVector2D(Client->ViewRect.Size());
            FSceneView::DeprojectScreenToWorld(Pixel,Client->ViewRect,Client->ViewProjection.Inverse(),O,D);FVector P;
            if(NativePreview->PickSection(O,D.GetSafeNormal(),P)&&OnWalkPoint)OnWalkPoint(P);
        }
        NavigationButton = FKey(); return FReply::Handled().ReleaseMouseCapture();
    }
    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override { NavigationButton = FKey(); }
    virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent& E) override
    { Client->Navigation.Dolly(E.GetWheelDelta()); Client->ApplyNavigation(); return FReply::Handled(); }
    virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent&) override { return FReply::Unhandled(); }
    void Construct(const FArguments& Args)
    {
        SEditorViewport::Construct(SEditorViewport::FArguments());
    }
    virtual ~SCLSectionViewport()
    {
        NativePreview.Reset();
        if (Client) Client->Viewport = nullptr;
    }
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override
    { Client = MakeShared<FCLSectionClient>(&Scene, SharedThis(this)); return Client.ToSharedRef(); }
    bool IsUpdating() const{return NativePreview&&NativePreview->IsUpdating();}
    uint64 SurfacePublicationFrame=0;
    bool IsSurfaceReady(const TSharedPtr<const FCLCompiledPlanetRecipe>& Plan)const{return NativePreview&&NativePreview->PublishedPlan()==Plan&&!NativePreview->IsUpdating()&&GFrameCounter>SurfacePublicationFrame+2;}
    bool IsApplyingInput()const{return NativePreview&&NativePreview->IsApplyingInput();}
    TOptional<float> InputProgress()const{return NativePreview?NativePreview->InputProgress():TOptional<float>();}
    FString InputProgressText()const{return NativePreview?NativePreview->InputProgressText():TEXT("Preparing preview…");}
    void CancelPendingBuild(){NativePreview.Reset();}
    void PumpBuild()
    {
        if(NativePreview&&NativePreview->Poll(false)){++AppliedBuilds;SurfacePublicationFrame=GFrameCounter;if(!bCameraFramed){bCameraFramed=true;Client->ApplyNavigation();}Client->Invalidate();}
    }
    virtual void Tick(const FGeometry& G,double T,float D) override
    {SEditorViewport::Tick(G,T,D);PumpBuild();}
    void RebuildAsync(TSharedPtr<const FCLCompiledPlanetRecipe> Plan)
    {
        if(!Plan){CancelPendingBuild();return;}Client->Plan=Plan;
        if(Client->AreaWidthMeters<=0)Client->AreaWidthMeters=FCLSectionGeometry::DefaultWidth(*Plan);
        if(!NativePreview)NativePreview=MakeUnique<FCLNativePreview>();FString Error;
        if(!NativePreview->Start(*Scene.GetWorld(),Plan,true,Client->AreaWidthMeters,Error,bDraft))UE_LOG(LogTemp,Error,TEXT("Native section: %s"),*Error);
        ++StartedBuilds;if(!bCameraFramed){bCameraFramed=true;Client->ApplyNavigation();}
    }
    void Rebuild(TSharedPtr<const FCLCompiledPlanetRecipe> Plan){RebuildAsync(MoveTemp(Plan));}

};

class FCLPlanetDetailsLayout : public IDetailCustomization
{
    TFunction<void(IDetailLayoutBuilder&)> Build;
public:
    explicit FCLPlanetDetailsLayout(TFunction<void(IDetailLayoutBuilder&)> InBuild):Build(MoveTemp(InBuild)){}
    virtual void CustomizeDetails(IDetailLayoutBuilder& Layout) override
    {
        TArray<FName> Categories;Layout.GetCategoryNames(Categories);for(FName Name:Categories)Layout.HideCategory(Name);
        Build(Layout);
    }
};

class FCLRecipeToolkit : public FCFModelEditorToolkit
{
#if WITH_DEV_AUTOMATION_TESTS
    friend class FCLPlanetInspectorTest;
    friend class FCLInstalledPlanetEditorTest;
    friend class FCLTectonicLiveKnobTest;
    friend class FCLTectonicAdvancedTest;
    friend class FCLRockDesignerTest;
    friend class FCLSurfaceDesignerTest;
    friend class FCLWatershedDesignerTest;
    friend class FCLProvinceEditorTest;
    friend class FCLRiftDesignerTest;
    friend class FCLTransformDesignerTest;
    friend class FCLVolcanicArcDesignerTest;
    friend class FCLPlanetSurfaceFirstTest;
    friend class FCLPlanetLandformRowsTest;
    friend class FCLPlanetLandformInfoTest;
    friend class FCLCaveDesignerTest;
    friend class FCLAuthoringLayoutTest;
    friend class FCLBackgroundDesignerTest;
    friend class FCLNavigationResponsivenessTest;
    friend class FCLPropertyScrollTest;
#endif


    TSharedPtr<FStructOnScope> WatershedDraft;
    TArray<TSharedPtr<IPropertyHandle>> WatershedProperties;
    TSharedPtr<SSlider> WatershedSlider;
    TSharedPtr<SButton> LayerVisibility[5],WatershedVariation;
    TSharedPtr<SCheckBox> LayerIndicators[5];
    TSharedPtr<FStructOnScope> CaveDraft;
    TArray<TSharedPtr<IPropertyHandle>> CaveProperties;
    TSharedPtr<SSlider> CaveSlider;
    TSharedPtr<SButton> CaveVariation;
    TUniquePtr<FScopedTransaction> CaveTransaction;
    FTSTicker::FDelegateHandle CaveTicker;
    bool bCaveDragging=false,bCaveOverlay=false;
    double PendingCave=.5,AppliedCave=.5,LastCaveApply=0;
    TUniquePtr<FScopedTransaction> WatershedTransaction;
    FTSTicker::FDelegateHandle WatershedTicker;
    bool bWatershedDragging=false,bWatershedOverlay=false;
    double PendingWatershed=.5,AppliedWatershed=.5,LastWatershedApply=0;
    TSharedPtr<FStructOnScope> ElevationDraft;
    TArray<TSharedPtr<IPropertyHandle>> ElevationAdvancedProperties;
    TSharedPtr<SSlider> ElevationSlider;
    TSharedPtr<IPropertyHandle> RegionProperties;
    TSharedPtr<const FCLCompiledPlanetRecipe> AcceptedSurfacePlan;
    TSharedPtr<const FCLWatershedDebugGeometry> AcceptedWatershedDebug;
    FCLRecipeEvaluation Evaluation;
    TSharedPtr<const FCLPreparedPlanetCompile> PreparedEdit;
    TOptional<FCLPlanetRecipe> PreparedEditRecipe,RequestedRecipe;
    TOptional<FBox2D> RequestedRegion;
    FTSTicker::FDelegateHandle EvaluationTicker;
    uint64 PublishedEvaluation=0;
    double LastEvaluationSeconds=0;
    bool LastEvaluationWasWorker=false;
    TArray<FVector2D> MeasuredElevation;
    FString ElevationReadout;
    bool bElevationDragging=false;
    double PendingElevation=.5,AppliedElevation=.5,LastSurfaceApply=0;
    FTSTicker::FDelegateHandle SurfaceTicker;
    TUniquePtr<FScopedTransaction> SurfaceTransaction;
    TSharedPtr<FStructOnScope> RockDraft,RockBedDraft;
    TArray<TSharedPtr<IPropertyHandle>> RockBedProperties;
    FCLRockBedStatistics MeasuredRockBeds;
    TArray<double> TargetRockBeds;
    TArray<TSharedPtr<IPropertyHandle>> RockAdvancedProperties;
    TSharedPtr<SSlider> RockSlider;
    TSharedPtr<SButton> RockVariation;
    bool bRockDragging=false;
    double PendingRock=.55,AppliedRock=.55,LastRockApply=0;
    FTSTicker::FDelegateHandle RockTicker;
    TUniquePtr<FScopedTransaction> RockTransaction;
    TSharedPtr<SCLSectionViewport> Preview;
    FCLPlanetRecipe InspectorRecipe;
    FString InspectorError;
    TSharedPtr<SCLPlanetPreview> Globe;
    bool bGlobe=true,bTectonicDragging=false;
    double PendingTectonicActivity=.55,LastTectonicApply=0,AppliedTectonicActivity=.55;
    FTSTicker::FDelegateHandle TectonicTicker;
    TUniquePtr<FScopedTransaction> TectonicDragTransaction;
    TSharedPtr<SSlider> TectonicSlider;
    IDetailCategoryBuilder* TectonicAdvanced=nullptr;
    IDetailCategoryBuilder* ContinentalAdvanced=nullptr;
    IDetailCategoryBuilder* RockAdvanced=nullptr;
    TArray<TSharedPtr<IPropertyHandle>> ContinentalAdvancedProperties;
    TSharedPtr<SButton> ContinentalVariation,TectonicVariation;
    TSharedPtr<FStructOnScope> ContinentalDraft;
    TSharedPtr<SSlider> ContinentalSlider;
    TArray<TSharedPtr<IDetailsView>> PlanetInspectors;
    TSharedPtr<SWidgetSwitcher> InspectorPages;
    int32 InspectorPage=1;
    bool bContinentalDragging=false;
    double PendingContinentalStrength=.7,AppliedContinentalStrength=.7;
    TArray<TSharedPtr<IPropertyHandle>> TectonicAdvancedProperties;
    TSharedPtr<FStructOnScope> TectonicDraft;
    FGuid WalkRequestId;
    FString WalkStatus=TEXT("Choose a location on the globe, then walk there.");
    TSharedPtr<SCLHeightAnnotations> Annotations;
    TSharedPtr<FCLRadialGuideSession> RadialGuidesSession;
    TSharedPtr<FCLSelectionInput> SelectionInput;
    TStrongObjectPtr<UCLTerrainTypeAsset> TerrainSource;
    TSharedPtr<FCLTerrainEditing> TerrainEditing;
    TStrongObjectPtr<UCLPlanetRecipeAsset> PreviewContext;
    TSharedPtr<SCLNoiseLayerStack> NoiseStack;
    TSharedPtr<SCLRegionGenerationPanel> GenerationPanel;
    TSharedPtr<FCLPlanetLandforms> Landforms;
    TSharedPtr<SCLPlanetLandforms> LandformRows;
    FGuid RequestedSolo;
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
    static const FName SurfaceTab;
    TSharedPtr<IDetailsView> SurfaceDetails;
    static const FName PlanetTab;
public:
    virtual ~FCLRecipeToolkit()
    {
        FTSTicker::GetCoreTicker().RemoveTicker(EvaluationTicker);Evaluation.Cancel();
        FTSTicker::GetCoreTicker().RemoveTicker(TectonicTicker);TectonicDragTransaction.Reset();
        FTSTicker::GetCoreTicker().RemoveTicker(RockTicker);RockTransaction.Reset();
        FTSTicker::GetCoreTicker().RemoveTicker(CaveTicker);CaveTransaction.Reset();
        FTSTicker::GetCoreTicker().RemoveTicker(WatershedTicker);WatershedTransaction.Reset();
        FTSTicker::GetCoreTicker().RemoveTicker(SurfaceTicker);SurfaceTransaction.Reset();
        if (SelectionInput && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(SelectionInput);
        if (EditingAsset) RecipeAsset()->OnRecipeChanged.Remove(Changed);
        if (TerrainEditing) TerrainEditing->Asset->OnTerrainChanged.Remove(TerrainChanged);
    }
    static TWeakPtr<FCLRecipeToolkit> ActivePlanet;
    void SyncLandformHighlights()
    {if(!Globe||!Landforms)return;TSet<FGuid> Ids;for(FGuid Id:Landforms->Highlighted)if(Landforms->Included(Id))Ids.Add(Id);Globe->SetHighlightedLandforms(Ids);}
    void ShowLandforms(UCLTerrainTypeAsset* Source=nullptr)
    {if(Source)Landforms->AddSource(Source);if(LandformRows){LandformRows->Refresh();if(Source)LandformRows->Focus(Source->TerrainType.Id);}InspectorPage=2;FocusWindow(RecipeAsset());}
    void Open(UCLPlanetRecipeAsset* Asset, EToolkitMode::Type Mode, TSharedPtr<IToolkitHost> Host)
    {
        EditingAsset = Asset;
        ActivePlanet=StaticCastSharedRef<FCLRecipeToolkit>(AsShared());
        Landforms=MakeShared<FCLPlanetLandforms>(Asset);Landforms->Discover();
        Landforms->PreviewChanged=FSimpleDelegate::CreateSP(this,&FCLRecipeToolkit::Refresh);
        Landforms->HighlightsChanged=FSimpleDelegate::CreateSP(this,&FCLRecipeToolkit::SyncLandformHighlights);
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
        InspectorRecipe=Asset->Recipe;InspectorRecipe.UseGuidedGeology();
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
            if(Globe)Globe->SetGuides(RadialGuidesSession->Asset->Recipe.RadialGuides,Preview->Client->bGuides);
            if (Annotations) Annotations->Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
        };
        BuildDetailsPanel();
        if (TerrainEditing) DetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& P)
        { return P.Property.GetFName() != GET_MEMBER_NAME_CHECKED(FCLTerrainType, NoiseLayers); }));
        if(!TerrainEditing)DetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& P)
        {return P.Property.GetFName()!=GET_MEMBER_NAME_CHECKED(FCLPlanetRecipe,RadialGuides);}));
        Changed = Asset->OnRecipeChanged.AddSP(this, &FCLRecipeToolkit::Refresh);
        auto Left=FTabManager::NewStack()->SetSizeCoefficient(.34)->AddTab(TerrainEditing?NoiseTab:PlanetTab,ETabState::OpenedTab);
        if(TerrainEditing&&!TerrainEditing->Asset->TerrainType.Rift.IsActive())Left->AddTab(SurfaceTab,ETabState::OpenedTab);
        if(TerrainEditing)Left->AddTab(DetailsTabId,ETabState::OpenedTab)->AddTab(ValidationTabId,ETabState::OpenedTab);
        Left->SetForegroundTab(TerrainEditing?NoiseTab:PlanetTab);
        const auto Layout = FTabManager::NewLayout(TerrainEditing ? "CLTerrainDesigner_v2" : "CLRecipeDesigner_v5")->AddArea(
            FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
            ->Split(Left)
            ->Split(FTabManager::NewStack()->SetSizeCoefficient(.68)->AddTab(SectionTab, ETabState::OpenedTab)->SetHideTabWell(true)));
        InitAssetEditor(Mode, Host, GetToolkitFName(), Layout, true, true, EditingAsset.Get());
        const auto Extender = MakeShared<FExtender>();
        Extender->AddToolBarExtension(TEXT("Asset"), EExtensionHook::After, nullptr,
            FToolBarExtensionDelegate::CreateLambda([this](FToolBarBuilder& Builder)
            {
                if(!TerrainEditing)Builder.AddToolBarButton(FUIAction(FExecuteAction::CreateSP(this,&FCLRecipeToolkit::CreatePlanet)),NAME_None,
                    FText::FromString(TEXT("New planet")),FText::GetEmpty(),FSlateIcon(FAppStyle::GetAppStyleSetName(),TEXT("Icons.Plus")));
                Builder.AddToolBarButton(FUIAction(FExecuteAction::CreateSP(this, &FCLRecipeToolkit::Export)), NAME_None,
                    FText::FromString(TerrainEditing ? TEXT("Export terrain type") : TEXT("Export recipe")), FText::FromString(TEXT("Export the authored definition through Foundation")));
            }));
        AddToolbarExtender(Extender); RegenerateMenusAndToolbars();
        if(!TerrainEditing)GetTabManager()->TryInvokeTab(PlanetTab);
        const TWeakPtr<FCLRecipeToolkit> EvaluationOwner=StaticCastSharedRef<FCLRecipeToolkit>(AsShared());
        EvaluationTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([EvaluationOwner](float){if(auto E=EvaluationOwner.Pin()){E->PumpEvaluation();return true;}return false;}));
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
                            if(Candidate.Coordinates==ECLTerrainCoordinates::SphericalSurface){Candidate.SchemaVersion=FMath::Max(11,Candidate.SchemaVersion);Region.bPlacedLandform=true;}
                            Target=Region.Id;Candidate.TerrainRegions.Add(Region);
                        }
                        FString Error;
                        if(!FCLPlanetCompiler::WithAuthoringValidation([&]{return Weak->TerrainType.ApplyToRegion(Candidate,Target,Error);})){Status=TEXT("Region was not applied: ")+Error;return;}
                        const FScopedTransaction Tx(NSLOCTEXT("CL", "ApplyRegion", "Apply terrain to planet region"));
                        RecipeAsset()->Modify();RecipeAsset()->Recipe=MoveTemp(Candidate);RecipeAsset()->LandformSources.Add(Target,Weak.Get());RecipeAsset()->PostEditChange();RecipeAsset()->MarkPackageDirty();
                        return;
                    }
                    const FScopedTransaction Tx(NSLOCTEXT("CL", "ApplyTerrainType", "Apply terrain type")); FString Error;
                    if (!FCLPlanetCompiler::WithAuthoringValidation([&]{return Weak->ApplyToRecipe(RecipeAsset(),Error);})) Status = TEXT("Terrain type was not applied: ") + Error;
                })));
        }
        return Menu.MakeWidget();
    }
    TSharedRef<SWidget> StructureMenu(FGuid Id)
    {
        FMenuBuilder Menu(true,nullptr);
        auto Add=[&](FGuid FeatureId,const FString& Name)
        {
            Menu.AddMenuEntry(FText::FromString(Name),FText(),FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([this,Id,FeatureId]
            {
                auto* A=RecipeAsset();auto Candidate=A->Recipe;auto* R=Candidate.TerrainRegions.FindByPredicate([&](const auto& V){return V.Id==Id;});if(!R)return;
                R->Placement.StructureFeatureId=FeatureId;R->bPlacedLandform=true;Candidate.SchemaVersion=FMath::Max(11,Candidate.SchemaVersion);
                FString Error;if(!PrepareEdit(Candidate,Error)){Status=Error;return;}
                const FScopedTransaction Tx(FText::FromString(TEXT("Connect Landform to structure")));A->Modify();A->Recipe=MoveTemp(Candidate);A->PostEditChange();A->MarkPackageDirty();
            })));
        };
        Add(FGuid(),TEXT("Planet frame"));for(const auto& F:RecipeAsset()->Recipe.BaseStructure)Add(F.Id,F.Name);return Menu.MakeWidget();
    }
    void CommitGlobePlacement(const FCLGlobePlacementTarget& Target,bool Resize)
    {
        if(TerrainEditing)return;
        auto* Asset=RecipeAsset();auto Candidate=Asset->Recipe;FString Error;
        if(!FCLGlobePlacementEditing::Apply(Candidate,Target,Resize,Error)||!PrepareEdit(Candidate,Error)){Status=Error;return;}
        const FScopedTransaction Tx(FText::FromString(Resize?TEXT("Resize globe Landform"):TEXT("Place globe Landform")));
        Asset->Modify();Asset->Recipe=MoveTemp(Candidate);Asset->PostEditChange();Asset->MarkPackageDirty();
    }
    TSharedRef<SWidget> GlobeSelectionMenu()
    {
        FMenuBuilder Menu(true,nullptr);
        Menu.AddMenuEntry(FText::FromString(TEXT("Deselect")),FText(),FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([this]{if(Globe)Globe->Select({});})));
        for(const auto& T:FCLGlobePlacementEditing::Resolve(RecipeAsset()->Recipe))
            Menu.AddMenuEntry(FText::FromString((T.Key.bStructure?TEXT("Structure: "):TEXT("Landform: "))+T.Name),FText(),FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([this,Key=T.Key]{if(Globe)Globe->Select(Key);})));
        return Menu.MakeWidget();
    }
    TSharedRef<SWidget> RadialGuidesMenu()
    {
        FMenuBuilder Menu(true, nullptr);
        for (const FCLRadialGuideSet& S : RadialGuidesSession->Library->Library.Sets)
            Menu.AddMenuEntry(FText::FromString(S.Name), FText::FromString(TerrainEditing ? TEXT("Preview this guide set without saving it to the terrain or changing a planet") : TEXT("Reference this shared guide set; edits affect every planet using it. Duplicate for an independent style.")), FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([Session = RadialGuidesSession, Id = S.Id] { Session->Apply(Id); })));
        return Menu.MakeWidget();
    }
    virtual FName GetToolkitFName() const override { return TEXT("CLRecipeDesigner"); }
    virtual FText GetBaseToolkitName() const override { return NSLOCTEXT("CL", "PlanetEditor", "Planet"); }
    virtual FString GetWorldCentricTabPrefix() const override { return TEXT("Landform"); }
    void Export() { HandleExportJson(); }
    virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& Manager) override
    {
        FCFModelEditorToolkit::RegisterTabSpawners(Manager);
        Manager->RegisterTabSpawner(SectionTab, FOnSpawnTab::CreateSP(this, &FCLRecipeToolkit::SpawnSection));
        Manager->RegisterTabSpawner(PlanetTab,FOnSpawnTab::CreateSP(this,&FCLRecipeToolkit::SpawnPlanet)).SetDisplayName(FText::FromString(TEXT("Planet")));
    }
    virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& Manager) override
    {Manager->UnregisterTabSpawner(SectionTab);Manager->UnregisterTabSpawner(PlanetTab);FCFModelEditorToolkit::UnregisterTabSpawners(Manager);}
    void CreatePlanet()
    {
        auto& Tools=FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
        if(auto* A=Cast<UCLPlanetRecipeAsset>(Tools.CreateAssetWithDialog(TEXT("DA_NewPlanet"),TEXT("/Game/LandformDesigner/Planets"),UCLPlanetRecipeAsset::StaticClass(),NewObject<UCLPlanetRecipeFactory>())))
            GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(A);
    }
    bool CommitTectonicLayer(FCLTectonicPreset Preset)
    {
        InspectorError.Reset();auto* A=RecipeAsset();auto Candidate=LayeredRecipe();Candidate.Tectonics=Preset;
        if(Preset.AlgorithmVersion==3&&!Candidate.SurfaceStack.IsActive())Candidate.SurfaceStack=FCLSurfaceStack::MakeDefault();
        FString Error;if(!PrepareEdit(Candidate,Error)){Status=Error;InspectorError=Error;return false;}
        TUniquePtr<FScopedTransaction> Tx;if(!TectonicDragTransaction)Tx=MakeUnique<FScopedTransaction>(FText::FromString(TEXT("Change tectonic layer")));A->Modify();A->Recipe=MoveTemp(Candidate);A->MarkPackageDirty();A->PostEditChange();return true;
    }
    void BeginTectonicDrag()
    {
        if(bTectonicDragging)return;
        PendingTectonicActivity=AppliedTectonicActivity=LayeredRecipe().Tectonics.Activity;LastTectonicApply=0;bTectonicDragging=true;
        TectonicDragTransaction=MakeUnique<FScopedTransaction>(FText::FromString(TEXT("Change tectonic activity")));RecipeAsset()->Modify();
        if(Globe)Globe->SetDraftMode(true);if(Preview)Preview->SetDraftMode(true);
        const TWeakPtr<FCLRecipeToolkit> Weak=StaticCastSharedRef<FCLRecipeToolkit>(AsShared());
        TectonicTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
        {if(auto Editor=Weak.Pin();Editor&&Editor->bTectonicDragging){Editor->PumpTectonicChanges(false);return true;}return false;}));
    }
    void PumpTectonicChanges(bool Force)
    {
        if(FMath::IsNearlyEqual(PendingTectonicActivity,AppliedTectonicActivity,1.e-7))return;
        const double Now=FPlatformTime::Seconds();if(!Force&&Now-LastTectonicApply<.12)return;
        auto P=LayeredRecipe().Tectonics;P.ApplyProgression(PendingTectonicActivity);
        if(CommitTectonicLayer(P))AppliedTectonicActivity=PendingTectonicActivity;LastTectonicApply=Now;
    }
    void EndTectonicDrag()
    {
        if(!bTectonicDragging)return;PumpTectonicChanges(true);bTectonicDragging=false;
        FTSTicker::GetCoreTicker().RemoveTicker(TectonicTicker);TectonicTicker.Reset();TectonicDragTransaction.Reset();
        if(Globe)Globe->SetDraftMode(false);if(Preview)Preview->SetDraftMode(false);Refresh();
    }
    const FCLPlanetRecipe& LayeredRecipe() const {return InspectorRecipe;}
    bool CommitContinents(FCLContinentalLayer Layer)
    {
        InspectorError.Reset();auto* A=RecipeAsset();auto R=LayeredRecipe();R.Continents=Layer;FString Error;
        if(!PrepareEdit(R,Error)){Status=Error;InspectorError=Error;return false;}
        TUniquePtr<FScopedTransaction> Tx;if(!TectonicDragTransaction)Tx=MakeUnique<FScopedTransaction>(FText::FromString(TEXT("Change continental layer")));
        A->Modify();A->Recipe=MoveTemp(R);A->MarkPackageDirty();A->PostEditChange();return true;
    }
    void BeginContinentalDrag()
    {
        if(bContinentalDragging)return;bContinentalDragging=true;
        PendingContinentalStrength=AppliedContinentalStrength=LayeredRecipe().Continents.Strength;LastTectonicApply=0;
        TectonicDragTransaction=MakeUnique<FScopedTransaction>(FText::FromString(TEXT("Change continental strength")));RecipeAsset()->Modify();
        if(Globe)Globe->SetDraftMode(true);if(Preview)Preview->SetDraftMode(true);
        const TWeakPtr<FCLRecipeToolkit> Weak=StaticCastSharedRef<FCLRecipeToolkit>(AsShared());
        TectonicTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float){if(auto E=Weak.Pin();E&&E->bContinentalDragging){E->PumpContinentalChanges(false);return true;}return false;}));
    }
    void PumpContinentalChanges(bool Force)
    {
        if(FMath::IsNearlyEqual(PendingContinentalStrength,AppliedContinentalStrength,1.e-7))return;
        const double Now=FPlatformTime::Seconds();if(!Force&&Now-LastTectonicApply<.12)return;
        auto L=LayeredRecipe().Continents;L.ApplyProgression(PendingContinentalStrength);
        if(CommitContinents(L))AppliedContinentalStrength=PendingContinentalStrength;LastTectonicApply=Now;
    }
    void EndContinentalDrag()
    {
        if(!bContinentalDragging)return;PumpContinentalChanges(true);bContinentalDragging=false;
        FTSTicker::GetCoreTicker().RemoveTicker(TectonicTicker);TectonicTicker.Reset();TectonicDragTransaction.Reset();
        if(Globe)Globe->SetDraftMode(false);if(Preview)Preview->SetDraftMode(false);Refresh();
    }
    void VaryLayer(bool Continental)
    {
        auto R=LayeredRecipe();int32 NewSeed;const int32 Old=Continental?R.Continents.Seed:R.Tectonics.Seed;
        do{NewSeed=int32(FGuid::NewGuid().A);}while(NewSeed==Old);
        if(Continental){R.Continents.Seed=NewSeed;CommitContinents(R.Continents);}else{R.Tectonics.Seed=NewSeed;CommitTectonicLayer(R.Tectonics);}
    }
    void AddLayerAdvanced(IDetailCategoryBuilder& Category,TSharedRef<FStructOnScope> Draft,
        const TSet<FName>& Hidden,TAttribute<bool> Enabled,TArray<TSharedPtr<IPropertyHandle>>& Properties,bool Advanced=true)
    {
        Properties.Reset();
        // Native property rows live in the layer's own Advanced area. A nested
        // StructureDetailsView would add another category and expander around them.
        for(TFieldIterator<FProperty> P(Draft->GetStruct());P;++P)
        {
            if(!P->HasAnyPropertyFlags(CPF_Edit)||Hidden.Contains(P->GetFName())||P->GetName().StartsWith(TEXT("bOverride")))continue;
            if(auto* Row=Category.AddExternalStructureProperty(Draft,P->GetFName(),Advanced?EPropertyLocation::Advanced:EPropertyLocation::Default))
            {Row->IsEnabled(Enabled);Properties.Add(Row->GetPropertyHandle());}
        }
    }
    void BuildTectonicAdvanced(IDetailCategoryBuilder& Category)
    {
        TectonicDraft=MakeShared<FStructOnScope>(FCLTectonicPreset::StaticStruct());
        *reinterpret_cast<FCLTectonicPreset*>(TectonicDraft->GetStructMemory())=ResolvedTectonicDraft();
        AddLayerAdvanced(Category,TectonicDraft.ToSharedRef(),{TEXT("Activity"),TEXT("bEnabled"),TEXT("AlgorithmVersion")},
            TAttribute<bool>::CreateLambda([this]{return LayeredRecipe().Tectonics.bEnabled&&!bTectonicDragging;}),TectonicAdvancedProperties,false);
    }
    void FinishGeologyAdvanced(const FPropertyChangedEvent& Event)
    {
        if(Event.ChangeType&EPropertyChangeType::Interactive)return;
        const auto* Property=Event.MemberProperty?Event.MemberProperty:Event.Property;
        if(!Property)return;
        // The native Details view finishes the gesture after editing the external
        // draft. Validate before touching the asset and keep a single asset Undo.
        if(Property->GetOwnerStruct()==FCLCaveSettings::StaticStruct()&&CaveDraft)
        {if(!CommitCave(*reinterpret_cast<const FCLCaveSettings*>(CaveDraft->GetStructMemory())))*reinterpret_cast<FCLCaveSettings*>(CaveDraft->GetStructMemory())=RecipeAsset()->Recipe.Caves;return;}
        if(Property->GetOwnerStruct()==FCLWatershedSettings::StaticStruct()&&WatershedDraft)
        {
            if(!CommitWatershed(*reinterpret_cast<const FCLWatershedSettings*>(WatershedDraft->GetStructMemory())))
                *reinterpret_cast<FCLWatershedSettings*>(WatershedDraft->GetStructMemory())=RecipeAsset()->Recipe.Watershed;
        }
        else if(Property->GetOwnerStruct()==FCLSurfaceDistributionSettings::StaticStruct()&&ElevationDraft)
        {
            if(!CommitElevation(*reinterpret_cast<const FCLSurfaceDistributionSettings*>(ElevationDraft->GetStructMemory())))
                *reinterpret_cast<FCLSurfaceDistributionSettings*>(ElevationDraft->GetStructMemory())=ElevationSettings();
        }
        else if(Property->GetOwnerStruct()==FCLTectonicPreset::StaticStruct()&&TectonicDraft)
        {
            if(!CommitTectonicLayer(*reinterpret_cast<const FCLTectonicPreset*>(TectonicDraft->GetStructMemory())))
                *reinterpret_cast<FCLTectonicPreset*>(TectonicDraft->GetStructMemory())=ResolvedTectonicDraft();
        }
        else if(Property->GetOwnerStruct()==FCLContinentalLayer::StaticStruct()&&ContinentalDraft)
        {
            if(!CommitContinents(*reinterpret_cast<const FCLContinentalLayer*>(ContinentalDraft->GetStructMemory())))
                *reinterpret_cast<FCLContinentalLayer*>(ContinentalDraft->GetStructMemory())=ResolvedContinentalDraft();
        }
        else if(Property->GetOwnerStruct()==FCLRockBedDistribution::StaticStruct()&&RockBedDraft)
        {
            auto L=LayeredRecipe().Rock;L.BedDistribution=*reinterpret_cast<const FCLRockBedDistribution*>(RockBedDraft->GetStructMemory());
            if(!CommitRock(L))*reinterpret_cast<FCLRockBedDistribution*>(RockBedDraft->GetStructMemory())=LayeredRecipe().Rock.BedDistribution;
        }
        else if((Property->GetOwnerStruct()==FCLRockLayer::StaticStruct()||Property->GetOwnerStruct()==FCLRockUnit::StaticStruct())&&RockDraft)
        {
            if(!CommitRock(*reinterpret_cast<const FCLRockLayer*>(RockDraft->GetStructMemory())))
                *reinterpret_cast<FCLRockLayer*>(RockDraft->GetStructMemory())=LayeredRecipe().Rock;
        }
    }
    void EditStructure()
    {
        FPropertyPath Path;Path.AddProperty(FPropertyInfo(FindFProperty<FProperty>(UCLPlanetRecipeAsset::StaticClass(),TEXT("Recipe"))));
        Path.AddProperty(FPropertyInfo(FindFProperty<FProperty>(FCLPlanetRecipe::StaticStruct(),TEXT("BaseStructure"))));
        InspectorPage=1;if(PlanetInspectors.IsValidIndex(1))PlanetInspectors[1]->ScrollPropertyIntoView(Path,true);
    }
    void AddFeature(bool Basin,bool Canyon=false)
    {
        auto* A=RecipeAsset();if(TerrainEditing || A->Recipe.Coordinates!=ECLTerrainCoordinates::SphericalSurface || A->Recipe.BaseStructure.Num()>=64)return;
        FScopedTransaction Transaction(FText::FromString(TEXT("Add planet structure")));A->Modify();A->Recipe.SchemaVersion=FMath::Max(11,A->Recipe.SchemaVersion);
        FCLPlanetFeature F;F.Id=FGuid::NewGuid();F.Name=Basin?TEXT("Basin"):TEXT("Broad uplift");F.Relief.Fraction=Basin?-.25:.25;
        if(Canyon){F.Name=TEXT("Great canyon");F.Profile=ECLPlanetFeatureProfile::Heightmap;F.CollectionId=TEXT("Canyons");F.CollectionRevision=2;F.SourceId=TEXT("MFC_RiverCanyonRampDepth");F.HalfExtentDegrees=FVector2D(78,24);F.Relief.Fraction=-.85;}
        A->Recipe.BaseStructure.Add(F);A->MarkPackageDirty();A->PostEditChange();DetailsView->ForceRefresh();EditStructure();
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
    FCLTectonicPreset ResolvedTectonicDraft() const
    {
        auto P=LayeredRecipe().Tectonics;if(P.AlgorithmVersion>=4)return P;FCLTectonicControls C;FString Error;
        if(FCLTectonicControls::Resolve(P,C,Error,true))
        {if(!P.bOverrideMotion)P.Motion=C.Motion;if(!P.bOverrideRelief)P.Relief=C.Relief;if(!P.bOverridePlateCount)P.PlateCount=C.LowerPlateCount;}
        return P;
    }
    FCLContinentalLayer ResolvedContinentalDraft() const
    {
        auto L=LayeredRecipe().Continents;const auto C=FCLContinentalControls::Resolve(L);
        if(!L.bOverrideExtent)L.Extent=C.Extent;if(!L.bOverrideRelief)L.Relief=C.Relief;return L;
    }
    void Refresh()
    {
        auto* Asset = RecipeAsset();
        if(LandformRows)LandformRows->SyncControls();SyncLandformHighlights();
        InspectorRecipe=Asset->Recipe;InspectorRecipe.UseGuidedGeology();
        if(TectonicDraft)
        {
            *reinterpret_cast<FCLTectonicPreset*>(TectonicDraft->GetStructMemory())=ResolvedTectonicDraft();
        }
        if(ContinentalDraft)*reinterpret_cast<FCLContinentalLayer*>(ContinentalDraft->GetStructMemory())=ResolvedContinentalDraft();
        if(CaveDraft)*reinterpret_cast<FCLCaveSettings*>(CaveDraft->GetStructMemory())=RecipeAsset()->Recipe.Caves;
        if(WatershedDraft)*reinterpret_cast<FCLWatershedSettings*>(WatershedDraft->GetStructMemory())=RecipeAsset()->Recipe.Watershed;
        if(RockDraft)*reinterpret_cast<FCLRockLayer*>(RockDraft->GetStructMemory())=LayeredRecipe().Rock;
        if(RockBedDraft)*reinterpret_cast<FCLRockBedDistribution*>(RockBedDraft->GetStructMemory())=LayeredRecipe().Rock.BedDistribution;
        if(ElevationDraft)*reinterpret_cast<FCLSurfaceDistributionSettings*>(ElevationDraft->GetStructMemory())=ElevationSettings();
        if (TerrainEditing) TerrainSource.Reset(TerrainEditing->Asset);
        else if (!Asset->Recipe.TerrainTypeId.IsValid()) TerrainSource.Reset();
        else if (!TerrainSource || TerrainSource->TerrainType.Id != Asset->Recipe.TerrainTypeId)
        {
            TerrainSource.Reset(); TArray<FAssetData> Assets;
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByClass(UCLTerrainTypeAsset::StaticClass()->GetClassPathName(), Assets, true);
            for (const auto& Data : Assets) if (auto* Source = Cast<UCLTerrainTypeAsset>(Data.GetAsset()); Source && Source->TerrainType.Id == Asset->Recipe.TerrainTypeId)
            { TerrainSource.Reset(Source); break; }
        }
        FString GuideError;const bool GuidesValid=Asset->SynchronizeRadialGuides(GuideError);
        if(Globe){Globe->SetAuthoringRecipe(Asset->Recipe);Globe->SetGuides(Asset->Recipe.RadialGuides,Preview?Preview->Client->bGuides:true);}
        if (Preview) Preview->Client->RadialGuides = Asset->Recipe.RadialGuides;
        if (TerrainEditing) TerrainEditing->Context = Asset->Recipe;
        FCLPlanetRecipe Input=Landforms?Landforms->PreviewRecipe():Asset->Recipe;const FGuid Solo=Landforms?Landforms->PreviewSolo():FGuid();TOptional<FBox2D> Region;
        if(TerrainEditing){FBox2D Bounds;if(!TerrainEditing->PreviewRecipe(Input,Bounds,Status,PendingAreaWidth>0?PendingAreaWidth:500)){Evaluation.Cancel();if(Globe){Globe->SetEvaluationPending(false);Globe->SetAuthoringValid(false);}bPreviewInvalid=true;InspectorError=Status;return;}if(!TerrainEditing->Asset->TerrainType.Rift.IsActive())Region=Bounds;}
        const bool Same=Solo==RequestedSolo&&RequestedRecipe.IsSet()&&FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&RequestedRecipe.GetValue(),&Input,0)&&Region.IsSet()==RequestedRegion.IsSet()&&(!Region.IsSet()||(Region->Min==RequestedRegion->Min&&Region->Max==RequestedRegion->Max));
        if(!Same)
        {
            RequestedRecipe=Input;RequestedRegion=Region;RequestedSolo=Solo;FString Error;
            auto Prepared=!Solo.IsValid()&&!TerrainEditing&&PreparedEditRecipe.IsSet()&&FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&PreparedEditRecipe.GetValue(),&Input,0)?PreparedEdit:Evaluation.Prepare(Input,Error,Region.IsSet()?&Region.GetValue():nullptr,Solo);
            PreparedEdit.Reset();PreparedEditRecipe.Reset();
            if(!GuidesValid||!Prepared){Evaluation.Cancel();if(Globe){Globe->SetEvaluationPending(false);Globe->SetAuthoringValid(false);}bPreviewInvalid=true;InspectorError=Status=!GuidesValid?GuideError:Error;}
            else {Evaluation.Request(Input,Prepared,true);bPreviewInvalid=false;InspectorError.Reset();if(Globe){Globe->SetAuthoringValid(true);Globe->SetEvaluationPending(true);}PumpEvaluation();}
        }
        else if((!Evaluation.IsBusy()||PublishedEvaluation==Evaluation.Latest())&&AcceptedSurfacePlan&&!bPreviewInvalid)PublishPlan(AcceptedSurfacePlan,true);
        RefreshAllUI(EditingAsset,false);
    }
    bool PrepareEdit(const FCLPlanetRecipe& R,FString& Error)
    {
        PreparedEdit=Evaluation.Prepare(R,Error);if(PreparedEdit)PreparedEditRecipe=R;else PreparedEditRecipe.Reset();return PreparedEdit.IsValid();
    }
    void PumpEvaluation()
    {
        // Give VPP its first publication and usable collision before starting
        // cave generation. Keep the accepted surface interactive during that job.
        if(Evaluation.AwaitingSurface()&&PublishedEvaluation==Evaluation.Latest()&&
            (UsesGlobePreview()?Globe&&Globe->IsSurfaceReady(AcceptedSurfacePlan):Preview&&Preview->IsSurfaceReady(AcceptedSurfacePlan)))
        {
#if WITH_DEV_AUTOMATION_TESTS
            if(!HoldCavesForTesting)
#endif
            Evaluation.ResumeAfterSurface(PublishedEvaluation);
        }
        if(auto R=Evaluation.Pump())
        {
            if(R->Serial!=Evaluation.Latest())return;
            if(Globe){Globe->SetEvaluationPending(false);Globe->SetAuthoringValid(R->Plan.IsValid());}
            LastEvaluationSeconds=R->Seconds;LastEvaluationWasWorker=R->WorkerThread;
            if(!R->Plan){bPreviewInvalid=true;InspectorError=Status=R->Error;return;}
            const bool PreserveWalk=PublishedEvaluation==R->Serial;
            bPreviewInvalid=false;InspectorError.Reset();PublishedEvaluation=R->Serial;AcceptedSurfacePlan=R->Plan;AcceptedWatershedDebug=R->WatershedDebug;
            if(!R->SurfaceOnly){MeasuredElevation=MoveTemp(R->Elevation);ElevationReadout=MoveTemp(R->ElevationReadout);MeasuredRockBeds=R->Beds;TargetRockBeds=MoveTemp(R->TargetBeds);}
            PublishPlan(R->Plan,PreserveWalk);
        }
    }
    void PublishPlan(const TSharedPtr<const FCLCompiledPlanetRecipe>& Plan,bool PreserveWalk=false)
    {
        auto* Asset=RecipeAsset();
        if(GenerationPanel)GenerationPanel->Refresh(Plan?Plan->GeneratedRegions:nullptr);
        if (Plan)
        {
            Status = TerrainEditing ? FString::Printf(TEXT("%s | %d layers | %s"), *TerrainEditing->Asset->TerrainType.Name,
                (TerrainEditing->Asset->TerrainType.Rift.IsActive()?TerrainEditing->Asset->TerrainType.NoiseLayers.Num():Plan->SurfacePatches.IsEmpty()?Plan->Layers.Num():Plan->SurfacePatches[0]->Layers.Num()), TerrainEditing->Solo != INDEX_NONE ? TEXT("Solo preview") : TEXT("Terrain preview")) :
                FString::Printf(TEXT("%s | radius %.0f m | %d broad features | %d editorial Landforms | %d generated regions"), *Asset->Recipe.Name, Plan->RadiusMeters, Asset->Recipe.BaseStructure.Num(), Asset->Recipe.TerrainRegions.Num(), Plan->GeneratedRegions?Plan->GeneratedRegions->Cells.Num():0);
            if (Preview)
            {
                if(UsesGlobePreview()){Preview->CancelPendingBuild();Preview->Client->Plan=Plan;}
                else if ((TerrainEditing&&!TerrainEditing->Asset->TerrainType.Rift.IsActive()) || !Plan->Regions.IsEmpty() || !Plan->SurfacePatches.IsEmpty()) Preview->RebuildAsync(Plan); else Preview->Rebuild(Plan);
                if(Globe && (!TerrainEditing||TerrainEditing->Asset->TerrainType.Rift.IsActive())){if(bGlobe){Globe->Rebuild(Plan,AcceptedWatershedDebug,PreserveWalk);if(TerrainEditing)Globe->SetRegionInspection(false);}else Globe->Cancel();}
                if (PendingAreaWidth <= 0) PendingAreaWidth = Preview->Client->AreaWidthMeters;
            }
        }
    }
    bool UsesGlobePreview()const{return bGlobe&&(!TerrainEditing||TerrainEditing->Asset->TerrainType.Rift.IsActive());}
    bool EvaluationUpdating()const{return Evaluation.IsBusy()||(UsesGlobePreview()?Globe&&Globe->IsUpdating():Preview&&Preview->IsUpdating());}
    bool AuthoringUpdating()const{return Evaluation.IsBusy()||(UsesGlobePreview()?Globe&&Globe->IsApplyingInput():Preview&&Preview->IsApplyingInput());}
    TOptional<float> EvaluationFraction()const
    {return Evaluation.IsBusy()?Evaluation.Fraction():UsesGlobePreview()?(Globe?Globe->InputProgress():TOptional<float>()):(Preview?Preview->InputProgress():TOptional<float>());}
    FText EvaluationCaption()const
    {
        if(!Evaluation.IsBusy())return FText::FromString(UsesGlobePreview()?Globe->InputProgressText():Preview->InputProgressText());
        if(Evaluation.AwaitingSurface())return FText::FromString(UsesGlobePreview()?Globe->InputProgressText():Preview->InputProgressText());
        FString Caption;switch(Evaluation.Stage())
        {
        case ECLCompileStage::Preparing:Caption=TEXT("Preparing terrain…");break;
        case ECLCompileStage::Geology:Caption=TEXT("Building geology…");break;
        case ECLCompileStage::Elevation:Caption=TEXT("Calibrating terrain…");break;
        case ECLCompileStage::Drainage:Caption=TEXT("Building drainage…");break;
        case ECLCompileStage::Caves:Caption=TEXT("Building caves…");break;
        case ECLCompileStage::Presentation:Caption=TEXT("Building watershed view…");break;
        default:Caption=TEXT("Finishing terrain…");break;
        }
        const uint64 Work=Evaluation.Work();if(uint32(Work>>32))Caption+=FString::Printf(TEXT(" · %u / %u"),uint32(Work),uint32(Work>>32));
        return FText::FromString(Caption);
    }
#if WITH_DEV_AUTOMATION_TESTS
    bool HoldCavesForTesting=false;
    void AwaitEvaluationForTesting()
    {
        // Field/controller tests deliberately do not tick a Slate viewport.
        const double Start=FPlatformTime::Seconds();while(Evaluation.IsBusy()&&FPlatformTime::Seconds()-Start<30){Evaluation.ResumeAfterSurface(Evaluation.Latest());PumpEvaluation();FPlatformProcess::SleepNoStats(.001f);}
    }
#endif
    TSharedRef<SWidget> BuildViewport()
    {
        TSharedRef<SWidget> GlobeWidget = SNullWidget::NullWidget;
        if (!TerrainEditing||TerrainEditing->Asset->TerrainType.Rift.IsActive())
            GlobeWidget = SAssignNew(Globe,SCLPlanetPreview)
                .OnPlacementCommitted(FOnCLGlobePlacementCommitted::CreateSP(this,&FCLRecipeToolkit::CommitGlobePlacement))
                .OnRegionSelected(FOnCLGlobeRegionSelected::CreateLambda([this](FGuid Id){if(GenerationPanel)GenerationPanel->SelectRegion(Id);}))
                .Visibility_Lambda([this]{return bGlobe?EVisibility::Visible:EVisibility::Collapsed;});
        const auto Content=SNew(SOverlay)
                + SOverlay::Slot()[SAssignNew(Preview, SCLSectionViewport).Visibility_Lambda([this]{return UsesGlobePreview()?EVisibility::Collapsed:EVisibility::Visible;})]
                + SOverlay::Slot()[GlobeWidget]
                + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(12)
                    [SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(8).Visibility_Lambda([this]{return AuthoringUpdating()?EVisibility::HitTestInvisible:EVisibility::Collapsed;})
                        [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(this,&FCLRecipeToolkit::EvaluationCaption)]
                            +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)[SNew(SBox).WidthOverride(200)[SNew(SProgressBar).Percent_Lambda([this]{return TOptional<float>(EvaluationFraction().Get(0.f));}).Visibility_Lambda([this]{return EvaluationFraction().IsSet()?EVisibility::HitTestInvisible:EVisibility::Collapsed;})]]]]
                + SOverlay::Slot()[SAssignNew(Annotations, SCLHeightAnnotations).Client(Preview->Client).Session(RadialGuidesSession).Globe(Globe).UseGlobe_Lambda([this]{return UsesGlobePreview();})
                    .Visibility_Lambda([this] { return TerrainEditing ? EVisibility::HitTestInvisible : EVisibility::SelfHitTestInvisible; })]
                + SOverlay::Slot().VAlign(VAlign_Top).Padding(12)[SNew(SBorder).Padding(10)
                    .Visibility_Lambda([this] { return bPreviewInvalid ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
                    .BorderBackgroundColor(FLinearColor(.12, .06, .025, .95))
                    [SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FLinearColor(1, .65, .3))
                        .Text_Lambda([this] { return FText::FromString(Status + TEXT("\nThe displayed terrain does not reflect the current layer settings.")); })]];
        const TWeakPtr<FCLRecipeToolkit> Weak=StaticCastSharedRef<FCLRecipeToolkit>(AsShared());
        Preview->OnWalkPoint=[Weak](FVector P){if(auto E=Weak.Pin();E&&E->Globe)E->Globe->SetWalkPoint(P);};
        return Content;
    }
    void WalkHere()
    {
        if(EvaluationUpdating()||bPreviewInvalid)return;
        if(!Globe||Globe->WalkLocation().IsNearlyZero()||!FCLPlanetInspection::Launch.IsBound())return;
        FCLPlanetInspectionRequest Request;FString Error;
        if(!RecipeAsset()->ExportJsonString(Request.RecipeJson,Error)){WalkStatus=Error;InspectorError=Error;return;}
        Request.Direction=Globe->WalkLocation();Request.SurfacePointMeters=Globe->WalkPoint();WalkRequestId=Request.Id;
        const TWeakPtr<FCLRecipeToolkit> Weak=StaticCastSharedRef<FCLRecipeToolkit>(AsShared());
        Request.OnReturned=[Weak](FGuid Id,FVector Direction,FString Result)
        {
            if(auto Editor=Weak.Pin();Editor&&Editor->WalkRequestId==Id)
            {
                Editor->WalkStatus=MoveTemp(Result);if(Editor->Globe&&!Direction.IsNearlyZero())Editor->Globe->SetWalkLocation(Direction);
            }
        };
        Request.OnPositionReturned=[Weak](FGuid Id,FVector P){if(auto E=Weak.Pin();E&&E->WalkRequestId==Id&&E->Globe)E->Globe->SetWalkPoint(P);};
        InspectorError.Reset();WalkStatus=TEXT("Opening walking preview…");
        if(!FCLPlanetInspection::Launch.Execute(MoveTemp(Request),Error)){WalkStatus=Error;InspectorError=Error;}
    }
    static TSharedRef<STextBlock> InspectorLabel(const TCHAR* Text)
    {return SNew(STextBlock).Text(FText::FromString(Text)).Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"));}
    static FDetailWidgetRow& InspectorRow(IDetailCategoryBuilder& Category,const TCHAR* Label,const TSharedRef<SWidget>& Widget,bool Advanced=false)
    {return Category.AddCustomRow(FText::FromString(Label),Advanced).NameContent()[InspectorLabel(Label)].ValueContent().MinDesiredWidth(220).MaxDesiredWidth(900)[Widget];}
    static TSharedRef<SWidget> CharacterReadout(TAttribute<double> Value)
    {
        return SNew(SBox).WidthOverride(36)[SNew(STextBlock)
            .Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont")).Justification(ETextJustify::Right)
            .Text_Lambda([Value]{return FText::AsNumber(FMath::RoundToInt(100*FMath::Clamp(Value.Get(),0.,1.)));})];
    }
    TSharedRef<SWidget> GeologyHeader(int32 Index,const TCHAR* Name,TAttribute<bool> Enabled,
        TFunction<void()> Toggle,const TSharedRef<SWidget>& Character,const TSharedRef<SWidget>& Reseed)
    {
        return SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)
            [SAssignNew(LayerVisibility[Index],SButton).ButtonStyle(FAppStyle::Get(),"HoverHintOnly").ContentPadding(0)
                .ToolTipText(FText::FromString(TEXT("Toggle layer visibility")))
                .OnClicked_Lambda([Toggle]{Toggle();return FReply::Handled();})
                [SNew(SImage).Image_Lambda([Enabled]{return FAppStyle::GetBrush(Enabled.Get()?TEXT("Level.VisibleIcon16x"):TEXT("Level.NotVisibleIcon16x"));})]]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)
            [SAssignNew(LayerIndicators[Index],SCheckBox).Style(FAppStyle::Get(),"ToggleButtonCheckbox").Padding(2).IsEnabled(Index==3||Index==4)
                .ToolTipText(FText::FromString(Index==3?TEXT("Toggle Watershed indicators: drainage, channels and potential lake rims (globe)"):Index==4?TEXT("Inspect caves through a dimmed planet surface"):TEXT("This layer has no visual indicators")))
                .IsChecked_Lambda([this,Index]{return ((Index==3&&bWatershedOverlay)||(Index==4&&bCaveOverlay))?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this,Index](ECheckBoxState V){if(Index==3){bWatershedOverlay=V==ECheckBoxState::Checked;if(Globe)Globe->SetWatershedOverlay(bWatershedOverlay);}else if(Index==4){bCaveOverlay=V==ECheckBoxState::Checked;if(Globe)Globe->SetCaveOverlay(bCaveOverlay);}})
                [SNew(SImage).Image(FAppStyle::GetBrush("EditorViewport.WireframeMode"))]]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
            [SNew(SBox).WidthOverride(100)[SNew(STextBlock).Text(FText::FromString(Name)).Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"))]]
            +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Character]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6,0,2,0)[Reseed];
    }
    TSharedRef<SWidget> VariationButton(bool Continental)
    {
        return SAssignNew((Continental?ContinentalVariation:TectonicVariation),SButton).ButtonStyle(FAppStyle::Get(),"SimpleButton").ContentPadding(3)
            .ToolTipText(FText::FromString(TEXT("New variation")))
            .OnClicked_Lambda([this,Continental]{VaryLayer(Continental);return FReply::Handled();})
            [SNew(SImage).Image(FAppStyle::GetBrush("Icons.Refresh"))];
    }
    bool CommitRock(FCLRockLayer Layer)
    {
        auto* A=RecipeAsset();auto R=LayeredRecipe();R.Rock=MoveTemp(Layer);R.SchemaVersion=FMath::Max(R.SchemaVersion,R.Rock.Units.ContainsByPredicate([](const auto& U){return U.Lithology!=ECLRockLithology::Unknown||U.FracturePotential>0;})?23:20);if(R.Rock.bEnabled&&!R.SurfaceStack.IsActive())R.SurfaceStack=FCLSurfaceStack::MakeDefault();FString Error;
        if(!PrepareEdit(R,Error)){InspectorError=Status=Error;return false;}
        InspectorError.Reset();TUniquePtr<FScopedTransaction> Tx;if(!RockTransaction)Tx=MakeUnique<FScopedTransaction>(FText::FromString(TEXT("Change rock character")));
        A->Modify();A->Recipe=MoveTemp(R);A->MarkPackageDirty();A->PostEditChange();return true;
    }
    void BeginRockDrag()
    {
        if(bRockDragging)return;bRockDragging=true;PendingRock=AppliedRock=LayeredRecipe().Rock.Character;LastRockApply=0;
        RockTransaction=MakeUnique<FScopedTransaction>(FText::FromString(TEXT("Change rock character")));RecipeAsset()->Modify();
        if(Globe)Globe->SetDraftMode(true);if(Preview)Preview->SetDraftMode(true);
        const TWeakPtr<FCLRecipeToolkit> Weak=StaticCastSharedRef<FCLRecipeToolkit>(AsShared());
        RockTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float){if(auto E=Weak.Pin();E&&E->bRockDragging){E->PumpRock(false);return true;}return false;}));
    }
    void PumpRock(bool Force)
    {
        if(FMath::IsNearlyEqual(PendingRock,AppliedRock,1.e-7))return;const double Now=FPlatformTime::Seconds();if(!Force&&Now-LastRockApply<.12)return;
        auto L=LayeredRecipe().Rock;L.ApplyProgression(PendingRock);if(CommitRock(L))AppliedRock=PendingRock;LastRockApply=Now;
    }
    void EndRockDrag()
    {
        if(!bRockDragging)return;PumpRock(true);bRockDragging=false;FTSTicker::GetCoreTicker().RemoveTicker(RockTicker);RockTicker.Reset();RockTransaction.Reset();
        if(Globe)Globe->SetDraftMode(false);if(Preview)Preview->SetDraftMode(false);Refresh();
    }
    void BuildRockAdvanced(IDetailCategoryBuilder& Category)
    {
        RockDraft=MakeShared<FStructOnScope>(FCLRockLayer::StaticStruct());*reinterpret_cast<FCLRockLayer*>(RockDraft->GetStructMemory())=LayeredRecipe().Rock;
        AddLayerAdvanced(Category,RockDraft.ToSharedRef(),{TEXT("AlgorithmVersion"),TEXT("bEnabled"),TEXT("Character"),TEXT("BedDistribution")},
            TAttribute<bool>::CreateLambda([this]{return LayeredRecipe().Rock.bEnabled&&!bRockDragging;}),RockAdvancedProperties,false);
        RockBedDraft=MakeShared<FStructOnScope>(FCLRockBedDistribution::StaticStruct());*reinterpret_cast<FCLRockBedDistribution*>(RockBedDraft->GetStructMemory())=LayeredRecipe().Rock.BedDistribution;
        AddLayerAdvanced(Category,RockBedDraft.ToSharedRef(),{},TAttribute<bool>::CreateLambda([this]{return LayeredRecipe().Rock.bEnabled&&LayeredRecipe().Rock.AlgorithmVersion>=5&&!bRockDragging;}),RockBedProperties,false);

    }
    void BuildContinentalAdvanced(IDetailCategoryBuilder& Category)
    {
        ContinentalDraft=MakeShared<FStructOnScope>(FCLContinentalLayer::StaticStruct());
        *reinterpret_cast<FCLContinentalLayer*>(ContinentalDraft->GetStructMemory())=ResolvedContinentalDraft();
        AddLayerAdvanced(Category,ContinentalDraft.ToSharedRef(),{TEXT("AlgorithmVersion"),TEXT("bCoastalMargin"),TEXT("bEnabled"),TEXT("Strength")},
            TAttribute<bool>::CreateLambda([this]{return LayeredRecipe().Continents.bEnabled&&!bContinentalDragging;}),ContinentalAdvancedProperties,false);
    }
    bool CommitWatershed(FCLWatershedSettings Layer)
    {
        auto R=RecipeAsset()->Recipe;R.UseGuidedGeology();R.SchemaVersion=FMath::Max(22,R.SchemaVersion);R.AlgorithmVersion=2;R.Watershed=MoveTemp(Layer);FString Error;
        if(!PrepareEdit(R,Error)){InspectorError=Status=Error;return false;}
        TUniquePtr<FScopedTransaction> Tx;if(!WatershedTransaction)Tx=MakeUnique<FScopedTransaction>(FText::FromString(TEXT("Change watershed character")));
        auto* A=RecipeAsset();A->Modify();A->Recipe=MoveTemp(R);A->MarkPackageDirty();A->PostEditChange();InspectorError.Reset();return true;
    }
    void BeginWatershedDrag()
    {
        if(bWatershedDragging)return;bWatershedDragging=true;PendingWatershed=AppliedWatershed=RecipeAsset()->Recipe.Watershed.Character;LastWatershedApply=0;
        WatershedTransaction=MakeUnique<FScopedTransaction>(FText::FromString(TEXT("Change watershed character")));RecipeAsset()->Modify();
        if(Globe)Globe->SetDraftMode(true);if(Preview)Preview->SetDraftMode(true);
        const TWeakPtr<FCLRecipeToolkit> Weak=StaticCastSharedRef<FCLRecipeToolkit>(AsShared());
        WatershedTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float){if(auto E=Weak.Pin();E&&E->bWatershedDragging){E->PumpWatershed(false);return true;}return false;}));
    }
    void PumpWatershed(bool Force)
    {
        if(FMath::IsNearlyEqual(PendingWatershed,AppliedWatershed,1.e-7))return;const double Now=FPlatformTime::Seconds();if(!Force&&Now-LastWatershedApply<.3)return;
        auto L=RecipeAsset()->Recipe.Watershed;L.ApplyProgression(PendingWatershed);if(CommitWatershed(L))AppliedWatershed=PendingWatershed;LastWatershedApply=Now;
    }
    void EndWatershedDrag()
    {
        if(!bWatershedDragging)return;PumpWatershed(true);bWatershedDragging=false;FTSTicker::GetCoreTicker().RemoveTicker(WatershedTicker);WatershedTicker.Reset();WatershedTransaction.Reset();
        if(Globe)Globe->SetDraftMode(false);if(Preview)Preview->SetDraftMode(false);Refresh();
    }
    void BuildWatershedInspector(IDetailCategoryBuilder& C)
    {
        C.HeaderContent(GeologyHeader(3,TEXT("Watershed"),TAttribute<bool>::CreateLambda([this]{return RecipeAsset()->Recipe.Watershed.bEnabled;}),
            [this]{auto L=RecipeAsset()->Recipe.Watershed;L.bEnabled=!L.bEnabled;CommitWatershed(L);},
            SNew(SHorizontalBox).IsEnabled_Lambda([this]{return RecipeAsset()->Recipe.Watershed.bEnabled;})
            +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0,0,8,0)[SAssignNew(WatershedSlider,SSlider)
                .Value_Lambda([this]{return float(bWatershedDragging?PendingWatershed:RecipeAsset()->Recipe.Watershed.Character);})
                .OnMouseCaptureBegin_Lambda([this]{BeginWatershedDrag();}).OnMouseCaptureEnd_Lambda([this]{EndWatershedDrag();})
                .OnControllerCaptureBegin_Lambda([this]{BeginWatershedDrag();}).OnControllerCaptureEnd_Lambda([this]{EndWatershedDrag();})
                .OnValueChanged_Lambda([this](float V){PendingWatershed=V;if(bWatershedDragging)PumpWatershed(false);else{auto L=RecipeAsset()->Recipe.Watershed;L.ApplyProgression(V);CommitWatershed(L);}})]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[CharacterReadout(TAttribute<double>::CreateLambda([this]{return bWatershedDragging?PendingWatershed:RecipeAsset()->Recipe.Watershed.Character;}))],SAssignNew(WatershedVariation,SButton).ButtonStyle(FAppStyle::Get(),"SimpleButton").ContentPadding(3).IsEnabled(false).ToolTipText(FText::FromString(TEXT("Watershed derives from the existing terrain; it has no independent seed")))[SNew(SImage).Image(FAppStyle::GetBrush("Icons.Refresh"))]),true);
        C.InitiallyCollapsed(true);
        WatershedDraft=MakeShared<FStructOnScope>(FCLWatershedSettings::StaticStruct());*reinterpret_cast<FCLWatershedSettings*>(WatershedDraft->GetStructMemory())=RecipeAsset()->Recipe.Watershed;
        AddLayerAdvanced(C,WatershedDraft.ToSharedRef(),{TEXT("AlgorithmVersion"),TEXT("bEnabled"),TEXT("Character")},TAttribute<bool>::CreateLambda([this]{return RecipeAsset()->Recipe.Watershed.bEnabled&&!bWatershedDragging;}),WatershedProperties,false);C.SetShowAdvanced(false);
    }
    bool CommitCave(FCLCaveSettings Layer)
    {
        auto R=RecipeAsset()->Recipe;R.UseGuidedGeology();R.SchemaVersion=23;R.AlgorithmVersion=2;R.Caves=MoveTemp(Layer);FString Error;
        if(!PrepareEdit(R,Error)){InspectorError=Status=Error;return false;}
        TUniquePtr<FScopedTransaction> Tx;if(!CaveTransaction)Tx=MakeUnique<FScopedTransaction>(FText::FromString(TEXT("Change caves")));
        auto* A=RecipeAsset();A->Modify();A->Recipe=MoveTemp(R);A->MarkPackageDirty();A->PostEditChange();InspectorError.Reset();return true;
    }
    void BeginCaveDrag()
    {
        if(bCaveDragging)return;bCaveDragging=true;PendingCave=AppliedCave=RecipeAsset()->Recipe.Caves.Character;LastCaveApply=0;
        CaveTransaction=MakeUnique<FScopedTransaction>(FText::FromString(TEXT("Change caves")));RecipeAsset()->Modify();
        if(Globe)Globe->SetDraftMode(true);if(Preview)Preview->SetDraftMode(true);
        const TWeakPtr<FCLRecipeToolkit> Weak=StaticCastSharedRef<FCLRecipeToolkit>(AsShared());
        CaveTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float){if(auto E=Weak.Pin();E&&E->bCaveDragging){E->PumpCave(false);return true;}return false;}));
    }
    void PumpCave(bool Force)
    {
        if(FMath::IsNearlyEqual(PendingCave,AppliedCave,1.e-7))return;const double Now=FPlatformTime::Seconds();if(!Force&&Now-LastCaveApply<.3)return;
        auto L=RecipeAsset()->Recipe.Caves;L.ApplyProgression(PendingCave);if(CommitCave(L))AppliedCave=PendingCave;LastCaveApply=Now;
    }
    void EndCaveDrag()
    {
        if(!bCaveDragging)return;PumpCave(true);bCaveDragging=false;FTSTicker::GetCoreTicker().RemoveTicker(CaveTicker);CaveTicker.Reset();CaveTransaction.Reset();
        if(Globe)Globe->SetDraftMode(false);if(Preview)Preview->SetDraftMode(false);Refresh();
    }
    void BuildCaveInspector(IDetailCategoryBuilder& C)
    {
        C.HeaderContent(GeologyHeader(4,TEXT("Caves"),TAttribute<bool>::CreateLambda([this]{return RecipeAsset()->Recipe.Caves.bEnabled;}),
            [this]{auto L=RecipeAsset()->Recipe.Caves;L.bEnabled=!L.bEnabled;CommitCave(L);},
            SNew(SHorizontalBox).IsEnabled_Lambda([this]{return RecipeAsset()->Recipe.Caves.bEnabled;})
            +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0,0,8,0)[SAssignNew(CaveSlider,SSlider)
                .Value_Lambda([this]{return float(bCaveDragging?PendingCave:RecipeAsset()->Recipe.Caves.Character);})
                .OnMouseCaptureBegin_Lambda([this]{BeginCaveDrag();}).OnMouseCaptureEnd_Lambda([this]{EndCaveDrag();})
                .OnControllerCaptureBegin_Lambda([this]{BeginCaveDrag();}).OnControllerCaptureEnd_Lambda([this]{EndCaveDrag();})
                .OnValueChanged_Lambda([this](float V){PendingCave=V;if(bCaveDragging)PumpCave(false);else{auto L=RecipeAsset()->Recipe.Caves;L.ApplyProgression(V);CommitCave(L);}})]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[CharacterReadout(TAttribute<double>::CreateLambda([this]{return bCaveDragging?PendingCave:RecipeAsset()->Recipe.Caves.Character;}))],
            SAssignNew(CaveVariation,SButton).ButtonStyle(FAppStyle::Get(),"SimpleButton").ContentPadding(3)
                .OnClicked_Lambda([this]{auto L=RecipeAsset()->Recipe.Caves;const int32 Before=L.Seed;do{L.Seed=int32(FGuid::NewGuid().A);}while(L.Seed==Before);CommitCave(L);return FReply::Handled();})[SNew(SImage).Image(FAppStyle::GetBrush("Icons.Refresh"))]),true);
        C.InitiallyCollapsed(true);CaveDraft=MakeShared<FStructOnScope>(FCLCaveSettings::StaticStruct());*reinterpret_cast<FCLCaveSettings*>(CaveDraft->GetStructMemory())=RecipeAsset()->Recipe.Caves;
        AddLayerAdvanced(C,CaveDraft.ToSharedRef(),{TEXT("AlgorithmVersion"),TEXT("bEnabled"),TEXT("Character")},TAttribute<bool>::CreateLambda([this]{return RecipeAsset()->Recipe.Caves.bEnabled&&!bCaveDragging;}),CaveProperties,false);C.SetShowAdvanced(false);
    }
    FCLSurfaceDistributionSettings ElevationSettings()const {return RecipeAsset()->Recipe.ElevationDistribution;}
    bool CommitSurfaceRecipe(FCLPlanetRecipe R)
    {
        FString Error;if(!PrepareEdit(R,Error)){InspectorError=Status=Error;return false;}
        TUniquePtr<FScopedTransaction> Tx;if(!SurfaceTransaction)Tx=MakeUnique<FScopedTransaction>(FText::FromString(TEXT("Change surface character")));
        auto* A=RecipeAsset();A->Modify();A->Recipe=MoveTemp(R);A->MarkPackageDirty();A->PostEditChange();InspectorError.Reset();return true;
    }
    bool CommitElevation(FCLSurfaceDistributionSettings Settings)
    {
        auto R=RecipeAsset()->Recipe;R.UseGuidedGeology();R.AlgorithmVersion=2;R.SchemaVersion=FMath::Max(21,R.SchemaVersion);
        R.ElevationDistribution=Settings;
        return CommitSurfaceRecipe(MoveTemp(R));
    }
    void BeginSurfaceDrag()
    {
        if(bElevationDragging)return;bElevationDragging=true;
        PendingElevation=AppliedElevation=ElevationSettings().Character;LastSurfaceApply=0;
        SurfaceTransaction=MakeUnique<FScopedTransaction>(FText::FromString(TEXT("Change surface character")));RecipeAsset()->Modify();
        if(Globe)Globe->SetDraftMode(true);if(Preview)Preview->SetDraftMode(true);
        const TWeakPtr<FCLRecipeToolkit> Weak=StaticCastSharedRef<FCLRecipeToolkit>(AsShared());
        SurfaceTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float){if(auto E=Weak.Pin();E&&E->bElevationDragging){E->PumpSurface(false);return true;}return false;}));
    }
    void PumpSurface(bool Force)
    {
        const double Now=FPlatformTime::Seconds();if(!Force&&Now-LastSurfaceApply<.12)return;
        if(bElevationDragging&&!FMath::IsNearlyEqual(PendingElevation,AppliedElevation,1.e-7))
        {auto P=ElevationSettings();P.ApplyProgression(PendingElevation);if(CommitElevation(P))AppliedElevation=PendingElevation;}
        LastSurfaceApply=Now;
    }
    void EndSurfaceDrag()
    {
        if(!bElevationDragging)return;PumpSurface(true);bElevationDragging=false;
        FTSTicker::GetCoreTicker().RemoveTicker(SurfaceTicker);SurfaceTicker.Reset();SurfaceTransaction.Reset();
        if(Globe)Globe->SetDraftMode(false);if(Preview)Preview->SetDraftMode(false);Refresh();
    }
    void BuildSurfaceInspector(IDetailCategoryBuilder& E)
    {
        E.HeaderContent(SNew(SCheckBox).IsChecked_Lambda([this]{return ElevationSettings().bEnabled?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this](ECheckBoxState V){auto P=ElevationSettings();P.bEnabled=V==ECheckBoxState::Checked;CommitElevation(P);}));
        InspectorRow(E,TEXT("Character"),SNew(SHorizontalBox).IsEnabled_Lambda([this]{return ElevationSettings().bEnabled;})
            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,8,0)[SAssignNew(ElevationSlider,SSlider)
                .Value_Lambda([this]{return float(bElevationDragging?PendingElevation:ElevationSettings().Character);})
                .ToolTipText(FText::FromString(TEXT("Lowlands → mixed elevations → uplands. Tail areas remain independent.")))
                .OnMouseCaptureBegin_Lambda([this]{BeginSurfaceDrag();}).OnMouseCaptureEnd(this,&FCLRecipeToolkit::EndSurfaceDrag)
                .OnControllerCaptureBegin_Lambda([this]{BeginSurfaceDrag();}).OnControllerCaptureEnd(this,&FCLRecipeToolkit::EndSurfaceDrag)
                .OnValueChanged_Lambda([this](float V){PendingElevation=V;if(bElevationDragging)PumpSurface(false);else{auto P=ElevationSettings();P.ApplyProgression(V);CommitElevation(P);}})]
            +SHorizontalBox::Slot().AutoWidth()[CharacterReadout(TAttribute<double>::CreateLambda([this]{return bElevationDragging?PendingElevation:ElevationSettings().Character;}))]);
        ElevationDraft=MakeShared<FStructOnScope>(FCLSurfaceDistributionSettings::StaticStruct());*reinterpret_cast<FCLSurfaceDistributionSettings*>(ElevationDraft->GetStructMemory())=ElevationSettings();
        const auto Enabled=TAttribute<bool>::CreateLambda([this]{return ElevationSettings().bEnabled&&!bElevationDragging;});
        AddLayerAdvanced(E,ElevationDraft.ToSharedRef(),{TEXT("AlgorithmVersion"),TEXT("bEnabled"),TEXT("Character")},Enabled,ElevationAdvancedProperties);
        E.SetShowAdvanced(false);
    }
    void BuildPlanetInspector(IDetailLayoutBuilder& B,int32 Page)
    {
        const auto Recipe=B.GetProperty(GET_MEMBER_NAME_CHECKED(UCLPlanetRecipeAsset,Recipe));
        auto Property=[&](const TCHAR* Name){return Recipe->GetChildHandle(FName(Name));};
        auto Add=[&](IDetailCategoryBuilder& C,const TCHAR* Name,bool Advanced=false){C.AddProperty(Property(Name),EPropertyLocation::Type(Advanced?EPropertyLocation::Advanced:EPropertyLocation::Default));};
        if(Page==0)
        {
            auto& C=B.EditCategory(TEXT("Body"),FText::FromString(TEXT("Planet")));C.SetSortOrder(0);
            Add(C,TEXT("Name"));Add(C,TEXT("RadiusMeters"));
            InspectorRow(C,TEXT("Reference area"),SNew(STextBlock).Text_Lambda([this]{const auto& R=RecipeAsset()->Recipe;return FText::FromString(FString::Printf(TEXT("%.2f km²"),4*UE_DOUBLE_PI*FMath::Square(R.RadiusMeters*R.BaseSurfaceNormalized)/1.e6));}));
            InspectorRow(C,TEXT("Radial Guides"),SNew(SComboButton).OnGetMenuContent(this,&FCLRecipeToolkit::RadialGuidesMenu)
                .ButtonContent()[SNew(STextBlock).Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont")).Text_Lambda([this]{return FText::FromString(RecipeAsset()->Recipe.RadialGuides.Name);})]);
            Add(C,TEXT("BaseSurfaceNormalized"),true);Add(C,TEXT("Seed"),true);
            C.AddProperty(B.GetProperty(GET_MEMBER_NAME_CHECKED(UCLPlanetRecipeAsset,RadialGuideLibrary)),EPropertyLocation::Advanced);
            InspectorRow(C,TEXT("Guide set"),SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1)[SNew(SInlineEditableTextBlock).Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
                    .Text_Lambda([this]{return FText::FromString(RecipeAsset()->Recipe.RadialGuides.Name);})
                    .OnTextCommitted_Lambda([this](const FText& Text,ETextCommit::Type Type)
                    {
                        if(Type==ETextCommit::OnCleared||Text.IsEmpty())return;
                        const FString Name=Text.ToString().TrimStartAndEnd();if(Name.IsEmpty()||RadialGuidesSession->Library->Library.Sets.ContainsByPredicate([&](const auto& S){return S.Id!=RecipeAsset()->Recipe.RadialGuides.Id&&S.Name.Equals(Name,ESearchCase::IgnoreCase);}))return;
                        RadialGuidesSession->Begin(FText::FromString(TEXT("Rename radial guides")));RecipeAsset()->Recipe.RadialGuides.Name=Name;RadialGuidesSession->Changed();RadialGuidesSession->End();
                    })]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Duplicate"))).OnClicked_Lambda([this]{RadialGuidesSession->NewSet();return FReply::Handled();})]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Remove"))).OnClicked_Lambda([this]{RadialGuidesSession->RemoveSet();return FReply::Handled();})],true);
            C.SetShowAdvanced(false);
            auto& E=B.EditCategory(TEXT("SurfaceDistribution"),FText::FromString(TEXT("Surface Distribution")));E.SetSortOrder(1);BuildSurfaceInspector(E);
        }
        else if(Page==1)
        {
            auto& C=B.EditCategory(TEXT("ContinentalLayer"),FText::FromString(TEXT("Continents")));C.SetSortOrder(1);ContinentalAdvanced=&C;
            C.HeaderContent(GeologyHeader(0,TEXT("Continents"),TAttribute<bool>::CreateLambda([this]{return LayeredRecipe().Continents.bEnabled;}),
                [this]{auto L=LayeredRecipe().Continents;L.bEnabled=!L.bEnabled;CommitContinents(L);},
                SNew(SHorizontalBox).IsEnabled_Lambda([this]{return LayeredRecipe().Continents.bEnabled;})
                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0,0,8,0)[SAssignNew(ContinentalSlider,SSlider)
                    .Value_Lambda([this]{return float(bContinentalDragging?PendingContinentalStrength:LayeredRecipe().Continents.Strength);})
                    .OnMouseCaptureBegin_Lambda([this]{BeginContinentalDrag();}).OnMouseCaptureEnd_Lambda([this]{EndContinentalDrag();})
                    .OnControllerCaptureBegin_Lambda([this]{BeginContinentalDrag();}).OnControllerCaptureEnd_Lambda([this]{EndContinentalDrag();})
                    .OnValueChanged_Lambda([this](float V){PendingContinentalStrength=V;if(bContinentalDragging)PumpContinentalChanges(false);else{auto L=LayeredRecipe().Continents;L.ApplyProgression(V);CommitContinents(L);}})]
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[CharacterReadout(TAttribute<double>::CreateLambda([this]{return bContinentalDragging?PendingContinentalStrength:LayeredRecipe().Continents.Strength;}))],VariationButton(true)),true);
            C.InitiallyCollapsed(true);
            BuildContinentalAdvanced(C);C.SetShowAdvanced(false);
            auto& T=B.EditCategory(TEXT("TectonicLayer"),FText::FromString(TEXT("Tectonics")));T.SetSortOrder(0);TectonicAdvanced=&T;
            T.HeaderContent(GeologyHeader(1,TEXT("Tectonics"),TAttribute<bool>::CreateLambda([this]{return LayeredRecipe().Tectonics.bEnabled;}),
                [this]{auto L=LayeredRecipe().Tectonics;L.bEnabled=!L.bEnabled;CommitTectonicLayer(L);},
                SNew(SHorizontalBox).IsEnabled_Lambda([this]{return LayeredRecipe().Tectonics.bEnabled;})
                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0,0,8,0)[SAssignNew(TectonicSlider,SSlider)
                    .Value_Lambda([this]{return float(bTectonicDragging?PendingTectonicActivity:LayeredRecipe().Tectonics.Activity);})
                    .OnMouseCaptureBegin_Lambda([this]{BeginTectonicDrag();}).OnMouseCaptureEnd_Lambda([this]{EndTectonicDrag();})
                    .OnControllerCaptureBegin_Lambda([this]{BeginTectonicDrag();}).OnControllerCaptureEnd_Lambda([this]{EndTectonicDrag();})
                    .OnValueChanged_Lambda([this](float V){PendingTectonicActivity=V;if(bTectonicDragging)PumpTectonicChanges(false);else{auto L=LayeredRecipe().Tectonics;L.ApplyProgression(V);CommitTectonicLayer(L);}})]
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[CharacterReadout(TAttribute<double>::CreateLambda([this]{return bTectonicDragging?PendingTectonicActivity:LayeredRecipe().Tectonics.Activity;}))],VariationButton(false)),true);
            T.InitiallyCollapsed(true);
            BuildTectonicAdvanced(T);T.SetShowAdvanced(false);
            auto& R=B.EditCategory(TEXT("RockLayer"),FText::FromString(TEXT("Rock")));R.SetSortOrder(2);RockAdvanced=&R;
            R.HeaderContent(GeologyHeader(2,TEXT("Rock"),TAttribute<bool>::CreateLambda([this]{return LayeredRecipe().Rock.bEnabled;}),
                [this]{auto L=LayeredRecipe().Rock;L.bEnabled=!L.bEnabled;if(L.bEnabled&&L.AlgorithmVersion<5)L.ApplyProgression(L.Character);CommitRock(L);},
                SNew(SHorizontalBox).IsEnabled_Lambda([this]{return LayeredRecipe().Rock.bEnabled;})
                +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,8,0)[SAssignNew(RockSlider,SSlider)
                    .Value_Lambda([this]{return float(bRockDragging?PendingRock:LayeredRecipe().Rock.Character);})
                    .OnMouseCaptureBegin_Lambda([this]{BeginRockDrag();}).OnMouseCaptureEnd_Lambda([this]{EndRockDrag();})
                    .OnControllerCaptureBegin_Lambda([this]{BeginRockDrag();}).OnControllerCaptureEnd_Lambda([this]{EndRockDrag();})
                    .OnValueChanged_Lambda([this](float V){PendingRock=V;if(bRockDragging)PumpRock(false);else{auto L=LayeredRecipe().Rock;L.ApplyProgression(V);CommitRock(L);}})]
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[CharacterReadout(TAttribute<double>::CreateLambda([this]{return bRockDragging?PendingRock:LayeredRecipe().Rock.Character;}))],SAssignNew(RockVariation,SButton).ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("HoverHintOnly"))
                    .OnClicked_Lambda([this]{auto L=LayeredRecipe().Rock;const int32 Old=L.Seed;do{L.Seed=int32(FGuid::NewGuid().A);}while(L.Seed==Old);CommitRock(L);return FReply::Handled();})[SNew(SImage).Image(FAppStyle::GetBrush("Icons.Refresh"))]),true);
            R.InitiallyCollapsed(true);
            BuildRockAdvanced(R);R.SetShowAdvanced(false);
            auto& W=B.EditCategory(TEXT("Watershed"),FText::FromString(TEXT("Watershed")));W.SetSortOrder(3);BuildWatershedInspector(W);
            auto& V=B.EditCategory(TEXT("Caves"),FText::FromString(TEXT("Caves")));V.SetSortOrder(4);BuildCaveInspector(V);


        }
        else if(Page==2)
        {
            auto& C=B.EditCategory(TEXT("TerrainComposition"),FText::FromString(TEXT("Landforms")));C.SetSortOrder(0);auto& S=C;
            InspectorRow(S,TEXT("Add"),SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Uplift"))).OnClicked_Lambda([this]{AddFeature(false);return FReply::Handled();})]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Basin"))).OnClicked_Lambda([this]{AddFeature(true);return FReply::Handled();})]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Canyon"))).OnClicked_Lambda([this]{AddFeature(true,true);return FReply::Handled();})],true);
            Add(S,TEXT("BaseStructure"),true);S.SetShowAdvanced(false);
            InspectorRow(C,TEXT("Base terrain"),SNew(SComboButton).OnGetMenuContent(this,&FCLRecipeToolkit::TerrainMenu)
                .ButtonContent()[SNew(STextBlock).Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont")).Text_Lambda([this]{const FString& Name=RecipeAsset()->Recipe.TerrainTypeName;return FText::FromString(Name.IsEmpty()?TEXT("None"):Name);})],true);
            InspectorRow(C,TEXT("Base definition"),SNew(SButton).Text(FText::FromString(TEXT("Edit"))).IsEnabled_Lambda([this]{return TerrainSource.IsValid();})
                .OnClicked_Lambda([this]{if(TerrainSource)CLOpenTerrainDesigner(TerrainSource.Get(),&RecipeAsset()->Recipe.RadialGuides);return FReply::Handled();}),true);
            InspectorRow(C,TEXT("Add Landform"),SNew(SComboButton).OnGetMenuContent_Lambda([this]{return TerrainSourceMenu(FGuid(),true);})
                .ButtonContent()[InspectorLabel(TEXT("Landform"))],true);
            InspectorRow(C,TEXT("Selection"),SNew(SComboButton).OnGetMenuContent(this,&FCLRecipeToolkit::GlobeSelectionMenu)
                .ButtonContent()[SNew(STextBlock).Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont")).Text_Lambda([this]{return FText::FromString(Globe?Globe->SelectionName():TEXT("None"));})],true);
            auto Modes=SNew(SHorizontalBox);
            for(auto Mode:{ECLGlobePlacementMode::Move,ECLGlobePlacementMode::Rotate,ECLGlobePlacementMode::Resize})
                Modes->AddSlot().AutoWidth()[SNew(SButton).Text(FText::FromString(Mode==ECLGlobePlacementMode::Move?TEXT("Move"):Mode==ECLGlobePlacementMode::Rotate?TEXT("Rotate"):TEXT("Scale")))
                    .OnClicked_Lambda([this,Mode]{if(Globe)Globe->SetPlacementMode(Mode);return FReply::Handled();})];
            InspectorRow(C,TEXT("Transform"),Modes,true);
            InspectorRow(C,TEXT("Source"),SNew(SHorizontalBox).IsEnabled_Lambda([this]{return Globe&&!Globe->Selection().bStructure&&RecipeAsset()->LandformSources.Contains(Globe->Selection().Id);})
                +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Edit"))).OnClicked_Lambda([this]{if(auto* Source=RecipeAsset()->LandformSources.Find(Globe->Selection().Id))CLOpenTerrainDesigner(Source->Get(),&RecipeAsset()->Recipe.RadialGuides);return FReply::Handled();})]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Refresh"))).OnClicked_Lambda([this]{const FScopedTransaction Tx(FText::FromString(TEXT("Refresh Landform source")));InspectorError.Reset();FCLPlanetCompiler::WithAuthoringValidation([&]{return RecipeAsset()->RefreshLandformSource(Globe->Selection().Id,InspectorError);});return FReply::Handled();})],true);
            InspectorRow(C,TEXT("Landform"),SNew(SButton).Text(FText::FromString(TEXT("Remove")))
                .IsEnabled_Lambda([this]{return Globe&&Globe->Selection().IsValid()&&!Globe->Selection().bStructure;})
                .OnClicked_Lambda([this]{const auto Id=Globe->Selection().Id;const FScopedTransaction Tx(FText::FromString(TEXT("Remove Landform")));auto* A=RecipeAsset();A->Modify();A->Recipe.TerrainRegions.RemoveAll([&](const auto& R){return R.Id==Id;});A->LandformSources.Remove(Id);Globe->Select({});A->MarkPackageDirty();A->PostEditChange();return FReply::Handled();}),true);
            InspectorRow(C,TEXT("Structure"),SNew(SComboButton).IsEnabled_Lambda([this]{return Globe&&Globe->Selection().IsValid()&&!Globe->Selection().bStructure;})
                .OnGetMenuContent_Lambda([this]{return StructureMenu(Globe->Selection().Id);})
                .ButtonContent()[SNew(STextBlock).Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont")).Text_Lambda([this]{if(Globe){const auto* R=RecipeAsset()->Recipe.TerrainRegions.FindByPredicate([&](const auto& V){return V.Id==Globe->Selection().Id;});if(R){const auto* F=RecipeAsset()->Recipe.BaseStructure.FindByPredicate([&](const auto& V){return V.Id==R->Placement.StructureFeatureId;});if(F)return FText::FromString(F->Name);}}return FText::FromString(TEXT("Planet frame"));})],true);
            RegionProperties=Property(TEXT("TerrainRegions"));C.AddProperty(RegionProperties,EPropertyLocation::Advanced);Add(C,TEXT("NoiseLayers"),true);C.SetShowAdvanced(false);
            auto& G=C;
            const auto Settings=Property(TEXT("RegionGeneration"));G.AddProperty(Settings->GetChildHandle(TEXT("bEnabled")),EPropertyLocation::Advanced);

            G.AddProperty(Settings->GetChildHandle(TEXT("ReliefStrength")),EPropertyLocation::Advanced);
            for(const TCHAR* Name:{TEXT("RegionCount"),TEXT("Seed"),TEXT("StructureGuidance"),TEXT("BlendFraction"),TEXT("Overrides")})G.AddProperty(Settings->GetChildHandle(FName(Name)),EPropertyLocation::Advanced);
            InspectorRow(G,TEXT("Region"),SNew(SComboButton).OnGetMenuContent(GenerationPanel.ToSharedRef(),&SCLRegionGenerationPanel::RegionMenu)
                .ButtonContent()[SNew(STextBlock).Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont")).Text_Lambda([this]{return GenerationPanel->SelectionText();})],true);
            InspectorRow(G,TEXT("Assignment"),SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1)[SNew(SComboButton).OnGetMenuContent(GenerationPanel.ToSharedRef(),&SCLRegionGenerationPanel::OverrideMenu).ButtonContent()[InspectorLabel(TEXT("Landform"))]]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Clear"))).OnClicked_Lambda([this]{GenerationPanel->ClearOverrides();return FReply::Handled();})],true);G.SetShowAdvanced(false);
            if(RecipeAsset()->Recipe.SchemaVersion>=18)
            {Add(C,TEXT("Volumes"),true);}
        }
        else if(Page==3)
        {auto& C=B.EditCategory(TEXT("GroundMaterials"),FText::FromString(TEXT("Surface")));Add(C,TEXT("SurfaceStack"));}
        else
        {
            auto& C=B.EditCategory(TEXT("Presentation"),FText::FromString(TEXT("View")));C.SetSortOrder(0);
            InspectorRow(C,TEXT("View"),SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).Text_Lambda([this]{return FText::FromString(bGlobe?TEXT("Globe"):TEXT("Cross-section"));})
                    .OnClicked_Lambda([this]{bGlobe=!bGlobe;Refresh();return FReply::Handled();})]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Frame"))).OnClicked_Lambda([this]{if(Globe&&bGlobe)Globe->Frame();return FReply::Handled();})]);
            InspectorRow(C,TEXT("Radial Guides"),SNew(SCheckBox).IsChecked_Lambda([this]{return Preview&&Preview->Client->bGuides?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState V){if(Preview){Preview->Client->bGuides=V==ECheckBoxState::Checked;Preview->Client->Invalidate();if(Globe)Globe->SetGuides(RecipeAsset()->Recipe.RadialGuides,Preview->Client->bGuides);}}));
            InspectorRow(C,TEXT("Ocean"),SNew(SCheckBox).IsChecked_Lambda([this]{return Globe&&Globe->IsOceanVisible()?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState V){if(Globe)Globe->SetOceanVisible(V==ECheckBoxState::Checked);}));
            InspectorRow(C,TEXT("Globe grid"),SNew(SCheckBox).IsChecked_Lambda([this]{return Globe&&Globe->IsGlobeGridVisible()?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState V){if(Globe)Globe->SetGlobeGrid(V==ECheckBoxState::Checked);}));
            InspectorRow(C,TEXT("Channel"),SNew(SButton).Text(FText::FromString(TEXT("Focus largest channel"))).IsEnabled_Lambda([this]{return AcceptedSurfacePlan&&AcceptedSurfacePlan->Watershed&&!AcceptedSurfacePlan->Watershed->GetChannels().IsEmpty();}).OnClicked_Lambda([this]{if(!bGlobe){bGlobe=true;Refresh();}if(Globe)Globe->FocusWatershed();return FReply::Handled();}));
            InspectorRow(C,TEXT("Section area"),SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1)[SNew(SSlider).Value_Lambda([this]{return float((PendingAreaWidth-100)/900);})
                    .OnMouseCaptureBegin_Lambda([this]{bAreaDragging=true;}).OnMouseCaptureEnd_Lambda([this]{bAreaDragging=false;ApplyPreviewArea();})
                    .OnValueChanged_Lambda([this](float V){PendingAreaWidth=FMath::RoundToDouble((100+900*V)/10)*10;if(!bAreaDragging)ApplyPreviewArea();})]
                +SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont")).Text(this,&FCLRecipeToolkit::AreaCaption)]);
            InspectorRow(C,TEXT("Area below height"),SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[SNew(SCLSurfaceDistributionPlot).Profile_Lambda([this]{return ElevationSettings();}).Measured_Lambda([this]{return MeasuredElevation;})]
                +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("Min → Max height · blue target · orange measured"))).AutoWrapText(true)]
                +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(ElevationReadout);}).AutoWrapText(true)],true);
            C.AddCustomRow(FText::FromString(TEXT("Bed thickness distribution")),true).WholeRowContent()
            [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]{return FText::FromString(MeasuredRockBeds.Count?FString::Printf(TEXT("%.1f–%.1f m · %d older beds · mean %.2f m\nBlue: requested bed count · orange: measured count · green: depth share. Excludes terminal partial bed; before erosion/onlap."),MeasuredRockBeds.MinimumMeters,MeasuredRockBeds.MaximumMeters,MeasuredRockBeds.Count,MeasuredRockBeds.TotalMeters/MeasuredRockBeds.Count):TEXT("Move Character to adopt physical beds. Saved profiles retain their original thickness."));})]
                +SVerticalBox::Slot().AutoHeight()[SNew(SCLRockThicknessPlot).Measured_Lambda([this]{return MeasuredRockBeds;}).Target_Lambda([this]{return TargetRockBeds;})]];
            C.SetShowAdvanced(false);
            auto& S=B.EditCategory(TEXT("Walking"),FText::FromString(TEXT("Surface inspection")));S.SetSortOrder(1);
            InspectorRow(S,TEXT("Pick location"),SNew(SCheckBox).IsChecked_Lambda([this]{return Globe&&Globe->IsWalkPicking()?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState V){if(!bGlobe&&!RecipeAsset()->Recipe.Volumes.bEnabled){bGlobe=true;Refresh();}if(Globe)Globe->SetWalkPicking(V==ECheckBoxState::Checked);if(Preview)Preview->bWalkPicking=V==ECheckBoxState::Checked;}));
            InspectorRow(S,TEXT("Walk"),SNew(SButton).Text(FText::FromString(TEXT("Walk here")))
                .IsEnabled_Lambda([this]{return !EvaluationUpdating()&&!bPreviewInvalid&&(bGlobe||RecipeAsset()->Recipe.Volumes.bEnabled)&&Globe&&!Globe->WalkLocation().IsNearlyZero()&&FCLPlanetInspection::Launch.IsBound();})
                .OnClicked_Lambda([this]{WalkHere();return FReply::Handled();}));
        }
    }
    TSharedRef<SDockTab> SpawnPlanet(const FSpawnTabArgs&)
    {
        GenerationPanel=SNew(SCLRegionGenerationPanel).Asset(RecipeAsset()).MenusOnly(true)
            .OnSelectRegion(FOnCLGeneratedRegionSelection::CreateLambda([this](FGuid Id){if(Globe)Globe->SelectGeneratedRegion(Id);}));
        const auto Tabs=SNew(SHorizontalBox);SAssignNew(InspectorPages,SWidgetSwitcher).WidgetIndex_Lambda([this]{return InspectorPage;});
        const TCHAR* Names[]={TEXT("Planet"),TEXT("Geology"),TEXT("Landforms"),TEXT("Surface"),TEXT("Preview")};
        const TWeakPtr<FCLRecipeToolkit> Weak=StaticCastSharedRef<FCLRecipeToolkit>(AsShared());
        for(int32 Page=0;Page<5;++Page)
        {
            Tabs->AddSlot().FillWidth(1)[SNew(SCheckBox).Style(FAppStyle::Get(),"DetailsView.SectionButton")
                .IsChecked_Lambda([this,Page]{return InspectorPage==Page?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this,Page](ECheckBoxState V){if(V==ECheckBoxState::Checked)InspectorPage=Page;})[InspectorLabel(Names[Page])]];
            FDetailsViewArgs Args;Args.bUpdatesFromSelection=false;Args.bHideSelectionTip=true;Args.NameAreaSettings=FDetailsViewArgs::HideNameArea;Args.ViewIdentifier=FName(*FString::Printf(TEXT("CLPlanetInspector%d"),Page));
            const auto View=FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(Args);
            View->RegisterInstancedCustomPropertyLayout(UCLPlanetRecipeAsset::StaticClass(),FOnGetDetailCustomizationInstance::CreateLambda([Weak,Page]
            {return MakeShared<FCLPlanetDetailsLayout>([Weak,Page](IDetailLayoutBuilder& B){if(auto E=Weak.Pin())E->BuildPlanetInspector(B,Page);});}));
            if(Page==0||Page==1)View->OnFinishedChangingProperties().AddSP(this,&FCLRecipeToolkit::FinishGeologyAdvanced);
            View->SetObject(RecipeAsset());PlanetInspectors.Add(View);
            if(Page==2)InspectorPages->AddSlot()[SNew(SVerticalBox)
                +SVerticalBox::Slot().FillHeight(1)[SAssignNew(LandformRows,SCLPlanetLandforms).Session(Landforms)]
                +SVerticalBox::Slot().AutoHeight()[SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[InspectorLabel(TEXT("Advanced"))].BodyContent()[SNew(SBox).MaxDesiredHeight(340)[View]]]];
            else InspectorPages->AddSlot()[View];
        }
        return SNew(SDockTab).Label(FText::FromString(TEXT("Planet")))[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(4)[Tabs]
            +SVerticalBox::Slot().FillHeight(1)[InspectorPages.ToSharedRef()]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont")).AutoWrapText(true).ColorAndOpacity(FLinearColor(1,.35,.2))
                .Visibility_Lambda([this]{return bPreviewInvalid||!InspectorError.IsEmpty()?EVisibility::Visible:EVisibility::Collapsed;})
                .Text_Lambda([this]{return FText::FromString(bPreviewInvalid?Status:InspectorError);})]];
    }
    TSharedRef<SDockTab> SpawnSection(const FSpawnTabArgs&)
    {
        if (!TerrainEditing) return SNew(SDockTab)[BuildViewport()];
        auto Tab=SNew(SDockTab)[SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] { return FText::FromString(Status); })]
            + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()[SNew(SComboButton).OnGetMenuContent(this, &FCLRecipeToolkit::TerrainMenu)
                    .ToolTipText(FText::FromString(TerrainEditing ? TEXT("Open a reusable terrain definition using the current preview guides") : TEXT("Apply a terrain type's layers using this planet's Radial Guides, radius and seed")))
                    .ButtonContent()[SNew(STextBlock).Text_Lambda([this]
                    { const FString& Name = TerrainEditing ? TerrainEditing->Asset->TerrainType.Name : RecipeAsset()->Recipe.TerrainTypeName;
                      return FText::FromString((TerrainEditing?TEXT("Terrain: "):TEXT("Base terrain: ")) + (Name.IsEmpty() ? TEXT("Neutral sphere") : Name)); })]]
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
                + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Duplicate guide set")))
                    .Visibility_Lambda([this] { return TerrainEditing ? EVisibility::Collapsed : EVisibility::Visible; })
                    .OnClicked_Lambda([this] { RadialGuidesSession->NewSet(); return FReply::Handled(); })]
                + SHorizontalBox::Slot().AutoWidth().Padding(8, 0)[SNew(SButton).Text(FText::FromString(TEXT("Remove guide set"))).ToolTipText(FText::FromString(TEXT("Remove this set from the shared catalog; other planets referencing it will report a missing set until reassigned")))
                    .Visibility_Lambda([this] { return TerrainEditing ? EVisibility::Collapsed : EVisibility::Visible; })
                    .OnClicked_Lambda([this] { RadialGuidesSession->RemoveSet(); return FReply::Handled(); })]]
            + SVerticalBox::Slot().AutoHeight().Padding(8, 0)[SNew(STextBlock).AutoWrapText(true)
                .Visibility_Lambda([this] { return TerrainEditing ? EVisibility::Visible : EVisibility::Collapsed; })
                .Text(FText::FromString(TEXT("Preview guides only. Guide values are supplied when this terrain is assembled onto a planet."))) ]
            + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()[SNew(SCheckBox).IsChecked_Lambda([this] { return !Preview || Preview->Client->bGuides ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }).OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                    { Preview->Client->bGuides = State == ECheckBoxState::Checked; Preview->Client->Invalidate();if(Globe)Globe->SetGuides(RecipeAsset()->Recipe.RadialGuides,Preview->Client->bGuides); })[SNew(STextBlock).Text(FText::FromString(TEXT("Radial Guides")))]]]
            + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
                .Visibility_Lambda([this]{return UsesGlobePreview()?EVisibility::Collapsed:EVisibility::Visible;})
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
            + SVerticalBox::Slot().FillHeight(1)[BuildViewport()]
            + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SBox).HeightOverride(42)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]
                {
                    FString Legend = RadialGuidesSession->Gesture ? TEXT("Release to update terrain | Esc to cancel") :
                        TEXT("Left drag: orbit | Middle drag: pan cropped view | Right drag or wheel: zoom");
                    if(UsesGlobePreview()&&!RadialGuidesSession->Gesture)Legend=Globe->PlacementHint()+TEXT(" | Middle drag: pan | Right drag / wheel: zoom | Drag guides: radius");
                    else if (!TerrainEditing) Legend += TEXT(" | + beside Core: add Radial Guide");
                    if (AuthoringUpdating()) Legend = TEXT("Updating terrain preview… | ") + Legend;
                    if (Preview && Preview->Client->Plan && Preview->Client->Plan->Coordinates == ECLTerrainCoordinates::Sphere && Preview->Client->AreaWidthMeters / FMath::Sqrt(2.0) >=
                        UE_PI * FCLSectionGeometry::ReferenceSurfaceRadius(*Preview->Client->Plan))
                        Legend += TEXT(" | Area passes the far side of this small planet: terrain repeats");
                    return FText::FromString(Legend);
                })]]];
        return Tab;
    }
};
const FName FCLRecipeToolkit::SectionTab(TEXT("CLRecipeSection"));
TWeakPtr<FCLRecipeToolkit> FCLRecipeToolkit::ActivePlanet;
const FName FCLRecipeToolkit::NoiseTab(TEXT("CLNoiseLayers"));
const FName FCLRecipeToolkit::SurfaceTab(TEXT("CLSurfaceLayers"));
const FName FCLRecipeToolkit::PlanetTab(TEXT("CLPlanetControls"));

void OpenPlanetRecipe()
{
    if (!GEditor) return;
    const auto* Planet = GEditor->GetSelectedActors()->GetTop<ACLPlanet>();
    auto* Asset = Planet && Planet->RecipeAsset ? Planet->RecipeAsset.Get() :
        nullptr;
    if(!Asset)Asset=Cast<UCLPlanetRecipeAsset>(FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateAssetWithDialog(UCLPlanetRecipeAsset::StaticClass(),NewObject<UCLPlanetRecipeFactory>()));
    if (Asset) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
}
void OpenLandformDesigner()
{
    if(auto Editor=FCLRecipeToolkit::ActivePlanet.Pin()){Editor->ShowLandforms();return;}
    OpenPlanetRecipe();if(auto Editor=FCLRecipeToolkit::ActivePlanet.Pin())Editor->ShowLandforms();
}
class FCLDesignerModule : public IModuleInterface
{
    TUniquePtr<FAutoConsoleCommand> Command;
    TUniquePtr<FAutoConsoleCommand> TerrainCommand;
    TUniquePtr<FAutoConsoleCommand> PlanetCommand;
    TUniquePtr<FAutoConsoleCommand> BuilderCommand;
    FDelegateHandle MenuHandle;
public:
    virtual void StartupModule() override
    {
        FCLRecipeEvaluation::Startup();
        CLRegisterGlobePlacementMode();
        Command = MakeUnique<FAutoConsoleCommand>(TEXT("Landform.Designer"), TEXT("Open the planet Landforms tab"), FConsoleCommandDelegate::CreateStatic(&OpenLandformDesigner));
        PlanetCommand = MakeUnique<FAutoConsoleCommand>(TEXT("Landform.PlanetRecipe"), TEXT("Open selected planet recipe or rolling-plains recipe"), FConsoleCommandDelegate::CreateStatic(&OpenPlanetRecipe));
        BuilderCommand = MakeUnique<FAutoConsoleCommand>(TEXT("Landform.PlanetBuilder"),TEXT("Open the planet builder"),FConsoleCommandDelegate::CreateStatic(&OpenPlanetRecipe));
        TerrainCommand = MakeUnique<FAutoConsoleCommand>(TEXT("Landform.TerrainDesigner"),TEXT("Legacy alias for the planet Landforms tab"),FConsoleCommandDelegate::CreateStatic(&OpenLandformDesigner));
        MenuHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]
        {
            FToolMenuOwnerScoped Owner(TEXT("CatalystLandformDesigner"));
            auto* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
            Menu->AddSection(TEXT("CatalystLandform"), FText::FromString(TEXT("Catalyst")));
            Menu->FindOrAddSection(TEXT("CatalystLandform")).AddMenuEntry(TEXT("LandformPlanetRecipe"),
                FText::FromString(TEXT("Planet")), FText::FromString(TEXT("Edit the assembled planet")),
                FSlateIcon(), FUIAction(FExecuteAction::CreateStatic(&OpenPlanetRecipe)));
        }));
    }
    virtual void ShutdownModule() override
    { FCLRecipeEvaluation::Shutdown(); CLUnregisterGlobePlacementMode(); Command.Reset(); TerrainCommand.Reset(); PlanetCommand.Reset(); BuilderCommand.Reset(); UToolMenus::UnRegisterStartupCallback(MenuHandle); UToolMenus::UnregisterOwner(TEXT("CatalystLandformDesigner")); }
};
}

void CLOpenTerrainDesigner(UCLTerrainTypeAsset* Terrain, const FCLRadialGuideSet* PreviewRadialGuides, EToolkitMode::Type Mode, TSharedPtr<IToolkitHost> Host)
{
    if(!GEditor||!Terrain)return;
    if(!FCLRecipeToolkit::ActivePlanet.IsValid())OpenPlanetRecipe();
    if(auto Editor=FCLRecipeToolkit::ActivePlanet.Pin())Editor->ShowLandforms(Terrain);
}

UCLPlanetRecipeFactory::UCLPlanetRecipeFactory()
{ SupportedClass = UCLPlanetRecipeAsset::StaticClass(); bCreateNew = true; bEditAfterNew = true; }
UObject* UCLPlanetRecipeFactory::FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{ auto* A=NewObject<UCLPlanetRecipeAsset>(Parent, Class, Name, Flags);A->Recipe=FCLPlanetCompiler::BasePlanet();A->Recipe.Caves.AlgorithmVersion=7;A->Recipe.SurfaceStack=FCLSurfaceStack::MakeDefault();
    FCLRadialGuideSession Session;Session.Open(A);FString Error;if(!A->SetRadialGuideSet(Session.Library.Get(),A->Recipe.RadialGuides.Id,Error))Session.Changed();return A; }
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
    const FBox InitialBounds = Preview->NativePreview->DisplayBounds();
    FVector PreviousCorner; bool bPrevious = false;
    for (double Width : {100.0, 500.0, 1000.0})
    {
        Preview->Client->AreaWidthMeters = Width;
        Preview->Rebuild(Plan);
        TestTrue(TEXT("Coverage preserves the user's camera position"), Preview->Client->GetViewLocation().Equals(Eye, .001));
        TestTrue(TEXT("Coverage preserves the user's camera rotation"), Preview->Client->GetViewRotation().Equals(Rotation, .001));
        TestTrue(TEXT("Coverage preserves the orbit target"), Preview->Client->GetLookAtLocation().Equals(Target, .001));
        const FBox Bounds = Preview->NativePreview->DisplayBounds();
        TestTrue(TEXT("Coverage leaves the native stamp display domain unchanged"), Bounds.Min.Equals(InitialBounds.Min, .01) && Bounds.Max.Equals(InitialBounds.Max, .01));
        const FVector Corner = FCLSectionGeometry::SourceSurfacePoint(*Plan, Width / 2, Width / 2, 0);
        if (bPrevious) TestFalse(TEXT("Larger coverage samples different data at the same displayed corner"), Corner.Equals(PreviousCorner, .01));
        PreviousCorner = Corner; bPrevious = true;
    }
    Recipe.RadiusMeters = 100;
    Preview->Rebuild(FCLPlanetCompiler::Compile(Recipe, Error));
    TestTrue(TEXT("Resizing a planet preserves the display camera"), Preview->Client->GetViewLocation().Equals(Eye, .001));
    const FBox ResizedBounds = Preview->NativePreview->DisplayBounds();
    TestTrue(TEXT("Normalized native stamp bounds are independent of planet radius"), ResizedBounds.Min.Equals(InitialBounds.Min, .01) && ResizedBounds.Max.Equals(InitialBounds.Max, .01));
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

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPlanetGuideFactoryTest,"Catalyst.Landform.PlanetBuilder.NewPlanetReference",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPlanetGuideFactoryTest::RunTest(const FString&)
{
    TStrongObjectPtr<UCLPlanetRecipeFactory> Factory(NewObject<UCLPlanetRecipeFactory>());
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(CastChecked<UCLPlanetRecipeAsset>(Factory->FactoryCreateNew(UCLPlanetRecipeAsset::StaticClass(),GetTransientPackage(),NAME_None,RF_Transient|RF_Transactional,nullptr,nullptr)));
    TestNotNull(TEXT("New planets reference the shared catalog"),A->RadialGuideLibrary.Get());
    FString Error;TestTrue(TEXT("Selected set resolves"),A->SynchronizeRadialGuides(Error));
    TestEqual(TEXT("Resolved set has the referenced identity"),A->Recipe.RadialGuides.Id,A->RadialGuideSetId);
    TestTrue(TEXT("New planets contain no terrain layers or regions"),A->Recipe.NoiseLayers.IsEmpty()&&A->Recipe.TerrainRegions.IsEmpty());
    TestEqual(TEXT("Fresh factory planets explicitly use the current cave family"),A->Recipe.Caves.AlgorithmVersion,7);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGuideTickAttachmentTest,"Catalyst.Landform.PlanetBuilder.GuideTickAttachment",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGuideTickAttachmentTest::RunTest(const FString&)
{
    const auto Session=MakeShared<FCLRadialGuideSession>();Session->Asset=NewObject<UCLPlanetRecipeAsset>();
    const auto Section=SNew(SCLSectionViewport);const auto Globe=SNew(SCLPlanetPreview);
    const auto Overlay=SNew(SCLHeightAnnotations).Client(Section->Client).Session(Session).Globe(Globe).UseGlobe(true);
    TestTrue(TEXT("Globe tick begins exactly at its projected ring anchor"),Overlay->TickStart(0).Equals(Overlay->Anchor(0)));
    Overlay->UseGlobe.Set(false);
    TestTrue(TEXT("Designer cutaway retains one tick-width gap"),Overlay->TickStart(0).Equals(Overlay->Anchor(0)+FVector2D(GuideTickWidth,0)));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
class FCLWaitForGlobeRefinement : public IAutomationLatentCommand
{
    TSharedPtr<SCLPlanetPreview> Globe;
    TFunction<void()> Check;
    double Started=FPlatformTime::Seconds();
public:
    FCLWaitForGlobeRefinement(TSharedPtr<SCLPlanetPreview> InGlobe,TFunction<void()> InCheck):Globe(MoveTemp(InGlobe)),Check(MoveTemp(InCheck)){}
    virtual bool Update() override
    {if(Globe->IsUpdating()&&FPlatformTime::Seconds()-Started<30)return false;Check();return true;}
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTectonicAdvancedTest,"Catalyst.Landform.Tectonics.InlineAdvanced",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLTectonicAdvancedTest::RunTest(const FString&)
{
    const auto Package=CreatePackage(*(TEXT("/Temp/CLAdvanced_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    TStrongObjectPtr<UCLPlanetRecipeAsset> Asset(NewObject<UCLPlanetRecipeAsset>(Package,NAME_None,RF_Transactional));Asset->Recipe=FCLPlanetCompiler::TectonicBase();
    const auto Original=Asset->Recipe;const auto Editor=MakeShared<FCLRecipeToolkit>();Editor->Open(Asset.Get(),EToolkitMode::Standalone,nullptr);Editor->AwaitEvaluationForTesting();
    Editor->Globe->Cancel();const uint64 Builds=Editor->Globe->PublishedBuilds();
    TestNotNull(TEXT("Native Advanced category is available"),Editor->TectonicAdvanced);Editor->PlanetInspectors[1]->SetRootExpansionStates(true,false);
    TestTrue(TEXT("Revealing Advanced keeps the Planet tab foreground"),Editor->GetTabManager()->FindExistingLiveTab(FCLRecipeToolkit::PlanetTab)->IsForeground());
    TestTrue(TEXT("Revealing controls preserves the recipe"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Original,&Asset->Recipe,0));
    TestTrue(TEXT("The one-knob control remains in the same editor"),Editor->TectonicSlider.IsValid());
    FString Visible;
    for(const auto& Handle:Editor->TectonicAdvancedProperties)Visible+=Handle->GetProperty()->GetName()+TEXT(";");
    TestFalse(TEXT("Advanced contains no override checkboxes"),Visible.Contains(TEXT("bOverride")));
    TestTrue(TEXT("Advanced shows the driven shape values"),Visible.Contains(TEXT("PlateCount"))&&Visible.Contains(TEXT("Motion"))&&Visible.Contains(TEXT("BeltWidthDegrees"))&&Visible.Contains(TEXT("Roughness")));
    auto* Property=FCLTectonicPreset::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FCLTectonicPreset,PlateCount));
    auto& Draft=*reinterpret_cast<FCLTectonicPreset*>(Editor->TectonicDraft->GetStructMemory());Draft.PlateCount=4;
    FPropertyChangedEvent Interactive(Property,EPropertyChangeType::Interactive);Editor->PlanetInspectors[1]->OnFinishedChangingProperties().Broadcast(Interactive);
    TestEqual(TEXT("Interactive advanced edits stay in their draft"),Asset->Recipe.Tectonics.PlateCount,Original.Tectonics.PlateCount);
    FPropertyChangedEvent Finished(Property,EPropertyChangeType::ValueSet);
    const auto PlateHandle=*Editor->TectonicAdvancedProperties.FindByPredicate([](const auto& H){return H->GetProperty()->GetFName()==TEXT("PlateCount");});
    PlateHandle->NotifyFinishedChangingProperties();
    FString Error;FCLTectonicControls Controls;FCLTectonicControls::Resolve(Asset->Recipe.Tectonics,Controls,Error);
    TestEqual(TEXT("Direct advanced edit reaches the planet compiler"),Controls.LowerPlateCount,4);TestEqual(TEXT("Advanced edit leaves character unchanged"),Asset->Recipe.Tectonics.Activity,Original.Tectonics.Activity);
    GEditor->UndoTransaction();TestTrue(TEXT("One Undo restores the advanced edit"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Original,&Asset->Recipe,0));
    TestEqual(TEXT("Inline draft follows Undo"),Draft.PlateCount,Editor->LayeredRecipe().Tectonics.PlateCount);GEditor->RedoTransaction();TestEqual(TEXT("Redo restores the advanced edit"),Asset->Recipe.Tectonics.PlateCount,4);
    Draft.PlateCount=99;Editor->PlanetInspectors[1]->OnFinishedChangingProperties().Broadcast(Finished);TestEqual(TEXT("Invalid advanced edits preserve the accepted recipe"),Asset->Recipe.Tectonics.PlateCount,4);TestEqual(TEXT("Invalid draft returns to the accepted value"),Draft.PlateCount,4);
    Editor->PlanetInspectors[1]->SetRootExpansionStates(false,false);TestEqual(TEXT("Reveal alone publishes no geometry"),Editor->Globe->PublishedBuilds(),Builds);
    Asset->GetOutermost()->SetDirtyFlag(false);TestTrue(TEXT("Inline editor retires"),Editor->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed));return true;
}
// Exercise the installed host asset through its real asset-definition entry point.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLInstalledPlanetEditorTest,"Catalyst.Landform.Planet.InstalledBasePlanet",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLInstalledPlanetEditorTest::RunTest(const FString&)
{
    auto* Asset=LoadObject<UCLPlanetRecipeAsset>(nullptr,TEXT("/Game/LandformDesigner/DA_BasePlanet"));
    if(!TestNotNull(TEXT("The host's BasePlanet exists"),Asset))return false;
    const auto Before=Asset->Recipe;const bool WasDirty=Asset->GetOutermost()->IsDirty();
    auto* Editors=GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    TestTrue(TEXT("The real asset definition opens BasePlanet"),Editors->OpenEditorForAsset(Asset));
    auto E=FCLRecipeToolkit::ActivePlanet.Pin();if(!TestTrue(TEXT("BasePlanet opens in Planet"),E&&E->RecipeAsset()==Asset))return false;
    TestEqual(TEXT("All five workflow pages are present"),E->PlanetInspectors.Num(),5);
    TestTrue(TEXT("All five geology controls are available even on an older asset"),E->ContinentalSlider&&E->TectonicSlider&&E->RockSlider&&E->WatershedSlider&&E->CaveSlider);
    TestTrue(TEXT("The province catalogue is available"),E->LandformRows&&E->LandformRows->Controls.Num()>=17);
    for(const TCHAR* Tab:{TEXT("CLNoiseLayers"),TEXT("CLSurfaceLayers")})TestFalse(TEXT("No standalone landform editing tabs are registered"),E->GetTabManager()->HasTabSpawner(FName(Tab)));
    auto* Source=LoadObject<UCLTerrainTypeAsset>(nullptr,TEXT("/CatalystLandform/TerrainTypes/Provinces/DA_Province_ContinentalRift"));
    if(TestNotNull(TEXT("Installed Rift source is present"),Source))
    {
        Editors->OpenEditorForAsset(Source);
        TestTrue(TEXT("Opening a landform focuses the same Planet instance"),E==FCLRecipeToolkit::ActivePlanet.Pin()&&E->InspectorPage==2);
        TestTrue(TEXT("Landforms have no separate asset editor"),Editors->FindEditorForAsset(Source,false)==nullptr);
    }
    TestTrue(TEXT("Opening the current editor preserves the authored recipe"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Before,&Asset->Recipe,0));
    TestEqual(TEXT("Opening does not change the dirty state"),Asset->GetOutermost()->IsDirty(),WasDirty);
    E->InspectorPage=1;
    if(!FParse::Param(FCommandLine::Get(),TEXT("LandformKeepBasePlanet")))E->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPlanetInspectorTest,"Catalyst.Landform.PlanetLayers.InspectorAndVariationUndo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPlanetInspectorTest::RunTest(const FString&)
{
    const auto Package=CreatePackage(*(TEXT("/Temp/CLInspector_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    TStrongObjectPtr<UCLPlanetRecipeAsset> Asset(NewObject<UCLPlanetRecipeAsset>(Package,NAME_None,RF_Transactional));Asset->Recipe=FCLPlanetCompiler::ContinentalBase();
    const auto Original=Asset->Recipe;const auto Editor=MakeShared<FCLRecipeToolkit>();Editor->Open(Asset.Get(),EToolkitMode::Standalone,nullptr);Editor->AwaitEvaluationForTesting();
    TestTrue(TEXT("Opening the new inspector preserves the saved recipe"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Original,&Asset->Recipe,0));
    TestEqual(TEXT("Workflow has five native Details views"),Editor->PlanetInspectors.Num(),5);
    TestTrue(TEXT("Both layer controls coexist in Geology"),Editor->ContinentalSlider.IsValid()&&Editor->TectonicSlider.IsValid()&&Editor->InspectorPage==1);
    Editor->ContinentalVariation->SimulateClick();const auto CSeed=Asset->Recipe.Continents.Seed;
    TestTrue(TEXT("Continental variation converts and preserves both layers"),Asset->Recipe.SchemaVersion==17&&Asset->Recipe.Continents.bEnabled&&Asset->Recipe.Tectonics.bEnabled&&CSeed!=0);
    TestEqual(TEXT("Continental variation preserves tectonic seed"),Asset->Recipe.Tectonics.Seed,Original.Tectonics.Seed);
    TestEqual(TEXT("Variation preserves the knob setting"),Asset->Recipe.Tectonics.Activity,Original.Tectonics.Activity);
    const auto AfterContinental=Asset->Recipe;
    const auto& CD=*reinterpret_cast<const FCLContinentalLayer*>(Editor->ContinentalDraft->GetStructMemory());
    TestEqual(TEXT("Advanced shows resolved continental relief"),CD.Relief,FCLContinentalControls::Resolve(Asset->Recipe.Continents).Relief);
    auto T=Asset->Recipe.Tectonics;T.Activity=.9;T.bOverrideMotion=false;T.bOverrideRelief=false;Editor->CommitTectonicLayer(T);
    const auto& TD=*reinterpret_cast<const FCLTectonicPreset*>(Editor->TectonicDraft->GetStructMemory());FCLTectonicControls Controls;FString ResolveError;FCLTectonicControls::Resolve(T,Controls,ResolveError,true);
    TestEqual(TEXT("Advanced follows motion driven by knob"),TD.Motion,Controls.Motion);TestEqual(TEXT("Advanced follows relief driven by knob"),TD.Relief,Controls.Relief);
    GEditor->UndoTransaction();
    Editor->TectonicVariation->SimulateClick();
    TestTrue(TEXT("Tectonic variation changes its own seed"),Asset->Recipe.Tectonics.Seed!=AfterContinental.Tectonics.Seed);
    TestEqual(TEXT("Tectonic variation preserves the continental seed"),Asset->Recipe.Continents.Seed,CSeed);
    GEditor->UndoTransaction();TestTrue(TEXT("One Undo restores one variation"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&AfterContinental,&Asset->Recipe,0));
    GEditor->UndoTransaction();TestTrue(TEXT("Undo restores the exact pinned recipe before conversion"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Original,&Asset->Recipe,0));
    GEditor->RedoTransaction();TestTrue(TEXT("Redo restores the same deterministic variation"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&AfterContinental,&Asset->Recipe,0));
    auto Invalid=Asset->Recipe.Continents;Invalid.Strength=2;
    TestFalse(TEXT("Invalid layer data is refused"),Editor->CommitContinents(Invalid));
    TestTrue(TEXT("Invalid edits preserve the accepted recipe and expose an error"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&AfterContinental,&Asset->Recipe,0)&&!Editor->InspectorError.IsEmpty());
    Asset->GetOutermost()->SetDirtyFlag(false);Editor->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);return true;
}
struct FCLAutomationSlateInputScope
{
    bool Previous=FSlateApplication::Get().GetHandleDeviceInputWhenApplicationNotActive();
    // Automation may run behind another app; Slate otherwise declines the capture reply.
    FCLAutomationSlateInputScope(){FSlateApplication::Get().SetHandleDeviceInputWhenApplicationNotActive(true);}
    ~FCLAutomationSlateInputScope(){FSlateApplication::Get().SetHandleDeviceInputWhenApplicationNotActive(Previous);}
};
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FCLTectonicLiveKnobTest,"Catalyst.Landform.Tectonics.LiveKnobAndUndo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FCLTectonicLiveKnobTest::GetTests(TArray<FString>& Names,TArray<FString>& Commands) const
{for(const TCHAR* Name:{TEXT("Tectonic"),TEXT("Continental"),TEXT("ContinentalLayer")}){Names.Add(Name);Commands.Add(Name);}}
bool FCLTectonicLiveKnobTest::RunTest(const FString& Parameters)
{
    auto* Package=CreatePackage(*(TEXT("/Temp/CLLiveTectonics_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto Owned=MakeShared<TStrongObjectPtr<UCLPlanetRecipeAsset>>(NewObject<UCLPlanetRecipeAsset>(Package,NAME_None,RF_Transactional));auto* Asset=Owned->Get();
    FString Json,Error;const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"));
    if(!Plugin||!FFileHelper::LoadFileToString(Json,*(Plugin->GetBaseDir()/TEXT("Examples")/(Parameters!=TEXT("Tectonic")?TEXT("ContinentalBase.json"):TEXT("TectonicBase.json"))))||!Asset->ImportRecipeJson(Json,Error)){AddError(TEXT("Cannot load owned slider recipe: ")+Error);return false;}
    const bool Crust=Parameters==TEXT("ContinentalLayer");if(Crust){Asset->Recipe.SeparateGeologyLayers();Asset->Recipe.Continents.Strength=.55;}Asset->Recipe.Tectonics.Activity=.55;
    auto Editor=MakeShared<FCLRecipeToolkit>();Editor->Open(Asset,EToolkitMode::Standalone,nullptr);Editor->AwaitEvaluationForTesting();Editor->GetTabManager()->TryInvokeTab(FName(TEXT("CLPlanetControls")));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this,Editor,Owned,Crust]
    {
        auto& App=FSlateApplication::Get();App.ReleaseAllPointerCapture();FWidgetPath Actual;App.GeneratePathToWidgetUnchecked((Crust?Editor->ContinentalSlider:Editor->TectonicSlider).ToSharedRef(),Actual,EVisibility::All);
        if(Actual.Widgets.Num()==0){AddError(TEXT("Live slider has no routed path"));Editor->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);return;}
        const auto Geometry=Actual.Widgets.Last().Geometry;TArray<FWidgetAndPointer> Arranged;for(const auto& A:Actual.Widgets.GetInternalArray())Arranged.Emplace(A,TOptional<FVirtualPointerPosition>());const auto Path=MakeShared<FWidgetPath>(MakeArrayView(Arranged));
        const FVector2D Start=Geometry.LocalToAbsolute(FVector2D(Geometry.GetLocalSize().X*.2,Geometry.GetLocalSize().Y*.5));
        const FVector2D Mid=Geometry.LocalToAbsolute(FVector2D(Geometry.GetLocalSize().X*.6,Geometry.GetLocalSize().Y*.5));
        const FVector2D End=Geometry.LocalToAbsolute(FVector2D(Geometry.GetLocalSize().X*.85,Geometry.GetLocalSize().Y*.5));
        // A virtual user keeps delayed platform mouse events out of this routed drag.
        const auto User=App.FindOrCreateVirtualUser(37);const auto InputScope=MakeShared<FCLAutomationSlateInputScope>();
        const uint64 Before=Editor->Globe->PublishedBuilds();
        App.RoutePointerDownEvent(*Path,FPointerEvent(User->GetUserIndex(),FSlateApplication::CursorPointerIndex,Start,Start,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        TestTrue(TEXT("Routed automation obtains slider capture"),(Crust?Editor->ContinentalSlider:Editor->TectonicSlider)->HasMouseCaptureByUser(User->GetUserIndex(),FSlateApplication::CursorPointerIndex));
        App.RoutePointerMoveEvent(*Path,FPointerEvent(User->GetUserIndex(),FSlateApplication::CursorPointerIndex,Mid,Start,TSet<FKey>{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()),false);
        TestTrue(TEXT("Real slider drag enters live draft mode"),(Crust?Editor->bContinentalDragging:Editor->bTectonicDragging)&&Editor->Globe->IsDraftMode());
        ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this,Editor,Owned,Path,Mid,End,Before,User,InputScope,Crust]
        {
            TestTrue(TEXT("Queued middle value is applied while the button is held"),(Crust?Owned->Get()->Recipe.Continents.Strength:Owned->Get()->Recipe.Tectonics.Activity)>.5);
            FSlateApplication::Get().RoutePointerMoveEvent(*Path,FPointerEvent(User->GetUserIndex(),FSlateApplication::CursorPointerIndex,End,Mid,TSet<FKey>{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()),false);
            ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this,Editor,Owned,Path,End,Before,User,InputScope,Crust]
            {
                TestTrue(TEXT("Newest value is applied before mouse release"),(Crust?Owned->Get()->Recipe.Continents.Strength:Owned->Get()->Recipe.Tectonics.Activity)>.75);
                TestTrue(TEXT("A generated draft mesh is published during the drag"),(Crust?Editor->bContinentalDragging:Editor->bTectonicDragging)&&Editor->Globe->PublishedBuilds()>Before&&Editor->Globe->HasDraftMesh());
                const auto& TD=*reinterpret_cast<const FCLTectonicPreset*>(Editor->TectonicDraft->GetStructMemory());
                const auto& CD=*reinterpret_cast<const FCLContinentalLayer*>(Editor->ContinentalDraft->GetStructMemory());
                FCLTectonicControls TC;FString E;FCLTectonicControls::Resolve(Owned->Get()->Recipe.Tectonics,TC,E,true);
                TestEqual(TEXT("Advanced relief follows accepted live knob before release"),Crust?CD.Relief:TD.Relief,Crust?FCLContinentalControls::Resolve(Owned->Get()->Recipe.Continents).Relief:TC.Relief);
                if(Crust)
                {
                    TestEqual(TEXT("A continental knob gesture adopts the new version explicitly"),Owned->Get()->Recipe.Continents.AlgorithmVersion,3);
                    TestEqual(TEXT("Advanced shows the live next-domain blend"),CD.DomainTransition,Owned->Get()->Recipe.Continents.DomainTransition);
                    TestEqual(TEXT("Advanced shows live fragmentation"),CD.Fragmentation,Owned->Get()->Recipe.Continents.Fragmentation);
                }
                FSlateApplication::Get().RoutePointerUpEvent(*Path,FPointerEvent(User->GetUserIndex(),FSlateApplication::CursorPointerIndex,End,End,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState()));
                const double Final=(Crust?Owned->Get()->Recipe.Continents.Strength:Owned->Get()->Recipe.Tectonics.Activity);TestFalse(TEXT("Release ends draft mode"),Editor->Globe->IsDraftMode());
                GEditor->UndoTransaction();TestTrue(TEXT("One undo restores the entire drag"),FMath::IsNearlyEqual((Crust?Owned->Get()->Recipe.Continents.Strength:Owned->Get()->Recipe.Tectonics.Activity),.55,1.e-6));
                GEditor->RedoTransaction();TestTrue(TEXT("One redo restores the final activity"),FMath::IsNearlyEqual((Crust?Owned->Get()->Recipe.Continents.Strength:Owned->Get()->Recipe.Tectonics.Activity),Final,1.e-6));
                FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FCLWaitForGlobeRefinement>(Editor->Globe,[this,Editor,Owned,User,InputScope]
                {
                    TestTrue(TEXT("A full quality mesh replaces the draft after release"),!Editor->Globe->IsUpdating()&&!Editor->Globe->HasDraftMesh());
                    FSlateApplication::Get().ReleaseAllPointerCapture();
                    Owned->Get()->GetOutermost()->SetDirtyFlag(false);TestTrue(TEXT("Live editor retires cleanly"),Editor->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed));
                }));
            },2.f));
        },.4f));
    },3.f));return true;
}
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGuideMeasurementsTest,"Catalyst.Landform.PlanetBuilder.RadialGuideMeasurements",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGuideMeasurementsTest::RunTest(const FString&)
{
    auto R=FCLPlanetCompiler::ContinentalBase();R.RadiusMeters=1000;
    auto& G=R.RadialGuides.Guides;
    const int32 Low=G.IndexOfByPredicate([](const auto& V){return V.Name==TEXT("SurfaceMin");});
    const int32 High=G.IndexOfByPredicate([](const auto& V){return V.Name==TEXT("SurfaceMax");});
    const int32 Sea=G.IndexOfByPredicate([](const auto& V){return V.Name==TEXT("OceanHeight");});
    G[Low].NormalizedHeight=.3;G[High].NormalizedHeight=.6;G[Sea].NormalizedHeight=.4;
    const auto Before=R;
    auto Spans=FCLRadialGuideMeasurements::SurfaceSpans(R);
    TestEqual(TEXT("Surface range and both ocean intervals"),Spans.Num(),3);
    TestTrue(TEXT("Measured full surface range in metres"),FMath::IsNearlyEqual(Spans[0].Meters,300.));
    TestTrue(TEXT("Measured below ocean"),FMath::IsNearlyEqual(Spans[1].Meters,100.));
    TestTrue(TEXT("Measured above ocean"),FMath::IsNearlyEqual(Spans[2].Meters,200.));
    TestEqual(TEXT("Heights are from Core, not reference surface"),FCLRadialGuideMeasurements::Height(R,Low).ToString(),TEXT("300 m"));
    TestEqual(TEXT("Core height"),FCLRadialGuideMeasurements::Height(R,-2).ToString(),TEXT("0 m"));
    TestEqual(TEXT("Sky is the outer radius"),FCLRadialGuideMeasurements::Height(R,-3).ToString(),FCLRadialGuideMeasurements::Meters(1000).ToString());
    TestTrue(TEXT("Reading measurements does not change the recipe"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Before,&R,0));
    R.RadiusMeters=2000;Spans=FCLRadialGuideMeasurements::SurfaceSpans(R);
    TestTrue(TEXT("Resizing proportionally updates dimensions"),FMath::IsNearlyEqual(Spans[0].Meters,600.));
    G[Sea].NormalizedHeight=.6;TestEqual(TEXT("Ocean at endpoint does not create an empty dimension"),FCLRadialGuideMeasurements::SurfaceSpans(R).Num(),1);
    G[Sea].NormalizedHeight=.9;TestEqual(TEXT("Ocean outside range is omitted"),FCLRadialGuideMeasurements::SurfaceSpans(R).Num(),1);
    G.RemoveAt(Sea);TestEqual(TEXT("Missing ocean retains full surface dimension"),FCLRadialGuideMeasurements::SurfaceSpans(R).Num(),1);
    G.Reset();TestTrue(TEXT("Missing surface guides do not invent dimensions"),FCLRadialGuideMeasurements::SurfaceSpans(R).IsEmpty());
    const auto Session=MakeShared<FCLRadialGuideSession>();Session->Asset=NewObject<UCLPlanetRecipeAsset>();Session->Asset->Recipe=Before;
    const auto Handle=SNew(SCLRadialGuideHandle).Session(Session).Index(Low).Label(true);
    TestEqual(TEXT("Editable guide name excludes the display measurement"),Handle->Name->GetText().ToString(),TEXT("SurfaceMin"));
    Session->Asset->Recipe.RadialGuides.Guides[Low].NormalizedHeight=.3506;
    TestEqual(TEXT("Dragging a guide updates its live metre value"),FCLRadialGuideMeasurements::Height(Session->Asset->Recipe,Low).ToString(),TEXT("351 m"));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGuideMeasurementsRenderTest,"Catalyst.Landform.PlanetBuilder.RadialGuideMeasurementsRendered",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGuideMeasurementsRenderTest::RunTest(const FString&)
{
    const auto Session=MakeShared<FCLRadialGuideSession>();Session->Asset=NewObject<UCLPlanetRecipeAsset>();Session->Asset->Recipe=FCLPlanetCompiler::ContinentalBase();
    auto& R=Session->Asset->Recipe;R.UseGuidedGeology();R.Continents.ApplyProgression(.65);R.Tectonics.ApplyProgression(.65);
    const auto Section=SNew(SCLSectionViewport);const auto Globe=SNew(SCLPlanetPreview);
    const auto Annotations=SNew(SCLHeightAnnotations).Client(Section->Client).Session(Session).Globe(Globe).UseGlobe(true);
    const auto Content=SNew(SOverlay)+SOverlay::Slot()[Globe]+SOverlay::Slot()[Annotations];
    const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Radial measurements"))).ClientSize(FVector2D(1000,750))[Content];
    FSlateApplication::Get().AddWindow(Window,true);Window->SlatePrepass();
    FString Error;const auto Plan=FCLPlanetCompiler::Compile(R,Error);if(!Plan){AddError(Error);FSlateApplication::Get().RequestDestroyWindow(Window);return false;}
    Globe->SetGuides(R.RadialGuides,true);Globe->Rebuild(Plan);
    ADD_LATENT_AUTOMATION_COMMAND(FCLWaitForGlobeRefinement(Globe,[this,Session,Section,Globe,Annotations,Content,Window]
    {
        TestFalse(TEXT("Annotated globe finishes refinement"),Globe->IsUpdating());
        Window->SlatePrepass();TArray<FColor> Pixels;FIntVector Size;
        TestTrue(TEXT("Annotated globe screenshot renders"),FSlateApplication::Get().TakeScreenshot(Content,Pixels,Size));
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/guide-measurement-frames");IFileManager::Get().MakeDirectory(*Folder,true);
        if(!Pixels.IsEmpty())
        {
            TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Folder/TEXT("globe-guides.png")));
        }
        for(const auto& Span:FCLRadialGuideMeasurements::SurfaceSpans(Session->Asset->Recipe))
        {
            TestTrue(TEXT("Dimension callouts stay within the viewport"),Annotations->Label(Span.High).X+Annotations->LabelWidth()+105<=Annotations->GetCachedGeometry().GetLocalSize().X+1);
        }
        FSlateApplication::Get().RequestDestroyWindow(Window);
    }));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLNativeLocalPreviewTest,"Catalyst.Landform.NativeGeneration.NativeLocalSection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLNativeLocalPreviewTest::RunTest(const FString&)
{
    FString Error;auto R=FCLPlanetCompiler::BasePlanet();R.Coordinates=ECLTerrainCoordinates::LocalPatch;
    const auto First=FCLPlanetCompiler::Compile(R,Error);R.BaseSurfaceNormalized=.6;const auto Latest=FCLPlanetCompiler::Compile(R,Error);
    if(!First||!Latest){AddError(Error);return false;}
    const auto Section=SNew(SCLSectionViewport);const auto Window=SNew(SWindow).ClientSize(FVector2D(640,480))[Section];
    FSlateApplication::Get().AddWindow(Window,false);Window->SlatePrepass();Section->Rebuild(First);
    class FWait final:public IAutomationLatentCommand
    {
    public:
        FAutomationTestBase* Test;TSharedRef<SCLSectionViewport> Section;TSharedRef<SWindow> Window;TSharedPtr<const FCLCompiledPlanetRecipe> First,Latest;
        int32 Phase=0;uint32 Owner=0;const void* Runtime=nullptr;double Start=FPlatformTime::Seconds();
        FWait(FAutomationTestBase* T,TSharedRef<SCLSectionViewport> S,TSharedRef<SWindow> W,TSharedPtr<const FCLCompiledPlanetRecipe> A,TSharedPtr<const FCLCompiledPlanetRecipe> B):Test(T),Section(S),Window(W),First(A),Latest(B){}
        bool Update()override
        {
            Section->PumpBuild();auto Close=[&]{FSlateApplication::Get().RequestDestroyWindow(Window);return true;};
            if(FPlatformTime::Seconds()-Start>30){Test->AddError(Section->NativePreview->Status());return Close();}
            if(Section->IsUpdating())return false;
            auto& P=*Section->NativePreview;Test->TestTrue(TEXT("Local section is rendered by VPP"),P.UsesNativePresentation());
            if(Phase==0){Owner=P.WorldIdentity();Runtime=P.RuntimeIdentity();Section->Rebuild(Latest);Section->Rebuild(First);Section->Rebuild(Latest);FVector PendingHit;Test->TestFalse(TEXT("Queued input cannot pick retained collision with a new recipe"),P.PickSection(FVector(0,0,90000),FVector(0,0,-1),PendingHit));++Phase;Start=FPlatformTime::Seconds();return false;}
            Test->TestEqual(TEXT("Local section reuses native world"),P.WorldIdentity(),Owner);Test->TestTrue(TEXT("Local section reuses native runtime"),P.RuntimeIdentity()==Runtime);
            Test->TestTrue(TEXT("Local section publishes latest input"),P.PublishedPlan()==Latest);
            FString MaterialError;Test->TestTrue(TEXT("Updated native section mapping matches its input"),P.CheckMaterialCoordinates(MaterialError));Test->AddInfo(MaterialError);
            FVector Hit;const bool Picked=P.PickSection(FVector(0,0,90000),FVector(0,0,-1),Hit);
            Test->TestTrue(TEXT("Native local collision follows changed physical height"),Picked&&FMath::Abs(Hit.Z-600)<.5);
            Test->AddInfo(FString::Printf(TEXT("Local section native contact: %d %s"),Picked,*Hit.ToString()));return Close();
        }
    };
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FWait>(this,Section,Window,First,Latest));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPreviewMaterialRenderedTest,"Catalyst.Landform.NativeMaterials.RenderedPreviewCoordinates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPreviewMaterialRenderedTest::RunTest(const FString&)
{
    auto R=FCLPlanetCompiler::BasePlanet();R.SchemaVersion=17;R.RadiusMeters=10000;R.SurfaceStack=FCLSurfaceStack::MakeDefault();R.SurfaceStack.Layers.Reset();
    FString Error;const auto Plan=FCLPlanetCompiler::Compile(R,Error);if(!Plan){AddError(Error);return false;}
    const auto Globe=SNew(SCLPlanetPreview);const auto Section=SNew(SCLSectionViewport);
    const auto Content=SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[Globe]+SHorizontalBox::Slot().FillWidth(1)[Section];
    const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Native canonical material coordinates"))).ClientSize(FVector2D(1200,600))[Content];
    FSlateApplication::Get().AddWindow(Window,true);Window->SlatePrepass();Globe->Rebuild(Plan);Section->Rebuild(Plan);
    class FWait final:public IAutomationLatentCommand
    {
    public:
        FAutomationTestBase* Test;TSharedRef<SCLPlanetPreview> Globe;TSharedRef<SCLSectionViewport> Section;TSharedRef<SWindow> Window;TSharedRef<SWidget> Content;
        TArray<TArray<FColor>> Frames;int32 Phase=0;double Start=FPlatformTime::Seconds(),Stable=0;uint32 Owner=0;const void* Runtime=nullptr;
        FWait(FAutomationTestBase* T,TSharedRef<SCLPlanetPreview> G,TSharedRef<SCLSectionViewport> S,TSharedRef<SWindow> W,TSharedRef<SWidget> C):Test(T),Globe(G),Section(S),Window(W),Content(C){}
        bool Update()override
        {
            Globe->Pump();Section->PumpBuild();auto Close=[&]{FSlateApplication::Get().RequestDestroyWindow(Window);return true;};
            const double Now=FPlatformTime::Seconds();if(Now-Start>90){Test->AddError(Globe->NativePreviewStatus());return Close();}
            if(Globe->IsUpdating()||Section->IsUpdating()){Stable=0;return false;}
            if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling()){GShaderCompilingManager->FinishAllCompilation();Stable=0;return false;}
            if(Stable==0){Stable=Now;return false;}if(Now-Stable<1)return false;
            FString E;Test->TestTrue(TEXT("Displayed globe has state-owned native coordinate metadata"),Globe->NativePreview->CheckMaterialCoordinates(E));Test->AddInfo(E);
            Test->TestTrue(TEXT("Other preview retains its own native section mapping"),Section->NativePreview->CheckMaterialCoordinates(E));Test->AddInfo(E);
            Test->TestTrue(TEXT("Both viewports retain native presentation"),Globe->NativePreview->UsesNativePresentation()&&Section->NativePreview->UsesNativePresentation());
            if(Phase==0){Owner=Globe->NativePreview->WorldIdentity();Runtime=Globe->NativePreview->RuntimeIdentity();}
            else {Test->TestEqual(TEXT("Coordinate changes retain the native world"),Globe->NativePreview->WorldIdentity(),Owner);Test->TestTrue(TEXT("Coordinate changes retain native runtime"),Globe->NativePreview->RuntimeIdentity()==Runtime);}
            TArray<FColor> Pixels;FIntVector Size;Window->SlatePrepass();if(!FSlateApplication::Get().TakeScreenshot(Content,Pixels,Size)||Size.X<=0||Size.Y<=0||Pixels.Num()!=Size.X*Size.Y){Test->AddError(TEXT("Native coordinate frame did not render"));return Close();}Frames.Add(Pixels);
            const FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/PreviewMaterials/rendered");IFileManager::Get().MakeDirectory(*Folder,true);TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Folder/FString::Printf(TEXT("phase-%d.png"),Phase)));
            if(Phase<2){Globe->NativePreview->SetMaterialCoordinatesEnabledForTesting(Phase==1);++Phase;Start=Now;Stable=0;return false;}
            if(Frames[0].Num()!=Frames[1].Num()||Frames[0].Num()!=Frames[2].Num()){Test->AddError(TEXT("Frame dimensions changed"));return Close();}
            double Changed=0,Repeated=0;int32 Count=0;
            for(int32 I=0;I<Frames[0].Num();++I)if(I%Size.X<Size.X/2&&Frames[0][I].R+Frames[0][I].G+Frames[0][I].B>300&&Frames[1][I].R+Frames[1][I].G+Frames[1][I].B>300)
            {const auto A=Frames[0][I],B=Frames[1][I],C=Frames[2][I];Changed+=FMath::Abs(int(A.R)-B.R)+FMath::Abs(int(A.G)-B.G)+FMath::Abs(int(A.B)-B.B);Repeated+=FMath::Abs(int(A.R)-C.R)+FMath::Abs(int(A.G)-C.G)+FMath::Abs(int(A.B)-C.B);++Count;}
            Changed/=FMath::Max(1,Count)*3;Repeated/=FMath::Max(1,Count)*3;
            Test->AddInfo(FString::Printf(TEXT("Native material coordinate control: %d surface pixels, on/off %.6f/255, repeated-on %.6f/255"),Count,Changed,Repeated));
            Test->TestTrue(TEXT("Canonical scale changes visible native material sampling beyond frame noise"),Count>1000&&Changed>Repeated+.2);return Close();
        }
    };
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FWait>(this,Globe,Section,Window,Content));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLNativePreviewTest,"Catalyst.Landform.Volumes.NativePreview",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLNativePreviewTest::RunTest(const FString&)
{
    FString Json,Error;const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"));
    TStrongObjectPtr<UCLPlanetRecipeAsset> Asset(NewObject<UCLPlanetRecipeAsset>());
    if(!FFileHelper::LoadFileToString(Json,*(Plugin->GetBaseDir()/TEXT("Examples/VolumetricWorld.json")))||!Asset->ImportRecipeJson(Json,Error)){AddError(Error);return false;}
    const auto Plan=FCLPlanetCompiler::Compile(Asset->Recipe,Error);if(!Plan){AddError(Error);return false;}
    const auto Section=SNew(SCLSectionViewport);const auto Globe=SNew(SCLPlanetPreview);
    const auto Content=SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[Globe]+SHorizontalBox::Slot().FillWidth(1)[Section];
    const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Native volumetric globe and cutaway"))).ClientSize(FVector2D(1600,700))[Content];
    FSlateApplication::Get().AddWindow(Window,true);Window->SlatePrepass();Section->RebuildAsync(Plan);Globe->Rebuild(Plan);
    class FWait final:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;TSharedPtr<SCLSectionViewport> Section;TSharedPtr<SCLPlanetPreview> Globe;TSharedPtr<SWindow> Window;TSharedPtr<SWidget> Content;double Started=FPlatformTime::Seconds(),StableAt=0;
    public:
        FWait(FAutomationTestBase* T,TSharedPtr<SCLSectionViewport> S,TSharedPtr<SCLPlanetPreview> G,TSharedPtr<SWindow> W,TSharedPtr<SWidget> C):Test(T),Section(S),Globe(G),Window(W),Content(C){}
        virtual bool Update()override
        {
            if((Section->IsUpdating()||Globe->IsUpdating())&&FPlatformTime::Seconds()-Started<120){StableAt=0;return false;}
            if(!StableAt)StableAt=FPlatformTime::Seconds();
            if(FPlatformTime::Seconds()-StableAt<1){Section->Client->Invalidate();Globe->Invalidate();return false;}
            Test->TestFalse(TEXT("Native globe completes publication"),Globe->IsUpdating());Test->TestFalse(TEXT("Native flat cutaway completes publication"),Section->IsUpdating());
            Test->TestTrue(TEXT("Native globe publishes geometry"),Globe->PublishedBuilds()>0);
            FString MaterialError;Test->TestTrue(TEXT("Globe native meshes carry canonical material mapping"),Globe->NativePreview->CheckMaterialCoordinates(MaterialError));Test->AddInfo(MaterialError);
            Test->TestTrue(TEXT("Section native meshes carry canonical material mapping"),Section->NativePreview->CheckMaterialCoordinates(MaterialError));Test->AddInfo(MaterialError);
            Test->AddInfo(Globe->NativePreviewStatus());
            Test->TestTrue(TEXT("Native cutaway publishes its VPP triangles"),Section->AppliedBuilds>0&&Section->NativePreview&&Section->NativePreview->Published().Triangles.Num()>0);
            if(Section->NativePreview)
            {
                const auto& B=Section->NativePreview->Published();bool Underground=false,Floating=false;
                FBox SourceBox(ForceInit),DisplayBox(ForceInit);double MinR=MAX_dbl,MaxR=0;
                for(const auto& P:B.SourcePoints){Underground|=P.Size()<64500;Floating|=P.Size()>80000;SourceBox+=P;MinR=FMath::Min(MinR,P.Size());MaxR=FMath::Max(MaxR,P.Size());}
                for(const auto& P:B.Vertices)DisplayBox+=P;
                Test->AddInfo(FString::Printf(TEXT("Source %s display %s radii %.3f..%.3f"),*SourceBox.ToString(),*DisplayBox.ToString(),MinR,MaxR));
                Test->TestTrue(TEXT("Native cutaway includes cave-depth rock and detached formation above base max"),Underground&&Floating);
                Test->AddInfo(FString::Printf(TEXT("Cutaway native triangles %d, original body positions %d; producer: %s"),B.Triangles.Num()/3,B.SourcePoints.Num(),*Section->NativePreview->Status()));
                const double Reference=FCLSectionGeometry::ReferenceSurfaceRadius(*Section->Client->Plan);
                auto Display=[Reference](const FVector& P){const double A=FMath::Atan2(FVector2D(P.X,P.Y).Size(),P.Z);const FVector2D XY=FVector2D(P.X,P.Y).GetSafeNormal()*(A*Reference);return FVector(XY.X/500,XY.Y/500,P.Length()/1000)*100000.;};
                for(double Z:{631.,604.})
                {
                    FVector Pick;const FVector Start=Display(FVector(0,45,Z+2));
                    const bool Hit=Section->NativePreview->PickSection(Start,FVector(0,0,-1),Pick);
                    Test->TestTrue(TEXT("Native cutaway selects each underground floor by XYZ"),Hit&&FMath::Abs(Pick.Z-Z)<.3);
                    Test->AddInfo(FString::Printf(TEXT("Cutaway floor %.0f: hit %d, %s"),Z,Hit,*Pick.ToString()));

                }
            }
                FVector FloatingPick;
                const bool FloatingHit=Globe->NativePreview&&Globe->NativePreview->PickGlobe(FVector(8500,-2000,85000),FVector(0,0,-1),FloatingPick);
                Test->TestTrue(TEXT("Globe picking selects the detached solid above the radial base"),FloatingHit&&FloatingPick.Z>800&&FloatingPick.Z<830);
                Test->AddInfo(FString::Printf(TEXT("Globe detached pick: hit %d, %s"),FloatingHit,*FloatingPick.ToString()));
            TArray<FColor> Pixels;FIntVector Size;
            if(Test->TestTrue(TEXT("Native preview pair renders"),FSlateApplication::Get().TakeScreenshot(Content.ToSharedRef(),Pixels,Size))&&!Pixels.IsEmpty())
            {
                const FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/volumetric-preview");IFileManager::Get().MakeDirectory(*Folder,true);TArray64<uint8> PNG;
                FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Folder/TEXT("native-globe-section.png")));
            }
            FSlateApplication::Get().RequestDestroyWindow(Window.ToSharedRef());return true;
        }
    };
    ADD_LATENT_AUTOMATION_COMMAND(FWait(this,Section,Globe,Window,Content));return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
namespace {
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRockDesignerTest,"Catalyst.Landform.Rock.DesignerControlsAndUndo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRockDesignerTest::RunTest(const FString&)
{
    auto* Package=CreatePackage(*(TEXT("/Temp/CLRockControls_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>(Package,NAME_None,RF_Transactional));A->Recipe=FCLPlanetCompiler::ContinentalBase();const auto Original=A->Recipe;
    auto E=MakeShared<FCLRecipeToolkit>();E->Open(A.Get(),EToolkitMode::Standalone,nullptr);E->AwaitEvaluationForTesting();
    TestTrue(TEXT("Rock joins the existing Geology controls"),E->RockSlider.IsValid()&&!E->RockAdvancedProperties.IsEmpty()&&E->RockVariation.IsValid());
    auto Layer=E->LayeredRecipe().Rock;Layer.bEnabled=true;TestTrue(TEXT("Explicit enable resolves the versioned native material profile"),E->CommitRock(Layer));const auto Before=A->Recipe;
    E->BeginRockDrag();E->PendingRock=.3;E->PumpRock(true);E->PendingRock=.85;E->PumpRock(true);TestTrue(TEXT("The held gesture supplies the latest geological structure"),E->bRockDragging&&E->Globe->IsDraftMode()&&A->Recipe.Rock.Character==.85);E->EndRockDrag();const auto After=A->Recipe;TestEqual(TEXT("A new Character gesture adopts material-aware folding explicitly"),After.Rock.AlgorithmVersion,5);
    GEditor->UndoTransaction();TestTrue(TEXT("One Undo restores the complete geological gesture"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Before,&A->Recipe,0));GEditor->RedoTransaction();TestTrue(TEXT("Redo restores the exact resolved strata"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&After,&A->Recipe,0));
    auto& Draft=*reinterpret_cast<FCLRockLayer*>(E->RockDraft->GetStructMemory());auto* P=FCLRockLayer::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FCLRockLayer,DipDegrees));FPropertyChangedEvent Changed(P,EPropertyChangeType::ValueSet);
    const auto DipHandle=*E->RockAdvancedProperties.FindByPredicate([](const auto& H){return H->GetProperty()->GetFName()==TEXT("DipDegrees");});
    TestEqual(TEXT("Native direct property edit succeeds"),DipHandle->SetValue(3.),FPropertyAccess::Success);TestEqual(TEXT("Advanced changes the accepted model"),A->Recipe.Rock.DipDegrees,3.);TestEqual(TEXT("Advanced preserves the gesture position"),A->Recipe.Rock.Character,.85);
    Draft.DipDegrees=99;E->PlanetInspectors[1]->OnFinishedChangingProperties().Broadcast(Changed);TestEqual(TEXT("Invalid controls retain the accepted model"),A->Recipe.Rock.DipDegrees,3.);
    const auto UnitsHandle=*E->RockAdvancedProperties.FindByPredicate([](const auto& H){return H->GetProperty()->GetFName()==TEXT("Units");});
    const auto ResistanceHandle=UnitsHandle->GetChildHandle(1)->GetChildHandle(TEXT("MiningResistance"));
    TestEqual(TEXT("Native nested palette edit succeeds"),ResistanceHandle->SetValue(.61),FPropertyAccess::Success);
    TestEqual(TEXT("Nested array edit reaches the accepted geological palette"),A->Recipe.Rock.Units[1].MiningResistance,.61);
    GEditor->UndoTransaction();TestEqual(TEXT("One Undo restores the nested palette edit"),A->Recipe.Rock.Units[1].MiningResistance,.55);
    const auto PeakHandle=*E->RockBedProperties.FindByPredicate([](const auto& H){return H->GetProperty()->GetFName()==TEXT("PeakMeters");});
    TestEqual(TEXT("Native flat physical-bed edit succeeds"),PeakHandle->SetValue(4.2),FPropertyAccess::Success);TestEqual(TEXT("Bed peak reaches the accepted canonical recipe"),A->Recipe.Rock.BedDistribution.PeakMeters,4.2);
    GEditor->UndoTransaction();TestEqual(TEXT("One Undo restores the physical peak"),A->Recipe.Rock.BedDistribution.PeakMeters,After.Rock.BedDistribution.PeakMeters);
    A->Recipe.SchemaVersion=21;A->Recipe.AlgorithmVersion=2;A->Recipe.ElevationDistribution.bEnabled=true;A->PostEditChange();
    auto Physical=A->Recipe.Rock;Physical.BedDistribution.ThickBedPercent=8;TestTrue(TEXT("Physical geology edits coexist with schema-21 elevation"),E->CommitRock(Physical));TestEqual(TEXT("Rock never downgrades the current recipe schema"),A->Recipe.SchemaVersion,21);TestTrue(TEXT("Elevation mapping remains enabled"),A->Recipe.ElevationDistribution.bEnabled);
    const auto BeforeVariation=A->Recipe;E->RockVariation->SimulateClick();TestTrue(TEXT("Variation changes only the rock seed"),A->Recipe.Rock.Seed!=BeforeVariation.Rock.Seed&&A->Recipe.Continents.Seed==BeforeVariation.Continents.Seed&&A->Recipe.Tectonics.Seed==BeforeVariation.Tectonics.Seed);GEditor->UndoTransaction();TestTrue(TEXT("Variation has one Undo"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&BeforeVariation,&A->Recipe,0));
    E->PlanetInspectors[1]->SetRootExpansionStates(false,false);
    class FLayoutReview final:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;TSharedRef<FCLRecipeToolkit> Editor;TStrongObjectPtr<UCLPlanetRecipeAsset> Asset;int32 Layer=-1;double Started=0;
    public:
        FLayoutReview(FAutomationTestBase* T,TSharedRef<FCLRecipeToolkit> E,UCLPlanetRecipeAsset* A):Test(T),Editor(E),Asset(A){}
        static void AdvancedButtons(TSharedRef<SWidget> Widget,TArray<TSharedPtr<SButton>>& Out)
        {
            if(Widget->GetTypeAsString()==TEXT("SDetailCategoryTableRow"))
            {
                TFunction<TSharedPtr<SButton>(TSharedRef<SWidget>)> FindButton;
                FindButton=[&](TSharedRef<SWidget> W)->TSharedPtr<SButton>
                {
                    if(W->GetTypeAsString()==TEXT("SButton"))return StaticCastSharedRef<SButton>(W);
                    auto* Children=W->GetChildren();for(int32 I=0;I<Children->Num();++I)if(auto B=FindButton(Children->GetChildAt(I)))return B;
                    return nullptr;
                };
                if(auto B=FindButton(Widget))Out.Add(B);return;
            }
            auto* Children=Widget->GetChildren();for(int32 I=0;I<Children->Num();++I)AdvancedButtons(Children->GetChildAt(I),Out);
        }
        bool Update()override
        {
            const double Now=FPlatformTime::Seconds();const auto View=Editor->PlanetInspectors[1];
            auto Close=[&]{Asset->GetOutermost()->SetDirtyFlag(false);Editor->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);return true;};
            TArray<TSharedPtr<SButton>> Buttons;AdvancedButtons(View.ToSharedRef(),Buttons);
            if(Layer<0)Layer=0;
            if(!Started)
            {
                if(!Test->TestTrue(TEXT("Native layer chevrons are reachable"),Buttons.Num()>Layer))return Close();
                Buttons[Layer]->SimulateClick();Started=Now;return false;
            }
            if(Now-Started<.5)return false;
            FString Visible;for(const auto& Path:View->GetPropertiesInOrderDisplayed())Visible+=Path.ToString()+TEXT(";");
            const TCHAR* Expected[]={TEXT("Extent"),TEXT("PlateCount"),TEXT("PeakMeters")};
            Test->TestTrue(TEXT("Opening the native layer reveals direct property rows"),Visible.Contains(Expected[Layer]));
            TArray<FColor> Pixels;FIntVector Size;
            Test->TestTrue(TEXT("Direct layer property layout renders"),FSlateApplication::Get().TakeScreenshot(View.ToSharedRef(),Pixels,Size));
            if(!Pixels.IsEmpty())
            {
                const FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/rock-character");IFileManager::Get().MakeDirectory(*Folder,true);TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
                FFileHelper::SaveArrayToFile(PNG,*(Folder/FString::Printf(TEXT("advanced-%d.png"),Layer)));
            }
            if(Buttons.Num()>Layer)Buttons[Layer]->SimulateClick();
            if(++Layer==3)return Close();Started=0;return false;
        }
    };
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FLayoutReview>(this,E,A.Get()));return true;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRockRenderedTest,"Catalyst.Landform.Rock.NativeRenderedReview",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRockRenderedTest::RunTest(const FString&)
{
    const auto Globe=SNew(SCLPlanetPreview);const auto Section=SNew(SCLSectionViewport);const auto Content=SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[Globe]+SHorizontalBox::Slot().FillWidth(1)[Section];const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Native Rock Character globe and section"))).ClientSize(FVector2D(1400,700))[Content];FSlateApplication::Get().AddWindow(Window,true);Window->SlatePrepass();
    class FReview final:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;TSharedRef<SCLPlanetPreview> Globe;TSharedRef<SCLSectionViewport> Section;TSharedRef<SWindow> Window;TSharedRef<SWidget> Content;int32 Index=-1;double Started=FPlatformTime::Seconds(),Stable=0;
    public:
        FReview(FAutomationTestBase* T,TSharedRef<SCLPlanetPreview> G,TSharedRef<SCLSectionViewport> S,TSharedRef<SWindow> W,TSharedRef<SWidget> C):Test(T),Globe(G),Section(S),Window(W),Content(C){}
        bool Update()override
        {
            auto Close=[&]{FSlateApplication::Get().RequestDestroyWindow(Window);return true;};const double Now=FPlatformTime::Seconds();Section->PumpBuild();if(Now-Started>120){Test->AddError(Globe->NativePreviewStatus());return Close();}
            if(Index>=0)
            {
                if(Globe->IsUpdating()||Section->IsUpdating()){Stable=0;return false;}if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling()){GShaderCompilingManager->FinishAllCompilation();Stable=0;return false;}if(!Stable){Stable=Now;return false;}if(Now-Stable<.75)return false;
                Test->TestTrue(TEXT("The geology review uses native VPP geometry in both views"),Globe->PublishedBuilds()>0&&Section->AppliedBuilds>0&&Section->NativePreview->Published().Triangles.Num()>0);FString E;Test->TestTrue(TEXT("Native geological preview keeps canonical material coordinates"),Section->NativePreview->CheckMaterialCoordinates(E));
                TArray<FColor> Pixels;FIntVector Size;Test->TestTrue(TEXT("Native geological review frame renders"),FSlateApplication::Get().TakeScreenshot(Content,Pixels,Size));if(!Pixels.IsEmpty()){const FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/rock-review");IFileManager::Get().MakeDirectory(*Folder,true);TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Folder/FString::Printf(TEXT("case-%02d.png"),Index)));}
            }
            if(++Index==24)return Close();auto R=FCLPlanetCompiler::ContinentalBase();R.UseGuidedGeology();R.SchemaVersion=20;const int32 Seeds[]={1729,817,529};const double Positions[]={0.,.10,.18,.38,.58,.78,.90,1.};R.Seed=Seeds[Index/8];R.Rock.bEnabled=true;R.Rock.ApplyProgression(Positions[Index%8]);FCLSolidOperation Cave;Cave.Id=FGuid(1,0,0,1);Cave.Name=TEXT("Review cave");Cave.Primitive=ECLSolidPrimitive::Box;Cave.Composition=ECLSolidComposition::Subtract;Cave.CenterMeters=FVector(0,0,700);Cave.HalfExtentsMeters=FVector(100,100,80);R.Volumes.bEnabled=true;R.Volumes.Operations={Cave};FString Error;const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){Test->AddError(Error);return Close();}Globe->Rebuild(P);Section->Rebuild(P);Started=Now;Stable=0;return false;
        }
    };
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FReview>(this,Globe,Section,Window,Content));return true;
}

namespace
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLNavigationResponsivenessTest,"Catalyst.Landform.Designer.NavigationResponsiveness",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLNavigationResponsivenessTest::RunTest(const FString&)
{
    FString J,Error;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/WatershedContinental.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>(CreatePackage(*(TEXT("/Temp/CLNavigation_")+FGuid::NewGuid().ToString(EGuidFormats::Digits))),NAME_None,RF_Transactional));
    if(!A->ImportRecipeJson(J,Error)){AddError(Error);return false;}A->Recipe.Watershed.bEnabled=false;
    auto E=MakeShared<FCLRecipeToolkit>();E->Open(A.Get(),EToolkitMode::Standalone,nullptr);
    class FRun final:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;TSharedRef<FCLRecipeToolkit> Editor;TStrongObjectPtr<UCLPlanetRecipeAsset> Asset;TSharedPtr<FCLAutomationSlateInputScope> InputScope;
        FCLPlanetRecipe Before;FCLNativePreview::FMetrics Initial;uint32 Evaluations=0,World=0;int32 Frame=-30,Indicators=0;double Start=FPlatformTime::Seconds(),Last=0,MaxWheelMs=0;TArray<double> Frames;bool Ready=false;
    public:
        FRun(FAutomationTestBase* T,TSharedRef<FCLRecipeToolkit> E,UCLPlanetRecipeAsset* A):Test(T),Editor(E),Asset(A){}
        bool Update()override
        {
            const auto Close=[&]{Asset->GetOutermost()->SetDirtyFlag(false);Editor->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);return true;};
            const double Now=FPlatformTime::Seconds();if(Now-Start>150){Test->AddError(TEXT("Navigation proof timed out"));return Close();}
            auto* Native=Editor->Globe->NativeForTesting();
            if(!Ready)
            {
                if(Editor->EvaluationUpdating()||!Native)return false;
                Test->TestTrue(TEXT("Navigation workload has Watershed disabled"),!Asset->Recipe.Watershed.bEnabled&&Editor->AcceptedSurfacePlan&&!Editor->AcceptedSurfacePlan->Watershed);
                InputScope=MakeShared<FCLAutomationSlateInputScope>();Before=Asset->Recipe;Ready=true;Last=Now;return false;
            }
            if(Frame<0){++Frame;Last=Now;if(Frame==0){Initial=Native->Metrics;Native->Metrics.MaxPollMs=0;Evaluations=Editor->Evaluation.Started;World=Native->WorldIdentity();}return false;}
            if(Frame<180)
            {
                Frames.Add((Now-Last)*1000);Last=Now;if(Editor->AuthoringUpdating())++Indicators;
                auto& App=FSlateApplication::Get();const auto User=App.FindOrCreateVirtualUser(39);FWidgetPath Actual;App.GeneratePathToWidgetUnchecked(Editor->Globe.ToSharedRef(),Actual,EVisibility::All);
                if(!Actual.IsValid()){Test->AddError(TEXT("Globe has no routed wheel path"));return Close();}
                TArray<FWidgetAndPointer> Arranged;for(const auto& W:Actual.Widgets.GetInternalArray())Arranged.Emplace(W,TOptional<FVirtualPointerPosition>());FWidgetPath Path(MakeArrayView(Arranged));
                const auto G=Editor->Globe->GetCachedGeometry();const FVector2D P=G.LocalToAbsolute(G.GetLocalSize()*.5);
                const double T=FPlatformTime::Seconds();const double Wheel=(Frame/30)%2?-.08:.08;
                App.RouteMouseWheelOrGestureEvent(Path,FPointerEvent(User->GetUserIndex(),FSlateApplication::CursorPointerIndex,P,P,TSet<FKey>{},EKeys::Invalid,Wheel,FModifierKeysState()));
                Editor->Globe->OrbitForTesting(FVector2D(1.5,.35));MaxWheelMs=FMath::Max(MaxWheelMs,(FPlatformTime::Seconds()-T)*1000);++Frame;return false;
            }
            if(Frame<240||(Editor->EvaluationUpdating()&&Frame<600)){Frames.Add((Now-Last)*1000);Last=Now;++Frame;return false;}
            Frames.Sort();const auto M=Native->Metrics;const double P50=Frames[Frames.Num()/2],P95=Frames[FMath::FloorToInt(.95*(Frames.Num()-1))],Worst=Frames.Last();
            Test->TestEqual(TEXT("Camera navigation starts no recipe evaluation"),Editor->Evaluation.Started,Evaluations);Test->TestEqual(TEXT("Camera navigation queues no native recipe input"),M.Starts,Initial.Starts);Test->TestEqual(TEXT("Camera navigation retains native world"),Native->WorldIdentity(),World);Test->TestTrue(TEXT("Navigation preserves the authored recipe"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Before,&Asset->Recipe,0));
            const bool Baseline=FParse::Param(FCommandLine::Get(),TEXT("CLNavigationBaseline"));
            if(!Baseline){Test->TestEqual(TEXT("Camera motion displays no authoring progress"),Indicators,0);Test->TestEqual(TEXT("Camera motion copies no native inspection mesh"),M.MeshCopies,Initial.MeshCopies);Test->TestTrue(TEXT("Routed wheel/orbit work stays below 8 ms"),MaxWheelMs<8);Test->TestTrue(TEXT("Navigation p95 stays below 50 ms in this bounded profile"),P95<50);}
            FString JSON=FString::Printf(TEXT("{\"baseline\":%s,\"watershed\":false,\"frames\":%d,\"frameP50Ms\":%.6f,\"frameP95Ms\":%.6f,\"maxFrameMs\":%.6f,\"maxInputMs\":%.6f,\"maxPollMs\":%.6f,\"meshCopies\":%llu,\"verticesCopied\":%llu,\"copyMs\":%.6f,\"progressFrames\":%d,\"nativeStarts\":%llu,\"evaluations\":%u}\n"),Baseline?TEXT("true"):TEXT("false"),Frames.Num(),P50,P95,Worst,MaxWheelMs,M.MaxPollMs,M.MeshCopies-Initial.MeshCopies,M.VerticesCopied-Initial.VerticesCopied,M.TotalCopyMs-Initial.TotalCopyMs,Indicators,M.Starts-Initial.Starts,Editor->Evaluation.Started-Evaluations);
            const FString Dir=FPaths::ProjectSavedDir()/TEXT("Verification/navigation");IFileManager::Get().MakeDirectory(*Dir,true);FFileHelper::SaveStringToFile(JSON,*(Dir/(Baseline?TEXT("baseline.json"):TEXT("fixed.json"))));Test->AddInfo(JSON);return Close();
        }
    };
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FRun>(this,E,A.Get()));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPropertyScrollTest,"Catalyst.Landform.Designer.PropertyScrollResponsiveness",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPropertyScrollTest::RunTest(const FString&)
{
    FString J,Error;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/WatershedContinental.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>(CreatePackage(*(TEXT("/Temp/CLPropertyScroll_")+FGuid::NewGuid().ToString(EGuidFormats::Digits))),NAME_None,RF_Transactional));
    if(!A->ImportRecipeJson(J,Error)){AddError(Error);return false;}A->Recipe.Watershed.bEnabled=false;
    auto E=MakeShared<FCLRecipeToolkit>();E->Open(A.Get(),EToolkitMode::Standalone,nullptr);E->InspectorPage=1;E->PlanetInspectors[1]->SetRootExpansionStates(true,true);
    class FRun final:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;TSharedRef<FCLRecipeToolkit> Editor;TStrongObjectPtr<UCLPlanetRecipeAsset> Asset;TSharedPtr<STableViewBase> Tree;TSharedPtr<FCLAutomationSlateInputScope> InputScope;
        FCLPlanetRecipe Before;FCLNativePreview::FMetrics Initial;uint32 Evaluations=0;int32 Frame=-30,Handled=0,Indicators=0;double Start=FPlatformTime::Seconds(),Last=0,MaxInputMs=0,MaxOffset=0;TArray<double> Frames;bool Ready=false;
        static TSharedPtr<STableViewBase> FindTree(TSharedRef<SWidget> W)
        {
            if(W->GetTypeAsString()==TEXT("SDetailTree")||W->GetTypeAsString()==TEXT("STreeView"))return StaticCastSharedRef<STableViewBase>(W);
            auto* C=W->GetChildren();for(int32 I=0;I<C->Num();++I)if(auto T=FindTree(C->GetChildAt(I)))return T;return nullptr;
        }
    public:
        FRun(FAutomationTestBase* T,TSharedRef<FCLRecipeToolkit> E,UCLPlanetRecipeAsset* A):Test(T),Editor(E),Asset(A){}
        bool Update()override
        {
            const auto Close=[&]{Asset->GetOutermost()->SetDirtyFlag(false);Editor->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);return true;};const double Now=FPlatformTime::Seconds();if(Now-Start>120){Test->AddError(TEXT("Property scrolling timed out"));return Close();}
            auto* Native=Editor->Globe->NativeForTesting();if(!Ready)
            {
                if(Editor->EvaluationUpdating()||!Native)return false;Tree=FindTree(Editor->PlanetInspectors[1].ToSharedRef());if(!Tree){Test->AddError(TEXT("Native Details tree was not found"));return Close();}
                InputScope=MakeShared<FCLAutomationSlateInputScope>();Before=Asset->Recipe;Initial=Native->Metrics;Evaluations=Editor->Evaluation.Started;Ready=true;Last=Now;return false;
            }
            if(Frame<0){++Frame;Last=Now;return false;}
            if(Frame<120)
            {
                Frames.Add((Now-Last)*1000);Last=Now;MaxOffset=FMath::Max(MaxOffset,double(Tree->GetScrollOffset()));if(Editor->AuthoringUpdating())++Indicators;
                auto& App=FSlateApplication::Get();const auto User=App.FindOrCreateVirtualUser(40);FWidgetPath Actual;App.GeneratePathToWidgetUnchecked(Tree.ToSharedRef(),Actual,EVisibility::All);if(!Actual.IsValid()){Test->AddError(TEXT("Details tree has no routed path"));return Close();}
                TArray<FWidgetAndPointer> Arranged;for(const auto& W:Actual.Widgets.GetInternalArray())Arranged.Emplace(W,TOptional<FVirtualPointerPosition>());FWidgetPath Path(MakeArrayView(Arranged));
                const auto G=Tree->GetCachedGeometry();const FVector2D P=G.LocalToAbsolute(G.GetLocalSize()*.5);const double T=FPlatformTime::Seconds();
                const auto Reply=App.RouteMouseWheelOrGestureEvent(Path,FPointerEvent(User->GetUserIndex(),FSlateApplication::CursorPointerIndex,P,P,TSet<FKey>{},EKeys::Invalid,(Frame/30)%2?1.:-1.,FModifierKeysState()));Handled+=Reply.IsEventHandled();MaxInputMs=FMath::Max(MaxInputMs,(FPlatformTime::Seconds()-T)*1000);++Frame;return false;
            }
            Frames.Sort();const double P95=Frames[FMath::FloorToInt(.95*(Frames.Num()-1))];const auto M=Native->Metrics;
            Test->TestTrue(TEXT("Native expanded Details tree actually scrolls"),Handled>0&&MaxOffset>1);Test->TestFalse(TEXT("Watershed stays disabled"),Asset->Recipe.Watershed.bEnabled);
            Test->TestEqual(TEXT("Property scrolling shows no authoring progress"),Indicators,0);Test->TestEqual(TEXT("Property scrolling evaluates no recipe"),Editor->Evaluation.Started,Evaluations);Test->TestEqual(TEXT("Property scrolling starts no native input"),M.Starts,Initial.Starts);Test->TestEqual(TEXT("Property scrolling copies no native inspection mesh"),M.MeshCopies,Initial.MeshCopies);
            Test->TestTrue(TEXT("Scrolling leaves authored fields unchanged"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Before,&Asset->Recipe,0));Test->TestTrue(TEXT("Native property wheel dispatch stays below 8 ms"),MaxInputMs<8);Test->TestTrue(TEXT("Expanded property scrolling p95 stays below 50 ms"),P95<50);
            const FString Dir=FPaths::ProjectSavedDir()/TEXT("Verification/navigation");IFileManager::Get().MakeDirectory(*Dir,true);const FString JSON=FString::Printf(TEXT("{\"watershed\":false,\"frames\":%d,\"frameP50Ms\":%.6f,\"frameP95Ms\":%.6f,\"maxFrameMs\":%.6f,\"maxInputMs\":%.6f,\"maxScrollOffset\":%.6f,\"handledWheels\":%d,\"progressFrames\":%d,\"nativeStarts\":%llu,\"evaluations\":%u}\n"),Frames.Num(),Frames[Frames.Num()/2],P95,Frames.Last(),MaxInputMs,MaxOffset,Handled,Indicators,M.Starts-Initial.Starts,Editor->Evaluation.Started-Evaluations);FFileHelper::SaveStringToFile(JSON,*(Dir/TEXT("properties.json")));Test->AddInfo(JSON);return Close();
        }
    };
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FRun>(this,E,A.Get()));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLBackgroundDesignerTest,"Catalyst.Landform.Background.ResponsiveDesigner",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLBackgroundDesignerTest::RunTest(const FString&)
{
    FString J,Error;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/WatershedContinental.json")));
    auto* Package=CreatePackage(*(TEXT("/Temp/CLBackground_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>(Package,NAME_None,RF_Transactional));if(!A->ImportRecipeJson(J,Error)){AddError(Error);return false;}
    auto E=MakeShared<FCLRecipeToolkit>();E->Open(A.Get(),EToolkitMode::Standalone,nullptr);
    class FReview final:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;TSharedPtr<FCLRecipeToolkit> Editor;TWeakPtr<FCLRecipeToolkit> ClosedEditor;TStrongObjectPtr<UCLPlanetRecipeAsset> Asset;TSharedPtr<const FCLCompiledPlanetRecipe> Previous;FCLPlanetRecipe Before;int32 Step=0,Heartbeats=0;double Started=FPlatformTime::Seconds(),MaxInputMs=0;uint32 InitialTasks=0;bool Captured=false,SawMeasuredProgress=false;
        bool Capture(const TCHAR* Name)
        {
            const auto Tab=Editor->GetTabManager()->FindExistingLiveTab(FCLRecipeToolkit::SectionTab);if(!Tab)return false;TArray<FColor> Pixels;FIntVector Size;if(!FSlateApplication::Get().TakeScreenshot(Tab->GetContent(),Pixels,Size))return false;
            const FString Dir=FPaths::ProjectSavedDir()/TEXT("Verification/background");IFileManager::Get().MakeDirectory(*Dir,true);TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);return FFileHelper::SaveArrayToFile(PNG,*(Dir/(FString(Name)+TEXT(".png"))));
        }
    public:
        FReview(FAutomationTestBase* T,TSharedRef<FCLRecipeToolkit> E,UCLPlanetRecipeAsset* A):Test(T),Editor(E),Asset(A){}
        bool Update()override
        {
            auto Close=[&]{Asset->GetOutermost()->SetDirtyFlag(false);if(Editor)Editor->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);Editor.Reset();return true;};
            if(Step==5)
            {
                if(FCLRecipeEvaluation::OutstandingTasks()>0&&FPlatformTime::Seconds()-Started<120)return false;
                Test->TestEqual(TEXT("Closed editor leaves no native evaluation task"),FCLRecipeEvaluation::OutstandingTasks(),0);Test->TestFalse(TEXT("Worker does not retain its closed toolkit"),ClosedEditor.IsValid());return true;
            }
            if(FPlatformTime::Seconds()-Started>120){Test->AddError(TEXT("Background editor proof timed out"));return Close();}
            if(Editor->Evaluation.IsBusy()){++Heartbeats;const auto Fraction=Editor->EvaluationFraction();SawMeasuredProgress|=Fraction.IsSet()&&Fraction.GetValue()>0&&Fraction.GetValue()<1;}
            if(Step==0)
            {
                if(Editor->EvaluationUpdating())return false;if(!Editor->AcceptedSurfacePlan){Test->AddError(Editor->Status);return Close();}Previous=Editor->AcceptedSurfacePlan;Before=Asset->Recipe;InitialTasks=Editor->Evaluation.Started;
                Editor->BeginWatershedDrag();for(int32 I=0;I<12;++I){const double T=FPlatformTime::Seconds();Editor->PendingWatershed=.1+.8*I/11;Editor->PumpWatershed(true);MaxInputMs=FMath::Max(MaxInputMs,(FPlatformTime::Seconds()-T)*1000);}Editor->EndWatershedDrag();
                Test->TestTrue(TEXT("Input returns with evaluation still in flight"),Editor->Evaluation.IsBusy());Test->TestFalse(TEXT("Pending evaluation blocks terrain picking"),Editor->Globe->CanPick());Test->TestTrue(TEXT("Previous terrain stays accepted while deriving newer settings"),Editor->AcceptedSurfacePlan==Previous);Step=1;return false;
            }
            if(Step==1)
            {
                if(Editor->Evaluation.IsBusy()){if(!Captured&&Editor->EvaluationFraction().IsSet()&&Editor->EvaluationFraction().GetValue()>0&&Editor->EvaluationFraction().GetValue()<1){Test->TestTrue(TEXT("Preview progress is visible while evaluation runs"),!Editor->EvaluationCaption().IsEmpty());Captured=Capture(TEXT("updating"));}return false;}
                Test->TestTrue(TEXT("Only latest requested recipe is published"),Editor->AcceptedSurfacePlan&&Editor->AcceptedSurfacePlan->SourceRecipe&&FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Asset->Recipe,Editor->AcceptedSurfacePlan->SourceRecipe.Get(),0));
                Test->TestTrue(TEXT("Actual derived work runs off the UI thread"),Editor->LastEvaluationWasWorker);Test->TestTrue(TEXT("Pending work is coalesced, not one task per setting"),Editor->Evaluation.Started-InitialTasks<12);Test->TestTrue(TEXT("Superseded completion is discarded"),Editor->Evaluation.Discarded>0);
                Test->TestTrue(TEXT("Measured phase progress was reported"),SawMeasuredProgress);Test->TestTrue(TEXT("UI heartbeat continues during evaluation"),Heartbeats>1);Test->TestTrue(TEXT("Warm setting input stays below 100 ms in this bounded profile"),MaxInputMs<100);
                Previous=Editor->AcceptedSurfacePlan;Editor->BeginWatershedDrag();Editor->PendingWatershed=.25;Editor->PumpWatershed(true);Editor->PendingWatershed=.75;Editor->PumpWatershed(true);Editor->EndWatershedDrag();GEditor->UndoTransaction();Step=2;return false;
            }
            if(Step==2)
            {
                if(Editor->EvaluationUpdating())return false;Test->TestTrue(TEXT("Undo during pending work fences all superseded results"),Editor->AcceptedSurfacePlan&&FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Asset->Recipe,Editor->AcceptedSurfacePlan->SourceRecipe.Get(),0));
                Previous=Editor->AcceptedSurfacePlan;auto Bad=Asset->Recipe;Bad.Watershed.bEnabled=false;Bad.Rock.bEnabled=false;Bad.Continents.bEnabled=false;Bad.Tectonics.bEnabled=false;Bad.NoiseLayers.Reset();Bad.BaseStructure.Reset();Bad.TerrainRegions.Reset();Bad.RegionGeneration=FCLRegionGenerationSettings();Bad.ElevationDistribution.bEnabled=true;
                Test->TestTrue(TEXT("Native authoring accepts structurally valid input for deferred evaluation"),Editor->CommitSurfaceRecipe(Bad));Step=3;return false;
            }
            if(Step==3)
            {
                if(Editor->Evaluation.IsBusy())return false;Test->TestTrue(TEXT("Derived failure is reported without destroying accepted terrain"),Editor->bPreviewInvalid&&!Editor->InspectorError.IsEmpty()&&Editor->AcceptedSurfacePlan==Previous);Test->TestTrue(TEXT("Failed authoring state remains editable"),Asset->Recipe.ElevationDistribution.bEnabled&&Asset->Recipe.NoiseLayers.IsEmpty());Test->TestFalse(TEXT("Derived failure blocks picking stale terrain"),Editor->Globe->CanPick());Capture(TEXT("failed-retained"));GEditor->UndoTransaction();Step=4;return false;
            }
            if(Step==4)
            {
                if(Editor->EvaluationUpdating())return false;Test->TestFalse(TEXT("Undo recovers the accepted native preview"),Editor->bPreviewInvalid);Capture(TEXT("ready"));
                FString Metrics=FString::Printf(TEXT("{\"heartbeats\":%d,\"maxInputMs\":%.6f,\"maxPreparationMs\":%.6f,\"tasksStarted\":%u,\"discarded\":%u,\"workerThread\":%s,\"progressCaptured\":%s}\n"),Heartbeats,MaxInputMs,Editor->Evaluation.MaximumPreparationMs,Editor->Evaluation.Started,Editor->Evaluation.Discarded,Editor->LastEvaluationWasWorker?TEXT("true"):TEXT("false"),Captured?TEXT("true"):TEXT("false"));
                const FString Dir=FPaths::ProjectSavedDir()/TEXT("Verification/background");IFileManager::Get().MakeDirectory(*Dir,true);FFileHelper::SaveStringToFile(Metrics,*(Dir/TEXT("responsive.json")));
                auto L=Asset->Recipe.Watershed;L.ApplyProgression(.4);Editor->CommitWatershed(L);Test->TestTrue(TEXT("Closing test has work in flight"),Editor->Evaluation.IsBusy());ClosedEditor=Editor;Close();Step=5;return false;
            }
            return false;
        }
    };
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FReview>(this,E,A.Get()));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLAuthoringLayoutTest,"Catalyst.Landform.Designer.AuthoringBoundaries",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLAuthoringLayoutTest::RunTest(const FString&)
{
    FString J,Error;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/WatershedContinental.json")));
    auto* Package=CreatePackage(*(TEXT("/Temp/CLAuthoringLayout_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>(Package,NAME_None,RF_Transactional));if(!A->ImportRecipeJson(J,Error)){AddError(Error);return false;}
    const auto Original=A->Recipe;auto E=MakeShared<FCLRecipeToolkit>();E->Open(A.Get(),EToolkitMode::Standalone,nullptr);E->AwaitEvaluationForTesting();E->Globe->Cancel();E->PlanetInspectors[1]->SetRootExpansionStates(false,false);
    TestTrue(TEXT("Opening the corrected editor never rewrites the saved planet"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Original,&A->Recipe,0));
    for(int32 I=0;I<4;++I)
    {
        const auto Before=A->Recipe;auto Expected=Before;
        if(I==0)Expected.Continents.bEnabled=!Expected.Continents.bEnabled;else if(I==1)Expected.Tectonics.bEnabled=!Expected.Tectonics.bEnabled;else if(I==2)Expected.Rock.bEnabled=!Expected.Rock.bEnabled;else Expected.Watershed.bEnabled=!Expected.Watershed.bEnabled;
        E->LayerVisibility[I]->SimulateClick();TestTrue(TEXT("Eye toggles only its layer's existing enable state"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Expected,&A->Recipe,0));
        GEditor->UndoTransaction();TestTrue(TEXT("One Undo restores an eye toggle"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Before,&A->Recipe,0));
    }
    TestFalse(TEXT("Layer indicators start off"),E->bWatershedOverlay||E->Globe->IsWatershedOverlayEnabled());
    for(int32 I=0;I<5;++I)TestEqual(TEXT("Only layers with indicators offer an enabled toggle"),E->LayerIndicators[I]->IsEnabled(),I>=3);
    const auto BeforeIndicators=A->Recipe;const bool WasDirty=A->GetOutermost()->IsDirty();const uint32 Evaluations=E->Evaluation.Started;
    const auto* Native=E->Globe->NativeForTesting();const auto NativeStarts=Native?Native->Metrics.Starts:0;
    for(bool Enabled:{true,false,true})
    {
        E->LayerIndicators[3]->ToggleCheckedState();
        TestEqual(TEXT("Indicator icon reflects its display preference"),E->LayerIndicators[3]->IsChecked(),Enabled);
        TestEqual(TEXT("Indicator toggle reaches the native globe renderer"),E->Globe->IsWatershedOverlayEnabled(),Enabled);
    }
    TestTrue(TEXT("Indicators never edit the recipe"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&BeforeIndicators,&A->Recipe,0));
    TestEqual(TEXT("Indicators never dirty the asset"),A->GetOutermost()->IsDirty(),WasDirty);
    TestEqual(TEXT("Indicators never evaluate terrain"),E->Evaluation.Started,Evaluations);
    TestTrue(TEXT("Indicators never create a native generation input"),E->Globe->NativeForTesting()==Native);
    if(Native)TestEqual(TEXT("Indicators never start native generation"),Native->Metrics.Starts,NativeStarts);
    E->LayerVisibility[3]->SimulateClick();GEditor->UndoTransaction();
    TestTrue(TEXT("Layer visibility and Undo preserve the independent indicator preference"),E->LayerIndicators[3]->IsChecked()&&E->Globe->IsWatershedOverlayEnabled());
    E->LayerIndicators[3]->ToggleCheckedState();
    TestFalse(TEXT("Watershed cannot reseed upstream terrain while it has no independent variation"),E->WatershedVariation->IsEnabled());

    class FReview final:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;TSharedRef<FCLRecipeToolkit> Editor;TStrongObjectPtr<UCLPlanetRecipeAsset> Asset;int32 Step=0;double Started=0;
        struct FWidgets {FString Text;int32 Sliders=0,Details=0;TArray<TSharedPtr<STextBlock>> Numbers;TArray<TSharedPtr<SButton>> Advanced,Categories;};
        static void Inspect(TSharedRef<SWidget> W,FWidgets& Out)
        {
            if(!W->GetVisibility().IsVisible())return;const auto Type=W->GetTypeAsString();
            if(Type==TEXT("STextBlock")){const auto Text=StaticCastSharedRef<STextBlock>(W);const auto Value=Text->GetText().ToString();Out.Text+=Value+TEXT(";");if(Value.IsNumeric())Out.Numbers.Add(Text);}
            if(Type==TEXT("SSlider"))++Out.Sliders;if(Type==TEXT("SDetailsView"))++Out.Details;
            if(Type==TEXT("SAdvancedDropdownRow")||Type==TEXT("SDetailCategoryTableRow"))
            {
                TFunction<TSharedPtr<SButton>(TSharedRef<SWidget>)> Find;Find=[&](TSharedRef<SWidget> X)->TSharedPtr<SButton>{if(X->GetTypeAsString()==TEXT("SButton"))return StaticCastSharedRef<SButton>(X);auto* C=X->GetChildren();for(int32 I=0;I<C->Num();++I)if(auto B=Find(C->GetChildAt(I)))return B;return nullptr;};if(auto B=Find(W)){if(Type==TEXT("SAdvancedDropdownRow"))Out.Advanced.Add(B);else Out.Categories.Add(B);}
            }
            auto* C=W->GetChildren();for(int32 I=0;I<C->Num();++I)Inspect(C->GetChildAt(I),Out);
        }
    public:
        FReview(FAutomationTestBase* T,TSharedRef<FCLRecipeToolkit> E,UCLPlanetRecipeAsset* A):Test(T),Editor(E),Asset(A){}
        bool Update()override
        {
            const double Now=FPlatformTime::Seconds();auto Close=[&]{Asset->GetOutermost()->SetDirtyFlag(false);Editor->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);return true;};
            if(!Started){Editor->InspectorPage=Step==0||Step>=4?0:Step==3?4:1;Started=Now;return false;}if(Now-Started<.6)return false;
            const auto View=Editor->PlanetInspectors[Editor->InspectorPage];FWidgets W;Inspect(View.ToSharedRef(),W);TSharedPtr<SButton> Toggle;
            if(Step==0)
            {
                Test->TestTrue(TEXT("Planet owns Surface Distribution"),W.Text.Contains(TEXT("Surface Distribution")));Test->TestEqual(TEXT("Planet has one distribution knob"),W.Sliders,1);
                for(const TCHAR* Forbidden:{TEXT("Watershed"),TEXT("Terrain detail"),TEXT("Target"),TEXT("Area below height"),TEXT("Low end area"),TEXT("High end area")})Test->TestFalse(FString(TEXT("Collapsed Planet excludes "))+Forbidden,W.Text.Contains(FString(TEXT(";"))+Forbidden+TEXT(";"))||W.Text.StartsWith(FString(Forbidden)+TEXT(";")));
            }
            if(Step==1)
            {
                Test->TestFalse(TEXT("Character readouts have no percent sign"),W.Text.Contains(TEXT("%")));
                Test->TestTrue(TEXT("Tectonics appears before Continents"),W.Text.Find(TEXT("Tectonics"))<W.Text.Find(TEXT("Continents")));
                Test->TestEqual(TEXT("Five plain numeric readouts"),W.Numbers.Num(),5);
                for(const auto& N:W.Numbers){Test->TestTrue(TEXT("Readouts share the native property font"),N->GetFont()==FAppStyle::GetFontStyle("PropertyWindow.NormalFont"));Test->TestTrue(TEXT("Readouts share the same width"),FMath::IsNearlyEqual(N->GetCachedGeometry().GetLocalSize().X,36.,1.));}
                Test->TestEqual(TEXT("Geology has exactly five Character knobs"),W.Sliders,5);Test->TestEqual(TEXT("Geology has no Advanced dropdowns"),W.Advanced.Num(),0);Test->TestEqual(TEXT("Each layer uses one native category chevron"),W.Categories.Num(),5);
                for(const TCHAR* Name:{TEXT("Continents"),TEXT("Tectonics"),TEXT("Rock"),TEXT("Watershed"),TEXT("Caves")})Test->TestTrue(FString(TEXT("Geology owns "))+Name,W.Text.Contains(Name));
                for(const TCHAR* Forbidden:{TEXT("Model"),TEXT("Variation"),TEXT("Character"),TEXT("Advanced"),TEXT("Drainage"),TEXT("Focus largest channel"),TEXT("Analysis"),TEXT("Bed thickness distribution")})Test->TestFalse(FString(TEXT("Collapsed geology excludes "))+Forbidden,W.Text.Contains(Forbidden));
                if(!Test->TestEqual(TEXT("The native watershed chevron can be clicked"),W.Categories.Num(),5))return Close();Toggle=W.Categories[3];
                TSharedPtr<SSlider> Sliders[]={Editor->ContinentalSlider,Editor->TectonicSlider,Editor->RockSlider,Editor->WatershedSlider,Editor->CaveSlider};
                TSharedPtr<SButton> Seeds[]={Editor->ContinentalVariation,Editor->TectonicVariation,Editor->RockVariation,Editor->WatershedVariation,Editor->CaveVariation};
                for(int32 I=0;I<5;++I){auto Middle=[](TSharedPtr<SWidget> X){const auto G=X->GetCachedGeometry();return G.GetAbsolutePosition().Y+G.GetAbsoluteSize().Y*.5;};const double Y=Middle(Sliders[I]);Test->TestTrue(TEXT("Eye, indicators, slider and reseed share the same main row"),FMath::Abs(Y-Middle(Editor->LayerVisibility[I]))<2&&FMath::Abs(Y-Middle(Editor->LayerIndicators[I]))<2&&FMath::Abs(Y-Middle(Seeds[I]))<2);
                    Test->TestTrue(TEXT("Indicator icon is immediately after the eye"),Editor->LayerIndicators[I]->GetCachedGeometry().GetAbsolutePosition().X>Editor->LayerVisibility[I]->GetCachedGeometry().GetAbsolutePosition().X);}
            }
            if(Step==2)
            {
                FString Properties;for(const auto& P:View->GetPropertiesInOrderDisplayed())Properties+=P.ToString()+TEXT(";");
                Test->TestTrue(TEXT("Opening Watershed exposes its direct physical properties"),Properties.Contains(TEXT("IncisionMeters"))&&Properties.Contains(TEXT("ValleyWidthMeters")));
                Test->TestEqual(TEXT("Opening a layer adds no nested Details container"),W.Details,1);
                Test->TestEqual(TEXT("Expanded geology still has no Advanced dropdown"),W.Advanced.Num(),0);if(W.Categories.Num()==5)Toggle=W.Categories[3];
            }
            if(Step==3){Test->TestFalse(TEXT("Preview has no duplicate drainage checkbox"),W.Text.Contains(TEXT("Drainage overlay")));Test->TestTrue(TEXT("Preview retains channel focus"),W.Text.Contains(TEXT("Focus largest channel")));Test->TestTrue(TEXT("Schema-22 preview measurement is available"),Editor->MeasuredElevation.Num()>100);Test->TestFalse(TEXT("Inspection overlays are opt-in"),Editor->bWatershedOverlay);}
            if(Step==4){if(!Test->TestEqual(TEXT("Planet has native body and surface Advanced dropdowns"),W.Advanced.Num(),2))return Close();Toggle=W.Advanced[1];}
            if(Step==5)
            {
                FString Properties;for(const auto& P:View->GetPropertiesInOrderDisplayed())Properties+=P.ToString()+TEXT(";");
                Test->TestTrue(TEXT("Surface tail percentages live directly under Advanced"),Properties.Contains(TEXT("LowEndAreaPercent"))&&Properties.Contains(TEXT("HighEndAreaPercent")));
                Test->TestEqual(TEXT("Surface Advanced adds no Details container"),W.Details,1);Test->TestFalse(TEXT("Surface Advanced never adds a Target selector"),W.Text.Contains(TEXT("Target")));
                if(W.Advanced.Num()==2)Toggle=W.Advanced[1];
            }
            TArray<FColor> Pixels;FIntVector Size;Test->TestTrue(TEXT("Corrected native authoring layout renders"),FSlateApplication::Get().TakeScreenshot(View.ToSharedRef(),Pixels,Size));
            if(!Pixels.IsEmpty()){const FString Dir=FPaths::ProjectSavedDir()/TEXT("Verification/authoring");IFileManager::Get().MakeDirectory(*Dir,true);TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Dir/FString::Printf(TEXT("page-%d.png"),Step)));}
            if(Toggle)Toggle->SimulateClick();
            if(++Step==6)return Close();Started=0;return false;
        }
    };
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FReview>(this,E,A.Get()));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLWatershedDesignerTest,"Catalyst.Landform.Watershed.DesignerControlsAndUndo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLWatershedDesignerTest::RunTest(const FString&)
{
    FString J,Error;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/Watershed.json")));
    auto* Package=CreatePackage(*(TEXT("/Temp/CLWatershedDesigner_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>(Package,NAME_None,RF_Transactional));if(!A->ImportRecipeJson(J,Error)){AddError(Error);return false;}
    auto E=MakeShared<FCLRecipeToolkit>();E->Open(A.Get(),EToolkitMode::Standalone,nullptr);E->AwaitEvaluationForTesting();
    TestTrue(TEXT("Watershed joins native Geology with direct Advanced properties"),E->InspectorPage==1&&E->WatershedSlider&&!E->WatershedProperties.IsEmpty());
    const auto Before=A->Recipe;E->BeginWatershedDrag();E->PendingWatershed=.2;E->PumpWatershed(true);E->PendingWatershed=.8;E->PumpWatershed(true);E->EndWatershedDrag();const auto After=A->Recipe;
    TestEqual(TEXT("The held gesture publishes the final character"),A->Recipe.Watershed.Character,.8);GEditor->UndoTransaction();TestTrue(TEXT("One Undo restores the complete watershed gesture"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Before,&A->Recipe,0));GEditor->RedoTransaction();TestTrue(TEXT("Redo restores the complete accepted watershed recipe"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&After,&A->Recipe,0));
    const auto H=*E->WatershedProperties.FindByPredicate([](const auto& V){return V->GetProperty()->GetFName()==TEXT("IncisionMeters");});TestEqual(TEXT("Native direct incision edit succeeds"),H->SetValue(5.),FPropertyAccess::Success);TestEqual(TEXT("Advanced edits reach the canonical recipe"),A->Recipe.Watershed.IncisionMeters,5.);
    GEditor->UndoTransaction();TestEqual(TEXT("One Undo restores the direct edit"),A->Recipe.Watershed.IncisionMeters,After.Watershed.IncisionMeters);
    auto Bad=A->Recipe.Watershed;Bad.MaximumCells=1026;TestFalse(TEXT("Rejected work budget retains the accepted asset"),E->CommitWatershed(Bad));TestEqual(TEXT("Budget failure leaves the previous cell limit"),A->Recipe.Watershed.MaximumCells,After.Watershed.MaximumCells);
    auto Elevation=A->Recipe.ElevationDistribution;Elevation.ApplyProgression(.55);TestTrue(TEXT("Elevation edits recompile downstream watersheds"),E->CommitElevation(Elevation));TestEqual(TEXT("Elevation edits preserve schema 22"),A->Recipe.SchemaVersion,22);E->AwaitEvaluationForTesting();TestTrue(TEXT("The accepted graph is retained after upstream edits"),E->AcceptedSurfacePlan&&E->AcceptedSurfacePlan->Watershed);
    class FFrame final:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;TSharedRef<FCLRecipeToolkit> Editor;TStrongObjectPtr<UCLPlanetRecipeAsset> Asset;double Start=FPlatformTime::Seconds();
    public:
        FFrame(FAutomationTestBase* T,TSharedRef<FCLRecipeToolkit> E,UCLPlanetRecipeAsset* A):Test(T),Editor(E),Asset(A){}
        bool Update()override
        {
            if(FPlatformTime::Seconds()-Start<1)return false;
            TArray<FColor> Pixels;FIntVector Size;Test->TestTrue(TEXT("Native watershed inspector renders"),FSlateApplication::Get().TakeScreenshot(Editor->PlanetInspectors[1].ToSharedRef(),Pixels,Size));
            if(!Pixels.IsEmpty()){const FString Dir=FPaths::ProjectSavedDir()/TEXT("Verification/watershed");IFileManager::Get().MakeDirectory(*Dir,true);TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Dir/TEXT("designer.png")));}
            Asset->GetOutermost()->SetDirtyFlag(false);Editor->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);return true;
        }
    };
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFrame>(this,E,A.Get()));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLWatershedRenderedTest,"Catalyst.Landform.Watershed.NativeRenderedReview",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLWatershedRenderedTest::RunTest(const FString&)
{
    const auto Globe=SNew(SCLPlanetPreview);const auto Section=SNew(SCLSectionViewport);const auto Content=SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[Globe]+SHorizontalBox::Slot().FillWidth(1)[Section];const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Native watershed globe and section"))).ClientSize(FVector2D(1400,700))[Content];FSlateApplication::Get().AddWindow(Window,true);Window->SlatePrepass();
    class FReview final:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;TSharedRef<SCLPlanetPreview> Globe;TSharedRef<SCLSectionViewport> Section;TSharedRef<SWindow> Window;TSharedRef<SWidget> Content;int32 Index=-1;double Started=FPlatformTime::Seconds(),Stable=0;
    public:
        FReview(FAutomationTestBase* T,TSharedRef<SCLPlanetPreview> G,TSharedRef<SCLSectionViewport> S,TSharedRef<SWindow> W,TSharedRef<SWidget> C):Test(T),Globe(G),Section(S),Window(W),Content(C){}
        bool Update()override
        {
            auto Close=[&]{FSlateApplication::Get().RequestDestroyWindow(Window);return true;};const double Now=FPlatformTime::Seconds();Section->PumpBuild();
            if(Now-Started>180){Test->AddError(Globe->NativePreviewStatus());return Close();}
            if(Index>=0)
            {
                if(Globe->IsUpdating()||Section->IsUpdating()){Stable=0;return false;}if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling()){GShaderCompilingManager->FinishAllCompilation();Stable=0;return false;}if(!Stable){Stable=Now;return false;}if(Now-Stable<.75)return false;
                Test->TestTrue(TEXT("Watershed review retains native VPP producers in both views"),Globe->PublishedBuilds()>0&&Section->AppliedBuilds>0&&Section->NativePreview->Published().Triangles.Num()>0);FString E;Test->TestTrue(TEXT("Watershed preview retains canonical material coordinates"),Section->NativePreview->CheckMaterialCoordinates(E));
                TArray<FColor> Pixels;FIntVector Size;Test->TestTrue(TEXT("Native watershed frame renders"),FSlateApplication::Get().TakeScreenshot(Content,Pixels,Size));if(!Pixels.IsEmpty()){const FString Dir=FPaths::ProjectSavedDir()/TEXT("Verification/watershed");IFileManager::Get().MakeDirectory(*Dir,true);TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Dir/FString::Printf(TEXT("native-%d.png"),Index)));}
            }
            if(++Index==6)return Close();FString J,E;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/(Index>=4?TEXT("Examples/WatershedContinental.json"):TEXT("Examples/Watershed.json"))));
            TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());if(!A->ImportRecipeJson(J,E)){Test->AddError(E);return Close();}auto R=A->Recipe;R.Watershed.bEnabled=Index!=0;R.Watershed.ApplyProgression(Index==1?0:Index==3||Index==5?1:.5);const auto P=FCLPlanetCompiler::Compile(R,E);if(!P){Test->AddError(E);return Close();}
            Globe->Rebuild(P,FCLWatershedDebugGeometry::Build(*P));Globe->Frame();Globe->SetWatershedOverlay(Index==1||Index==2||Index==4);if(Index>=2&&Index!=4)Test->TestTrue(TEXT("A generated graded reach can be focused for inspection/walking"),Globe->FocusWatershed());Section->Rebuild(P);Started=Now;Stable=0;return false;
        }
    };
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FReview>(this,Globe,Section,Window,Content));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSurfaceDesignerTest,"Catalyst.Landform.SurfaceScale.DesignerControlsAndUndo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLSurfaceDesignerTest::RunTest(const FString&)
{
    FString Json,Error;FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/SurfaceScale10.json")));
    const auto Package=CreatePackage(*(TEXT("/Temp/CLSurfaceDesigner_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>(Package,NAME_None,RF_Transactional));if(!A->ImportRecipeJson(Json,Error)){AddError(Error);return false;}
    const auto Original=A->Recipe;const auto E=MakeShared<FCLRecipeToolkit>();E->Open(A.Get(),EToolkitMode::Standalone,nullptr);E->AwaitEvaluationForTesting();E->InspectorPage=0;E->Globe->Cancel();
    TestTrue(TEXT("Surface distribution is available on the native Planet inspector"),E->ElevationSlider.IsValid());
    TestTrue(TEXT("Opening preserves the saved recipe"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Original,&A->Recipe,0));
    E->BeginSurfaceDrag();E->PendingElevation=.25;E->PumpSurface(true);E->PendingElevation=.75;E->PumpSurface(true);E->EndSurfaceDrag();const auto After=A->Recipe;
    TestEqual(TEXT("Held elevation gesture reaches the latest character"),After.ElevationDistribution.Character,.75);
    GEditor->UndoTransaction();TestTrue(TEXT("One Undo restores the complete elevation gesture"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Original,&A->Recipe,0));GEditor->RedoTransaction();
    const auto HighHandle=*E->ElevationAdvancedProperties.FindByPredicate([](const auto& H){return H->GetProperty()->GetFName()==TEXT("HighEndAreaPercent");});
    TestEqual(TEXT("Native percentage edit succeeds"),HighHandle->SetValue(4.),FPropertyAccess::Success);TestEqual(TEXT("Four percent remains independent of Character"),A->Recipe.ElevationDistribution.HighEndAreaPercent,4.);TestEqual(TEXT("Tail editing preserves Character"),A->Recipe.ElevationDistribution.Character,.75);
    GEditor->UndoTransaction();TestEqual(TEXT("One Undo restores only the tail edit"),A->Recipe.ElevationDistribution.HighEndAreaPercent,1.);
    auto& Draft=*reinterpret_cast<FCLSurfaceDistributionSettings*>(E->ElevationDraft->GetStructMemory());Draft.HighEndAreaPercent=0;FPropertyChangedEvent Bad(FCLSurfaceDistributionSettings::StaticStruct()->FindPropertyByName(TEXT("HighEndAreaPercent")),EPropertyChangeType::ValueSet);E->PlanetInspectors[0]->OnFinishedChangingProperties().Broadcast(Bad);
    TestEqual(TEXT("Invalid native draft retains the accepted area target"),A->Recipe.ElevationDistribution.HighEndAreaPercent,1.);TestEqual(TEXT("Invalid draft rolls back"),Draft.HighEndAreaPercent,1.);
    E->AwaitEvaluationForTesting();TestTrue(TEXT("Measured curve remains available for Preview inspection"),E->MeasuredElevation.Num()>100&&E->ElevationReadout.Contains(TEXT("Measured area")));
    auto Regional=A->Recipe;FCLTerrainRegion Region;Region.Id=FGuid(7,8,9,10);Region.Name=TEXT("Disposable elevation region");Region.bSphericalMask=true;Region.SphericalMask.Shape=ECLTerrainRegionShape::HalfPlane;Region.SphericalMask.TransitionMeters=120;Region.TerrainTypeId=FGuid(8,9,10,11);Region.TerrainTypeName=TEXT("Metric snapshot");Region.NoiseLayers=Regional.NoiseLayers;Region.SurfaceStack=Regional.SurfaceStack;Region.ElevationDistribution.bEnabled=true;Region.ElevationDistribution.HighEndAreaPercent=3;Regional.TerrainRegions={Region};
    TestTrue(TEXT("Regional overrides remain in their owning region"),E->CommitSurfaceRecipe(Regional));auto Global=E->ElevationSettings();Global.HighEndAreaPercent=4;TestTrue(TEXT("Planet surface distribution remains whole-planet scoped"),E->CommitElevation(Global));TestEqual(TEXT("Planet edits leave regional distribution unchanged"),A->Recipe.TerrainRegions[0].ElevationDistribution.HighEndAreaPercent,3.);
    E->PlanetInspectors[2]->ForceRefresh(); // the array was authored after opening this native view
    const auto RegionHandle=E->RegionProperties->GetChildHandle(0);
    const auto DistributionHandle=RegionHandle?RegionHandle->GetChildHandle(TEXT("ElevationDistribution")):nullptr;
    const auto RegionTail=DistributionHandle?DistributionHandle->GetChildHandle(TEXT("HighEndAreaPercent")):nullptr;
    if(!TestTrue(TEXT("Native region rows are refreshed before editing"),RegionTail.IsValid())){A->GetOutermost()->SetDirtyFlag(false);E->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);return false;}
    TestEqual(TEXT("Regional distribution is edited through its actual native owner"),RegionTail->SetValue(5.),FPropertyAccess::Success);TestEqual(TEXT("Region edit reaches only the region"),A->Recipe.TerrainRegions[0].ElevationDistribution.HighEndAreaPercent,5.);TestEqual(TEXT("Region edit retains the planet distribution"),A->Recipe.ElevationDistribution.HighEndAreaPercent,4.);GEditor->UndoTransaction();TestEqual(TEXT("One Undo restores the regional edit"),A->Recipe.TerrainRegions[0].ElevationDistribution.HighEndAreaPercent,3.);
    E->Globe->Cancel();
    class FFrame final:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;TSharedRef<FCLRecipeToolkit> Editor;TStrongObjectPtr<UCLPlanetRecipeAsset> Asset;double Started=FPlatformTime::Seconds();
    public:
        FFrame(FAutomationTestBase* T,TSharedRef<FCLRecipeToolkit> E,UCLPlanetRecipeAsset* A):Test(T),Editor(E),Asset(A){}
        bool Update()override
        {
            if(FPlatformTime::Seconds()-Started<.75)return false;
            TArray<FColor> Pixels;FIntVector Size;Test->TestTrue(TEXT("Native Planet inspector renders Surface Distribution without diagnostics"),FSlateApplication::Get().TakeScreenshot(Editor->PlanetInspectors[0].ToSharedRef(),Pixels,Size));
            if(!Pixels.IsEmpty()){const FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/surface-scale");IFileManager::Get().MakeDirectory(*Folder,true);TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Folder/TEXT("designer.png")));}
            Asset->GetOutermost()->SetDirtyFlag(false);Editor->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);return true;
        }
    };
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFrame>(this,E,A.Get()));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSurfaceRenderedTest,"Catalyst.Landform.SurfaceScale.NativeRenderedReview",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLSurfaceRenderedTest::RunTest(const FString&)
{
    const auto Globe=SNew(SCLPlanetPreview);const auto Section=SNew(SCLSectionViewport);const auto Content=SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[Globe]+SHorizontalBox::Slot().FillWidth(1)[Section];const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Physical scale and elevation · native VPP"))).ClientSize(FVector2D(1400,700))[Content];FSlateApplication::Get().AddWindow(Window,true);Window->SlatePrepass();
    class FReview final:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;TSharedRef<SCLPlanetPreview> Globe;TSharedRef<SCLSectionViewport> Section;TSharedRef<SWindow> Window;TSharedRef<SWidget> Content;int32 Index=-1;double Started=FPlatformTime::Seconds(),Stable=0;
    public:
        FReview(FAutomationTestBase* T,TSharedRef<SCLPlanetPreview> G,TSharedRef<SCLSectionViewport> S,TSharedRef<SWindow> W,TSharedRef<SWidget> C):Test(T),Globe(G),Section(S),Window(W),Content(C){}
        bool Update()override
        {
            auto Close=[&]{FSlateApplication::Get().RequestDestroyWindow(Window);return true;};const double Now=FPlatformTime::Seconds();Section->PumpBuild();if(Now-Started>180){Test->AddError(Globe->NativePreviewStatus());return Close();}
            if(Index>=0)
            {
                if(Globe->IsUpdating()||Section->IsUpdating()){Stable=0;return false;}if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling()){GShaderCompilingManager->FinishAllCompilation();Stable=0;return false;}if(!Stable){Stable=Now;return false;}if(Now-Stable<.75)return false;
                Test->TestTrue(TEXT("Scale/elevation review uses native VPP geometry in both views"),Globe->PublishedBuilds()>0&&Section->AppliedBuilds>0&&Section->NativePreview->Published().Triangles.Num()>0);FString Error;Test->TestTrue(TEXT("Shaping retains canonical native material coordinates"),Section->NativePreview->CheckMaterialCoordinates(Error));
                TArray<FColor> Pixels;FIntVector Size;Test->TestTrue(TEXT("Native scale review frame renders"),FSlateApplication::Get().TakeScreenshot(Content,Pixels,Size));if(!Pixels.IsEmpty()){const FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/surface-scale");IFileManager::Get().MakeDirectory(*Folder,true);TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Folder/FString::Printf(TEXT("native-%d.png"),Index)));}
            }
            if(++Index==6)return Close();const int32 Area=Index<3?(Index==0?5:Index==1?10:20):10;FString Json,Error;
            FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/FString::Printf(TEXT("Examples/SurfaceScale%d.json"),Area)));
            TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());if(!A->ImportRecipeJson(Json,Error)){Test->AddError(Error);return Close();}auto R=A->Recipe;R.SurfaceStack=FCLSurfaceStack::MakeDefault();
            R.ElevationDistribution.ApplyProgression(Index==3?0.:Index==4?1.:.5);
            FCLNoiseLayer Stamps;Stamps.Name=TEXT("Metric native mountain stamps");Stamps.Operation=ECLTerrainOperation::StampDistribution;Stamps.Distribution.DensityPerSquareKilometer=20;Stamps.Distribution.SizeMode=ECLStampSizeMode::Absolute;Stamps.Distribution.MinimumSizeMeters=80;Stamps.Distribution.MaximumSizeMeters=120;R.NoiseLayers.Add(Stamps);
            if(Index==5)
            {
                FCLTerrainRegion Region;Region.Id=FGuid(1,2,3,4);Region.Name=TEXT("Upland region");Region.bSphericalMask=true;Region.SphericalMask.Shape=ECLTerrainRegionShape::HalfPlane;Region.SphericalMask.Axis=FVector::XAxisVector;Region.SphericalMask.TransitionMeters=120;Region.TerrainTypeId=FGuid(2,3,4,5);Region.TerrainTypeName=TEXT("Metric regional snapshot");Region.NoiseLayers={R.NoiseLayers[0]};Region.SurfaceStack=R.SurfaceStack;Region.ElevationDistribution.bEnabled=true;Region.ElevationDistribution.ApplyProgression(1);R.TerrainRegions={Region};
            }
            const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){Test->AddError(Error);return Close();}Globe->Rebuild(P);Section->Rebuild(P);Started=Now;Stable=0;return false;
        }
    };
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FReview>(this,Globe,Section,Window,Content));return true;
}
}
#endif


#if WITH_DEV_AUTOMATION_TESTS
namespace {
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLProvinceEditorTest,"Catalyst.Landform.Provinces.NativeDesignerAndNavigation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLProvinceEditorTest::RunTest(const FString&)
{
    auto* Review=LoadObject<UCLPlanetRecipeAsset>(nullptr,TEXT("/Game/LandformDesigner/Provinces/DA_ProvincePlacement"));if(!TestNotNull(TEXT("Owned province review planet exists"),Review))return false;TestEqual(TEXT("Review retains all native source links"),Review->PaletteSources.Num(),17);
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>(CreatePackage(*(TEXT("/Temp/CLProvinceEditor_")+FGuid::NewGuid().ToString(EGuidFormats::Digits))),NAME_None,RF_Transactional));A->Recipe=Review->Recipe;
    auto* Source=LoadObject<UCLTerrainTypeAsset>(nullptr,TEXT("/CatalystLandform/TerrainTypes/Provinces/DA_Province_CarbonatePlatform"));if(!TestNotNull(TEXT("Province source exists"),Source))return false;
    const auto SourceBefore=Source->TerrainType;const bool DirtyBefore=Source->GetOutermost()->IsDirty();FCLTerrainEditing Edit;Edit.Open(Source);FCLPlanetRecipe Preview;FBox2D Bounds;FString Error;
    if(!TestTrue(TEXT("Province source retains its existing isolated native preview"),Edit.PreviewRecipe(Preview,Bounds,Error))){AddError(Error);return false;}
    TestTrue(TEXT("Isolated preview preserves original province metadata"),FCLTerrainType::StaticStruct()->CompareScriptStruct(&SourceBefore,&Source->TerrainType,0));TestEqual(TEXT("Isolated preview does not dirty the source"),Source->GetOutermost()->IsDirty(),DirtyBefore);
    const auto Before=A->Recipe;auto E=MakeShared<FCLRecipeToolkit>();E->Open(A.Get(),EToolkitMode::Standalone,nullptr);E->InspectorPage=2;
    class FReview final:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;TSharedRef<FCLRecipeToolkit> Editor;TStrongObjectPtr<UCLPlanetRecipeAsset> Asset;FCLPlanetRecipe Before;
        int32 Frame=-1;double Start=FPlatformTime::Seconds(),Last=0;TArray<double> Times;uint32 Evaluations=0,Owner=0;uint64 Copies=0;
    public:
        FReview(FAutomationTestBase* T,TSharedRef<FCLRecipeToolkit> E,UCLPlanetRecipeAsset* A,FCLPlanetRecipe R):Test(T),Editor(E),Asset(A),Before(MoveTemp(R)){}
        bool Update()override
        {
            auto Close=[&]{Asset->GetOutermost()->SetDirtyFlag(false);Editor->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);return true;};const double Now=FPlatformTime::Seconds();
            if(Now-Start>150){Test->AddError(Editor->Status);return Close();}
            if(Frame<0)
            {
                if(Editor->EvaluationUpdating())return false;
                if(!Test->TestTrue(TEXT("Province selection uses the existing background worker"),Editor->LastEvaluationWasWorker))return Close();
                const auto P=Editor->AcceptedSurfacePlan;if(!Test->TestTrue(TEXT("Published review contains the 17 profiles plus neutral base"),P&&P->GeneratedRegions&&P->GeneratedRegions->Palette.Num()==18))return Close();
                Editor->Globe->SetRegionInspection(true);Evaluations=Editor->Evaluation.Started;auto* N=Editor->Globe->NativeForTesting();if(!Test->TestNotNull(TEXT("VPP presents the province review"),N))return Close();if(N->PublishedPlan()!=P||N->IsUpdating())return false;Owner=N->WorldIdentity();Copies=N->Metrics.MeshCopies;Frame=0;Last=Now;return false;
            }
            Times.Add((Now-Last)*1000);Last=Now;
            Editor->Globe->OrbitForTesting(FVector2D(.8,.1));Editor->Globe->Dolly(Frame%2?.01:-.01);
            Test->TestFalse(TEXT("Province inspection navigation has no authoring progress"),Editor->AuthoringUpdating());
            if(++Frame<90)return false;
            Times.RemoveAt(0);Times.Sort();const double P95=Times[FMath::Min(Times.Num()-1,int32(Times.Num()*.95))];Test->TestTrue(TEXT("Bounded province inspection p95 is below 50 ms"),P95<50);
            Test->TestEqual(TEXT("Navigation does not reclassify provinces"),Editor->Evaluation.Started,Evaluations);Test->TestEqual(TEXT("Navigation retains native owner"),Editor->Globe->NativeForTesting()->WorldIdentity(),Owner);Test->TestEqual(TEXT("Navigation extracts no terrain mesh"),Editor->Globe->NativeForTesting()->Metrics.MeshCopies,Copies);
            Test->TestTrue(TEXT("Opening/inspecting province placeholders never edits the recipe"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Before,&Asset->Recipe,0));
            const FString Dir=FPaths::ProjectSavedDir()/TEXT("Verification/provinces");IFileManager::Get().MakeDirectory(*Dir,true);TArray<FColor> Pixels;FIntVector Size;
            Test->TestTrue(TEXT("Native province review renders"),FSlateApplication::Get().TakeScreenshot(Editor->Globe.ToSharedRef(),Pixels,Size));if(!Pixels.IsEmpty()){TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Dir/TEXT("globe.png")));}
            const auto View=Editor->LandformRows.ToSharedRef();FString Text;TFunction<void(TSharedRef<SWidget>)> Collect=[&](TSharedRef<SWidget> W){if(!W->GetVisibility().IsVisible())return;if(W->GetType()==FName(TEXT("STextBlock")))Text+=StaticCastSharedRef<STextBlock>(W)->GetText().ToString()+TEXT(";");auto* Children=W->GetChildren();for(int32 I=0;I<Children->Num();++I)Collect(Children->GetChildAt(I));};Collect(View);
            Test->TestEqual(TEXT("Landforms exposes one row per selected province"),Editor->LandformRows->Controls.Num(),17);Test->TestTrue(TEXT("Landform rows expose whole-planet Solo"),Text.Contains(TEXT("Solo")));
            for(const TCHAR* Forbidden:{TEXT("Base terrain"),TEXT("Region count"),TEXT("Structure guidance"),TEXT("Relief strength"),TEXT("Transform"),TEXT("Solid forms")})Test->TestFalse(FString(TEXT("Default build UI excludes "))+Forbidden,(Text.Contains(FString(TEXT(";"))+Forbidden+TEXT(";"))||Text.StartsWith(FString(Forbidden)+TEXT(";"))));
            Pixels.Reset();Test->TestTrue(TEXT("Simple native Landform build UI renders"),FSlateApplication::Get().TakeScreenshot(View,Pixels,Size));if(!Pixels.IsEmpty()){TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Dir/TEXT("landforms.png")));}
            const auto G=Editor->AcceptedSurfacePlan->GeneratedRegions;FString Counts;int32 Assigned=0;for(int32 I=0;I<G->Palette.Num();++I){int32 Count=0;for(const auto& C:G->Cells)Count+=C.PaletteIndex==I;if(G->Palette[I].Id.IsValid())Assigned+=Count;if(!Counts.IsEmpty())Counts+=TEXT(",");Counts+=FString::Printf(TEXT("\"%s\":%d"),*G->Palette[I].Name,Count);}
            const FString JSON=FString::Printf(TEXT("{\"frames\":90,\"p95Ms\":%.6f,\"assignedRegions\":%d,\"retainedBaseRegions\":%d,\"counts\":{%s}}\n"),P95,Assigned,G->Cells.Num()-Assigned,*Counts);FFileHelper::SaveStringToFile(JSON,*(Dir/TEXT("native-review.json")));Test->AddInfo(JSON);return Close();
        }
    };
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FReview>(this,E,A.Get(),Before));return true;
}
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
namespace {
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLCaveDesignerTest,"Catalyst.Landform.Caves.DesignerControlsAndUndo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLCaveDesignerTest::RunTest(const FString&)
{
    FString J,Error;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/Caves.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>(CreatePackage(*(TEXT("/Temp/CLCaveDesigner_")+FGuid::NewGuid().ToString(EGuidFormats::Digits))),NAME_None,RF_Transactional));if(!A->ImportRecipeJson(J,Error)){AddError(Error);return false;}
    const auto Original=A->Recipe;const auto* Saved=LoadObject<UCLPlanetRecipeAsset>(nullptr,TEXT("/Game/LandformDesigner/Caves/DA_FissureCaves"));if(!TestNotNull(TEXT("Cave review is a real native library asset"),Saved))return false;FString SavedJson;TStrongObjectPtr<UCLPlanetRecipeAsset> SavedCopy(NewObject<UCLPlanetRecipeAsset>());TestTrue(TEXT("User cave asset exports"),Saved->ExportJsonString(SavedJson,Error));TestTrue(TEXT("User cave asset reloads"),SavedCopy->ImportRecipeJson(SavedJson,Error));TestTrue(TEXT("Cold-loaded user cave settings survive roundtrip"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Saved->Recipe,&SavedCopy->Recipe,0));TestEqual(TEXT("Cave review links its 17 selected native Landforms"),Saved->PaletteSources.Num(),17);
    const auto E=MakeShared<FCLRecipeToolkit>();E->Open(A.Get(),EToolkitMode::Standalone,nullptr);E->AwaitEvaluationForTesting();
    TestTrue(TEXT("Caves supplies the fifth standard one-knob row"),E->CaveSlider&&E->LayerVisibility[4]&&E->LayerIndicators[4]&&E->CaveVariation);TestTrue(TEXT("Cave indicators are independent and initially disabled"),E->LayerIndicators[4]->IsEnabled()&&!E->Globe->IsCaveOverlayEnabled());
    const auto Started=E->Evaluation.Started;E->LayerIndicators[4]->ToggleCheckedState();TestTrue(TEXT("The indicator toggles the dimmed underground view"),E->Globe->IsCaveOverlayEnabled());TestEqual(TEXT("Indicators start no authoring task"),E->Evaluation.Started,Started);TestTrue(TEXT("Indicators preserve the recipe"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Original,&A->Recipe,0));
    E->BeginCaveDrag();E->PendingCave=.25;E->PumpCave(true);E->PendingCave=.85;E->PumpCave(true);E->EndCaveDrag();TestEqual(TEXT("Cave knob commits the latest position"),A->Recipe.Caves.Character,.85);TestTrue(TEXT("Cave knob develops length and width"),A->Recipe.Caves.LengthMeters!=Original.Caves.LengthMeters&&A->Recipe.Caves.RadiusMeters!=Original.Caves.RadiusMeters);GEditor->UndoTransaction();TestTrue(TEXT("One Undo restores the complete cave gesture"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Original,&A->Recipe,0));
    E->CaveVariation->SimulateClick();TestTrue(TEXT("Caves has an independent reseed"),A->Recipe.Caves.Seed!=Original.Caves.Seed);GEditor->UndoTransaction();TestTrue(TEXT("Reseed Undo preserves geology"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Original,&A->Recipe,0));
    E->LayerVisibility[4]->SimulateClick();TestFalse(TEXT("Eye disables the cave producer"),A->Recipe.Caves.bEnabled);TestTrue(TEXT("Eye preserves the independent debug preference"),E->Globe->IsCaveOverlayEnabled());GEditor->UndoTransaction();
    auto W=A->Recipe.Watershed;W.bEnabled=false;TestTrue(TEXT("Watershed edits preserve the enclosing cave schema"),E->CommitWatershed(W));TestEqual(TEXT("Cave schema survives other geology edits"),A->Recipe.SchemaVersion,23);
    E->LayerIndicators[4]->ToggleCheckedState();TestFalse(TEXT("Debug mode switches off independently"),E->Globe->IsCaveOverlayEnabled());A->GetOutermost()->SetDirtyFlag(false);E->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);return true;
}
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
namespace {
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPlanetLandformRowsTest,"Catalyst.Landform.Planet.LandformRows.ControlsAndPersistence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPlanetLandformRowsTest::RunTest(const FString&)
{
    FString J,Error;const FString Root=IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir();
    TStrongObjectPtr<UCLTerrainTypeAsset> Source(NewObject<UCLTerrainTypeAsset>());
    FFileHelper::LoadFileToString(J,*(Root/TEXT("Examples/TerrainTypes/ContinentalRiftFamily.json")));if(!Source->ImportTerrainTypeJson(J,Error)){AddError(Error);return false;}
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>(CreatePackage(*(TEXT("/Temp/CLLandformRows_")+FGuid::NewGuid().ToString(EGuidFormats::Digits))),NAME_None,RF_Transactional));
    FFileHelper::LoadFileToString(J,*(Root/TEXT("Examples/ContinentalRiftProvince.json")));if(!A->ImportRecipeJson(J,Error)){AddError(Error);return false;}
    if(!FCLPlanetCompiler::WithAuthoringValidation([&]{return A->AddPaletteSource(Source.Get(),Error);})){AddError(Error);return false;}
    const auto OriginalSource=Source->TerrainType;const FGuid Id=Source->TerrainType.Id;auto E=MakeShared<FCLRecipeToolkit>();E->Open(A.Get(),EToolkitMode::Standalone,nullptr);E->InspectorPage=2;E->Globe->Cancel();
    auto State=E->Landforms;auto* Row=E->LandformRows->Controls.Find(Id);if(!TestNotNull(TEXT("Planet contains the applied Rift row"),Row))return false;
    TestTrue(TEXT("Catalogue and applied sources are listed in Planet"),E->LandformRows->Controls.Num()>=17);TestFalse(TEXT("The obsolete terrain-layer tab is not registered"),E->GetTabManager()->HasTabSpawner(FName(TEXT("CLNoiseLayers"))));TestFalse(TEXT("The obsolete source-surface tab is not registered"),E->GetTabManager()->HasTabSpawner(FName(TEXT("CLSurfaceLayers"))));
    Row->Include->ToggleCheckedState();TestFalse(TEXT("Checkbox excludes generation"),State->Included(Id));
    TestTrue(TEXT("Unchecked rows disable knob, eye, reseed and Solo"),!Row->Disclosure->IsEnabled()&&!Row->DetailsBox->IsEnabled()&&!Row->Knob->IsEnabled()&&!Row->Eye->IsEnabled()&&!Row->Info->IsEnabled()&&!Row->Reseed->IsEnabled()&&!Row->Solo->IsEnabled());
    const auto Disabled=A->Recipe;TestFalse(TEXT("Disabled controller refuses Character"),State->SetCharacter(Id,.8));TestFalse(TEXT("Disabled controller refuses reseed"),State->Reseed(Id));TestFalse(TEXT("Disabled controller refuses eye"),State->ToggleEye(Id));TestFalse(TEXT("Disabled controller refuses Solo"),State->ToggleSolo(Id));TestFalse(TEXT("Disabled controller refuses cell highlights"),State->ToggleHighlight(Id));
    TestTrue(TEXT("Disabled actions preserve all settings"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Disabled,&A->Recipe,0));
    Row->Include->ToggleCheckedState();TestTrue(TEXT("Reinclude restores all row controls"),Row->Knob->IsEnabled()&&Row->Eye->IsEnabled()&&Row->Info->IsEnabled()&&Row->Reseed->IsEnabled()&&Row->Solo->IsEnabled());
    A->Recipe.RegionGeneration.bEnabled=false;E->Refresh();TestFalse(TEXT("A saved disabled generator shows unchecked rows"),State->Included(Id));TestFalse(TEXT("A saved disabled generator disables its controls"),Row->Knob->IsEnabled());Row->Include->ToggleCheckedState();TestTrue(TEXT("The row checkbox reactivates generation explicitly"),State->Included(Id));
    Row->Disclosure->SimulateClick();TestTrue(TEXT("Disclosure creates native editable values"),Row->Details.IsValid()&&Row->Draft.IsValid());
    for(const auto& Path:Row->Details->GetDetailsView()->GetPropertiesInOrderDisplayed())TestTrue(TEXT("Details omit protected identity and algorithm fields"),Path.GetLeafMostProperty().Property->GetFName()!=TEXT("AlgorithmVersion")&&Path.GetLeafMostProperty().Property->GetFName()!=TEXT("Id"));
    auto& Values=*reinterpret_cast<FCLLandformPaletteEntry*>(Row->Draft->GetStructMemory());const auto BeforeDetails=A->Recipe;Values.Rift.MaximumThrowMeters=120;
    Row->Details->GetOnFinishedChangingPropertiesDelegate().Broadcast(FPropertyChangedEvent(FindFProperty<FProperty>(FCLRiftLandform::StaticStruct(),TEXT("MaximumThrowMeters"))));
    TestEqual(TEXT("Expanded values commit to the applied planet"),State->Entry(Id)->Rift.MaximumThrowMeters,120.);GEditor->UndoTransaction();TestTrue(TEXT("One Undo restores the expanded edit"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&BeforeDetails,&A->Recipe,0));
    Row->Disclosure->SimulateClick();
    const auto Before=A->Recipe;State->BeginGesture(Id);TestTrue(TEXT("Knob first update accepted"),State->SetCharacter(Id,.2));TestTrue(TEXT("Knob final update accepted"),State->SetCharacter(Id,.8));State->EndGesture();const auto After=A->Recipe;
    GEditor->UndoTransaction();TestTrue(TEXT("One Undo restores the complete knob gesture"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Before,&A->Recipe,0));GEditor->RedoTransaction();TestTrue(TEXT("Redo restores final Character"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&After,&A->Recipe,0));
    Row->Reseed->SimulateClick();TestTrue(TEXT("Reseed changes the planet's applied variation"),A->Recipe.RegionGeneration.Palette[0].Rift.Seed!=After.RegionGeneration.Palette[0].Rift.Seed);
    const auto Authored=A->Recipe;A->GetOutermost()->SetDirtyFlag(false);Row->Eye->SimulateClick();TestFalse(TEXT("Eye hides only preview"),State->PreviewRecipe().RegionGeneration.bEnabled);TestTrue(TEXT("Eye retains generation inclusion"),State->Included(Id));
    TestFalse(TEXT("Eye does not dirty the asset"),A->GetOutermost()->IsDirty());
    Row->Solo->SimulateClick();TestTrue(TEXT("Solo reveals a hidden selected row"),State->Visible(Id));TestEqual(TEXT("Solo supplies the explicit evaluator option"),State->PreviewSolo(),Id);
    auto Input=State->PreviewRecipe();auto Prepared=FCLPlanetCompiler::PrepareBackground(Input,Error,nullptr,INDEX_NONE,nullptr,Id);auto Plan=Prepared?FCLPlanetCompiler::CompleteBackground(*Prepared,MakeShared<FCLCompileProgress>(),Error):nullptr;
    if(!Plan){AddError(Error);return false;}for(const auto& C:Plan->GeneratedRegions->Cells)TestEqual(TEXT("Solo applies selected Landform to every planet region"),Plan->GeneratedRegions->Palette[C.PaletteIndex].Id,Id);
    TestTrue(TEXT("Solo leaves the recipe exactly unchanged"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Authored,&A->Recipe,0));TestFalse(TEXT("Solo does not dirty authoring"),A->GetOutermost()->IsDirty());
    Row->Solo->SimulateClick();TestFalse(TEXT("Unsolo restores assembled preview"),State->PreviewSolo().IsValid());
    TestFalse(TEXT("Unsolo restores the previous hidden eye state"),State->Visible(Id));Row->Eye->SimulateClick();TestTrue(TEXT("Restoring the eye restores the exact saved recipe"),([&]{const auto Preview=State->PreviewRecipe();return FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Authored,&Preview,0);}()));
    TestTrue(TEXT("Planet controls never alter source definitions"),FCLTerrainType::StaticStruct()->CompareScriptStruct(&OriginalSource,&Source->TerrainType,0));
    TStrongObjectPtr<UCLPlanetRecipeAsset> Copy(NewObject<UCLPlanetRecipeAsset>());TestTrue(TEXT("Planet row settings export"),A->ExportJsonString(J,Error));TestFalse(TEXT("Solo is absent from portable JSON"),J.Contains(TEXT("soloPreview")));TestTrue(TEXT("Planet row settings import"),Copy->ImportRecipeJson(J,Error));TestTrue(TEXT("Row settings round-trip"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Authored,&Copy->Recipe,0));
    TStrongObjectPtr<UCLTerrainTypeAsset> Added(NewObject<UCLTerrainTypeAsset>());Added->TerrainType=Source->TerrainType;Added->TerrainType.Id=FGuid(8,7,6,5);Added->TerrainType.Name=TEXT("Custom Rift variant");
    TestTrue(TEXT("A custom source can be assembled without leaving Planet"),FCLPlanetCompiler::WithAuthoringValidation([&]{return A->AddPaletteSource(Added.Get(),Error);}));TestTrue(TEXT("New applied sources immediately appear as rows"),E->LandformRows->Controls.Contains(Added->TerrainType.Id));
    const auto Toolkit=FCLRecipeToolkit::ActivePlanet.Pin();CLOpenTerrainDesigner(Source.Get());TestTrue(TEXT("Legacy terrain API focuses the existing planet"),Toolkit==FCLRecipeToolkit::ActivePlanet.Pin()&&E->InspectorPage==2);TestEqual(TEXT("The only toolkit is Planet"),E->GetToolkitFName(),FName(TEXT("CLRecipeDesigner")));TestTrue(TEXT("A source definition never gets a standalone editor"),GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Source.Get(),false)==nullptr);
    A->GetOutermost()->SetDirtyFlag(false);E->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPlanetLandformInfoTest,"Catalyst.Landform.Planet.LandformRows.AssignedCellHighlights",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPlanetLandformInfoTest::RunTest(const FString&)
{
    auto* Original=LoadObject<UCLPlanetRecipeAsset>(nullptr,TEXT("/Game/LandformDesigner/DA_BasePlanet"));if(!TestNotNull(TEXT("Normal authoring planet"),Original))return false;
    const auto Before=Original->Recipe;
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>(CreatePackage(*(TEXT("/Temp/CLInfo_")+FGuid::NewGuid().ToString(EGuidFormats::Digits))),TEXT("CellHighlightReview"),RF_Transactional));A->Recipe=Before;
    auto E=MakeShared<FCLRecipeToolkit>();E->HoldCavesForTesting=true;E->Open(A.Get(),EToolkitMode::Standalone,nullptr);E->InspectorPage=2;
    auto* Source=LoadObject<UCLTerrainTypeAsset>(nullptr,TEXT("/CatalystLandform/TerrainTypes/Provinces/DA_Province_VolcanicArc"));if(!TestNotNull(TEXT("Catalogue source"),Source))return false;
    const FGuid Id=Source->TerrainType.Id;E->Landforms->AddSource(Source);if(!E->Landforms->Include(Id,true)){AddError(E->Landforms->Error);return false;}
    E->LandformRows->Focus(Id);A->GetOutermost()->SetDirtyFlag(false);
    class FReview:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;TSharedRef<FCLRecipeToolkit> E;TStrongObjectPtr<UCLPlanetRecipeAsset> A,Original;FCLPlanetRecipe Before;FGuid Id;int32 Stage=0;double Start=FPlatformTime::Seconds(),Ready=0;bool Framed=false;uint32 Evaluations=0,Owner=0;uint64 Copies=0;TArray<FString> Records;
    public:
        FReview(FAutomationTestBase* T,TSharedRef<FCLRecipeToolkit> In,UCLPlanetRecipeAsset* Asset,UCLPlanetRecipeAsset* Source,FCLPlanetRecipe B,FGuid I):Test(T),E(In),A(Asset),Original(Source),Before(MoveTemp(B)),Id(I){}
        bool Update()override
        {
            auto Close=[&]{A->GetOutermost()->SetDirtyFlag(false);E->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);return true;};
            const double Now=FPlatformTime::Seconds();if(Now-Start>150){Test->AddError(FString::Printf(TEXT("Cell highlight stage %d: %s"),Stage,*E->Status));return Close();}
            const auto P=E->AcceptedSurfacePlan;auto* N=E->Globe->NativeForTesting();if(!P||!P->GeneratedRegions||!N||E->EvaluationUpdating()||N->PublishedPlan()!=P||N->IsUpdating()||!E->Globe->IsSurfaceReady(P)){Ready=0;return false;}
            const auto& G=*P->GeneratedRegions;
            if(!Framed)
            {
                for(const auto& C:G.Cells)if(G.Palette[C.PaletteIndex].Id==Id){E->Globe->FrameDirectionForTesting(C.Centre,.9);break;}
                E->Globe->SetGuides({},false);E->LandformRows->Focus(Id);Framed=true;return false;
            }
            E->Globe->GetViewportClient()->Viewport->Draw(false);if(!Ready){Ready=Now;return false;}if(Now-Ready<.4)return false;
            TArray<FGuid> Expected;if(Stage!=0&&Stage!=2)for(const auto& C:G.Cells)if(G.Palette[C.PaletteIndex].Id==Id)Expected.Add(C.Id);
            const auto Actual=E->Globe->HighlightedGeneratedRegions();Test->TestEqual(TEXT("Highlight count matches actual generated assignments"),Actual.Num(),Expected.Num());for(FGuid Cell:Actual)Test->TestTrue(TEXT("Every highlighted cell belongs to the requested Landform"),Expected.Contains(Cell));
            if(Stage==1||Stage==2){Test->TestEqual(TEXT("Info starts no authoring evaluation"),E->Evaluation.Started,Evaluations);Test->TestEqual(TEXT("Info retains the native world"),N->WorldIdentity(),Owner);Test->TestEqual(TEXT("Info extracts no terrain mesh"),N->Metrics.MeshCopies,Copies);Test->TestFalse(TEXT("Info leaves the preview asset clean"),A->GetOutermost()->IsDirty());}
            if(Stage==3||Stage==4)Test->TestEqual(TEXT("Solo and a single included Landform highlight every province cell"),Actual.Num(),G.Cells.Num());
            if(Stage==0)
            {
                FString Text;TFunction<void(TSharedRef<SWidget>)> Collect=[&](TSharedRef<SWidget> W){if(W->GetType()==FName(TEXT("STextBlock")))Text+=StaticCastSharedRef<STextBlock>(W)->GetText().ToString()+TEXT(";");auto* C=W->GetChildren();for(int32 I=0;I<C->Num();++I)Collect(C->GetChildAt(I));};Collect(E->LandformRows.ToSharedRef());Collect(E->PlanetInspectors[4].ToSharedRef());Test->TestFalse(TEXT("No Landforms instruction banner"),Text.Contains(TEXT("Choose Landforms"))||Text.Contains(TEXT("Eye and Solo affect")));Test->TestFalse(TEXT("Preview has no Province cells checkbox"),Text.Contains(TEXT("Province cells")));
                Evaluations=E->Evaluation.Started;Owner=N->WorldIdentity();Copies=N->Metrics.MeshCopies;
            }
            const FString Dir=FPaths::ProjectSavedDir()/TEXT("Verification/landform-info");IFileManager::Get().MakeDirectory(*Dir,true);TArray<FColor> Pixels;FIntVector Size;
            if(Test->TestTrue(TEXT("Actual Planet overlay capture"),FSlateApplication::Get().TakeScreenshot(E->GetToolkitHost()->GetParentWidget(),Pixels,Size))){TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Dir/FString::Printf(TEXT("mode-%d.png"),Stage)));}
            Records.Add(FString::Printf(TEXT("{\"stage\":%d,\"highlighted\":%d,\"cells\":%d}"),Stage,Actual.Num(),G.Cells.Num()));Ready=0;Start=Now;
            if(Stage==0||Stage==1)E->LandformRows->Controls[Id].Info->SimulateClick();
            else if(Stage==2){E->LandformRows->Controls[Id].Info->SimulateClick();E->Landforms->ToggleSolo(Id);}
            else if(Stage==3)
            {
                E->Landforms->ToggleSolo(Id);TArray<FGuid> Others;for(const auto& Entry:A->Recipe.RegionGeneration.Palette)if(Entry.Id!=Id&&Entry.bEnabled)Others.Add(Entry.Id);for(FGuid Other:Others)E->Landforms->Include(Other,false);
            }
            else
            {
                Test->TestTrue(TEXT("Tests preserve all original unsaved authoring"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Before,&Original->Recipe,0));FFileHelper::SaveStringToFile(TEXT("{\"cases\":[")+FString::Join(Records,TEXT(","))+TEXT("]}\n"),*(Dir/TEXT("cases.json")));return Close();
            }
            ++Stage;return false;
        }
    };
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FReview>(this,E,A.Get(),Original,Before,Id));return true;
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FCLRiftDesignerTest,"Catalyst.Landform.Rift.NativeDesigner",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FCLRiftDesignerTest::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{Names={TEXT("Character"),TEXT("PreviewModes"),TEXT("Landscape")};Commands={TEXT("Character"),TEXT("PreviewModes"),TEXT("Landscape")};}
bool FCLRiftDesignerTest::RunTest(const FString& Variant)
{
    FString J,Error;const FString Root=IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir();
    TStrongObjectPtr<UCLTerrainTypeAsset> Source(NewObject<UCLTerrainTypeAsset>());FFileHelper::LoadFileToString(J,*(Root/TEXT("Examples/TerrainTypes/ContinentalRiftFamily.json")));if(!Source->ImportTerrainTypeJson(J,Error)){AddError(Error);return false;}
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>(CreatePackage(*(TEXT("/Temp/CLPlanetRiftReview_")+FGuid::NewGuid().ToString(EGuidFormats::Digits))),NAME_None,RF_Transactional));
    if(auto* Review=LoadObject<UCLPlanetRecipeAsset>(nullptr,TEXT("/Game/LandformDesigner/Provinces/DA_ContinentalRiftFamilyReview"),nullptr,LOAD_NoWarn|LOAD_Quiet))A->Recipe=Review->Recipe;
    else {FFileHelper::LoadFileToString(J,*(Root/TEXT("Examples/ContinentalRiftProvince.json")));if(!A->ImportRecipeJson(J,Error)){AddError(Error);return false;}}
    if(!FCLPlanetCompiler::WithAuthoringValidation([&]{return A->AddPaletteSource(Source.Get(),Error);})){AddError(Error);return false;}
    auto E=MakeShared<FCLRecipeToolkit>();E->Open(A.Get(),EToolkitMode::Standalone,nullptr);E->InspectorPage=2;const bool Modes=Variant==TEXT("PreviewModes"),Landscape=Variant==TEXT("Landscape");E->Landforms->SetCharacter(Source->TerrainType.Id,Landscape?.5:Modes?1:0);if(Landscape)E->LandformRows->ToggleDetails(Source->TerrainType.Id);if(Modes)E->Landforms->ToggleSolo(Source->TerrainType.Id);
    E->LandformRows->Focus(Source->TerrainType.Id);
    class FReview:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;TSharedRef<FCLRecipeToolkit> E;TStrongObjectPtr<UCLPlanetRecipeAsset> A;FGuid Id;
        double Start=FPlatformTime::Seconds(),Ready=0;int32 Stage=0,EndStage=3,Framed=-1,Frame=0;uint32 Owner=0,Evaluations=0;uint64 Copies=0;double Last=0;bool NavigationReady=false,Landscape=false;
        FVector Focus;TArray<double> Times;TArray<FString> Rows;
    public:
        FReview(FAutomationTestBase* T,TSharedRef<FCLRecipeToolkit> In,UCLPlanetRecipeAsset* Asset,FGuid I,bool Modes,bool InLandscape):Test(T),E(In),A(Asset),Id(I),Stage(InLandscape?1:Modes?3:0),EndStage(InLandscape?2:Modes?5:3),Landscape(InLandscape){}
        bool Update()override
        {
            const double Now=FPlatformTime::Seconds();auto Close=[&]{A->GetOutermost()->SetDirtyFlag(false);E->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);return true;};
            if(Now-Start>180){Test->AddError(FString::Printf(TEXT("Planet Landforms preview timed out at stage %d: %s %s"),Stage,*E->Status,*E->InspectorError));return Close();}
            if(!E->AcceptedSurfacePlan||E->Evaluation.IsBusy()){Ready=0;return false;}
            auto* Native=E->Globe->NativeForTesting();if(!Native)return false;
            auto P=E->AcceptedSurfacePlan;
            // Set the requested review camera as soon as the field is accepted,
            // before native publication. Waiting for an unrelated overview first
            // caused a second complete LOD refinement for every knob position.
            if(Stage<EndStage&&Framed!=Stage)
            {
                if(!Owner)
                {
                    Owner=Native->WorldIdentity();double Best=0;for(int32 I=0;I<4096;++I){double Z=1-2*(I+.5)/4096,Angle=I*2.399963229728653;FVector D(FMath::Cos(Angle)*FMath::Sqrt(1-Z*Z),FMath::Sin(Angle)*FMath::Sqrt(1-Z*Z),Z);double V=FMath::Abs(P->GeneratedRegions->ProvinceDeformationMeters(D));if(V>Best){Best=V;Focus=D;}}
                    Test->TestTrue(TEXT("Assembled planet contains real Rift relief"),Best>.01);E->Globe->SetRegionInspection(false);E->Globe->SetGuides({},false);E->Preview->Client->bGuides=false;
                }
                if(Landscape)E->Globe->FrameSurfaceForTesting(Focus);else E->Globe->FrameDirectionForTesting(Focus,.7);Framed=Stage;Ready=0;return false;
            }
            // Settle once before timing navigation. Subsequent native camera
            // LOD work is asynchronous and must not be mistaken for a UI frame.
            if(!NavigationReady&&(Native->PublishedPlan()!=P||Native->IsUpdating())){Ready=0;return false;}
            if(!Ready){Ready=Now;return false;}if(Now-Ready<.5)return false;
            if(Stage<EndStage)
            {
                Test->TestEqual(TEXT("Rows retain the planet's native owner"),Native->WorldIdentity(),Owner);
                int32 Count=0;const auto Probes=Native->SamplePublishedVerticesForTesting(Count);Test->TestTrue(TEXT("Planet Landforms renders native geometry"),Count>100&&Probes.SourcePoints.Num()>100);
                if(Stage<3)Test->TestEqual(TEXT("Row knob reaches the rendered planet"),P->SourceRecipe->RegionGeneration.Palette[0].Rift.Character,Stage*.5);
                if(Stage==3)for(const auto& C:P->GeneratedRegions->Cells)Test->TestEqual(TEXT("Rendered Solo assigns every region to Rift"),P->GeneratedRegions->Palette[C.PaletteIndex].Id,Id);
                if(Stage==4)Test->TestFalse(TEXT("Rendered eye-off excludes regional relief"),P->GeneratedRegions&&P->GeneratedRegions->HasPhysicalRelief());
                Rows.Add(FString::Printf(TEXT("{\"stage\":%d,\"vertices\":%d,\"geometryCrc\":%u}"),Stage,Count,FCrc::MemCrc32(Probes.SourcePoints.GetData(),Probes.SourcePoints.Num()*sizeof(FVector))));
                const FString Dir=FPaths::ProjectSavedDir()/TEXT("Verification/landform-curation");IFileManager::Get().MakeDirectory(*Dir,true);
                auto Capture=[&](TSharedRef<SWidget> W,const FString& Name){TArray<FColor> Pixels;FIntVector Size;Test->TestTrue(TEXT("Actual Planet editor renders"),FSlateApplication::Get().TakeScreenshot(W,Pixels,Size));if(Pixels.Num()){TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Dir/Name));}};
                Capture(E->GetToolkitHost()->GetParentWidget(),FString::Printf(TEXT("%s-%d.png"),Landscape?TEXT("landscape"):TEXT("planet"),Stage));Capture(E->Globe.ToSharedRef(),FString::Printf(TEXT("globe-%d.png"),Stage));if(auto Tab=E->GetTabManager()->FindExistingLiveTab(FName(TEXT("CLPlanetControls"))))Capture(Tab->GetContent(),FString::Printf(TEXT("controls-%d.png"),Stage));
                ++Stage;Ready=0;
                if(Stage==EndStage)
                {
                    if(Landscape){Test->AddInfo(TEXT("Native landscape camera captured at Character 0.5"));return Close();}
                    if(EndStage==5){const FString Json=TEXT("{\"editor\":\"Planet\",\"cases\":[")+FString::Join(Rows,TEXT(","))+TEXT("]}\n");FFileHelper::SaveStringToFile(Json,*(Dir/TEXT("preview-modes.json")));Test->AddInfo(Json);return Close();}
                    // Keep the same whole-planet camera used by these UI captures.
                }
                else if(Stage<3){E->Landforms->SetCharacter(Id,Stage*.5);if(Stage==1)E->LandformRows->ToggleDetails(Id);}
                else E->LandformRows->Controls[Id].Eye->SimulateClick();
                return false;
            }
            if(!NavigationReady){NavigationReady=true;Evaluations=E->Evaluation.Started;Copies=Native->Metrics.MeshCopies;Last=Now;return false;}
            Times.Add((Now-Last)*1000);Last=Now;E->Globe->OrbitForTesting(FVector2D(.5,.1));E->Globe->Dolly(Frame%2?.01:-.01);if(++Frame<60)return false;
            Test->TestEqual(TEXT("Planet navigation starts no authoring evaluations"),E->Evaluation.Started,Evaluations);Test->TestEqual(TEXT("Navigation copies no mesh"),Native->Metrics.MeshCopies,Copies);Times.RemoveAt(0);Times.Sort();const double P95=Times[FMath::Min(Times.Num()-1,int32(Times.Num()*.95))];Test->TestTrue(TEXT("Native navigation p95 stays below 50ms"),P95<50);
            const FString Json=TEXT("{\"editor\":\"Planet\",\"cameraZoom\":0.7,\"navigationP95Ms\":")+LexToString(P95)+TEXT(",\"cases\":[")+FString::Join(Rows,TEXT(","))+TEXT("]}\n");FFileHelper::SaveStringToFile(Json,*(FPaths::ProjectSavedDir()/TEXT("Verification/landform-curation/native-review.json")));Test->AddInfo(Json);return Close();
        }
    };
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FReview>(this,E,A.Get(),Source->TerrainType.Id,Modes,Landscape));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPlanetSurfaceFirstTest,"Catalyst.Landform.Planet.SurfaceBeforeCaves",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPlanetSurfaceFirstTest::RunTest(const FString&)
{
    auto* Review=LoadObject<UCLPlanetRecipeAsset>(nullptr,TEXT("/Game/LandformDesigner/Provinces/DA_ContinentalRiftFamilyReview"));if(!TestNotNull(TEXT("Saved review exists"),Review))return false;
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>(CreatePackage(*(TEXT("/Temp/CLSurfaceFirst_")+FGuid::NewGuid().ToString(EGuidFormats::Digits))),NAME_None,RF_Transactional));A->Recipe=Review->Recipe;
    auto E=MakeShared<FCLRecipeToolkit>();E->HoldCavesForTesting=true;E->Open(A.Get(),EToolkitMode::Standalone,nullptr);E->InspectorPage=2;
    for(const auto& Entry:A->Recipe.RegionGeneration.Palette)if(Entry.Rift.IsActive()){E->LandformRows->ToggleDetails(Entry.Id);E->LandformRows->Focus(Entry.Id);break;}
    class FReview:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;TSharedRef<FCLRecipeToolkit> E;TStrongObjectPtr<UCLPlanetRecipeAsset> A;
        double Start=FPlatformTime::Seconds(),SurfaceTime=0,Stable=0;int32 Phase=0;uint32 Owner=0,Jobs=0;bool PickedDuringCaves=false;FVector Walk;TSharedPtr<const FCLCompiledPlanetRecipe> Surface;
        void Capture(const TCHAR* Name)
        {
            const FString Dir=FPaths::ProjectSavedDir()/TEXT("Verification/surface-first");IFileManager::Get().MakeDirectory(*Dir,true);
            TArray<FColor> Pixels;FIntVector Size;if(Test->TestTrue(TEXT("Actual Planet window capture"),FSlateApplication::Get().TakeScreenshot(E->GetToolkitHost()->GetParentWidget(),Pixels,Size))){TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Dir/(FString(Name)+TEXT(".png"))));}
        }
    public:
        FReview(FAutomationTestBase* T,TSharedRef<FCLRecipeToolkit> In,UCLPlanetRecipeAsset* Asset):Test(T),E(In),A(Asset){}
        bool Update()override
        {
            const double Now=FPlatformTime::Seconds();auto Close=[&]{A->GetOutermost()->SetDirtyFlag(false);E->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);return true;};
            if(Now-Start>180){Test->AddError(FString::Printf(TEXT("Surface-first preview timed out phase %d: %s"),Phase,*E->Status));return Close();}
            if(!E->AcceptedSurfacePlan)return false;auto P=E->AcceptedSurfacePlan;auto* N=E->Globe->NativeForTesting();if(!N)return false;
            if(Phase==0)
            {
                if(!E->Evaluation.AwaitingSurface()||!E->Globe->IsSurfaceReady(P)||!E->Globe->CanPick())return false;
                Surface=P;Owner=N->WorldIdentity();Jobs=E->Evaluation.Started;SurfaceTime=Now-Start;
                Test->TestTrue(TEXT("Surface is natively published and pickable before cave generation"),E->Evaluation.IsBusy()&&!P->Caves&&P->SourceRecipe->Caves.bEnabled);
                Test->TestTrue(TEXT("Geographic globe overlay is enabled by default"),E->Globe->IsGlobeGridVisible());Test->TestFalse(TEXT("Province cells are hidden by default"),E->Globe->IsInspectingRegions());
                int32 Vertices=0;N->SamplePublishedVerticesForTesting(Vertices);Test->TestTrue(TEXT("Surface checkpoint renders real VPP mesh"),Vertices>100);
                // Inspect the actual native viewport image, not only its field
                // or proxy readiness. Flush only in this bounded render test.
                auto* Viewport=E->Globe->GetViewportClient()->Viewport;Viewport->Draw(false);FlushRenderingCommands();TArray<FColor> Pixels;Viewport->ReadPixels(Pixels);
                int32 SurfacePixels=0;for(auto C:Pixels)SurfacePixels+=C.R>60&&C.G>55&&C.B>40;
                Test->TestTrue(TEXT("Opaque native surface pixels are visible before caves start"),SurfacePixels>10000);
                Test->AddInfo(FString::Printf(TEXT("SURFACE_BEFORE_CAVES visibleSurfacePixels=%d"),SurfacePixels));
                Walk=FVector(1,0,0);E->Globe->SetWalkLocation(Walk);Capture(TEXT("surface-ready-before-caves"));Stable=Now;Phase=1;return false;
            }
            if(Phase==1)
            {
                Test->TestEqual(TEXT("Navigation does not bypass surface gate"),E->Evaluation.Started,Jobs);if(Now-Stable<.5)return false;
                Test->TestTrue(TEXT("Published surface remains pickable while caves wait"),E->Globe->CanPick());E->HoldCavesForTesting=false;Phase=2;Stable=0;return false;
            }
            if(E->Evaluation.IsBusy()&&!E->Evaluation.AwaitingSurface()&&P==Surface){PickedDuringCaves=E->Globe->CanPick();Test->TestTrue(TEXT("Surface stays usable during cave computation"),PickedDuringCaves);}
            if(E->Evaluation.IsBusy()||!E->Globe->IsSurfaceReady(P)||!E->Globe->CanPick())return false;
            Test->TestTrue(TEXT("Observed usable native surface during cave computation"),PickedDuringCaves);
            if(!Stable){Stable=Now;return false;}if(Now-Stable<.5)return false;
            Test->TestTrue(TEXT("Final preview includes generated caves"),P->Caves&&P->Caves->Networks>0);
            Test->TestEqual(TEXT("Caves retain the same native world"),N->WorldIdentity(),Owner);Test->TestEqual(TEXT("Cave continuation is one additional job"),E->Evaluation.Started,Jobs+1);
            Test->TestEqual(TEXT("Walk location survives cave publication"),E->Globe->WalkLocation(),Walk);
            Test->TestEqual(TEXT("Cave continuation preserves surface height"),P->SampleHeightMeters(Walk),Surface->SampleHeightMeters(Walk));Capture(TEXT("surface-with-caves"));
            const FString Json=FString::Printf(TEXT("{\"surfaceNativeReadySeconds\":%.6f,\"cavesNativeReadySeconds\":%.6f,\"cavesStartedAfterSurfacePublication\":true,\"surfacePickableBeforeCaves\":true,\"nativeOwner\":%u}\n"),SurfaceTime,Now-Start,Owner);
            FFileHelper::SaveStringToFile(Json,*(FPaths::ProjectSavedDir()/TEXT("Verification/surface-first/timings.json")));Test->AddInfo(Json);return Close();
        }
    };
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FReview>(this,E,A.Get()));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTransformDesignerTest,"Catalyst.Landform.Planet.LandformRows.TransformProgression",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLTransformDesignerTest::RunTest(const FString&)
{
    auto* Review=LoadObject<UCLPlanetRecipeAsset>(nullptr,TEXT("/Game/LandformDesigner/Provinces/DA_TransformShearFamilyReview"));if(!TestNotNull(TEXT("Saved authored Transform planet exists"),Review))return false;
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>(CreatePackage(*(TEXT("/Temp/CLTransformCharacter_")+FGuid::NewGuid().ToString(EGuidFormats::Digits))),NAME_None,RF_Transactional));A->Recipe=Review->Recipe;
    const auto* Found=A->Recipe.RegionGeneration.Palette.FindByPredicate([](const auto& P){return P.Transform.IsActive();});if(!TestNotNull(TEXT("Authored Transform settings exist"),Found))return false;
    const FCLLandformPaletteEntry Original=*Found;const FGuid Id=Original.Id;const auto Authored=Review->Recipe;
    TestNull(TEXT("Physical settings have no Examples property"),FindFProperty<FProperty>(FCLTransformLandform::StaticStruct(),TEXT("Examples")));
    auto E=MakeShared<FCLRecipeToolkit>();E->HoldCavesForTesting=true;E->Open(A.Get(),EToolkitMode::Standalone,nullptr);E->InspectorPage=2;
    auto Expected=Original.Transform;Expected.ApplyProgression(.37);TestTrue(TEXT("Character applies the established progression operation"),E->Landforms->SetCharacter(Id,.37));
    TestTrue(TEXT("Knob writes current physical values"),FCLTransformLandform::StaticStruct()->CompareScriptStruct(&Expected,&E->Landforms->Entry(Id)->Transform,0));
    auto Manual=*E->Landforms->Entry(Id);Manual.Transform.ValleyDepthMeters=67;Manual.Transform.RidgeHeightMeters=29;
    TestTrue(TEXT("Disclosure values commit directly"),E->Landforms->SetValues(Id,Manual));TestEqual(TEXT("Manual edit preserves Character"),E->Landforms->Character(Id),.37);
    E->Landforms->BeginGesture(Id);E->Landforms->SetCharacter(Id,.21);E->Landforms->SetCharacter(Id,.72);E->Landforms->EndGesture();
    GEditor->UndoTransaction();TestEqual(TEXT("One Undo restores the manual physical value"),E->Landforms->Entry(Id)->Transform.ValleyDepthMeters,67.);TestEqual(TEXT("One Undo restores the prior Character"),E->Landforms->Character(Id),.37);
    GEditor->RedoTransaction();Expected=Manual.Transform;Expected.ApplyProgression(.72);TestTrue(TEXT("Redo restores the mapped settings"),FCLTransformLandform::StaticStruct()->CompareScriptStruct(&Expected,&E->Landforms->Entry(Id)->Transform,0));
    const int32 Seed=Expected.Seed;E->Landforms->Reseed(Id);Expected.Seed=E->Landforms->Entry(Id)->Transform.Seed;TestTrue(TEXT("Reseed changes only local variation"),Seed!=Expected.Seed&&FCLTransformLandform::StaticStruct()->CompareScriptStruct(&Expected,&E->Landforms->Entry(Id)->Transform,0));
    TestTrue(TEXT("Placement metadata survives knob and manual edits"),FCLLandformSuitability::StaticStruct()->CompareScriptStruct(&Original.Suitability,&E->Landforms->Entry(Id)->Suitability,0));
    TestTrue(TEXT("Restore the exact authored values for rendered inspection"),E->Landforms->SetValues(Id,Original));E->Landforms->ToggleSolo(Id);E->LandformRows->ToggleDetails(Id);E->LandformRows->Focus(Id);
    class FReview:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;TSharedRef<FCLRecipeToolkit> E;TStrongObjectPtr<UCLPlanetRecipeAsset> A;FCLPlanetRecipe Original;FGuid Id;double Start=FPlatformTime::Seconds(),Ready=0;bool Framed=false;
    public:
        FReview(FAutomationTestBase* T,TSharedRef<FCLRecipeToolkit> In,UCLPlanetRecipeAsset* Asset,FCLPlanetRecipe R,FGuid I):Test(T),E(In),A(Asset),Original(MoveTemp(R)),Id(I){}
        bool Update()override
        {
            auto Close=[&]{A->GetOutermost()->SetDirtyFlag(false);E->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);return true;};const double Now=FPlatformTime::Seconds();
            if(Now-Start>180){Test->AddError(E->Status+TEXT(" ")+E->InspectorError);return Close();}
            const auto P=E->AcceptedSurfacePlan;if(!P||(!E->Evaluation.AwaitingSurface()&&E->Evaluation.IsBusy()))return false;
            const auto* Current=P->SourceRecipe->RegionGeneration.Palette.FindByPredicate([&](const auto& Entry){return Entry.Id==Id;});const auto* Saved=Original.RegionGeneration.Palette.FindByPredicate([&](const auto& Entry){return Entry.Id==Id;});
            if(!Current||!FCLTransformLandform::StaticStruct()->CompareScriptStruct(&Current->Transform,&Saved->Transform,0))return false;
            auto* N=E->Globe->NativeForTesting();if(!N)return false;
            if(!Framed){for(const auto& F:P->GeneratedRegions->Transforms)if(F&&!F->GetCorridors().IsEmpty()){const auto& C=F->GetCorridors()[0];E->Globe->SetRegionInspection(false);E->Globe->SetGuides({},false);E->Globe->SetGlobeGrid(false);E->Globe->FrameLandformForTesting(C.Centre,C.Along,C.HalfLength*1.35);break;}E->LandformRows->Focus(Id);Framed=true;return false;}
            E->Globe->GetViewportClient()->Viewport->Draw(false);if(N->PublishedPlan()!=P||N->IsUpdating()||!E->Globe->IsSurfaceReady(P)){Ready=0;return false;}
            if(!Ready){Ready=Now;return false;}if(Now-Ready<.5)return false;
            int32 Count=0;auto Probe=N->SamplePublishedVerticesForTesting(Count);Test->TestTrue(TEXT("Native geometry renders the authored physical settings"),Count>100&&Probe.SourcePoints.Num()>100);
            Test->TestTrue(TEXT("Inspection preserves all authored planet settings"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Original,&A->Recipe,0));
            const FString Dir=FPaths::ProjectSavedDir()/TEXT("Verification/transform-character");IFileManager::Get().MakeDirectory(*Dir,true);
            auto* V=E->Globe->GetViewportClient()->Viewport;V->Draw(false);FlushRenderingCommands();TArray<FColor> Visible;V->ReadPixels(Visible);int32 Surface=0;for(auto C:Visible)Surface+=C.R>60&&C.G>55&&C.B>40;Test->TestTrue(TEXT("Actual surface pixels are visible"),Surface>10000);
            TArray<FColor> Pixels;FIntVector Size;if(Test->TestTrue(TEXT("Actual Planet row and disclosure capture"),FSlateApplication::Get().TakeScreenshot(E->GetToolkitHost()->GetParentWidget(),Pixels,Size))){TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Dir/TEXT("planet.png")));}
            Test->AddInfo(FString::Printf(TEXT("TRANSFORM_CHARACTER nativeVertices=%d visiblePixels=%d character=%.17g valleyDepth=%.9f seed=%d"),Count,Surface,Saved->Transform.Character,Saved->Transform.ValleyDepthMeters,Saved->Transform.Seed));return Close();
        }
    };
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FReview>(this,E,A.Get(),A->Recipe,Id));return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLVolcanicArcDesignerTest,"Catalyst.Landform.Planet.LandformRows.VolcanicArcRange",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLVolcanicArcDesignerTest::RunTest(const FString&)
{
    FString ReviewPath=TEXT("/Game/LandformDesigner/DA_BasePlanet");
    FParse::Value(FCommandLine::Get(),TEXT("LandformArcReviewAsset="),ReviewPath);
    auto* Review=LoadObject<UCLPlanetRecipeAsset>(nullptr,*ReviewPath);
    auto* Source=LoadObject<UCLTerrainTypeAsset>(nullptr,TEXT("/CatalystLandform/TerrainTypes/Provinces/DA_Province_VolcanicArc"));
    if(!TestNotNull(TEXT("Current saved authoring planet"),Review)||!TestNotNull(TEXT("Canonical volcanic arc source"),Source))return false;
    const auto Saved=Review->Recipe;const auto SourceBefore=Source->TerrainType;
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>(CreatePackage(*(TEXT("/Temp/CLArcRange_")+FGuid::NewGuid().ToString(EGuidFormats::Digits))),TEXT("VolcanicArcReview"),RF_Transactional));A->Recipe=Saved;
    auto E=MakeShared<FCLRecipeToolkit>();E->HoldCavesForTesting=true;E->Open(A.Get(),EToolkitMode::Standalone,nullptr);E->InspectorPage=2;
    const FGuid Id=Source->TerrainType.Id;E->Landforms->AddSource(Source);
    if(!E->Landforms->Include(Id,true)){AddError(E->Landforms->Error);E->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);return false;}
    const auto WithArc=A->Recipe;auto Expected=Source->TerrainType.VolcanicArc;Expected.ApplyProgression(.41);
    E->Landforms->SetCharacter(Id,.41);TestTrue(TEXT("Standard knob applies independent physical curves"),FCLVolcanicArcLandform::StaticStruct()->CompareScriptStruct(&Expected,&E->Landforms->Entry(Id)->VolcanicArc,0));
    auto Manual=*E->Landforms->Entry(Id);Manual.VolcanicArc.HeightMeters=165;E->Landforms->SetValues(Id,Manual);
    E->Landforms->BeginGesture(Id);E->Landforms->SetCharacter(Id,.23);E->Landforms->SetCharacter(Id,.78);E->Landforms->EndGesture();
    GEditor->UndoTransaction();TestEqual(TEXT("One Undo retains manual values and prior Character"),E->Landforms->Entry(Id)->VolcanicArc.HeightMeters,165.);TestEqual(TEXT("One Undo retains prior Character"),E->Landforms->Character(Id),.41);
    GEditor->RedoTransaction();TestEqual(TEXT("Redo restores the knob position"),E->Landforms->Character(Id),.78);
    const int32 Seed=E->Landforms->Entry(Id)->VolcanicArc.Seed;E->Landforms->Reseed(Id);TestTrue(TEXT("Standard reseed changes volcanic variation"),E->Landforms->Entry(Id)->VolcanicArc.Seed!=Seed);GEditor->UndoTransaction();
    E->Landforms->ToggleEye(Id);TestFalse(TEXT("Eye is preview only"),E->Landforms->Visible(Id));E->Landforms->ToggleEye(Id);
    E->Landforms->Include(Id,false);auto* Row=E->LandformRows->Controls.Find(Id);if(Row)TestTrue(TEXT("Unchecked row disables its controls"),!Row->Knob->IsEnabled()&&!Row->Solo->IsEnabled()&&!Row->Disclosure->IsEnabled());E->Landforms->Include(Id,true);
    E->Landforms->ToggleSolo(Id);E->Landforms->SetCharacter(Id,0);E->LandformRows->Focus(Id);
    class FReview:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;TSharedRef<FCLRecipeToolkit> E;TStrongObjectPtr<UCLPlanetRecipeAsset> A;TStrongObjectPtr<UCLPlanetRecipeAsset> OriginalAsset;TStrongObjectPtr<UCLTerrainTypeAsset> Source;
        FCLPlanetRecipe Saved,WithArc;FCLTerrainType SourceBefore;FGuid Id;
        int32 Stage=0;double Start=FPlatformTime::Seconds(),Ready=0;bool Framed=false;TArray<FString> Records;
        const TArray<double> Positions={0.,.17,.34,.50,.67,.84,1.};
    public:
        FReview(FAutomationTestBase* T,TSharedRef<FCLRecipeToolkit> In,UCLPlanetRecipeAsset* Asset,UCLPlanetRecipeAsset* Original,UCLTerrainTypeAsset* S,FCLPlanetRecipe R,FCLPlanetRecipe W,FCLTerrainType B,FGuid I):Test(T),E(In),A(Asset),OriginalAsset(Original),Source(S),Saved(MoveTemp(R)),WithArc(MoveTemp(W)),SourceBefore(MoveTemp(B)),Id(I){}
        bool Update()override
        {
            auto Close=[&]{A->GetOutermost()->SetDirtyFlag(false);if(FParse::Param(FCommandLine::Get(),TEXT("LandformKeepArcReview")))E->HoldCavesForTesting=false;else E->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);return true;};const double Now=FPlatformTime::Seconds();
            if(Now-Start>180){Test->AddError(FString::Printf(TEXT("Arc range stage %d: %s %s"),Stage,*E->Status,*E->InspectorError));return Close();}
            const auto P=E->AcceptedSurfacePlan;if(!P||!P->GeneratedRegions)return false;
            const auto* Current=P->SourceRecipe->RegionGeneration.Palette.FindByPredicate([&](const auto& V){return V.Id==Id;});
            if(!Current||FMath::Abs(Current->VolcanicArc.Character-Positions[Stage])>1.e-8)return false;
            auto* N=E->Globe->NativeForTesting();if(!N)return false;
            if(!Framed)
            {
                for(const auto& Arc:P->GeneratedRegions->VolcanicArcs)if(Arc&&!Arc->GetCentres().IsEmpty())
                {const auto& C=Arc->GetCentres()[0];E->Globe->SetRegionInspection(false);E->Globe->SetGuides({},false);E->Globe->SetGlobeGrid(false);E->Globe->FrameLandformForTesting(C.Direction,C.Along,700,FVector2D(.95,.45));break;}
                E->LandformRows->Focus(Id);Framed=true;return false;
            }
            E->Globe->SetGuides({},false);E->Globe->SetGlobeGrid(false);
            E->Globe->GetViewportClient()->Viewport->Draw(false);
            if(N->PublishedPlan()!=P||N->IsUpdating()||!E->Globe->IsSurfaceReady(P)){Ready=0;return false;}
            if(!Ready){Ready=Now;return false;}if(Now-Ready<.6)return false;
            int32 Count=0;const auto Probe=N->SamplePublishedVerticesForTesting(Count);Test->TestTrue(TEXT("Stock native VPP supplies visible arc geometry"),Count>100&&Probe.SourcePoints.Num()>100);
            const FString Dir=FPaths::ProjectSavedDir()/TEXT("Verification/volcanic-arc");IFileManager::Get().MakeDirectory(*Dir,true);
            auto Capture=[&](TSharedRef<SWidget> Widget,const FString& Name){TArray<FColor> Pixels;FIntVector Size;if(Test->TestTrue(TEXT("Actual Planet editor capture"),FSlateApplication::Get().TakeScreenshot(Widget,Pixels,Size))){TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Dir/Name));}};
            Capture(E->GetToolkitHost()->GetParentWidget(),FString::Printf(TEXT("planet-%02d.png"),FMath::RoundToInt(Positions[Stage]*100)));Capture(E->Globe.ToSharedRef(),FString::Printf(TEXT("surface-%02d.png"),FMath::RoundToInt(Positions[Stage]*100)));
            Records.Add(FString::Printf(TEXT("{\"character\":%.4f,\"nativeVertices\":%d,\"heightMeters\":%.6f,\"incisionMeters\":%.6f,\"secondaryHeight\":%.6f}"),Positions[Stage],Count,Current->VolcanicArc.HeightMeters,Current->VolcanicArc.IncisionMeters,Current->VolcanicArc.SecondaryHeight));
            ++Stage;Ready=0;Start=Now;
            if(Stage==Positions.Num())
            {
                E->LandformRows->ToggleDetails(Id);
                Test->TestTrue(TEXT("Review preserves the user's saved authoring exactly"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Saved,&OriginalAsset->Recipe,0));
                Test->TestTrue(TEXT("Review preserves the catalogue source"),FCLTerrainType::StaticStruct()->CompareScriptStruct(&SourceBefore,&Source->TerrainType,0));
                auto Other=A->Recipe;Other.RegionGeneration.Palette.RemoveAll([&](const auto& V){return V.Id==Id;});auto Before=WithArc;Before.RegionGeneration.Palette.RemoveAll([&](const auto& V){return V.Id==Id;});Test->TestTrue(TEXT("Arc review preserves every other authoring value"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Other,&Before,0));
                FFileHelper::SaveStringToFile(TEXT("{\"editor\":\"Planet\",\"cases\":[")+FString::Join(Records,TEXT(","))+TEXT("]}\n"),*(Dir/TEXT("range.json")));return Close();
            }
            E->Landforms->SetCharacter(Id,Positions[Stage]);E->LandformRows->Focus(Id);Framed=false;return false;
        }
    };
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FReview>(this,E,A.Get(),Review,Source,Saved,WithArc,SourceBefore,Id));return !HasAnyErrors();
}

}
#endif

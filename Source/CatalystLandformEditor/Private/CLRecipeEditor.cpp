#include "CLRecipeAssetTools.h"
#include "CLSectionGeometry.h"
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
class FCLStrataSession : public TSharedFromThis<FCLStrataSession>
{
public:
    UCLPlanetRecipeAsset* Asset = nullptr;
    TStrongObjectPtr<UCLStrataLibraryAsset> Library;
    int32 Selected = INDEX_NONE;
    TUniquePtr<FScopedTransaction> Gesture;
    FCLPlanetRecipe BeforeRecipe; FCLStrataLibrary BeforeLibrary;
    void Open(UCLPlanetRecipeAsset* InAsset)
    {
        Asset = InAsset; Asset->SetFlags(RF_Transactional);
        const TCHAR* Path = TEXT("/Game/LandformDesigner/DA_StrataLibrary.DA_StrataLibrary");
        Library.Reset(LoadObject<UCLStrataLibraryAsset>(nullptr, Path));
        if (!Library)
        {
            Library.Reset(NewObject<UCLStrataLibraryAsset>(CreatePackage(TEXT("/Game/LandformDesigner/DA_StrataLibrary")),
                TEXT("DA_StrataLibrary"), RF_Public | RF_Standalone | RF_Transactional));
            FAssetRegistryModule::AssetCreated(Library.Get());
            Library->Library.Sets.Add(FCLPlanetCompiler::RollingPlains().Strata); Library->MarkPackageDirty();
        }
        Library->SetFlags(RF_Transactional);
        if (!Asset->Recipe.Strata.Id.IsValid()) Asset->Recipe.Strata.Id = FGuid::NewGuid();
        if (!Library->Library.Sets.ContainsByPredicate([&](const FCLStrata& S) { return S.Id == Asset->Recipe.Strata.Id; }))
        { Library->Modify(); Library->Library.Sets.Add(Asset->Recipe.Strata); Library->MarkPackageDirty(); }
    }
    void Begin(const FText& Text)
    {
        if (Gesture) return;
        Gesture = MakeUnique<FScopedTransaction>(Text); BeforeRecipe = Asset->Recipe; BeforeLibrary = Library->Library; Asset->Modify(); Library->Modify();
    }
    void End(bool bCancel = false)
    {
        if (!Gesture) return;
        if (bCancel) { Asset->Recipe = BeforeRecipe; Library->Library = BeforeLibrary; Asset->PostEditChange(); Gesture->Cancel(); }
        Gesture.Reset();
    }
    void Changed()
    {
        if (FCLStrata* S = Library->Library.Sets.FindByPredicate([&](const FCLStrata& V) { return V.Id == Asset->Recipe.Strata.Id; }))
            *S = Asset->Recipe.Strata;
        Library->MarkPackageDirty(); Asset->MarkPackageDirty(); Asset->PostEditChange();
    }
    void Height(int32 Index, double Height)
    {
        if (!Asset->Recipe.Strata.Stratum.IsValidIndex(Index)) return;
        Asset->Recipe.Strata.Stratum[Index].NormalizedHeight = Height; Changed();
    }
    void Remove(int32 Index)
    {
        if (!Asset->Recipe.Strata.Stratum.IsValidIndex(Index)) return;
        Begin(NSLOCTEXT("CL", "RemoveStratum", "Remove Stratum"));
        FCLStrataEditing::Remove(Asset->Recipe.Strata, Index); Selected = INDEX_NONE; Changed(); End();
    }
    void Apply(FGuid Id)
    {
        const auto* S = Library->Library.Sets.FindByPredicate([&](const FCLStrata& V) { return V.Id == Id; });
        if (!S) return;
        Begin(NSLOCTEXT("CL", "ApplyStrata", "Apply Strata to planet"));
        Asset->Recipe.Strata = *S; Selected = INDEX_NONE; Changed(); End();
    }
    void NewSet()
    {
        Begin(NSLOCTEXT("CL", "NewStrata", "Create Strata"));
        FCLStrata S = Asset->Recipe.Strata; S.Id = FGuid::NewGuid();
        int32 N = 1;
        do { S.Name = FString::Printf(TEXT("New Strata %d"), N++); }
        while (Library->Library.Sets.ContainsByPredicate([&](const FCLStrata& V) { return V.Name.Equals(S.Name, ESearchCase::IgnoreCase); }));
        Library->Library.Sets.Add(S); Asset->Recipe.Strata = S; Selected = INDEX_NONE; Changed(); End();
    }
    void RemoveSet()
    {
        Begin(NSLOCTEXT("CL", "RemoveStrata", "Remove Strata"));
        const FGuid Id = Asset->Recipe.Strata.Id;
        Library->Library.Sets.RemoveAll([&](const FCLStrata& S) { return S.Id == Id; });
        if (!Library->Library.Sets.IsEmpty()) Asset->Recipe.Strata = Library->Library.Sets[0];
        else { Asset->Recipe.Strata = FCLStrata(); Asset->Recipe.Strata.Id = FGuid::NewGuid(); }
        Selected = INDEX_NONE; Changed(); End();
    }
};
class SCLSectionViewport;
class FCLSectionClient : public FEditorViewportClient
{
public:
    TSharedPtr<const FCLCompiledPlanetRecipe> Plan;
    double Heading = 0;
    bool bGuides = true;
    FCLStrata Strata;
    FVector2D RightEdge;
    FMatrix ViewProjection;
    FIntRect ViewRect;
    bool bHasProjection = false;
    double StratumHeightMeters(double Normalized) const { return (Normalized - 1) * Plan->RadiusMeters; }
    double NormalizedAtScreenY(double Y) const
    {
        return bHasProjection && Plan ? FCLSectionGeometry::StratumAtScreenY(*Plan, RightEdge, ViewProjection, Y) : 0;
    }
    struct FIndicator { FString Text; FLinearColor Color; FVector2D Position; int32 Index = INDEX_NONE; };
    TArray<FIndicator> Indicators;
    FCLSectionClient(FPreviewScene* Scene, const TSharedRef<SEditorViewport>& Viewport)
        : FEditorViewportClient(nullptr, Scene, Viewport)
    {
        SetViewMode(VMI_Lit); SetRealtime(true);
        ExposureSettings.bFixed = true; ExposureSettings.FixedEV100 = 0;
        SetViewLocation(FVector(8500, -10500, 8000));
        SetViewRotation((FVector(0, 0, -1500) - GetViewLocation()).Rotation());
        SetLookAtLocation(FVector(0, 0, -1500));
        EngineShowFlags.SetGrid(false); EngineShowFlags.SetSelectionOutline(false); EngineShowFlags.SetEyeAdaptation(false);
    }
    double TopHeight() const
    {
        return Plan->RadiusMeters;
    }
    bool GuidePoint(double X, double Y, double Height, FVector& Point) const
    {
        Point = FCLSectionGeometry::StratumPoint(*Plan, FVector2D(X, Y), 1 + Height / Plan->RadiusMeters);
        return true;
    }
    virtual FLinearColor GetBackgroundColor() const override { return FLinearColor(.075, .09, .11); }
    virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override
    {
        FEditorViewportClient::Draw(View, PDI);
        if (!Plan) return;
        const double W = FCLSectionGeometry::HalfWidth(*Plan);
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
            if (View->WorldToPixel(FVector(X, Y, Plan->RadiusMeters) * 100, Screen) && Screen.X > Rightmost)
            { Rightmost = Screen.X; Edge = FVector2D(X, Y); }
        }
        RightEdge = Edge; ViewProjection = View->ViewMatrices.GetViewProjectionMatrix();
        ViewRect = View->UnscaledViewRect; bHasProjection = true;
        auto Indicator = [&](const FVector& P, const FString& Text, FLinearColor Color, int32 Index = INDEX_NONE)
        {
            FVector2D Screen;
            if (View->WorldToPixel(P, Screen)) Indicators.Add({Text, Color,
                FVector2D((Screen.X - View->UnscaledViewRect.Min.X) / FMath::Max(1, View->UnscaledViewRect.Width()),
                    (Screen.Y - View->UnscaledViewRect.Min.Y) / FMath::Max(1, View->UnscaledViewRect.Height())), Index});
        };
        Indicator(FVector(Edge.X, Edge.Y, 0) * 100, TEXT("Core / centre plane"), FLinearColor(.9, .7, .5), -2);
        FVector Sky; GuidePoint(Edge.X, Edge.Y, 0, Sky);
        Indicator(Sky, TEXT("Sky boundary (1.0)"), FLinearColor(.65, .8, 1), -3);
        if (bGuides) for (int32 I = 0; I < Strata.Stratum.Num(); ++I)
        {
            const auto& G = Strata.Stratum[I]; FVector P;
            if (GuidePoint(Edge.X, Edge.Y, StratumHeightMeters(G.NormalizedHeight), P))
                Indicator(P, G.Name.ToString(), G.Color, I);
        }
        Indicators.Sort([](const FIndicator& A, const FIndicator& B) { return A.Position.Y < B.Position.Y; });
    }
};

/** A tick or label is a direct manipulator, with one undo transaction per gesture. */
class SCLStratumHandle : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SCLStratumHandle) : _Index(INDEX_NONE), _New(false), _Label(false) {}
        SLATE_ARGUMENT(TSharedPtr<FCLStrataSession>, Session)
        SLATE_ARGUMENT(TSharedPtr<FCLSectionClient>, Client)
        SLATE_ARGUMENT(TWeakPtr<SWidget>, Overlay)
        SLATE_ARGUMENT(int32, Index)
        SLATE_ARGUMENT(bool, New)
        SLATE_ARGUMENT(bool, Label)
    SLATE_END_ARGS()
    TSharedPtr<FCLStrataSession> Session;
    TSharedPtr<FCLSectionClient> Client;
    TWeakPtr<SWidget> Overlay;
    TSharedPtr<SInlineEditableTextBlock> Name;
    int32 Index = INDEX_NONE;
    bool bNew = false, bDragging = false;
    FVector2D MouseStart;
    void Construct(const FArguments& Args)
    {
        Session = Args._Session; Client = Args._Client; Overlay = Args._Overlay;
        Index = Args._Index; bNew = Args._New;
        SetToolTipText(FText::FromString(bNew ? TEXT("Drag onto the section to create a Stratum") :
            TEXT("Click to select; drag to move; Delete to remove; double-click a label or press F2 to rename")));
        if (bNew) ChildSlot[SNew(SBorder).Padding(8)[SNew(STextBlock).Text(FText::FromString(TEXT("+ New Stratum — drag up to create")))]];
        else if (Args._Label)
            ChildSlot[SNew(SBorder).Padding(FMargin(3, 1)).BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([this] { return Session->Selected == Index ? FLinearColor(.1, .3, .5, .65) : FLinearColor::Transparent; })
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth()[SAssignNew(Name, SInlineEditableTextBlock)
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10)).DelayedLeftClickEntersEditMode(false)
                        .Text_Lambda([this] { return FText::FromName(Session->Asset->Recipe.Strata.Stratum.IsValidIndex(Index) ?
                            Session->Asset->Recipe.Strata.Stratum[Index].Name : NAME_None); })
                        .ColorAndOpacity_Lambda([this] { return FSlateColor(Session->Asset->Recipe.Strata.Stratum.IsValidIndex(Index) ?
                            Session->Asset->Recipe.Strata.Stratum[Index].Color : FLinearColor::White); })
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
                    + SHorizontalBox::Slot().AutoWidth().Padding(6, 0)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                        .Text_Lambda([this]
                        {
                            if (!Session->Asset->Recipe.Strata.Stratum.IsValidIndex(Index)) return FText();
                            const double N = Session->Asset->Recipe.Strata.Stratum[Index].NormalizedHeight;
                            return FText::FromString(FString::Printf(TEXT("%.3f · %.1f m"), N, N * Client->Plan->RadiusMeters));
                        })]]];
        else ChildSlot[SNew(SBorder).Padding(0).BorderImage(FAppStyle::GetBrush("WhiteBrush"))
            .BorderBackgroundColor_Lambda([this] { return Session->Selected == Index ? FLinearColor(.2, .6, 1, .4) : FLinearColor::Transparent; })];
    }
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& E) override
    {
        if (E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
        MouseStart = E.GetScreenSpacePosition(); bDragging = false;
        if (!bNew) Session->Selected = Index;
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
        if (!bDragging && FVector2D::Distance(MouseStart, E.GetScreenSpacePosition()) > 3 &&
            (!bNew || (Local.Y >= 0 && Local.Y < G.GetLocalSize().Y)))
        {
            Session->Begin(bNew ? NSLOCTEXT("CL", "CreateStratum", "Create Stratum") : NSLOCTEXT("CL", "MoveStratum", "Move Stratum"));
            bDragging = true;
            if (bNew)
            {
                Index = FCLStrataEditing::Add(Session->Asset->Recipe.Strata, Client->NormalizedAtScreenY(Local.Y / G.GetLocalSize().Y));
                Session->Selected = Index;
            }
        }
        if (bDragging) Session->Height(Index, Client->NormalizedAtScreenY(Local.Y / FMath::Max(1.0, G.GetLocalSize().Y)));
        return FReply::Handled();
    }
    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& E) override
    {
        if (E.GetEffectingButton() != EKeys::LeftMouseButton || !HasMouseCapture()) return FReply::Unhandled();
        bool bCancel = false;
        if (bNew && bDragging) if (auto Layer = Overlay.Pin())
        {
            const auto& G = Layer->GetCachedGeometry(); const FVector2D P = G.AbsoluteToLocal(E.GetScreenSpacePosition());
            bCancel = P.X < 0 || P.Y < 0 || P.X > G.GetLocalSize().X || P.Y > G.GetLocalSize().Y;
        }
        Session->End(bCancel); bDragging = false;
        FReply Reply = FReply::Handled().ReleaseMouseCapture();
        if (bNew) if (auto Layer = Overlay.Pin()) Reply.SetUserFocus(Layer.ToSharedRef());
        return Reply;
    }
    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override { Session->End(true); bDragging = false; }
    virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& E) override
    {
        if ((E.GetKey() == EKeys::Delete || E.GetKey() == EKeys::BackSpace) && !bNew)
        { Session->Remove(Index); return FReply::Handled(); }
        if (E.GetKey() == EKeys::F2 && Name) { Name->EnterEditingMode(); return FReply::Handled(); }
        if (E.GetKey() == EKeys::Escape && bDragging)
        { Session->End(true); bDragging = false; return FReply::Handled().ReleaseMouseCapture(); }
        return FReply::Unhandled();
    }
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
            Canvas->AddSlot().Position_Lambda([this, I] { return Label(I); }).Size(FVector2D(300, 22))
                [SNew(SCLStratumHandle).Session(Session).Client(Client).Overlay(SharedThis(this)).Index(I).Label(true)];
        }
        for (int32 I : {-2, -3})
            Canvas->AddSlot().Position_Lambda([this, I] { return Label(I); }).Size(FVector2D(210, 22))
                [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 10)).Visibility(EVisibility::HitTestInvisible)
                    .Text_Lambda([this, I] { const auto* P = Indicator(I); return P ? FText::FromString(P->Text) : FText(); })
                    .ColorAndOpacity_Lambda([this, I] { const auto* P = Indicator(I); return FSlateColor(P ? P->Color : FLinearColor::White); })];
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
    double LastRadius = 0;
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
        const FCLSectionGeometry G = FCLSectionGeometry::Build(*Plan, Client->Heading);
        TArray<FVector> N; TArray<FVector2D> UV; TArray<FProcMeshTangent> Tangents;
        UV.Init(FVector2D::ZeroVector, G.Vertices.Num());
        TArray<FVector> DisplayVertices = G.Vertices;
        for (FVector& V : DisplayVertices) if (V.Z > 0) V = FCLSectionGeometry::DisplaySurfacePoint(V);
        UKismetProceduralMeshLibrary::CalculateTangentsForMesh(DisplayVertices, G.Triangles, {}, N, Tangents);
        Mesh->CreateMeshSection_LinearColor(0, DisplayVertices, G.Triangles, N, UV, G.Colors, Tangents, false);
        if (!FMath::IsNearlyEqual(LastRadius, Plan->RadiusMeters))
        {
            LastRadius = Plan->RadiusMeters;
            const double R = Plan->RadiusMeters * 100;
            const FVector Target(0, 0, Client->TopHeight() * 45);
            Client->SetViewLocation(Target + FVector(.8 * R, -1.0 * R, .8 * R));
            Client->SetViewRotation((Target - Client->GetViewLocation()).Rotation());
            Client->SetLookAtLocation(Target);
        }
        Client->Invalidate();
    }
};

class FCLRecipeToolkit : public FCFModelEditorToolkit
{
    TSharedPtr<SCLSectionViewport> Preview;
    TSharedPtr<SCLHeightAnnotations> Annotations;
    TSharedPtr<FCLStrataSession> StrataSession;
    FString Status;
    FDelegateHandle Changed;
    static const FName SectionTab;
public:
    virtual ~FCLRecipeToolkit()
    { if (auto* Asset = Cast<UCLPlanetRecipeAsset>(EditingAsset)) Asset->OnRecipeChanged.Remove(Changed); }
    void Open(UCLPlanetRecipeAsset* Asset, EToolkitMode::Type Mode, TSharedPtr<IToolkitHost> Host)
    {
        EditingAsset = Asset;
        StrataSession = MakeShared<FCLStrataSession>(); StrataSession->Open(Asset);
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
        if (Preview) Preview->Client->Strata = Asset->Recipe.Strata;
        const auto Plan = FCLPlanetCompiler::Compile(Asset->Recipe, Status);
        if (Plan)
        {
            Status = FString::Printf(TEXT("%s | radius %.0f m | %d layers | preview curvature 1/3"), *Asset->Recipe.Name, Plan->RadiusMeters, Plan->Layers.Num());
            if (Preview) Preview->Rebuild(Plan);
        }
        else Status = TEXT("Keeping last valid preview: ") + Status;
        RefreshAllUI(EditingAsset, false);
    }
    TSharedRef<SDockTab> SpawnSection(const FSpawnTabArgs&)
    {
        return SNew(SDockTab)[SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Status); })]
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
                + SHorizontalBox::Slot().AutoWidth()[SNew(SCheckBox).IsChecked(ECheckBoxState::Checked).OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                    { Preview->Client->bGuides = State == ECheckBoxState::Checked; Preview->Client->Invalidate(); })[SNew(STextBlock).Text(FText::FromString(TEXT("Height Guides")))]]
                + SHorizontalBox::Slot().AutoWidth().Padding(12, 0)[SNew(STextBlock).Text(FText::FromString(TEXT("Section heading")))]
                + SHorizontalBox::Slot().FillWidth(1)[SNew(SSlider).OnValueChanged_Lambda([this](float Value)
                    { Preview->Client->Heading = Value * 2 * UE_PI; Refresh(); })]]
            + SVerticalBox::Slot().FillHeight(1)[SNew(SOverlay)
                + SOverlay::Slot()[SAssignNew(Preview, SCLSectionViewport)]
                + SOverlay::Slot()[SAssignNew(Annotations, SCLHeightAnnotations).Client(Preview->Client).Session(StrataSession)]]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(8)
                [SNew(SCLStratumHandle).Session(StrataSession).Client(Preview->Client).Overlay(Annotations).New(true)]
            + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]
                {
                    const FString Legend = TEXT("Core-to-sky cutaway | Alt + mouse: orbit/pan/zoom | Preview curvature 1/3");
                    return FText::FromString(Legend);
                })]];
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

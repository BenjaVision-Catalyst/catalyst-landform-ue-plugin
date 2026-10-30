#include "CLRecipeAssetTools.h"
#include "CLSectionGeometry.h"
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
class SCLSectionViewport;
class FCLSectionClient : public FEditorViewportClient
{
public:
    TSharedPtr<const FCLCompiledPlanetRecipe> Plan;
    double Heading = 0;
    bool bGuides = true;
    struct FIndicator { FString Text; FLinearColor Color; FVector2D Position; };
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
        double Top = Plan->MaximumHeightMeters + Plan->RadiusMeters * .18;
        for (const auto& G : Plan->Guides) Top = FMath::Max(Top, G.HeightMeters);
        return Plan->RadiusMeters + Top;
    }
    bool GuidePoint(double X, double Y, double Height, FVector& Point) const
    {
        const double R = Plan->RadiusMeters + Height;
        const double Z2 = R * R - X * X - Y * Y;
        if (Z2 < 0) return false;
        Point = FCLSectionGeometry::DisplaySurfacePoint(FVector(X, Y, FMath::Sqrt(Z2)) * 100);
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
            PDI->DrawLine(FVector(X, Y, 0) * 100, FVector(X, Y, TopHeight()) * 100, Frame, SDPG_World, 1);
        for (double Side : {-W, W})
        {
            PDI->DrawLine(FVector(-W, Side, TopHeight()) * 100, FVector(W, Side, TopHeight()) * 100, Frame, SDPG_World, 1);
            PDI->DrawLine(FVector(Side, -W, TopHeight()) * 100, FVector(Side, W, TopHeight()) * 100, Frame, SDPG_World, 1);
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
        auto Indicator = [&](const FVector& P, const FString& Text, FLinearColor Color)
        {
            FVector2D Screen;
            if (View->WorldToPixel(P, Screen)) Indicators.Add({Text, Color,
                FVector2D((Screen.X - View->UnscaledViewRect.Min.X) / FMath::Max(1, View->UnscaledViewRect.Width()),
                    (Screen.Y - View->UnscaledViewRect.Min.Y) / FMath::Max(1, View->UnscaledViewRect.Height()))});
        };
        Indicator(FVector(Edge.X, Edge.Y, 0) * 100, TEXT("Core / centre plane"), FLinearColor(.9, .7, .5));
        Indicator(FVector(Edge.X, Edge.Y, TopHeight()) * 100, TEXT("Sky"), FLinearColor(.65, .8, 1));
        if (bGuides) for (const auto& G : Plan->Guides)
        {
            FVector P;
            if (GuidePoint(Edge.X, Edge.Y, G.HeightMeters, P)) Indicator(P,
                FString::Printf(TEXT("%s  %.1f m"), *G.Name.ToString(), G.HeightMeters), G.Color);
        }
        Indicators.Sort([](const FIndicator& A, const FIndicator& B) { return A.Position.Y < B.Position.Y; });
    }
};

/** Ticks attach to the rendered edge; leaders preserve exact heights when labels crowd. */
class SCLHeightAnnotations : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SCLHeightAnnotations) {} SLATE_ARGUMENT(TWeakPtr<FCLSectionClient>, Client) SLATE_END_ARGS()
    TWeakPtr<FCLSectionClient> Client;
    void Construct(const FArguments& Args) { Client = Args._Client; ForceVolatile(true); }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Elements,
        int32 Layer, const FWidgetStyle& Style, bool) const override
    {
        const auto Pinned = Client.Pin();
        if (!Pinned || Pinned->Indicators.IsEmpty()) return Layer;
        const auto Font = FCoreStyle::GetDefaultFontStyle("Regular", 10);
        const double Height = Geometry.GetLocalSize().Y;
        const double Spacing = FMath::Min(24.0, FMath::Max(12.0, (Height - 32) / Pinned->Indicators.Num()));
        const FVector2D Size = Geometry.GetLocalSize();
        double LabelX = 0;
        for (const auto& I : Pinned->Indicators) LabelX = FMath::Max(LabelX, I.Position.X * Size.X + 28);
        TArray<double> Ys;
        double Previous = 8 - Spacing;
        for (const auto& I : Pinned->Indicators)
        {
            const double Y = FMath::Max(8.0, FMath::Max(I.Position.Y * Height - 8, Previous + Spacing));
            Ys.Add(Y); Previous = Y;
        }
        const double Overflow = FMath::Max(0.0, Ys.Last() - Height + 24);
        for (int32 I = 0; I < Ys.Num(); ++I)
        {
            const double Y = Ys[I] - Overflow;
            const auto& Indicator = Pinned->Indicators[I];
            const FVector2D Anchor = Indicator.Position * Size;
            // Neither collision avoidance nor label clamping can move the tick.
            TArray<FVector2D> Tick{Anchor - FVector2D(4, 0), Anchor + FVector2D(12, 0)};
            FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Tick,
                ESlateDrawEffect::None, Indicator.Color * Style.GetColorAndOpacityTint(), true, 2);
            TArray<FVector2D> Leader{Anchor + FVector2D(12, 0), FVector2D(LabelX - 8, Y + 7), FVector2D(LabelX - 3, Y + 7)};
            FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Leader,
                ESlateDrawEffect::None, Indicator.Color * Style.GetColorAndOpacityTint(), true, 1);
            FSlateDrawElement::MakeText(Elements, Layer + 1,
                Geometry.ToPaintGeometry(FVector2D(190, 20), FSlateLayoutTransform(FVector2D(LabelX, Y))),
                FText::FromString(Indicator.Text), Font, ESlateDrawEffect::None, Indicator.Color * Style.GetColorAndOpacityTint());
        }
        return Layer + 1;
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
    FString Status;
    FDelegateHandle Changed;
    static const FName SectionTab;
public:
    virtual ~FCLRecipeToolkit()
    { if (auto* Asset = Cast<UCLPlanetRecipeAsset>(EditingAsset)) Asset->OnRecipeChanged.Remove(Changed); }
    void Open(UCLPlanetRecipeAsset* Asset, EToolkitMode::Type Mode, TSharedPtr<IToolkitHost> Host)
    {
        EditingAsset = Asset; BuildDetailsPanel();
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
                + SHorizontalBox::Slot().AutoWidth()[SNew(SCheckBox).IsChecked(ECheckBoxState::Checked).OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                    { Preview->Client->bGuides = State == ECheckBoxState::Checked; Preview->Client->Invalidate(); })[SNew(STextBlock).Text(FText::FromString(TEXT("Height Guides")))]]
                + SHorizontalBox::Slot().AutoWidth().Padding(12, 0)[SNew(STextBlock).Text(FText::FromString(TEXT("Section heading")))]
                + SHorizontalBox::Slot().FillWidth(1)[SNew(SSlider).OnValueChanged_Lambda([this](float Value)
                    { Preview->Client->Heading = Value * 2 * UE_PI; Refresh(); })]]
            + SVerticalBox::Slot().FillHeight(1)[SNew(SOverlay)
                + SOverlay::Slot()[SAssignNew(Preview, SCLSectionViewport)]
                + SOverlay::Slot()[SNew(SCLHeightAnnotations).Client(Preview->Client).Visibility(EVisibility::HitTestInvisible)]]
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
    auto* Asset = LoadObject<UCLPlanetRecipeAsset>(nullptr, TEXT("/Game/LandformDesigner/DA_RollingPlains.DA_RollingPlains"));
    if (Asset) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
}
class FCLDesignerModule : public IModuleInterface
{
    TUniquePtr<FAutoConsoleCommand> Command;
    FDelegateHandle MenuHandle;
public:
    virtual void StartupModule() override
    {
        Command = MakeUnique<FAutoConsoleCommand>(TEXT("Landform.Designer"), TEXT("Open rolling-plains recipe"), FConsoleCommandDelegate::CreateStatic(&OpenExample));
        MenuHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]
        {
            FToolMenuOwnerScoped Owner(TEXT("CatalystLandformDesigner"));
            auto* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
            Menu->AddSection(TEXT("CatalystLandform"), FText::FromString(TEXT("Catalyst"))).AddMenuEntry(TEXT("LandformDesigner"),
                FText::FromString(TEXT("Landform Designer")), FText::FromString(TEXT("Open the rolling-plains example recipe")),
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

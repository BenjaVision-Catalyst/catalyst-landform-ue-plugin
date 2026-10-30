#include "CLRecipeAssetTools.h"
#include "CLSectionGeometry.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
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
        Point = FVector(X, Y, FMath::Sqrt(Z2)) * 100;
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
        if (!bGuides) return;
        for (const FCLHeightGuide& G : Plan->Guides)
            for (int32 Side = 0; Side < 4; ++Side)
                for (int32 I = 0; I < 64; ++I)
                {
                    const double A = W * (2.0 * I / 64 - 1), B = W * (2.0 * (I + 1) / 64 - 1);
                    FVector P, Q;
                    const bool Valid = Side < 2 ?
                        GuidePoint(A, Side == 0 ? -W : W, G.HeightMeters, P) && GuidePoint(B, Side == 0 ? -W : W, G.HeightMeters, Q) :
                        GuidePoint(Side == 2 ? -W : W, A, G.HeightMeters, P) && GuidePoint(Side == 2 ? -W : W, B, G.HeightMeters, Q);
                    if (Valid) PDI->DrawLine(P, Q, G.Color, SDPG_Foreground, 2);
                }
    }
    virtual void Draw(FViewport* InViewport, FCanvas* Canvas) override
    {
        FEditorViewportClient::Draw(InViewport, Canvas);
        if (!Plan) return;
        FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(InViewport, GetScene(), EngineShowFlags));
        FSceneView* View = CalcSceneView(&Family);
        auto Label = [&](const FVector& P, const FString& Text, FLinearColor Color)
        {
            FVector2D Screen;
            if (View->WorldToPixel(P, Screen)) Canvas->DrawShadowedString(Screen.X / Canvas->GetDPIScale() + 8,
                Screen.Y / Canvas->GetDPIScale(), *Text, GEngine->GetSmallFont(), Color);
        };
        const double W = FCLSectionGeometry::HalfWidth(*Plan);
        Label(FVector(-W, -W, 0) * 100, TEXT("Core / centre plane"), FLinearColor(.9, .7, .5));
        Label(FVector(-W, -W, TopHeight()) * 100, TEXT("Sky"), FLinearColor(.65, .8, 1));
        TArray<FCLHeightGuide> Sorted = Plan->Guides;
        Sorted.Sort([](const FCLHeightGuide& A, const FCLHeightGuide& B) { return A.HeightMeters > B.HeightMeters; });
        double LastY = -1.e9;
        if (bGuides) for (const FCLHeightGuide& G : Sorted)
        {
            FVector P; FVector2D Screen;
            if (GuidePoint(W, -W, G.HeightMeters, P) && View->WorldToPixel(P, Screen))
            {
                LastY = FMath::Max(Screen.Y / Canvas->GetDPIScale(), LastY + 18);
                Canvas->DrawShadowedString(Screen.X / Canvas->GetDPIScale() + 8, LastY,
                    *FString::Printf(TEXT("%s  %.1f m"), *G.Name.ToString(), G.HeightMeters), GEngine->GetSmallFont(), G.Color);
            }
        }
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
        UKismetProceduralMeshLibrary::CalculateTangentsForMesh(G.Vertices, G.Triangles, UV, N, Tangents);
        UV.Init(FVector2D::ZeroVector, G.Vertices.Num());
        Mesh->CreateMeshSection_LinearColor(0, G.Vertices, G.Triangles, N, UV, G.Colors, Tangents, false);
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
            Status = FString::Printf(TEXT("%s | radius %.0f m | %d layers | actual scale"), *Asset->Recipe.Name, Plan->RadiusMeters, Plan->Layers.Num());
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
            + SVerticalBox::Slot().FillHeight(1)[SAssignNew(Preview, SCLSectionViewport)]
            + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]
                {
                    FString Legend = TEXT("Core-to-sky cutaway | Alt + mouse: orbit/pan/zoom | Height Guides: ");
                    for (const auto& G : CastChecked<UCLPlanetRecipeAsset>(EditingAsset)->Recipe.HeightGuides)
                        Legend += FString::Printf(TEXT("%s %.2f m   "), *G.Name.ToString(), G.HeightMeters);
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

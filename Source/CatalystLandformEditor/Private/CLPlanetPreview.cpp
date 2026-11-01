#include "CLPlanetPreview.h"
#include "Model/CLSphericalPatches.h"
#include "PreviewScene.h"
#include "EditorViewportClient.h"
#include "Components/DirectionalLightComponent.h"
#include "Materials/Material.h"
#include "Engine/TextureCube.h"
#include "Async/Async.h"
#include "InputKeyEventArgs.h"
#include "CLPlanetNavigation.h"

FCLGlobeBuffers FCLGlobeBuffers::Build(const FCLCompiledPlanetRecipe& Plan,int32 Resolution)
{
    FCLGlobeBuffers B;const int32 N=FMath::Clamp(Resolution,8,PreviewResolution),Row=N+1,Count=6*Row*Row;
    B.Vertices.Reserve(Count);B.Normals.Reserve(Count);B.SourceDirections.Reserve(Count);
    B.UV.Reserve(Count);B.Colors.Reserve(Count);B.Tangents.Reserve(Count);B.Triangles.Reserve(6*N*N*6);
    TArray<FVector> Samples;Samples.Reserve(SampleVertexBatch*5);
    auto Emit=[&]
    {
        if(Samples.IsEmpty())return;
        TArray<double> H;Plan.SampleHeightsMeters(Samples,H);
        auto Point=[&](int32 I){return Samples[I]*((Plan.RadiusMeters+H[I])/Plan.RadiusMeters*100000);};
        for(int32 K=0;K<Samples.Num();K+=5)
        {
            B.Vertices.Add(Point(K));FVector Normal=FVector::CrossProduct(Point(K+1)-Point(K+2),Point(K+3)-Point(K+4)).GetSafeNormal();
            if(FVector::DotProduct(Normal,Samples[K])<0)Normal*=-1;B.Normals.Add(Normal);
            const double Slope=FVector::DotProduct(Normal,Samples[K]);B.Colors.Add(FMath::Lerp(FLinearColor(.38,.36,.32),FLinearColor(.34,.43,.28),FMath::Clamp((Slope-.6)/.4,0.,1.)));
            B.UV.Add(FVector2D::ZeroVector);B.Tangents.Add(FProcMeshTangent(FVector::VectorPlaneProject(FVector::XAxisVector,Normal).GetSafeNormal(),false));
        }
        Samples.Reset();
    };
    for(int32 Face=0;Face<6;++Face)for(int32 Y=0;Y<=N;++Y)for(int32 X=0;X<=N;++X)
    {
        const FVector D=FCLSphericalPatches::Direction(Face,FVector2D(2.*X/N-1,2.*Y/N-1));B.SourceDirections.Add(D);
        const FVector T=FVector::CrossProduct(D,FMath::Abs(D.Z)<.8?FVector::ZAxisVector:FVector::XAxisVector).GetSafeNormal(),S=FVector::CrossProduct(D,T);
        Samples.Append({D,(D+T*.0001).GetSafeNormal(),(D-T*.0001).GetSafeNormal(),(D+S*.0001).GetSafeNormal(),(D-S*.0001).GetSafeNormal()});
        if(Samples.Num()==SampleVertexBatch*5)Emit();
    }
    Emit();
    B.DisplayRadius=(Plan.RadiusMeters+Plan.MaximumHeightMeters)/Plan.RadiusMeters*100000;
    for(int32 Face=0;Face<6;++Face)for(int32 Y=0;Y<N;++Y)for(int32 X=0;X<N;++X)
    {
        const int32 A=Face*Row*Row+Y*Row+X;B.Triangles.Append({A,A+Row,A+1,A+1,A+Row,A+Row+1});
    }
    return B;
}
class FCLGlobeClient : public FEditorViewportClient
{
public:
    FCLPlanetNavigation Navigation;
    FIntPoint NavigationSize;
    FCLRadialGuideSet Guides;
    bool bGuides=true;
    TArray<FCLGuideIndicator> Indicators;
    TArray<TArray<FVector2D>> Rings;
    FMatrix ViewProjection;
    FIntRect ViewRect;
    FVector2D Core,Sky;
    bool bHasProjection=false;
    explicit FCLGlobeClient(FPreviewScene* Scene,TSharedRef<SEditorViewport> Widget):FEditorViewportClient(nullptr,Scene,Widget)
    {SetViewMode(VMI_Lit);EngineShowFlags.SetGrid(false);EngineShowFlags.SetSelectionOutline(false);SetRealtime(false);ViewFOV=FOVAngle=60;Apply();}
    virtual bool InputKey(const FInputKeyEventArgs& E) override
    {
        if(E.Event==IE_Pressed && (E.Key==EKeys::MouseScrollUp || E.Key==EKeys::MouseScrollDown))Dolly(E.Key==EKeys::MouseScrollUp?1:-1);
        return true;
    }
    virtual bool InputAxis(const FInputKeyEventArgs&) override {return true;}
    virtual FLinearColor GetBackgroundColor() const override {return FLinearColor(.007,.012,.02);}
    virtual void Tick(float Delta) override
    {FEditorViewportClient::Tick(Delta);if(Viewport&&Viewport->GetSizeXY()!=NavigationSize)Apply();}
    virtual bool InputGesture(FViewport*,FInputDeviceId,EGestureEvent Type,const FVector2D& Delta,bool Inverted,uint64) override
    {if(Type==EGestureEvent::Magnify)Navigation.Dolly(Delta.X*6);else if(Type==EGestureEvent::Scroll)Navigation.PanBy(Delta*(Inverted?-10:10));Apply();return true;}
    void Apply()
    {
        NavigationSize=Viewport?Viewport->GetSizeXY():FIntPoint(1000,800);
        const double H=FMath::Tan(FMath::DegreesToRadians(30.)),V=H*FMath::Max(1,NavigationSize.Y)/FMath::Max(1,NavigationSize.X);
        const auto C=Navigation.Camera(H,V);SetViewLocation(C.Eye);SetViewRotation(C.Rotation);SetLookAtLocation(C.Target);Invalidate();
    }
    void Dolly(double D){Navigation.Dolly(D);Apply();}
    void UpdateIndicators()
    {
        if(!bHasProjection)return;Indicators.Reset();Rings.Reset();
        const FRotationMatrix Axes(GetViewRotation());const FVector Right=Axes.GetUnitAxis(EAxis::Y),Up=Axes.GetUnitAxis(EAxis::Z);
        auto Project=[&](FVector P){FVector2D S;FSceneView::ProjectWorldToScreen(P,ViewRect,ViewProjection,S);return FVector2D((S.X-ViewRect.Min.X)/FMath::Max(1,ViewRect.Width()),(S.Y-ViewRect.Min.Y)/FMath::Max(1,ViewRect.Height()));};
        Core=Project(FVector::ZeroVector);
        // This camera-facing plane has constant clip depth. Project its basis
        // once, then share double-precision overlay coordinates for rings/ticks.
        const FVector2D X=Project(Right*FCLPlanetNavigation::Radius)-Core,Y=Project(Up*FCLPlanetNavigation::Radius)-Core;
        Sky=Core+X*FMath::Cos(UE_DOUBLE_PI/4)+Y*FMath::Sin(UE_DOUBLE_PI/4);
        Indicators.Add({TEXT("Core"),FLinearColor::White,Core,-2});Indicators.Add({TEXT("Sky boundary"),FLinearColor::White,Sky,-3});
        auto Ring=[&](double Radius)
        {TArray<FVector2D> Points;for(int32 I=0;I<=96;++I){const double A=2*UE_DOUBLE_PI*I/96;Points.Add(Core+(X*FMath::Cos(A)+Y*FMath::Sin(A))*Radius);}Rings.Add(MoveTemp(Points));};
        Ring(1);
        if(bGuides)for(int32 I=0;I<Guides.Guides.Num();++I)
        {const auto& G=Guides.Guides[I];Indicators.Add({G.Name.ToString(),FLinearColor::White,FMath::Lerp(Core,Sky,G.NormalizedHeight),I});Ring(G.NormalizedHeight);}
        Indicators.Sort([](const auto& A,const auto& B){return A.Position.Y<B.Position.Y;});
    }
    virtual void Draw(const FSceneView* View,FPrimitiveDrawInterface* PDI) override
    {FEditorViewportClient::Draw(View,PDI);ViewProjection=View->ViewMatrices.GetViewProjectionMatrix();ViewRect=View->UnscaledViewRect;bHasProjection=true;UpdateIndicators();}
    double NormalizedAtScreenY(double Y)const
    {return bHasProjection&&Core.Y-Sky.Y>1.e-9?FMath::Clamp((Core.Y-Y)/(Core.Y-Sky.Y),0.,1.):0;}

};
class FCLGlobeScene : public FPreviewScene
{
public:
    FCLGlobeScene():FPreviewScene(ConstructionValues()){DirectionalLight->SetForwardShadingPriority(1);}
};
void SCLPlanetPreview::Construct(const FArguments&)
{
    Scene=MakeUnique<FCLGlobeScene>();
    Scene->SetSkyCubemap(LoadObject<UTextureCube>(nullptr,TEXT("/Engine/MapTemplates/Sky/SunsetAmbientCubemap.SunsetAmbientCubemap")));Scene->SetSkyBrightness(.25);
    Scene->SetLightBrightness(2.2);Scene->SetLightDirection(FRotator(-35,-30,0));
    auto* Fill=NewObject<UDirectionalLightComponent>(GetTransientPackage());Fill->SetIntensity(1.3);Fill->SetLightColor(FLinearColor(.65,.8,1));Fill->SetCastShadows(false);Scene->AddComponent(Fill,FTransform(FRotator(-15,145,0)));
    Mesh.Reset(NewObject<UProceduralMeshComponent>(GetTransientPackage()));Scene->AddComponent(Mesh.Get(),FTransform::Identity);
    Mesh->SetMaterial(0,LoadObject<UMaterial>(nullptr,TEXT("/CatalystLandform/Designer/M_Section.M_Section")));
    SEditorViewport::Construct(SEditorViewport::FArguments());
}
SCLPlanetPreview::~SCLPlanetPreview(){if(Client)Client->Viewport=nullptr;if(Mesh&&Scene)Scene->RemoveComponent(Mesh.Get());}
TSharedRef<FEditorViewportClient> SCLPlanetPreview::MakeEditorViewportClient(){Client=MakeShared<FCLGlobeClient>(Scene.Get(),SharedThis(this));return Client.ToSharedRef();}
void SCLPlanetPreview::Rebuild(TSharedPtr<const FCLCompiledPlanetRecipe> Plan){++Revision;Pending=MoveTemp(Plan);Pump();}
void SCLPlanetPreview::Cancel(){++Revision;Pending.Reset();}
bool SCLPlanetPreview::IsUpdating()const{return Task.IsValid()||Pending.IsValid();}
void SCLPlanetPreview::Frame(){if(Client){Client->Navigation.Zoom=1;Client->Navigation.Pivot=FVector::ZeroVector;Client->Navigation.Pan=FVector2D::ZeroVector;Client->Apply();}}
void SCLPlanetPreview::Dolly(double Steps){Client->Dolly(Steps);}
void SCLPlanetPreview::SetGuides(const FCLRadialGuideSet& Guides,bool Visible){Client->Guides=Guides;Client->bGuides=Visible;Client->UpdateIndicators();Client->Invalidate();}
double SCLPlanetPreview::NormalizedAtScreenY(double Y)const{return Client->NormalizedAtScreenY(Y);}
const TArray<FCLGuideIndicator>& SCLPlanetPreview::Indicators()const{return Client->Indicators;}
const TArray<TArray<FVector2D>>& SCLPlanetPreview::Rings()const{return Client->Rings;}
void SCLPlanetPreview::Pump()
{
    if(Task.IsValid()&&Task.IsReady())
    {
        auto B=Task.Consume(); // Move the completed buffers; do not copy a dense mesh on the UI thread.
        if(TaskRevision==Revision)
        {
            Mesh->CreateMeshSection_LinearColor(0,B.Vertices,B.Triangles,B.Normals,B.UV,B.Colors,B.Tangents,false);
            if(!Framed){Framed=true;Frame();}else Client->Apply();
        }
    }
    if(!Task.IsValid()&&Pending){auto P=Pending;Pending.Reset();TaskRevision=Revision;Task=Async(EAsyncExecution::ThreadPool,[P]{return FCLGlobeBuffers::Build(*P);});}
}
void SCLPlanetPreview::Tick(const FGeometry& G,double T,float D){SEditorViewport::Tick(G,T,D);Pump();Scene->UpdateCaptureContents();}
FReply SCLPlanetPreview::OnPreviewMouseButtonDown(const FGeometry&,const FPointerEvent& E)
{
    const FKey Key=E.GetEffectingButton();
    if(Key!=EKeys::LeftMouseButton && Key!=EKeys::MiddleMouseButton && Key!=EKeys::RightMouseButton)return FReply::Unhandled();
    Button=Key;Previous=E.GetScreenSpacePosition();return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));
}
FReply SCLPlanetPreview::OnMouseMove(const FGeometry&,const FPointerEvent& E)
{
    if(!HasMouseCapture())return FReply::Unhandled();const auto Delta=E.GetScreenSpacePosition()-Previous;Previous=E.GetScreenSpacePosition();
    if(Button==EKeys::LeftMouseButton){Client->Navigation.Orbit(Delta);Client->Apply();}
    else if(Button==EKeys::MiddleMouseButton){Client->Navigation.PanBy(Delta);Client->Apply();}
    else if(Button==EKeys::RightMouseButton)Client->Dolly(-Delta.Y*.05);return FReply::Handled();
}
FReply SCLPlanetPreview::OnMouseButtonUp(const FGeometry&,const FPointerEvent& E)
{
    if(!HasMouseCapture() || E.GetEffectingButton()!=Button)return FReply::Unhandled();
    Button=FKey();return FReply::Handled().ReleaseMouseCapture();
}
void SCLPlanetPreview::OnMouseCaptureLost(const FCaptureLostEvent&){Button=FKey();}
FReply SCLPlanetPreview::OnMouseWheel(const FGeometry&,const FPointerEvent& E){Client->Dolly(E.GetWheelDelta());return FReply::Handled();}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGlobePreviewTest,"Catalyst.Landform.PlanetStructure.GlobeMesh",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGlobePreviewTest::RunTest(const FString&)
{
    auto R=FCLPlanetCompiler::BasePlanet();FCLPlanetFeature F;F.Id=FGuid::NewGuid();F.LocationDegrees=FVector2D(45,0);R.BaseStructure.Add(F);FString Error;
    const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}const auto Mesh=FCLGlobeBuffers::Build(*P,16);
    TArray<double> H;P->SampleHeightsMeters(Mesh.SourceDirections,H);
    TestEqual(TEXT("Six complete preview faces"),Mesh.Vertices.Num(),6*17*17);
    for(int32 I=0;I<H.Num();++I){TestTrue(TEXT("Displayed vertex is the compiled spherical surface"),FMath::IsNearlyEqual(Mesh.Vertices[I].Size(),(1000+H[I])*100,1.e-7));TestTrue(TEXT("Finite unit normal"),!Mesh.Normals[I].ContainsNaN()&&FMath::IsNearlyEqual(Mesh.Normals[I].Size(),1.,1.e-9));}
    for(int32 I=0;I<Mesh.Triangles.Num();I+=3){const FVector A=Mesh.Vertices[Mesh.Triangles[I]],B=Mesh.Vertices[Mesh.Triangles[I+1]],C=Mesh.Vertices[Mesh.Triangles[I+2]];TestTrue(TEXT("UE clockwise outward winding"),FVector::DotProduct(FVector::CrossProduct(B-A,C-A),A)<0);}
    return true;
}
#endif
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGlobeGuidesTest,"Catalyst.Landform.PlanetBuilder.GuideProjection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGlobeGuidesTest::RunTest(const FString&)
{
    const auto Widget=SNew(SCLPlanetPreview);const auto C=Widget->Client;
    for(double Aspect:{.5,1.,2.5})
    {
        C->ViewRect=FIntRect(0,0,1920,int32(1920/Aspect));C->bHasProjection=true;
        const FRotationMatrix Axes(C->GetViewRotation());const FVector Right=Axes.GetUnitAxis(EAxis::Y),Up=Axes.GetUnitAxis(EAxis::Z);
        C->ViewProjection=FMatrix::Identity;
        for(int32 I=0;I<3;++I){C->ViewProjection.M[I][0]=Right[I]/150000;C->ViewProjection.M[I][1]=Up[I]/150000;C->ViewProjection.M[I][3]=0;}
        C->ViewProjection.M[3][0]=C->ViewProjection.M[3][1]=0;
        Widget->SetGuides(FCLRadialGuidePresets::Defaults()[0],true);
        TestEqual(TEXT("Core, sky and all four guide labels exist"),Widget->Indicators().Num(),6);
        TestEqual(TEXT("Sky plus four radial section circles"),Widget->Rings().Num(),5);
        for(const auto& I:Widget->Indicators())if(I.Index>=0)
            {TestTrue(TEXT("Drag inverts the displayed guide anchor"),FMath::IsNearlyEqual(Widget->NormalizedAtScreenY(I.Position.Y),C->Guides.Guides[I.Index].NormalizedHeight,1.e-9));
            TestTrue(TEXT("Indicator lies on the matching rendered circle"),I.Position.Equals(Widget->Rings()[I.Index+1][12],1.e-9));}
        auto Changed=C->Guides;Changed.Guides[0].NormalizedHeight=.72;Widget->SetGuides(Changed,true);
        TestEqual(TEXT("Guide overlay updates without mesh work"),C->Guides.Guides[0].NormalizedHeight,.72);
        TestFalse(TEXT("Overlay-only edit schedules no terrain worker"),Widget->IsUpdating());
        Widget->SetGuides(Changed,false);TestEqual(TEXT("Hidden guides leave core and sky only"),Widget->Indicators().Num(),2);
    }
    return true;
}
#endif
#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Widgets/SWindow.h"
#include "Widgets/SViewport.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGlobePointerTest,"Catalyst.Landform.PlanetBuilder.RoutedPointerOrbit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGlobePointerTest::RunTest(const FString&)
{
    auto& App=FSlateApplication::Get();const auto Widget=SNew(SCLPlanetPreview);
    const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Globe pointer routing test"))).ClientSize(FVector2D(800,600))[Widget];
    App.AddWindow(Window,false);Window->SlatePrepass();
    FWidgetPath WidgetPath;App.GeneratePathToWidgetUnchecked(Widget->ViewportWidget.ToSharedRef(),WidgetPath,EVisibility::All);
    // FindPathToWidget produces a focus path. Pointer routing also requires one
    // optional virtual-pointer slot per arranged widget (UE 5.7's modern ctor).
    TArray<FWidgetAndPointer> PointerWidgets;for(const auto& W:WidgetPath.Widgets.GetInternalArray())PointerWidgets.Emplace(W,TOptional<FVirtualPointerPosition>());
    const FWidgetPath Path(MakeArrayView(PointerWidgets));
    if(!TestTrue(TEXT("Actual inner viewport is in the routed widget path"),Path.IsValid())){App.RequestDestroyWindow(Window);return false;}
    const FVector2D Start(300,300),End(420,340);const auto InitialRotation=Widget->Client->GetViewRotation();const double InitialZoom=Widget->Client->Navigation.Zoom;
    const FPointerEvent Down(FSlateApplication::CursorPointerIndex,Start,Start,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());
    const auto Reply=App.RoutePointerDownEvent(Path,Down);
    TestTrue(TEXT("Globe intercepts the press before the inner viewport"),Reply.GetMouseCaptor()==Widget);
    TestTrue(TEXT("Globe owns pointer capture"),Widget->HasMouseCapture());
    const FPointerEvent Move(FSlateApplication::CursorPointerIndex,End,Start,TSet<FKey>{EKeys::LeftMouseButton},FKey(),0,FModifierKeysState());
    App.RoutePointerMoveEvent(Path,Move,false);
    TestFalse(TEXT("A routed drag rotates the actual camera"),Widget->Client->GetViewRotation().Equals(InitialRotation,.01));
    TestEqual(TEXT("Orbit does not change zoom"),Widget->Client->Navigation.Zoom,InitialZoom);
    const FPointerEvent Up(FSlateApplication::CursorPointerIndex,End,End,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState());
    App.RoutePointerUpEvent(Path,Up);TestFalse(TEXT("Release ends globe capture"),Widget->HasMouseCapture());
    TestFalse(TEXT("Uncaptured move is ignored"),Widget->OnMouseMove(FGeometry(),Move).IsEventHandled());
    Widget->Client->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseScrollUp,IE_Pressed,1));
    TestTrue(TEXT("The inner viewport wheel-key path zooms the globe"),Widget->Client->Navigation.Zoom<InitialZoom);
    const double WheelZoom=Widget->Client->Navigation.Zoom;
    Widget->Client->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseScrollUp,IE_Released,1));
    TestEqual(TEXT("Wheel release does not apply a second zoom step"),Widget->Client->Navigation.Zoom,WheelZoom);
    App.ReleaseAllPointerCapture();App.RequestDestroyWindow(Window);return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "CLSectionGeometry.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLAssemblyPreviewTest,"Catalyst.Landform.PlanetAssembly.DesignerAndGlobeParity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLAssemblyPreviewTest::RunTest(const FString&)
{
    FString J,E;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/PlanetAssembly.json")));
    auto* A=NewObject<UCLPlanetRecipeAsset>();if(!A->ImportRecipeJson(J,E)){AddError(E);return false;}const auto P=FCLPlanetCompiler::Compile(A->Recipe,E);if(!P){AddError(E);return false;}
    const auto Globe=FCLGlobeBuffers::Build(*P,24);const auto Section=FCLSectionGeometry::Build(*P,0,500);double Worst=0;
    TArray<double> H;P->SampleHeightsMeters(Globe.SourceDirections,H);
    for(int32 I=0;I<H.Num();++I)Worst=FMath::Max(Worst,FMath::Abs(Globe.Vertices[I].Size()-(P->RadiusMeters+H[I])/P->RadiusMeters*100000));
    TArray<FVector> Directions;for(const auto& V:Section.SourceSurfacePoints)Directions.Add(V.GetSafeNormal());P->SampleHeightsMeters(Directions,H);
    double Min=0,Max=-1.e9;
    for(int32 I=0;I<H.Num();++I){Worst=FMath::Max(Worst,FMath::Abs(Section.SourceSurfacePoints[I].Size()-(P->RadiusMeters+H[I])*100));Worst=FMath::Max(Worst,FMath::Abs(Section.Vertices[I].Z-(P->RadiusMeters+H[I])/P->RadiusMeters*100000));Min=FMath::Min(Min,H[I]);Max=FMath::Max(Max,H[I]);}
    TestTrue(TEXT("Cutaway covers real terrain and canyon relief"),Max-Min>80);TestTrue(TEXT("Unwrapped cutaway and globe retain the exact assembly field"),Worst<.01);
    AddInfo(FString::Printf(TEXT("Assembly preview: %d globe and %d section vertices; %.9g cm maximum field residual"),Globe.Vertices.Num(),Section.SourceSurfacePoints.Num(),Worst));return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGlobeDetailTest,"Catalyst.Landform.PlanetBuilder.GlobeDetail",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGlobeDetailTest::RunTest(const FString&)
{
    FString J,E;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/PlanetAssembly.json")));
    auto* A=NewObject<UCLPlanetRecipeAsset>();if(!A->ImportRecipeJson(J,E)){AddError(E);return false;}const auto P=FCLPlanetCompiler::Compile(A->Recipe,E);if(!P){AddError(E);return false;}
    const double Start=FPlatformTime::Seconds();const auto Coarse=FCLGlobeBuffers::Build(*P,64);const double FineStart=FPlatformTime::Seconds();const auto Fine=FCLGlobeBuffers::Build(*P);const double FineSeconds=FPlatformTime::Seconds()-FineStart,CoarseSeconds=FineStart-Start;
    TestEqual(TEXT("Detailed globe uses 512 cells on each cube face"),Fine.Vertices.Num(),6*513*513);
    TestEqual(TEXT("Sixty-four times as many triangles as the original overview"),Fine.Triangles.Num(),Coarse.Triangles.Num()*64);
    // Common source directions inside triangles, not vertices: measure the actual
    // rendered triangle planes against the authoritative field between samples.
    TArray<FVector> Directions;TArray<double> C,F;
    auto Radius=[&](const FCLGlobeBuffers& Mesh,int32 N,int32 Face,double X,double Y,const FVector& D)
    {
        const double U=(X+1)*.5*N,V=(Y+1)*.5*N;const int32 IX=FMath::Min(int32(U),N-1),IY=FMath::Min(int32(V),N-1),Row=N+1,K=Face*Row*Row+IY*Row+IX;
        const bool First=U-IX+V-IY<=1;const FVector A=Mesh.Vertices[First?K:K+1],B=Mesh.Vertices[K+Row],C=Mesh.Vertices[First?K+1:K+Row+1];
        const FVector Normal=FVector::CrossProduct(B-A,C-A);return FVector::DotProduct(A,Normal)/FVector::DotProduct(D,Normal)*P->RadiusMeters/100000;
    };
    for(int32 Face=0;Face<6;++Face)for(int32 Y=0;Y<64;++Y)for(int32 X=0;X<64;++X)
    {
        const double U=2*(X+.37)/64-1,V=2*(Y+.41)/64-1;const FVector D=FCLSphericalPatches::Direction(Face,FVector2D(U,V));Directions.Add(D);
        C.Add(Radius(Coarse,64,Face,U,V,D));F.Add(Radius(Fine,512,Face,U,V,D));
    }
    TArray<double> H;P->SampleHeightsMeters(Directions,H);double CoarseSum=0,FineSum=0,CoarseMax=0,FineMax=0;
    for(int32 I=0;I<H.Num();++I){const double Expected=P->RadiusMeters+H[I],CE=FMath::Abs(C[I]-Expected),FE=FMath::Abs(F[I]-Expected);CoarseSum+=CE*CE;FineSum+=FE*FE;CoarseMax=FMath::Max(CoarseMax,CE);FineMax=FMath::Max(FineMax,FE);}
    const double CoarseRms=FMath::Sqrt(CoarseSum/H.Num()),FineRms=FMath::Sqrt(FineSum/H.Num());
    TestTrue(TEXT("Finer globe reduces between-vertex terrain error"),FineRms<CoarseRms*.5&&FineMax<CoarseMax);
    // Matching per-face boundary vertices retain one common radial surface.
    double Seam=0;constexpr int32 Row=513;
    for(int32 Face=0;Face<6;++Face)for(int32 Edge=0;Edge<4;++Edge)for(int32 I=0;I<Row;++I)
    {
        const int32 X=Edge==0?0:Edge==1?512:I,Y=Edge==2?0:Edge==3?512:I,K=Face*Row*Row+Y*Row+X;const FVector D=Fine.SourceDirections[K];
        Seam=FMath::Max(Seam,FMath::Abs(Fine.Vertices[K].Size()-(P->RadiusMeters+P->SampleHeightMeters(D))/P->RadiusMeters*100000));
    }
    TestTrue(TEXT("Detailed face boundaries still match the exact shared field"),Seam<.01);
    const FString Report=FString::Printf(TEXT("{\"passed\":%s,\"cells_per_face\":512,\"vertices\":%d,\"triangles\":%d,\"samples\":%d,\"coarse_rms_m\":%.12g,\"fine_rms_m\":%.12g,\"coarse_max_m\":%.12g,\"fine_max_m\":%.12g,\"coarse_seconds\":%.6f,\"fine_seconds\":%.6f,\"max_boundary_field_residual_cm\":%.12g,\"maximum_sample_batch\":20480}"),HasAnyErrors()?TEXT("false"):TEXT("true"),Fine.Vertices.Num(),Fine.Triangles.Num()/3,H.Num(),CoarseRms,FineRms,CoarseMax,FineMax,CoarseSeconds,FineSeconds,Seam);
    IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("Verification")),true);FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectSavedDir()/TEXT("Verification/globe-detail.json")));
    AddInfo(Report);return true;
}
#endif

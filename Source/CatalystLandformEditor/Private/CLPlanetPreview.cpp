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
    FCLGlobeBuffers B;const int32 N=FMath::Clamp(Resolution,8,256),Row=N+1;TArray<FVector> Samples;
    for(int32 Face=0;Face<6;++Face)for(int32 Y=0;Y<=N;++Y)for(int32 X=0;X<=N;++X)
    {
        const FVector D=FCLSphericalPatches::Direction(Face,FVector2D(2.*X/N-1,2.*Y/N-1));B.SourceDirections.Add(D);
        const FVector T=FVector::CrossProduct(D,FMath::Abs(D.Z)<.8?FVector::ZAxisVector:FVector::XAxisVector).GetSafeNormal(),S=FVector::CrossProduct(D,T);
        Samples.Append({D,(D+T*.0001).GetSafeNormal(),(D-T*.0001).GetSafeNormal(),(D+S*.0001).GetSafeNormal(),(D-S*.0001).GetSafeNormal()});
    }
    TArray<double> H;Plan.SampleHeightsMeters(Samples,H);
    auto Point=[&](int32 I){return Samples[I]*((Plan.RadiusMeters+H[I])/Plan.RadiusMeters*100000);};
    B.DisplayRadius=(Plan.RadiusMeters+Plan.MaximumHeightMeters)/Plan.RadiusMeters*100000;
    for(int32 I=0;I<B.SourceDirections.Num();++I)
    {
        const int32 K=5*I;B.Vertices.Add(Point(K));FVector Normal=FVector::CrossProduct(Point(K+1)-Point(K+2),Point(K+3)-Point(K+4)).GetSafeNormal();
        if(FVector::DotProduct(Normal,Samples[K])<0)Normal*=-1;B.Normals.Add(Normal);
        const double Slope=FVector::DotProduct(Normal,Samples[K]);B.Colors.Add(FMath::Lerp(FLinearColor(.38,.36,.32),FLinearColor(.34,.43,.28),FMath::Clamp((Slope-.6)/.4,0.,1.)));
        B.UV.Add(FVector2D::ZeroVector);B.Tangents.Add(FProcMeshTangent(FVector::VectorPlaneProject(FVector::XAxisVector,Normal).GetSafeNormal(),false));
    }
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
    virtual bool InputKey(const FInputKeyEventArgs&) override {return true;}
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
        Core=Project(FVector::ZeroVector);Sky=Project((Right+Up).GetSafeNormal()*FCLPlanetNavigation::Radius);
        Indicators.Add({TEXT("Core"),FLinearColor::White,Core,-2});Indicators.Add({TEXT("Sky boundary"),FLinearColor::White,Sky,-3});
        auto Ring=[&](double Radius)
        {TArray<FVector2D> Points;for(int32 I=0;I<=96;++I){const double A=2*UE_PI*I/96;Points.Add(Project((Right*FMath::Cos(A)+Up*FMath::Sin(A))*FCLPlanetNavigation::Radius*Radius));}Rings.Add(MoveTemp(Points));};
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
        auto B=Task.Get();Task=TFuture<FCLGlobeBuffers>();
        if(TaskRevision==Revision)
        {
            Mesh->CreateMeshSection_LinearColor(0,B.Vertices,B.Triangles,B.Normals,B.UV,B.Colors,B.Tangents,false);
            if(!Framed){Framed=true;Frame();}else Client->Apply();
        }
    }
    if(!Task.IsValid()&&Pending){auto P=Pending;Pending.Reset();TaskRevision=Revision;Task=Async(EAsyncExecution::ThreadPool,[P]{return FCLGlobeBuffers::Build(*P);});}
}
void SCLPlanetPreview::Tick(const FGeometry& G,double T,float D){SEditorViewport::Tick(G,T,D);Pump();Scene->UpdateCaptureContents();}
FReply SCLPlanetPreview::OnMouseButtonDown(const FGeometry&,const FPointerEvent& E){Button=E.GetEffectingButton();Previous=E.GetScreenSpacePosition();return FReply::Handled().CaptureMouse(SharedThis(this));}
FReply SCLPlanetPreview::OnMouseMove(const FGeometry&,const FPointerEvent& E)
{
    if(!HasMouseCapture())return FReply::Unhandled();const auto Delta=E.GetScreenSpacePosition()-Previous;Previous=E.GetScreenSpacePosition();
    if(Button==EKeys::LeftMouseButton){Client->Navigation.Orbit(Delta);Client->Apply();}
    else if(Button==EKeys::MiddleMouseButton){Client->Navigation.PanBy(Delta);Client->Apply();}
    else if(Button==EKeys::RightMouseButton)Client->Dolly(-Delta.Y*.05);return FReply::Handled();
}
FReply SCLPlanetPreview::OnMouseButtonUp(const FGeometry&,const FPointerEvent&){Button=FKey();return FReply::Handled().ReleaseMouseCapture();}
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
            TestTrue(TEXT("Drag inverts the displayed guide anchor"),FMath::IsNearlyEqual(Widget->NormalizedAtScreenY(I.Position.Y),C->Guides.Guides[I.Index].NormalizedHeight,1.e-9));
        auto Changed=C->Guides;Changed.Guides[0].NormalizedHeight=.72;Widget->SetGuides(Changed,true);
        TestEqual(TEXT("Guide overlay updates without mesh work"),C->Guides.Guides[0].NormalizedHeight,.72);
        TestFalse(TEXT("Overlay-only edit schedules no terrain worker"),Widget->IsUpdating());
        Widget->SetGuides(Changed,false);TestEqual(TEXT("Hidden guides leave core and sky only"),Widget->Indicators().Num(),2);
    }
    return true;
}
#endif

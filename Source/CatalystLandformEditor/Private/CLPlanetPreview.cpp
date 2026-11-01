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
#include "SceneManagement.h"

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
    TSharedPtr<const FCLCompiledPlanetRecipe> Plan;
    TArray<FCLGlobePlacementTarget> Targets;
    FCLGlobeSelection Selected;
    ECLGlobePlacementMode Mode=ECLGlobePlacementMode::Move;
    TOptional<FCLGlobePlacementTarget> Draft;
    struct FOutline {FCLGlobeSelection Key;TArray<FVector> Points;FVector Centre;};
    struct FHandle {ECLGlobePlacementHandle Kind;FVector Point;};
    TArray<FOutline> Outlines;
    TArray<FVector> RotationRing;
    TArray<FHandle> Handles;
    const FCLGlobePlacementTarget* SelectedTarget() const
    {if(Draft.IsSet())return &Draft.GetValue();return Targets.FindByPredicate([&](const auto& T){return T.Key==Selected;});}
    void UpdatePlacementOverlay()
    {
        Outlines.Reset();Handles.Reset();RotationRing.Reset();if(!Plan)return;
        TArray<FVector> Directions;TArray<int32> Centres;
        for(const auto& Original:Targets)
        {
            const auto& T=Draft.IsSet()&&Draft->Key==Original.Key?Draft.GetValue():Original;
            FOutline O;O.Key=T.Key;Centres.Add(Directions.Num());Directions.Add(T.Frame.GetAxisZ());
            for(int32 I=0;I<=64;++I)
            {
                const double A=2*UE_DOUBLE_PI*I/64;FVector2D V(FMath::Cos(A),FMath::Sin(A));
                if(T.bRectangle)V/=V.GetAbs().GetMax();
                Directions.Add(T.Frame.RotateVector(FCLGlobePlacementEditing::Direction(V*T.HalfExtentDegrees*(UE_DOUBLE_PI/180))));
            }
            Outlines.Add(MoveTemp(O));
        }
        TArray<double> Heights;Plan->SampleHeightsMeters(Directions,Heights);
        auto Point=[&](int32 I){return Directions[I]*((Plan->RadiusMeters+Heights[I])/Plan->RadiusMeters*100000+140);};
        for(int32 I=0;I<Outlines.Num();++I){Outlines[I].Centre=Point(Centres[I]);for(int32 J=1;J<=65;++J)Outlines[I].Points.Add(Point(Centres[I]+J));}
        const auto* T=SelectedTarget();if(!T)return;
        TArray<FVector> HD;TArray<ECLGlobePlacementHandle> Kinds;
        auto Add=[&](ECLGlobePlacementHandle Kind,FVector2D A){HD.Add(T->Frame.RotateVector(FCLGlobePlacementEditing::Direction(A*(UE_DOUBLE_PI/180))));Kinds.Add(Kind);};
        Add(ECLGlobePlacementHandle::Centre,FVector2D::ZeroVector);
        if(Mode==ECLGlobePlacementMode::Rotate)
        {
            const double R=FMath::Min(8.,T->HalfExtentDegrees.GetMin()*.4);Add(ECLGlobePlacementHandle::Rotation,FVector2D(R,0));
            for(int32 I=0;I<=48;++I){const double A=2*UE_DOUBLE_PI*I/48;HD.Add(T->Frame.RotateVector(FCLGlobePlacementEditing::Direction(FVector2D(FMath::Cos(A),FMath::Sin(A))*R*(UE_DOUBLE_PI/180))));}
        }
        else if(Mode==ECLGlobePlacementMode::Resize)
        {
            Add(ECLGlobePlacementHandle::Width,FVector2D(T->HalfExtentDegrees.X,0));Add(ECLGlobePlacementHandle::Length,FVector2D(0,T->HalfExtentDegrees.Y));
            Add(ECLGlobePlacementHandle::Uniform,T->HalfExtentDegrees*(T->bRectangle?1:.7071067811865476));
        }
        Plan->SampleHeightsMeters(HD,Heights);
        for(int32 I=0;I<HD.Num();++I){const FVector P=HD[I]*((Plan->RadiusMeters+Heights[I])/Plan->RadiusMeters*100000+220);if(I<Kinds.Num())Handles.Add({Kinds[I],P});else RotationRing.Add(P);}
    }
    bool Project(const FVector& P,FVector2D& Pixel) const
    {return bHasProjection&&FVector::DotProduct(GetViewLocation()-P,P)>0&&FSceneView::ProjectWorldToScreen(P,ViewRect,ViewProjection,Pixel);}
    bool CursorDirection(FVector2D Normalized,FVector& Direction) const
    {
        if(!Plan||!bHasProjection)return false;FVector O,V;const FVector2D Pixel=FVector2D(ViewRect.Min)+Normalized*FVector2D(ViewRect.Size());
        FSceneView::DeprojectScreenToWorld(Pixel,ViewRect,ViewProjection.Inverse(),O,V);V.Normalize();
        const double Radius=(Plan->RadiusMeters+Plan->MaximumHeightMeters)/Plan->RadiusMeters*100000+200;
        const double B=FVector::DotProduct(O,V),C=O.SizeSquared()-Radius*Radius,Disc=B*B-C;if(Disc<0)return false;
        const double Lo=FMath::Max(0.,-B-FMath::Sqrt(Disc)),Hi=-B+FMath::Sqrt(Disc);if(Hi<=Lo)return false;
        constexpr int32 Steps=96;TArray<FVector> D;TArray<double> Radial;
        for(int32 I=0;I<=Steps;++I){const FVector P=O+V*FMath::Lerp(Lo,Hi,double(I)/Steps);D.Add(P.GetSafeNormal());Radial.Add(P.Size());}
        TArray<double> H;Plan->SampleHeightsMeters(D,H);int32 Hit=INDEX_NONE;
        for(int32 I=1;I<=Steps;++I)if(Radial[I]-(Plan->RadiusMeters+H[I])/Plan->RadiusMeters*100000<=0){Hit=I;break;}
        if(Hit==INDEX_NONE)return false;
        double A=FMath::Lerp(Lo,Hi,double(Hit-1)/Steps),Z=FMath::Lerp(Lo,Hi,double(Hit)/Steps);
        for(int32 I=0;I<14;++I){const double M=(A+Z)*.5;const FVector P=O+V*M;const double F=P.Size()-(Plan->RadiusMeters+Plan->SampleHeightMeters(P))/Plan->RadiusMeters*100000;if(F>0)A=M;else Z=M;}
        Direction=(O+V*((A+Z)*.5)).GetSafeNormal();return true;
    }
    ECLGlobePlacementHandle HitHandle(FVector2D Normalized) const
    {
        const FVector2D Pixel=FVector2D(ViewRect.Min)+Normalized*FVector2D(ViewRect.Size());double Best=22*22;auto Result=ECLGlobePlacementHandle::None;
        for(const auto& H:Handles)
        {
            if(H.Kind==ECLGlobePlacementHandle::Centre&&Mode!=ECLGlobePlacementMode::Move)continue;
            FVector2D P;if(Project(H.Point,P)){const double E=(P-Pixel).SizeSquared();if(E<Best){Best=E;Result=H.Kind;}}
        }
        if(Mode==ECLGlobePlacementMode::Rotate)for(int32 I=1;I<RotationRing.Num();++I)
        {
            FVector2D A,B;if(!Project(RotationRing[I-1],A)||!Project(RotationRing[I],B))continue;
            const FVector2D AB=B-A;const double T=AB.SizeSquared()>1.e-9?FMath::Clamp(FVector2D::DotProduct(Pixel-A,AB)/AB.SizeSquared(),0.,1.):0;
            if((Pixel-(A+AB*T)).SizeSquared()<144)return ECLGlobePlacementHandle::Rotation;
        }
        return Result;
    }
    FCLGlobeSelection PickTarget(FVector2D Normalized) const
    {
        const FVector2D Pixel=FVector2D(ViewRect.Min)+Normalized*FVector2D(ViewRect.Size());double Best=18*18;FCLGlobeSelection Result;
        for(const auto& O:Outlines){FVector2D P;if(Project(O.Centre,P)){const double E=(P-Pixel).SizeSquared();if(E<Best){Best=E;Result=O.Key;}}}
        if(Result.IsValid())return Result;
        FVector D;if(!CursorDirection(Normalized,D))return {};
        double Contribution=0,Remaining=1;
        for(int32 I=Targets.Num()-1;I>=0;--I)if(!Targets[I].Key.bStructure)
        {const double W=Targets[I].Weight(D),C=W*Remaining;Remaining*=1-W;if(C>Contribution){Contribution=C;Result=Targets[I].Key;}}
        if(Contribution>.01)return Result;
        for(const auto& T:Targets)if(T.Key.bStructure&&T.bRectangle&&T.Weight(D)>0)return T.Key;
        for(const auto& T:Targets)if(T.Key.bStructure&&T.Weight(D)>.01)return T.Key;
        return {};
    }
    void DrawPlacements(FPrimitiveDrawInterface* PDI)
    {
        for(const auto& O:Outlines)
        {
            const bool IsSelected=O.Key==Selected;const FLinearColor C=IsSelected?FLinearColor(.05,.8,1):FLinearColor(.16,.3,.35);
            for(int32 I=1;I<O.Points.Num();++I)PDI->DrawLine(O.Points[I-1],O.Points[I],C,SDPG_World,IsSelected?2.5:1.2);
            FVector2D Pixel;if(Project(O.Centre,Pixel))PDI->DrawPoint(O.Centre,C,IsSelected?15:8,SDPG_Foreground);
        }
        for(int32 I=1;I<RotationRing.Num();++I)PDI->DrawLine(RotationRing[I-1],RotationRing[I],FLinearColor(1,.7,.15),SDPG_World,3);
        for(const auto& H:Handles)
        {
            if(H.Kind==ECLGlobePlacementHandle::Centre&&Mode!=ECLGlobePlacementMode::Move)continue;
            FVector2D Pixel;if(!Project(H.Point,Pixel))continue;
            const FLinearColor C=H.Kind==ECLGlobePlacementHandle::Width?FLinearColor(1,.3,.2):H.Kind==ECLGlobePlacementHandle::Length?FLinearColor(.3,1,.4):FLinearColor(1,.8,.2);
            PDI->DrawPoint(H.Point,C,18,SDPG_Foreground);
        }
    }
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
    {FEditorViewportClient::Draw(View,PDI);ViewProjection=View->ViewMatrices.GetViewProjectionMatrix();ViewRect=View->UnscaledViewRect;bHasProjection=true;UpdateIndicators();DrawPlacements(PDI);}
    double NormalizedAtScreenY(double Y)const
    {return bHasProjection&&Core.Y-Sky.Y>1.e-9?FMath::Clamp((Core.Y-Y)/(Core.Y-Sky.Y),0.,1.):0;}

};
class FCLGlobeScene : public FPreviewScene
{
public:
    FCLGlobeScene():FPreviewScene(ConstructionValues()){DirectionalLight->SetForwardShadingPriority(1);}
};
void SCLPlanetPreview::Construct(const FArguments& Args)
{
    OnPlacementCommitted=Args._OnPlacementCommitted;
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
            Client->Plan=BuildingPlan;Client->UpdatePlacementOverlay();
            Mesh->CreateMeshSection_LinearColor(0,B.Vertices,B.Triangles,B.Normals,B.UV,B.Colors,B.Tangents,false);
            if(!Framed){Framed=true;Frame();}else Client->Apply();
        }
        BuildingPlan.Reset();
    }
    if(!Task.IsValid()&&Pending){auto P=Pending;BuildingPlan=P;Pending.Reset();TaskRevision=Revision;Task=Async(EAsyncExecution::ThreadPool,[P]{return FCLGlobeBuffers::Build(*P);});}
}
void SCLPlanetPreview::Tick(const FGeometry& G,double T,float D){SEditorViewport::Tick(G,T,D);Pump();Scene->UpdateCaptureContents();}
FVector2D SCLPlanetPreview::PointerPosition(const FGeometry& G,const FPointerEvent& E) const
{return G.AbsoluteToLocal(E.GetScreenSpacePosition())/FVector2D(FMath::Max(1.,G.GetLocalSize().X),FMath::Max(1.,G.GetLocalSize().Y));}
void SCLPlanetPreview::SetAuthoringRecipe(const FCLPlanetRecipe& R)
{
    CancelPlacementDrag();Client->Targets=FCLGlobePlacementEditing::Resolve(R);
    if(!Client->Targets.ContainsByPredicate([&](const auto& T){return T.Key==Client->Selected;}))Client->Selected={};
    Client->UpdatePlacementOverlay();Client->Invalidate();
}
void SCLPlanetPreview::Select(FCLGlobeSelection Key)
{
    CancelPlacementDrag();Client->Selected=Client->Targets.ContainsByPredicate([&](const auto& T){return T.Key==Key;})?Key:FCLGlobeSelection();Client->UpdatePlacementOverlay();Client->Invalidate();
}
FCLGlobeSelection SCLPlanetPreview::Selection() const{return Client->Selected;}
FString SCLPlanetPreview::SelectionName() const{const auto* T=Client->SelectedTarget();return T?T->Name:TEXT("None");}
void SCLPlanetPreview::SetPlacementMode(ECLGlobePlacementMode Mode){if(DragHandle==ECLGlobePlacementHandle::None){Client->Mode=Mode;Client->UpdatePlacementOverlay();Client->Invalidate();}}
ECLGlobePlacementMode SCLPlanetPreview::PlacementMode() const{return Client->Mode;}
FString SCLPlanetPreview::PlacementHint() const
{
    if(DragHandle!=ECLGlobePlacementHandle::None)return TEXT("Release to apply placement and rebuild terrain | Esc: cancel");
    if(!Client->Selected.IsValid())return TEXT("Click a Landform / feature to select | Left drag: orbit");
    return Client->Mode==ECLGlobePlacementMode::Move?TEXT("Drag centre: move along globe | W: move | E: rotate | R: resize"):
        Client->Mode==ECLGlobePlacementMode::Rotate?TEXT("Drag gold ring handle: rotate | Esc: deselect"):
        TEXT("Drag red / green ends: resize width / length | Gold corner: resize both | Esc: deselect");
}
void SCLPlanetPreview::CancelPlacementDrag(){DragHandle=ECLGlobePlacementHandle::None;bPlacementMoved=false;if(Client){Client->Draft.Reset();Client->UpdatePlacementOverlay();Client->Invalidate();}}
FReply SCLPlanetPreview::OnPreviewMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
    const FKey Key=E.GetEffectingButton();if(Key!=EKeys::LeftMouseButton&&Key!=EKeys::MiddleMouseButton&&Key!=EKeys::RightMouseButton)return FReply::Unhandled();
    if(DragHandle!=ECLGlobePlacementHandle::None){CancelPlacementDrag();Button=FKey();return FReply::Handled().ReleaseMouseCapture();}
    Button=Key;Previous=MouseDown=E.GetScreenSpacePosition();bOrbitDrag=false;bPlacementMoved=false;
    if(Key==EKeys::LeftMouseButton)
    {
        const auto P=PointerPosition(G,E);const auto H=Client->HitHandle(P);const auto* T=Client->SelectedTarget();FVector D;
        if(H!=ECLGlobePlacementHandle::None&&T&&Client->CursorDirection(P,D)){DragHandle=H;DragStart=*T;DragStartDirection=D;}
        else ClickSelection=Client->PickTarget(P);
    }
    return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));
}
FReply SCLPlanetPreview::OnMouseMove(const FGeometry& G,const FPointerEvent& E)
{
    if(!HasMouseCapture())return FReply::Unhandled();const auto Delta=E.GetScreenSpacePosition()-Previous;Previous=E.GetScreenSpacePosition();
    if(DragHandle!=ECLGlobePlacementHandle::None)
    {
        FVector D;if(Client->CursorDirection(PointerPosition(G,E),D))
        {
            Client->Draft=FCLGlobePlacementEditing::Drag(DragStart,DragHandle,DragStartDirection,D);
            bPlacementMoved=!Client->Draft->Frame.Equals(DragStart.Frame,1.e-8)||!Client->Draft->HalfExtentDegrees.Equals(DragStart.HalfExtentDegrees,1.e-8);
            Client->UpdatePlacementOverlay();Client->Invalidate();
        }
    }
    else if(Button==EKeys::LeftMouseButton){bOrbitDrag|=(E.GetScreenSpacePosition()-MouseDown).Size()>3;if(bOrbitDrag){Client->Navigation.Orbit(Delta);Client->Apply();}}
    else if(Button==EKeys::MiddleMouseButton){Client->Navigation.PanBy(Delta);Client->Apply();}
    else if(Button==EKeys::RightMouseButton)Client->Dolly(-Delta.Y*.05);return FReply::Handled();
}
FReply SCLPlanetPreview::OnMouseButtonUp(const FGeometry&,const FPointerEvent& E)
{
    if(!HasMouseCapture()||E.GetEffectingButton()!=Button)return FReply::Unhandled();
    if(DragHandle!=ECLGlobePlacementHandle::None)
    {
        const auto Target=Client->Draft.IsSet()?Client->Draft.GetValue():DragStart;const bool Changed=bPlacementMoved;
        const bool Resize=DragHandle==ECLGlobePlacementHandle::Width||DragHandle==ECLGlobePlacementHandle::Length||DragHandle==ECLGlobePlacementHandle::Uniform;
        CancelPlacementDrag();if(Changed)OnPlacementCommitted.ExecuteIfBound(Target,Resize);
    }
    else if(Button==EKeys::LeftMouseButton&&!bOrbitDrag)Select(ClickSelection);
    Button=FKey();return FReply::Handled().ReleaseMouseCapture();
}
void SCLPlanetPreview::OnMouseCaptureLost(const FCaptureLostEvent&){Button=FKey();CancelPlacementDrag();}
FReply SCLPlanetPreview::OnPreviewKeyDown(const FGeometry&,const FKeyEvent& E)
{
    if(E.GetKey()==EKeys::Escape){const bool WasDragging=DragHandle!=ECLGlobePlacementHandle::None;CancelPlacementDrag();if(!WasDragging)Select({});Button=FKey();return FReply::Handled().ReleaseMouseCapture();}
    if(E.IsControlDown()||E.IsCommandDown()||E.IsAltDown())return FReply::Unhandled();
    if(E.GetKey()==EKeys::W)SetPlacementMode(ECLGlobePlacementMode::Move);else if(E.GetKey()==EKeys::E)SetPlacementMode(ECLGlobePlacementMode::Rotate);else if(E.GetKey()==EKeys::R)SetPlacementMode(ECLGlobePlacementMode::Resize);else return FReply::Unhandled();return FReply::Handled();
}
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

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGlobePlacementPointerTest,"Catalyst.Landform.PlanetBuilder.RoutedPlacementHandles",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGlobePlacementPointerTest::RunTest(const FString&)
{
    auto Recipe=FCLPlanetCompiler::BasePlanet();FCLTerrainRegion Region;Region.Id=FGuid::NewGuid();Region.bPlacedLandform=true;Region.bSphericalMask=true;Region.TerrainTypeId=FGuid::NewGuid();Region.TerrainTypeName=TEXT("Pointer test source");Region.Placement.HalfExtentDegrees=FVector2D(25,15);Recipe.TerrainRegions.Add(Region);const auto Original=Recipe;
    FString Error;const auto Plan=FCLPlanetCompiler::Compile(Recipe,Error);if(!Plan){AddError(Error);return false;}
    int32 Commits=0;auto& App=FSlateApplication::Get();const auto Widget=SNew(SCLPlanetPreview).OnPlacementCommitted(FOnCLGlobePlacementCommitted::CreateLambda([&](const FCLGlobePlacementTarget& T,bool Resize){++Commits;TestTrue(TEXT("Routed gesture applies valid planet placement"),FCLGlobePlacementEditing::Apply(Recipe,T,Resize,Error));}));
    const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Globe placement routing proof"))).ClientSize(FVector2D(800,600))[Widget];App.AddWindow(Window,false);Window->SlatePrepass();
    FWidgetPath Actual;App.GeneratePathToWidgetUnchecked(Widget->ViewportWidget.ToSharedRef(),Actual,EVisibility::All);
    const FGeometry Geometry=Actual.Widgets.Last().Geometry;TArray<FWidgetAndPointer> Arranged;
    for(const auto& W:Actual.Widgets.GetInternalArray())Arranged.Emplace(W,TOptional<FVirtualPointerPosition>());
    const FWidgetPath Path(MakeArrayView(Arranged));if(!Path.IsValid()){AddError(TEXT("Missing real viewport hierarchy"));App.RequestDestroyWindow(Window);return false;}
    AddInfo(FString::Printf(TEXT("Viewport geometry %.2f x %.2f at %.2f / %.2f"),Geometry.GetLocalSize().X,Geometry.GetLocalSize().Y,Geometry.GetAbsolutePosition().X,Geometry.GetAbsolutePosition().Y));
    auto Screen=[&](FVector2D Pixel){return Geometry.LocalToAbsolute(Pixel/FVector2D(800,600)*Geometry.GetLocalSize());};
    const auto Client=Widget->Client;Client->Plan=Plan;Client->SetViewLocation(FVector(0,0,200000));Client->ViewRect=FIntRect(0,0,800,600);Client->bHasProjection=true;
    Client->ViewProjection=FMatrix::Identity;Client->ViewProjection.M[0][0]=Client->ViewProjection.M[1][1]=1./100000;Client->ViewProjection.M[2][2]=1./200000;
    auto Down=[&](FVector2D P){P=Screen(P);App.RoutePointerDownEvent(Path,FPointerEvent(FSlateApplication::CursorPointerIndex,P,P,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));};
    auto Move=[&](FVector2D A,FVector2D B){A=Screen(A);B=Screen(B);App.RoutePointerMoveEvent(Path,FPointerEvent(FSlateApplication::CursorPointerIndex,B,A,TSet<FKey>{EKeys::LeftMouseButton},FKey(),0,FModifierKeysState()),false);};
    auto Up=[&](FVector2D P){P=Screen(P);App.RoutePointerUpEvent(Path,FPointerEvent(FSlateApplication::CursorPointerIndex,P,P,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState()));};
    const FCLGlobeSelection Key{Region.Id,false};
    for(auto Handle:{ECLGlobePlacementHandle::Centre,ECLGlobePlacementHandle::Rotation,ECLGlobePlacementHandle::Width,ECLGlobePlacementHandle::Length,ECLGlobePlacementHandle::Uniform})
    {
        Recipe=Original;Widget->SetAuthoringRecipe(Recipe);Widget->Select(Key);Widget->SetPlacementMode(Handle==ECLGlobePlacementHandle::Centre?ECLGlobePlacementMode::Move:Handle==ECLGlobePlacementHandle::Rotation?ECLGlobePlacementMode::Rotate:ECLGlobePlacementMode::Resize);
        const auto* H=Client->Handles.FindByPredicate([&](const auto& V){return V.Kind==Handle;});if(!H){AddError(TEXT("Expected placement handle was not built"));continue;}
        FVector2D Start;if(!Client->Project(H->Point,Start)){AddError(TEXT("Handle did not project"));continue;}
        if(Handle==ECLGlobePlacementHandle::Rotation&&!Client->RotationRing.IsEmpty())Client->Project(Client->RotationRing[12],Start);
        FVector2D End=Start;
        if(Handle==ECLGlobePlacementHandle::Centre)End+=FVector2D(60,0);
        else if(Handle==ECLGlobePlacementHandle::Rotation){const double R=FMath::Min(8.,Region.Placement.HalfExtentDegrees.GetMin()*.4);const FVector P=FCLGlobePlacementEditing::Direction(FVector2D(-FMath::DegreesToRadians(R),0))*45220;Client->Project(P,End);}
        else End=FVector2D(400,300)+(Start-FVector2D(400,300))*1.2;
        FVector Pick;TestTrue(TEXT("Handle ray reaches displayed field"),Client->CursorDirection(Start/FVector2D(800,600),Pick));TestTrue(TEXT("Projected handle hit is recognized"),Client->HitHandle(Start/FVector2D(800,600))==Handle);
        const int32 Before=Commits;Down(Start);TestTrue(TEXT("Handle press captures the real viewport pointer"),Widget->HasMouseCapture());Move(Start,End);TestTrue(TEXT("Routed drag enters placement mode"),Widget->DragHandle==Handle);TestTrue(TEXT("Routed drag creates a changed draft"),Widget->bPlacementMoved);
        TestEqual(TEXT("Drag does not commit per mouse event"),Commits,Before);TestTrue(TEXT("Draft gesture does not alter the recipe"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Recipe,&Original,0));TestFalse(TEXT("Footprint drag does not generate dense terrain"),Widget->IsUpdating());
        Up(End);TestEqual(TEXT("Exactly one placement commit on release"),Commits,Before+1);TestFalse(TEXT("Release ends pointer capture"),Widget->HasMouseCapture());
        TestFalse(TEXT("Released handle changes planet placement"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Recipe,&Original,0));
    }
    Recipe=Original;Widget->SetAuthoringRecipe(Recipe);Widget->Select(Key);Widget->SetPlacementMode(ECLGlobePlacementMode::Move);const int32 Before=Commits;
    Down(FVector2D(400,300));Move(FVector2D(400,300),FVector2D(440,300));Widget->OnPreviewKeyDown(Geometry,FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));
    TestEqual(TEXT("Escape discards a drag"),Commits,Before);TestTrue(TEXT("Escape preserves planet recipe"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Recipe,&Original,0));
    App.ReleaseAllPointerCapture();Widget->Select({});Down(FVector2D(400,300));Up(FVector2D(400,300));TestTrue(TEXT("Click selects the Landform on the globe"),Widget->Selection()==Key);
    Down(FVector2D(10,10));Up(FVector2D(10,10));TestFalse(TEXT("Click empty space deselects"),Widget->Selection().IsValid());
    App.ReleaseAllPointerCapture();App.RequestDestroyWindow(Window);return true;
}
#endif

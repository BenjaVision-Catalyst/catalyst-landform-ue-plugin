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
#include "UnrealWidget.h"
#include "EdMode.h"
#include "EditorModeRegistry.h"
#include "AssetEditorModeManager.h"
#include "SceneView.h"

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
    if(Plan.GeneratedRegions)
    {
        B.RegionBoundaries=Plan.GeneratedRegions->Boundaries;TArray<FVector> D;
        for(const auto& Edge:B.RegionBoundaries)D.Append({Edge.A,Edge.B});for(const auto& Cell:Plan.GeneratedRegions->Cells)D.Add(Cell.Centre);
        TArray<FVector> Positions;Positions.SetNumUninitialized(D.Num());
        for(int32 Start=0;Start<D.Num();Start+=SampleVertexBatch)
        {const int32 Size=FMath::Min(SampleVertexBatch,D.Num()-Start);TArray<double> H;Plan.SampleHeightsMeters(MakeArrayView(D.GetData()+Start,Size),H);for(int32 I=0;I<Size;++I)Positions[Start+I]=D[Start+I]*((Plan.RadiusMeters+H[I])/Plan.RadiusMeters*100000+100);}
        for(int32 I=0;I<B.RegionBoundaries.Num();++I){B.RegionBoundaries[I].A=Positions[2*I];B.RegionBoundaries[I].B=Positions[2*I+1];}
        for(int32 I=2*B.RegionBoundaries.Num();I<Positions.Num();++I)B.RegionCentres.Add(Positions[I]);
    }
    return B;
}
namespace
{
const FEditorModeID GlobePlacementModeId(TEXT("CLGlobeSurfacePlacement"));
// A private preview mode filters Unreal's own widget to the degrees of freedom
// supported by a surface-bound placement. No level actors or global mode changes.
class FCLGlobeSurfaceMode : public FEdMode
{
public:
    virtual bool UsesTransformWidget() const override { return true; }
    virtual bool UsesTransformWidget(UE::Widget::EWidgetMode) const override { return true; }
    virtual bool ShouldDrawWidget() const override { return true; }
    virtual EAxisList::Type GetWidgetAxisToDraw(UE::Widget::EWidgetMode M) const override
    { return M==UE::Widget::WM_Rotate?EAxisList::Z:EAxisList::XY; }
};
}
void CLRegisterGlobePlacementMode(){FEditorModeRegistry::Get().RegisterMode<FCLGlobeSurfaceMode>(GlobePlacementModeId);}
void CLUnregisterGlobePlacementMode(){FEditorModeRegistry::Get().UnregisterMode(GlobePlacementModeId);}
class FCLGlobeScene : public FPreviewScene
{
    UDirectionalLightComponent* Fill=nullptr;
    UDirectionalLightComponent* Rim=nullptr;
public:
    FCLGlobeScene():FPreviewScene(ConstructionValues())
    {
        SetSkyCubemap(LoadObject<UTextureCube>(nullptr,TEXT("/Engine/MapTemplates/Sky/SunsetAmbientCubemap.SunsetAmbientCubemap")));SetSkyBrightness(.25);
        DirectionalLight->SetMobility(EComponentMobility::Movable);DirectionalLight->SetForwardShadingPriority(1);DirectionalLight->SetIntensity(2.2);DirectionalLight->SetLightSourceAngle(4);
        auto AddLight=[&](float Intensity,FLinearColor Color){auto* L=NewObject<UDirectionalLightComponent>(GetTransientPackage());L->SetMobility(EComponentMobility::Movable);L->SetIntensity(Intensity);L->SetLightColor(Color);L->SetCastShadows(false);AddComponent(L,FTransform::Identity);return L;};
        Fill=AddLight(.7,FLinearColor(.65,.8,1));Rim=AddLight(1.1,FLinearColor(.58,.79,1));
    }
    void AimLights(const FRotator& Rotation)
    {
        const FRotationMatrix Axes(Rotation);const FVector Back=-Axes.GetUnitAxis(EAxis::X),Right=Axes.GetUnitAxis(EAxis::Y),Up=Axes.GetUnitAxis(EAxis::Z);
        auto Aim=[](UDirectionalLightComponent* L,FVector ToSource){L->SetWorldRotation((-ToSource).Rotation());};
        Aim(DirectionalLight,Back*.8-Right*.7+Up*.7);Aim(Fill,Back*.5+Right+Up*.25);Aim(Rim,-Back*.8+Up*.9);
    }
};
class FCLGlobeClient : public FEditorViewportClient
{
public:
    FCLPlanetNavigation Navigation;
    FCLGlobeScene* StudioScene=nullptr;
    TSharedPtr<const FCLCompiledPlanetRecipe> Plan;
    TArray<FCLGlobePlacementTarget> Targets;
    FCLGlobeSelection Selected;
    bool bInspectRegions=true;
    FGuid SelectedRegion;
    TArray<FCLGeneratedRegionBoundary> RegionBoundaries;
    TArray<FVector> RegionCentres;
    ECLGlobePlacementMode Mode=ECLGlobePlacementMode::Move;
    TOptional<FCLGlobePlacementTarget> Draft;
    struct FOutline {FCLGlobeSelection Key;TArray<FVector> Points;FVector Centre;};
    TArray<FOutline> Outlines;
    FVector PlacementOrigin=FVector::ZeroVector;
    const FCLGlobePlacementTarget* SelectedTarget() const
    {if(Draft.IsSet())return &Draft.GetValue();return Targets.FindByPredicate([&](const auto& T){return T.Key==Selected;});}
    void UpdatePlacementOverlay()
    {
        Outlines.Reset();ShowWidget(SelectedTarget()!=nullptr&&!(bInspectRegions&&Plan&&Plan->GeneratedRegions));if(Viewport)Viewport->InvalidateHitProxy();if(!Plan)return;
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
        for(int32 I=0;I<Outlines.Num();++I){Outlines[I].Centre=Point(Centres[I]);if(Outlines[I].Key==Selected)PlacementOrigin=Outlines[I].Centre+Directions[Centres[I]]*80;for(int32 J=1;J<=65;++J)Outlines[I].Points.Add(Point(Centres[I]+J));}
    }
    virtual UE::Widget::EWidgetMode GetWidgetMode() const override
    {return Mode==ECLGlobePlacementMode::Move?UE::Widget::WM_Translate:Mode==ECLGlobePlacementMode::Rotate?UE::Widget::WM_Rotate:UE::Widget::WM_Scale;}
    virtual FVector GetWidgetLocation() const override
    {return SelectedTarget()&&Plan?PlacementOrigin:FVector::ZeroVector;}
    virtual FMatrix GetWidgetCoordSystem() const override
    {const auto* T=SelectedTarget();return T?FQuatRotationMatrix(T->Frame):FMatrix::Identity;}
    virtual ECoordSystem GetWidgetCoordSystemSpace() const override {return COORD_Local;}
    EAxisList::Type HitWidgetAxis(FVector2D Normalized) const
    {
        if(!Viewport||!SelectedTarget())return EAxisList::None;
        const auto Size=Viewport->GetSizeXY();const int32 X=FMath::FloorToInt(Normalized.X*Size.X),Y=FMath::FloorToInt(Normalized.Y*Size.Y);
        if(X<0||Y<0||X>=Size.X||Y>=Size.Y)return EAxisList::None;
        HHitProxy* Proxy=Viewport->GetHitProxy(X,Y);
        if(Proxy&&Proxy->IsA(HWidgetAxis::StaticGetType()))
        {const auto* H=static_cast<HWidgetAxis*>(Proxy);if(!H->bDisabled)return H->Axis;}
        return EAxisList::None;
    }
    void HoverAxis(EAxisList::Type Axis)
    {if(GetCurrentWidgetAxis()!=Axis){SetCurrentWidgetAxis(Axis);Invalidate(false,false);}}
    bool WidgetPoint(FVector2D Normalized,const FCLGlobePlacementTarget& Start,EAxisList::Type Axis,const FVector& Origin,FVector& Point) const
    {
        if(!bHasProjection)return false;
        FVector O,V;FSceneView::DeprojectScreenToWorld(FVector2D(ViewRect.Min)+Normalized*FVector2D(ViewRect.Size()),ViewRect,ViewProjection.Inverse(),O,V);V.Normalize();
        if(Mode!=ECLGlobePlacementMode::Rotate&&(Axis==EAxisList::X||Axis==EAxisList::Y))
        {
            const FVector A=Axis==EAxisList::X?Start.Frame.GetAxisX():Start.Frame.GetAxisY(),Q=O-Origin;
            const double D=FVector::DotProduct(A,V),Denom=1-D*D;if(Denom<1.e-5)return false;
            Point=Origin+A*((FVector::DotProduct(A,Q)-D*FVector::DotProduct(V,Q))/Denom);return true;
        }
        const FVector N=Start.Frame.GetAxisZ();const double Denom=FVector::DotProduct(V,N);if(FMath::Abs(Denom)<1.e-5)return false;
        const double T=FVector::DotProduct(Origin-O,N)/Denom;if(T<0)return false;Point=O+V*T;return !Point.ContainsNaN();
    }
    double WidgetWorldSize() const
    {
        const FVector4 Clip=ViewProjection.TransformFVector4(FVector4(GetWidgetLocation(),1));
        const double PixelsPerWorld=ViewProjection.GetColumn(0).Size()*ViewRect.Width()*.5/FMath::Max(1.,FMath::Abs(Clip.W));
        return 70/FMath::Max(1.e-6,PixelsPerWorld);
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
        if(bInspectRegions&&Plan&&Plan->GeneratedRegions)return;
        for(const auto& O:Outlines)
        {
            const bool IsSelected=O.Key==Selected;const FLinearColor C=IsSelected?FLinearColor(.05,.8,1):FLinearColor(.16,.3,.35);
            for(int32 I=1;I<O.Points.Num();++I)PDI->DrawLine(O.Points[I-1],O.Points[I],C,SDPG_World,IsSelected?2.5:1.2);
            FVector2D Pixel;if(!IsSelected&&Project(O.Centre,Pixel))PDI->DrawPoint(O.Centre,C,8,SDPG_Foreground);
        }
    }
    void DrawGeneratedRegions(FPrimitiveDrawInterface* PDI)
    {
        if(!bInspectRegions||!Plan||!Plan->GeneratedRegions)return;const auto& G=*Plan->GeneratedRegions;
        const int32 SelectedIndex=G.Cells.IndexOfByPredicate([&](const auto& C){return C.Id==SelectedRegion;});
        for(const auto& Edge:RegionBoundaries)
        {
            const bool Highlight=Edge.CellA==SelectedIndex||Edge.CellB==SelectedIndex;
            const auto& Cell=G.Cells[Edge.CellA];const auto Id=G.Palette[Cell.PaletteIndex].Id;
            const FLinearColor Tint=FLinearColor::MakeFromHSV8(uint8((Id.A^Id.B^Id.C^Id.D)*53),140,190);
            PDI->DrawLine(Edge.A,Edge.B,Highlight?FLinearColor(.1,.9,1):Tint,SDPG_World,Highlight?3:1.1,0,true);
        }
        if(RegionCentres.IsValidIndex(SelectedIndex))PDI->DrawPoint(RegionCentres[SelectedIndex],FLinearColor(.1,.9,1),12,SDPG_Foreground);
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
    {
        StudioScene=static_cast<FCLGlobeScene*>(Scene);
        static_cast<FAssetEditorModeManager*>(ModeTools.Get())->SetPreviewScene(Scene);
        ModeTools->ActivateMode(GlobePlacementModeId);this->Widget->SetUsesEditorModeTools(ModeTools.Get());ShowWidget(false);
        SetViewMode(VMI_Lit);EngineShowFlags.SetGrid(false);EngineShowFlags.SetSelectionOutline(false);
        // Every camera angle uses the same studio exposure; nothing adapts while
        // this non-realtime viewport redraws intermittently during an orbit.
        ExposureSettings.bFixed=true;ExposureSettings.FixedEV100=-2;
        EngineShowFlags.SetEyeAdaptation(false);EngineShowFlags.SetLocalExposure(false);
        EngineShowFlags.SetBloom(false);EngineShowFlags.SetLensFlares(false);EngineShowFlags.SetLightShafts(false);
        SetRealtime(false);ViewFOV=FOVAngle=60;Apply();
    }
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
        const auto C=Navigation.Camera(H,V);SetViewLocation(C.Eye);SetViewRotation(C.Rotation);SetLookAtLocation(C.Target);StudioScene->AimLights(C.Rotation);Invalidate();
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
    {FEditorViewportClient::Draw(View,PDI);ViewProjection=View->ViewMatrices.GetViewProjectionMatrix();ViewRect=View->UnscaledViewRect;bHasProjection=true;UpdateIndicators();DrawPlacements(PDI);DrawGeneratedRegions(PDI);}
    double NormalizedAtScreenY(double Y)const
    {return bHasProjection&&Core.Y-Sky.Y>1.e-9?FMath::Clamp((Core.Y-Y)/(Core.Y-Sky.Y),0.,1.):0;}

};
void SCLPlanetPreview::Construct(const FArguments& Args)
{
    OnPlacementCommitted=Args._OnPlacementCommitted;OnRegionSelected=Args._OnRegionSelected;
    Scene=MakeUnique<FCLGlobeScene>();
    Mesh.Reset(NewObject<UProceduralMeshComponent>(GetTransientPackage()));Scene->AddComponent(Mesh.Get(),FTransform::Identity);
    Mesh->SetMaterial(0,LoadObject<UMaterial>(nullptr,TEXT("/CatalystLandform/Designer/M_Section.M_Section")));
    SEditorViewport::Construct(SEditorViewport::FArguments());
}
SCLPlanetPreview::~SCLPlanetPreview(){if(Client)Client->Viewport=nullptr;if(Mesh&&Scene)Scene->RemoveComponent(Mesh.Get());}
TSharedRef<FEditorViewportClient> SCLPlanetPreview::MakeEditorViewportClient(){Client=MakeShared<FCLGlobeClient>(Scene.Get(),SharedThis(this));return Client.ToSharedRef();}
void SCLPlanetPreview::Rebuild(TSharedPtr<const FCLCompiledPlanetRecipe> Plan){++Revision;Pending=MoveTemp(Plan);Pump();}
void SCLPlanetPreview::Cancel(){++Revision;Pending.Reset();}
bool SCLPlanetPreview::IsUpdating()const{return Task.IsValid()||Pending.IsValid();}
void SCLPlanetPreview::SetRegionInspection(bool Inspect)
{CancelPlacementDrag();Client->bInspectRegions=Inspect;if(Inspect)Client->Selected={};Client->UpdatePlacementOverlay();Client->Invalidate();}
bool SCLPlanetPreview::IsInspectingRegions() const{return Client->bInspectRegions&&Client->Plan&&Client->Plan->GeneratedRegions.IsValid();}
void SCLPlanetPreview::SelectGeneratedRegion(FGuid Id)
{
    SetRegionInspection(true);const auto G=Client->Plan?Client->Plan->GeneratedRegions:nullptr;
    Client->SelectedRegion=G&&G->Cells.ContainsByPredicate([&](const auto& C){return C.Id==Id;})?Id:FGuid();Client->Invalidate();OnRegionSelected.ExecuteIfBound(Client->SelectedRegion);
}
FGuid SCLPlanetPreview::GeneratedSelection() const{return Client->SelectedRegion;}
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
            Client->Plan=BuildingPlan;Client->RegionBoundaries=MoveTemp(B.RegionBoundaries);Client->RegionCentres=MoveTemp(B.RegionCentres);Client->UpdatePlacementOverlay();
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
    CancelPlacementDrag();if(Key.IsValid()){Client->bInspectRegions=false;Client->SelectedRegion.Invalidate();OnRegionSelected.ExecuteIfBound(FGuid());}if(Key!=Client->Selected)Client->Mode=ECLGlobePlacementMode::Move;Client->Selected=Client->Targets.ContainsByPredicate([&](const auto& T){return T.Key==Key;})?Key:FCLGlobeSelection();Client->UpdatePlacementOverlay();Client->Invalidate();
}
FCLGlobeSelection SCLPlanetPreview::Selection() const{return Client->Selected;}
FString SCLPlanetPreview::SelectionName() const{const auto* T=Client->SelectedTarget();return T?T->Name:TEXT("None");}
void SCLPlanetPreview::SetPlacementMode(ECLGlobePlacementMode Mode){if(DragAxis==EAxisList::None){Client->bInspectRegions=false;Client->Mode=Mode;Client->UpdatePlacementOverlay();Client->Invalidate();}}
ECLGlobePlacementMode SCLPlanetPreview::PlacementMode() const{return Client->Mode;}
FString SCLPlanetPreview::PlacementHint() const
{
    if(IsInspectingRegions())return TEXT("Click a generated region to inspect its Landform assignment | Left drag: orbit");
    if(DragAxis!=EAxisList::None)return TEXT("Release to apply placement and rebuild terrain | Esc: cancel");
    if(!Client->Selected.IsValid())return TEXT("Click a Landform / feature to select | Left drag: orbit");
    return Client->Mode==ECLGlobePlacementMode::Move?TEXT("Drag Unreal arrows / plane: move on surface | W: move | E: rotate | R: scale | Space: cycle"):
        Client->Mode==ECLGlobePlacementMode::Rotate?TEXT("Drag Unreal radial ring: rotate | Space: cycle | Esc: deselect"):
        TEXT("Drag Unreal X / Y scale handles; XY scales both | Space: cycle | Esc: deselect");
}
void SCLPlanetPreview::CancelPlacementDrag(){DragAxis=EAxisList::None;bPlacementMoved=false;if(Client){Client->Draft.Reset();Client->UpdatePlacementOverlay();Client->Invalidate();}}
FReply SCLPlanetPreview::OnPreviewMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
    const FKey Key=E.GetEffectingButton();if(Key!=EKeys::LeftMouseButton&&Key!=EKeys::MiddleMouseButton&&Key!=EKeys::RightMouseButton)return FReply::Unhandled();
    if(DragAxis!=EAxisList::None){CancelPlacementDrag();Button=FKey();return FReply::Handled().ReleaseMouseCapture();}
    Button=Key;Previous=MouseDown=E.GetScreenSpacePosition();bOrbitDrag=false;bPlacementMoved=false;
    if(Key==EKeys::LeftMouseButton&&IsInspectingRegions())
    {
        FVector D;ClickGeneratedRegion.Invalidate();if(Client->CursorDirection(PointerPosition(G,E),D)){const auto& Cells=Client->Plan->GeneratedRegions->Cells;const int32 Index=Client->Plan->GeneratedRegions->Owner(D);if(Cells.IsValidIndex(Index))ClickGeneratedRegion=Cells[Index].Id;}
    }
    else if(Key==EKeys::LeftMouseButton)
    {
        const auto P=PointerPosition(G,E);const auto Axis=Client->HitWidgetAxis(P);const auto* T=Client->SelectedTarget();FVector Point;
        if(Axis!=EAxisList::None&&T&&Client->WidgetPoint(P,*T,Axis,Client->GetWidgetLocation(),Point))
        {DragAxis=Axis;DragStart=*T;DragStartPoint=Point;DragOrigin=Client->GetWidgetLocation();DragScale=Client->WidgetWorldSize();Client->HoverAxis(Axis);}
        else ClickSelection=Client->PickTarget(P);
    }
    return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));
}
FReply SCLPlanetPreview::OnMouseMove(const FGeometry& G,const FPointerEvent& E)
{
    if(!HasMouseCapture()){Client->HoverAxis(IsInspectingRegions()?EAxisList::None:Client->HitWidgetAxis(PointerPosition(G,E)));return FReply::Unhandled();}const auto Delta=E.GetScreenSpacePosition()-Previous;Previous=E.GetScreenSpacePosition();
    if(DragAxis!=EAxisList::None)
    {
        FVector Point;if(Client->WidgetPoint(PointerPosition(G,E),DragStart,DragAxis,DragOrigin,Point))
        {
            auto Target=DragStart;
            const FVector Local=DragStart.Frame.UnrotateVector(Point-DragStartPoint);
            if(Client->Mode==ECLGlobePlacementMode::Move)
            {
                const double Radius=FMath::Max(1.,DragOrigin.Size());
                const FVector D=DragStart.Frame.RotateVector(FCLGlobePlacementEditing::Direction(FVector2D(Local.X,Local.Y)/Radius));
                Target=FCLGlobePlacementEditing::Drag(DragStart,ECLGlobePlacementHandle::Centre,DragStart.Frame.GetAxisZ(),D);
            }
            else if(Client->Mode==ECLGlobePlacementMode::Rotate)
            {
                const FVector A=DragStart.Frame.UnrotateVector(DragStartPoint-DragOrigin),B=DragStart.Frame.UnrotateVector(Point-DragOrigin);
                const double Angle=FMath::FindDeltaAngleRadians(FMath::Atan2(A.Y,A.X),FMath::Atan2(B.Y,B.X));
                Target.Frame=(DragStart.Frame*FQuat(FVector::ZAxisVector,Angle)).GetNormalized();
            }
            else
            {
                FVector2D Factors(1);if(DragAxis==EAxisList::X)Factors.X=1+Local.X/DragScale;
                else if(DragAxis==EAxisList::Y)Factors.Y=1+Local.Y/DragScale;
                else Factors=FVector2D(1+(Local.X+Local.Y)/DragScale);
                Target.HalfExtentDegrees.X=FMath::Clamp(DragStart.HalfExtentDegrees.X*Factors.X,.1,84.9);
                Target.HalfExtentDegrees.Y=FMath::Clamp(DragStart.HalfExtentDegrees.Y*Factors.Y,.1,84.9);
            }
            Client->Draft=Target;
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
    if(DragAxis!=EAxisList::None)
    {
        const auto Target=Client->Draft.IsSet()?Client->Draft.GetValue():DragStart;const bool Changed=bPlacementMoved;
        const bool Resize=Client->Mode==ECLGlobePlacementMode::Resize;
        CancelPlacementDrag();if(Changed)OnPlacementCommitted.ExecuteIfBound(Target,Resize);
    }
    else if(Button==EKeys::LeftMouseButton&&!bOrbitDrag){if(IsInspectingRegions())SelectGeneratedRegion(ClickGeneratedRegion);else Select(ClickSelection);}
    Button=FKey();return FReply::Handled().ReleaseMouseCapture();
}
void SCLPlanetPreview::OnMouseCaptureLost(const FCaptureLostEvent&){Button=FKey();CancelPlacementDrag();}
FReply SCLPlanetPreview::OnPreviewKeyDown(const FGeometry&,const FKeyEvent& E)
{
    if(E.GetKey()==EKeys::Escape){const bool WasDragging=DragAxis!=EAxisList::None;CancelPlacementDrag();if(!WasDragging){if(IsInspectingRegions())SelectGeneratedRegion(FGuid());else Select({});}Button=FKey();return FReply::Handled().ReleaseMouseCapture();}
    if(E.IsControlDown()||E.IsCommandDown()||E.IsAltDown())return FReply::Unhandled();
    if(E.GetKey()==EKeys::SpaceBar)SetPlacementMode(Client->Mode==ECLGlobePlacementMode::Move?ECLGlobePlacementMode::Rotate:Client->Mode==ECLGlobePlacementMode::Rotate?ECLGlobePlacementMode::Resize:ECLGlobePlacementMode::Move);else if(E.GetKey()==EKeys::W)SetPlacementMode(ECLGlobePlacementMode::Move);else if(E.GetKey()==EKeys::E)SetPlacementMode(ECLGlobePlacementMode::Rotate);else if(E.GetKey()==EKeys::R)SetPlacementMode(ECLGlobePlacementMode::Resize);else return FReply::Unhandled();return FReply::Handled();
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
#include "Slate/SceneViewport.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGlobePlacementPointerTest,"Catalyst.Landform.PlanetBuilder.RoutedPlacementHandles",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGlobePlacementPointerTest::RunTest(const FString&)
{
    auto Recipe=FCLPlanetCompiler::BasePlanet();FCLTerrainRegion Region;Region.Id=FGuid::NewGuid();Region.bPlacedLandform=true;Region.bSphericalMask=true;Region.TerrainTypeId=FGuid::NewGuid();Region.TerrainTypeName=TEXT("Native widget source");Region.Placement.HalfExtentDegrees=FVector2D(25,15);Recipe.TerrainRegions.Add(Region);const auto Original=Recipe;
    FString Error;const auto Plan=FCLPlanetCompiler::Compile(Recipe,Error);if(!Plan){AddError(Error);return false;}
    auto& App=FSlateApplication::Get();int32 Commits=0;
    const auto W=SNew(SCLPlanetPreview).OnPlacementCommitted(FOnCLGlobePlacementCommitted::CreateLambda([&](const auto& Target,bool Resize){++Commits;TestTrue(TEXT("Native widget commits a valid spherical placement"),FCLGlobePlacementEditing::Apply(Recipe,Target,Resize,Error));}));
    const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Native globe widget proof"))).ClientSize(FVector2D(800,600))[W];App.AddWindow(Window,false);Window->SlatePrepass();
    const auto V=W->GetSceneViewport();V->UpdateViewportRHI(false,800,600,EWindowMode::Windowed,PF_Unknown);FlushRenderingCommands();
    const auto C=W->Client;C->Plan=Plan;C->SetViewLocation(FVector(80000,-60000,200000));C->SetViewRotation((FVector(0,0,45000)-C->GetViewLocation()).Rotation());C->StudioScene->AimLights(C->GetViewRotation());const auto B=FCLGlobeBuffers::Build(*Plan,32);W->Mesh->CreateMeshSection_LinearColor(0,B.Vertices,B.Triangles,B.Normals,B.UV,B.Colors,B.Tangents,false);W->SetAuthoringRecipe(Recipe);W->Select({Region.Id,false});
    FWidgetPath Actual;App.GeneratePathToWidgetUnchecked(W->ViewportWidget.ToSharedRef(),Actual,EVisibility::All);
    const auto Geometry=Actual.Widgets.Last().Geometry;TArray<FWidgetAndPointer> Arranged;for(const auto& A:Actual.Widgets.GetInternalArray())Arranged.Emplace(A,TOptional<FVirtualPointerPosition>());const FWidgetPath Path(MakeArrayView(Arranged));
    auto Screen=[&](FVector2D P){return Geometry.LocalToAbsolute(P/FVector2D(V->GetSizeXY())*Geometry.GetLocalSize());};
    auto Down=[&](FVector2D P){P=Screen(P);App.RoutePointerDownEvent(Path,FPointerEvent(FSlateApplication::CursorPointerIndex,P,P,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));};
    auto Move=[&](FVector2D A,FVector2D B){A=Screen(A);B=Screen(B);App.RoutePointerMoveEvent(Path,FPointerEvent(FSlateApplication::CursorPointerIndex,B,A,TSet<FKey>{EKeys::LeftMouseButton},FKey(),0,FModifierKeysState()),false);};
    auto Up=[&](FVector2D P){P=Screen(P);App.RoutePointerUpEvent(Path,FPointerEvent(FSlateApplication::CursorPointerIndex,P,P,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState()));};
    for(auto Mode:{ECLGlobePlacementMode::Move,ECLGlobePlacementMode::Rotate,ECLGlobePlacementMode::Resize})
    {
        for(auto Axis:Mode==ECLGlobePlacementMode::Rotate?TArray<EAxisList::Type>{EAxisList::Z}:TArray<EAxisList::Type>{EAxisList::X,EAxisList::Y,EAxisList::XY})
        {
            Recipe=Original;W->SetAuthoringRecipe(Recipe);W->Select({Region.Id,false});W->SetPlacementMode(Mode);V->Invalidate();V->Draw(false);FlushRenderingCommands();
            if(Mode==ECLGlobePlacementMode::Move&&Axis==EAxisList::X){TArray<FColor> Pixels;if(V->ReadPixels(Pixels)){TArray<uint8> PNG;FImageUtils::CompressImageArray(V->GetSizeXY().X,V->GetSizeXY().Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("Verification/native-widget-move.png")));}}
            const auto Size=V->GetSizeXY();TArray<HHitProxy*> Map;V->GetHitProxyMap(FIntRect(0,0,Size.X,Size.Y),Map);FVector2D Start(-1,-1);
            for(int32 I=0;I<Map.Num();++I)if(Map[I]&&Map[I]->IsA(HWidgetAxis::StaticGetType())&&static_cast<HWidgetAxis*>(Map[I])->Axis==Axis){Start=FVector2D(I%Size.X+.5,I/Size.X+.5);break;}
            AddInfo(FString::Printf(TEXT("Native handle mode %d axis %d at %.1f / %.1f; viewport %d x %d"),int32(Mode),int32(Axis),Start.X,Start.Y,Size.X,Size.Y));
            if(!TestTrue(TEXT("Unreal renders a real HWidgetAxis hit target"),Start.X>=0))continue;
            TestTrue(TEXT("Native hit proxy is used by the actual viewport"),C->HitWidgetAxis(Start/FVector2D(Size))==Axis);
            FVector2D End=Start+FVector2D(36,-28);const int32 Before=Commits;
            if(Mode==ECLGlobePlacementMode::Move&&Axis==EAxisList::X)
            {Down(Start);Move(Start,End);App.ProcessKeyDownEvent(FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));Up(End);TestFalse(TEXT("Escape releases native widget pointer capture"),W->HasMouseCapture());TestEqual(TEXT("Escape cancels a native widget draft without a commit"),Commits,Before);TestTrue(TEXT("Cancelled native drag preserves the recipe"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Recipe,&Original,0));}
            Down(Start);Move(Start,End);
            AddInfo(FString::Printf(TEXT("Routed actual axis %d, changed %d, commits %d"),int32(W->DragAxis),W->bPlacementMoved,Commits));
            TestTrue(TEXT("Routed native handle starts a changed draft"),W->DragAxis==Axis&&W->bPlacementMoved);
            TestEqual(TEXT("No per-move commit"),Commits,Before);TestFalse(TEXT("No per-move dense terrain work"),W->IsUpdating());
            TestTrue(TEXT("The recipe stays unchanged during a draft"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Recipe,&Original,0));
            Up(End);TestEqual(TEXT("One commit for each native widget gesture"),Commits,Before+1);
            TestFalse(TEXT("Native gesture changes the placement"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Recipe,&Original,0));
        }
    }
    W->SetPlacementMode(ECLGlobePlacementMode::Move);W->OnPreviewKeyDown(Geometry,FKeyEvent(EKeys::SpaceBar,FModifierKeysState(),0,false,0,0));TestTrue(TEXT("Space cycles to rotation"),W->PlacementMode()==ECLGlobePlacementMode::Rotate);
    W->OnPreviewKeyDown(Geometry,FKeyEvent(EKeys::R,FModifierKeysState(),0,false,0,0));TestTrue(TEXT("R selects scale"),W->PlacementMode()==ECLGlobePlacementMode::Resize);
    W->OnPreviewKeyDown(Geometry,FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));TestFalse(TEXT("Escape clears selection"),W->Selection().IsValid());
    App.ReleaseAllPointerCapture();App.RequestDestroyWindow(Window);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGlobeLightingTest,"Catalyst.Landform.PlanetBuilder.StablePreviewLighting",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGlobeLightingTest::RunTest(const FString&)
{
    auto& App=FSlateApplication::Get();const auto W=SNew(SCLPlanetPreview);const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Globe exposure orbit proof"))).ClientSize(FVector2D(640,480))[W];App.AddWindow(Window,false);Window->SlatePrepass();const auto V=W->GetSceneViewport();V->UpdateViewportRHI(false,640,480,EWindowMode::Windowed,PF_Unknown);FlushRenderingCommands();
    auto Recipe=FCLPlanetCompiler::BasePlanet();FString Error;const auto Plan=FCLPlanetCompiler::Compile(Recipe,Error);if(!Plan){AddError(Error);App.RequestDestroyWindow(Window);return false;}
    const auto B=FCLGlobeBuffers::Build(*Plan,48);W->Client->Plan=Plan;W->Mesh->CreateMeshSection_LinearColor(0,B.Vertices,B.Triangles,B.Normals,B.UV,B.Colors,B.Tangents,false);
    TestTrue(TEXT("Fixed studio exposure is stable during orbit"),W->Client->ExposureSettings.bFixed&&W->Client->ExposureSettings.FixedEV100==-2);
    TestFalse(TEXT("Automatic exposure cannot flash between intermittent orbit frames"),W->Client->EngineShowFlags.EyeAdaptation);
    int32 Frames=0,WorstBackground=0,MostClipped=0,LeastTerrain=MAX_int32,DarkestCentre=255;
    for(double Latitude:{-55.,0.,55.})for(int32 Longitude=0;Longitude<360;Longitude+=30)
    {
        const double A=FMath::DegreesToRadians(double(Longitude)),P=FMath::DegreesToRadians(Latitude);const FVector Eye=FVector(FMath::Cos(A)*FMath::Cos(P),FMath::Sin(A)*FMath::Cos(P),FMath::Sin(P))*220000;
        W->Client->SetViewLocation(Eye);W->Client->SetViewRotation((-Eye).Rotation());W->Client->StudioScene->AimLights(W->Client->GetViewRotation());W->Scene->UpdateCaptureContents();V->Invalidate();V->Draw(false);FlushRenderingCommands();TArray<FColor> Pixels;
        if(!TestTrue(TEXT("Actual rendered orbit frame can be read"),V->ReadPixels(Pixels)))continue;
        if(Pixels.IsEmpty())continue;int32 Clipped=0;for(const auto& C:Pixels)if(C.R>250&&C.G>250&&C.B>250)++Clipped;MostClipped=FMath::Max(MostClipped,Clipped);
        const int32 BackgroundSum=int32(Pixels[0].R)+Pixels[0].G+Pixels[0].B;int32 TerrainPixels=0;for(const auto& C:Pixels)if(int32(C.R)+C.G+C.B>BackgroundSum+60)++TerrainPixels;LeastTerrain=FMath::Min(LeastTerrain,TerrainPixels);
        if(Frames==0){TArray<uint8> PNG;FImageUtils::CompressImageArray(V->GetSizeXY().X,V->GetSizeXY().Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("Verification/native-widget-lighting.png")));}
        const auto Centre=Pixels[(V->GetSizeXY().Y/2)*V->GetSizeXY().X+V->GetSizeXY().X/2];DarkestCentre=FMath::Min(DarkestCentre,int32(.2126*Centre.R+.7152*Centre.G+.0722*Centre.B));
        WorstBackground=FMath::Max(WorstBackground,int32(Pixels[0].B));++Frames;
    }
    TestEqual(TEXT("Full rendered orbit sweep completes"),Frames,36);TestTrue(TEXT("Background remains dark at every orientation"),WorstBackground<100);TestTrue(TEXT("Terrain is never broadly clipped white"),MostClipped<640*480/100);TestTrue(TEXT("Lit terrain is actually rendered at every angle"),LeastTerrain>1000);TestTrue(TEXT("Visible terrain remains legible at fixed exposure"),DarkestCentre>=55);
    AddInfo(FString::Printf(TEXT("Rendered exposure sweep: %d orientations, background blue max %d/255, clipped-white pixels max %d, lit terrain min %d, centre luminance min %d/255"),Frames,WorstBackground,MostClipped,LeastTerrain,DarkestCentre));
    App.RequestDestroyWindow(Window);return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGlobeGeneratedInspectionTest,"Catalyst.Landform.PlanetBuilder.GeneratedRegionInspection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGlobeGeneratedInspectionTest::RunTest(const FString&)
{
    FString Json,Error;FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/GeneratedRegions.json")));
    const auto Asset=TStrongObjectPtr<UCLPlanetRecipeAsset>(NewObject<UCLPlanetRecipeAsset>());if(!Asset->ImportRecipeJson(Json,Error)){AddError(Error);return false;}
    const auto Plan=FCLPlanetCompiler::Compile(Asset->Recipe,Error);if(!Plan){AddError(Error);return false;}
    auto& App=FSlateApplication::Get();FGuid Callback;int32 Callbacks=0;
    const auto W=SNew(SCLPlanetPreview).OnRegionSelected(FOnCLGlobeRegionSelected::CreateLambda([&](FGuid Id){Callback=Id;++Callbacks;}));
    const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Generated region inspection proof"))).ClientSize(FVector2D(800,600))[W];App.AddWindow(Window,false);Window->SlatePrepass();
    const auto V=W->GetSceneViewport();V->UpdateViewportRHI(false,800,600,EWindowMode::Windowed,PF_Unknown);FlushRenderingCommands();
    const auto C=W->Client;C->Plan=Plan;const auto B=FCLGlobeBuffers::Build(*Plan,96);C->RegionBoundaries=B.RegionBoundaries;C->RegionCentres=B.RegionCentres;
    TestTrue(TEXT("Compiled regions have rendered surface contours"),B.RegionBoundaries.Num()>100&&B.RegionCentres.Num()==24);
    W->Mesh->CreateMeshSection_LinearColor(0,B.Vertices,B.Triangles,B.Normals,B.UV,B.Colors,B.Tangents,false);
    C->SetViewLocation(Plan->GeneratedRegions->Cells[0].Centre*150000);C->SetViewRotation((-C->GetViewLocation()).Rotation());C->StudioScene->AimLights(C->GetViewRotation());W->Scene->UpdateCaptureContents();
    auto Render=[&](TArray<FColor>& Pixels){V->Invalidate();V->Draw(false);FlushRenderingCommands();return V->ReadPixels(Pixels);};
    TArray<FColor> Hidden,Visible;W->SetRegionInspection(false);Render(Hidden);Render(Hidden);W->SetRegionInspection(true);TestTrue(TEXT("Region overlay frame renders"),Render(Visible));
    int32 Changed=0;for(int32 I=0;I<FMath::Min(Hidden.Num(),Visible.Num());++I)Changed+=FMath::Abs(int32(Hidden[I].R)-Visible[I].R)+FMath::Abs(int32(Hidden[I].G)-Visible[I].G)+FMath::Abs(int32(Hidden[I].B)-Visible[I].B)>30;
    TestTrue(TEXT("Screen-space region lines are visibly different from terrain alone"),Changed>100);
    FWidgetPath Actual;App.GeneratePathToWidgetUnchecked(W->ViewportWidget.ToSharedRef(),Actual,EVisibility::All);const auto Geometry=Actual.Widgets.Last().Geometry;TArray<FWidgetAndPointer> Arranged;for(const auto& A:Actual.Widgets.GetInternalArray())Arranged.Emplace(A,TOptional<FVirtualPointerPosition>());const FWidgetPath Path(MakeArrayView(Arranged));
    const FVector2D Screen=Geometry.LocalToAbsolute(Geometry.GetLocalSize()*.5);
    App.RoutePointerDownEvent(Path,FPointerEvent(FSlateApplication::CursorPointerIndex,Screen,Screen,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
    App.RoutePointerUpEvent(Path,FPointerEvent(FSlateApplication::CursorPointerIndex,Screen,Screen,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState()));
    TestTrue(TEXT("Actual viewport click selects the compiled cell under the cursor"),W->GeneratedSelection()==Plan->GeneratedRegions->Cells[0].Id&&Callback==W->GeneratedSelection()&&Callbacks==1);
    TestFalse(TEXT("Inspecting a region schedules no terrain work"),W->IsUpdating());
    Render(Visible);TArray<uint8> PNG;FImageUtils::CompressImageArray(V->GetSizeXY().X,V->GetSizeXY().Y,Visible,PNG);FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("Verification/generated-region-inspection.png")));
    W->OnPreviewKeyDown(Geometry,FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));TestFalse(TEXT("Escape clears the inspected region"),W->GeneratedSelection().IsValid());
    W->SetPlacementMode(ECLGlobePlacementMode::Move);TestFalse(TEXT("Editorial tools leave region inspection"),W->IsInspectingRegions());
    AddInfo(FString::Printf(TEXT("Generated region inspection: %d surface contour segments, %d visibly changed pixels; routed surface selection passed"),B.RegionBoundaries.Num(),Changed));
    App.ReleaseAllPointerCapture();App.RequestDestroyWindow(Window);return true;
}
#endif

#include "CLPlanetPreview.h"
#include "CLWatershedDebug.h"
#include "Scene/CLScene.h"
#include "Misc/ScopeExit.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "CLPreviewMesh.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Math/Float16.h"
#include "CLSurfacePreview.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "ShaderCompiler.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#endif


#include "Model/CLSphericalPatches.h"
#include "PreviewScene.h"
#include "CLNativePreview.h"
#include "EditorViewportClient.h"
#include "Components/DirectionalLightComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Surface/VoxelSurfaceTypeAsset.h"
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

#if WITH_DEV_AUTOMATION_TESTS
FCLGlobeBuffers FCLGlobeBuffers::Build(const FCLCompiledPlanetRecipe& Plan,int32 Resolution,const TAtomic<bool>* Stop)
{
    auto Aborted=[] {FCLGlobeBuffers Result;Result.bCancelled=true;return Result;};
    if(Stop&&Stop->Load())return Aborted();
    FCLGlobeBuffers B;B.Surfaces=Plan.Surfaces;const int32 N=FMath::Clamp(Resolution,8,PreviewResolution),Row=N+1,Count=6*Row*Row;
    B.Vertices.Reserve(Count);B.Normals.Reserve(Count);B.SourceDirections.Reserve(Count);
    B.UV.Reserve(Count);B.Colors.Reserve(Count);B.Tangents.Reserve(Count);B.Triangles.Reserve(6*N*N*6);
    const int32 Batch=Plan.Surfaces?512:SampleVertexBatch;
    TArray<FVector> Samples;Samples.Reserve(Batch*5);
    auto Emit=[&]
    {
        if(Stop&&Stop->Load())return false;
        if(Samples.IsEmpty())return true;
        TArray<double> H;Plan.SampleHeightsMeters(Samples,H);
        auto Point=[&](int32 I){return Samples[I]*((Plan.RadiusMeters+H[I])/Plan.RadiusMeters*100000);};
        for(int32 K=0;K<Samples.Num();K+=5)
        {
            B.Vertices.Add(Point(K));FVector Normal=CLSurfaceNormal(Samples,H,Plan.RadiusMeters,K);
            if(FVector::DotProduct(Normal,Samples[K])<0)Normal*=-1;B.Normals.Add(Normal);
            const double Slope=FVector::DotProduct(Normal,Samples[K]);B.Colors.Add(FMath::Lerp(FLinearColor(.38,.36,.32),FLinearColor(.34,.43,.28),FMath::Clamp((Slope-.6)/.4,0.,1.)));
            if(Plan.Surfaces)
            {
                const double Degrees=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Slope,0.,1.)));const auto W=Plan.SurfaceWeightsAt(Samples[K],H[K],Degrees);
                const FVector Source=Samples[K]*(Plan.RadiusMeters+H[K])*100;B.Colors.Last()=CLPackSurfaceWeights(W);
                B.UV.Add(FVector2D(Source.X,Source.Y));B.UV1.Add(FVector2D(Source.Z,1));B.UV2.Add(FVector2D(W.Values[4],W.Values[5]));B.UV3.Add(FVector2D(W.Values[6],W.Values[7]));
            }
            else B.UV.Add(FVector2D::ZeroVector);B.Tangents.Add(FProcMeshTangent(FVector::VectorPlaneProject(FVector::XAxisVector,Normal).GetSafeNormal(),false));
        }
        Samples.Reset();
        return true;
    };
    for(int32 Face=0;Face<6;++Face)for(int32 Y=0;Y<=N;++Y)for(int32 X=0;X<=N;++X)
    {
        const FVector D=FCLSphericalPatches::Direction(Face,FVector2D(2.*X/N-1,2.*Y/N-1));B.SourceDirections.Add(D);
        CLSurfaceProbe(D,Samples);
        if(Samples.Num()==Batch*5&&!Emit())return Aborted();
    }
    if(!Emit())return Aborted();
    B.DisplayRadius=(Plan.RadiusMeters+Plan.MaximumHeightMeters)/Plan.RadiusMeters*100000;
    for(int32 Face=0;Face<6;++Face)for(int32 Y=0;Y<N;++Y)for(int32 X=0;X<N;++X)
    {
        if(X==0&&Stop&&Stop->Load())return Aborted();
        const int32 A=Face*Row*Row+Y*Row+X;B.Triangles.Append({A,A+Row,A+1,A+1,A+Row,A+Row+1});
    }
    if(Plan.GeneratedRegions)
    {
        B.RegionBoundaries=Plan.GeneratedRegions->Boundaries;TArray<FVector> D;
        for(const auto& Edge:B.RegionBoundaries)D.Append({Edge.A,Edge.B});for(const auto& Cell:Plan.GeneratedRegions->Cells)D.Add(Cell.Centre);
        TArray<FVector> Positions;Positions.SetNumUninitialized(D.Num());
        for(int32 Start=0;Start<D.Num();Start+=SampleVertexBatch)
        {if(Stop&&Stop->Load())return Aborted();const int32 Size=FMath::Min(SampleVertexBatch,D.Num()-Start);TArray<double> H;Plan.SampleHeightsMeters(MakeArrayView(D.GetData()+Start,Size),H);for(int32 I=0;I<Size;++I)Positions[Start+I]=D[Start+I]*((Plan.RadiusMeters+H[I])/Plan.RadiusMeters*100000+100);}
        for(int32 I=0;I<B.RegionBoundaries.Num();++I){B.RegionBoundaries[I].A=Positions[2*I];B.RegionBoundaries[I].B=Positions[2*I+1];}
        for(int32 I=2*B.RegionBoundaries.Num();I<Positions.Num();++I)B.RegionCentres.Add(Positions[I]);
    }
    return B;
}
#endif

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
    void SetCaveInspection(bool Enabled)
    {const float K=Enabled?.025f:1.f;DirectionalLight->SetIntensity(2.2f*K);Fill->SetIntensity(.7f*K);Rim->SetIntensity(1.1f*K);SetSkyBrightness(.25f*K);}
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
    FVector WalkDirection=FVector::ZeroVector;
    FCLPlanetNavigation Navigation;
    FCLGlobeScene* StudioScene=nullptr;
    TSharedPtr<const FCLCompiledPlanetRecipe> Plan;
    FCLNativePreview* NativePreview=nullptr;
    bool bCaveOverlay=false;
#if WITH_DEV_AUTOMATION_TESTS
    bool bReferenceFixture=false;
#endif
    mutable TOptional<FVector> LastPickPoint;
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
        LastPickPoint.Reset();
        if(!Plan||!bHasProjection)return false;FVector O,V;const FVector2D Pixel=FVector2D(ViewRect.Min)+Normalized*FVector2D(ViewRect.Size());
        FSceneView::DeprojectScreenToWorld(Pixel,ViewRect,ViewProjection.Inverse(),O,V);V.Normalize();
        if(NativePreview)
        {FVector P;LastPickPoint.Reset();if(!NativePreview||!NativePreview->PickGlobe(O,V,P))return false;LastPickPoint=P;Direction=P.GetSafeNormal();return true;}
#if WITH_DEV_AUTOMATION_TESTS
        // Development editors also compile automation; require the explicit
        // fixture opt-in, not merely the build flag. Product picking stays native.
        if(!bReferenceFixture)return false;
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
#else
        return false;
#endif
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
    bool bWatershedOverlay=false;
    TSharedPtr<const FCLWatershedDebugGeometry> WatershedDebug;
    TSharedPtr<const FCLCompiledPlanetRecipe> DebugPlan;
    int32 LastWatershedSegments=0;
    bool CanDrawWatershed()const
    {return bWatershedOverlay&&WatershedDebug&&NativePreview&&NativePreview->PublishedPlan()==DebugPlan;}
    void DrawWatersheds(FPrimitiveDrawInterface* PDI)
    {
        LastWatershedSegments=0;if(!CanDrawWatershed())return;
        const double Scale=100000/DebugPlan->RadiusMeters;
        auto Line=[&](FVector A,FVector B,FLinearColor Color,float Width)
        {
            A*=Scale;B*=Scale;const FVector Mid=(A+B)*.5;
            // Debug lines stay legible above native terrain discretisation. Cull
            // the back hemisphere; translucent water itself retains ordinary depth.
            if(FVector::DotProduct(Mid.GetSafeNormal(),GetViewLocation()-Mid)<=0)return;
            PDI->DrawLine(A,B,Color,SDPG_Foreground,Width,0,true);++LastWatershedSegments;
        };
        for(const auto& S:WatershedDebug->Shores)
        {
            const FLinearColor C=WatershedDebug->Bodies[S.Body].Ocean?FLinearColor(.12,.55,.85):FLinearColor(.15,.9,.95);
            Line(S.A,S.B,C,1.3);
        }
        for(const auto& P:WatershedDebug->Paths)
        {
            const FLinearColor C=P.Channel?FLinearColor(.05,.5,1):FLinearColor(.25,.55,.65);
            for(int32 I=1;I<P.Curve.Points.Num();++I)
            {Line(P.Curve.Points[I-1].OutVal,P.Curve.Points[I].OutVal,C,P.Channel?2:1);}
        }
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
        SetViewMode(VMI_Lit);EngineShowFlags.SetGrid(false);EngineShowFlags.SetSelectionOutline(false);EngineShowFlags.SetTranslucency(true);
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
    {FEditorViewportClient::Draw(View,PDI);ViewProjection=View->ViewMatrices.GetViewProjectionMatrix();ViewRect=View->UnscaledViewRect;bHasProjection=true;UpdateIndicators();DrawPlacements(PDI);DrawGeneratedRegions(PDI);DrawWatersheds(PDI);
        if(Plan&&!WalkDirection.IsNearlyZero())
        {
            const FVector P=WalkDirection*((Plan->RadiusMeters+Plan->SampleHeightMeters(WalkDirection))/Plan->RadiusMeters*100000+150);
            const double R=WidgetWorldSize()*.12;const FVector X=FVector::CrossProduct(WalkDirection,FMath::Abs(WalkDirection.Z)<.9?FVector::ZAxisVector:FVector::XAxisVector).GetSafeNormal(),Y=FVector::CrossProduct(WalkDirection,X);
            FVector2D Pixel;if(Project(P,Pixel))
            {
            PDI->DrawLine(P-X*R,P+X*R,FLinearColor(1,.65,.08),SDPG_Foreground,2);
            PDI->DrawLine(P-Y*R,P+Y*R,FLinearColor(1,.65,.08),SDPG_Foreground,2);
            PDI->DrawLine(P,P+WalkDirection*R*2,FLinearColor(1,.65,.08),SDPG_Foreground,2);
            }
        }}
    double NormalizedAtScreenY(double Y)const
    {return bHasProjection&&Core.Y-Sky.Y>1.e-9?FMath::Clamp((Core.Y-Y)/(Core.Y-Sky.Y),0.,1.):0;}

};
void SCLPlanetPreview::Construct(const FArguments& Args)
{
    OnPlacementCommitted=Args._OnPlacementCommitted;OnRegionSelected=Args._OnRegionSelected;
    Scene=MakeUnique<FCLGlobeScene>();
    Ocean.Reset(NewObject<UStaticMeshComponent>(GetTransientPackage()));
    Ocean->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
    Ocean->SetCollisionEnabled(ECollisionEnabled::NoCollision);Ocean->SetCastShadow(false);Ocean->SetVisibility(false);Ocean->bSelectable=false;
    auto* Glass=NewObject<UMaterial>(Ocean.Get(),NAME_None,RF_Transient);
    Glass->TwoSided=true;Glass->BlendMode=BLEND_Translucent;Glass->TranslucencyLightingMode=TLM_SurfacePerPixelLighting;
    auto* Inputs=Glass->GetEditorOnlyData();
    Inputs->BaseColor.UseConstant=true;Inputs->BaseColor.Constant=FLinearColor(.015,.14,.48);
    Inputs->EmissiveColor.UseConstant=true;Inputs->EmissiveColor.Constant=FLinearColor(.005,.025,.10);
    Inputs->Opacity.UseConstant=true;Inputs->Opacity.Constant=.32;
    Inputs->Roughness.UseConstant=true;Inputs->Roughness.Constant=.12;
    Inputs->Specular.UseConstant=true;Inputs->Specular.Constant=.35;
    Glass->PostEditChange();Ocean->SetMaterial(0,Glass);Scene->AddComponent(Ocean.Get(),FTransform::Identity);
    SEditorViewport::Construct(SEditorViewport::FArguments());
}
SCLPlanetPreview::~SCLPlanetPreview()
{
    CavePreview.Reset();NativePreview.Reset();if(Client)Client->Viewport=nullptr;
#if WITH_DEV_AUTOMATION_TESTS
    if(Mesh&&Scene)Scene->RemoveComponent(Mesh.Get());
#endif
    if(DebugWater&&Scene)Scene->RemoveComponent(DebugWater.Get());
    if(Ocean&&Scene)Scene->RemoveComponent(Ocean.Get());
}
#if WITH_DEV_AUTOMATION_TESTS
void SCLPlanetPreview::CreateReferenceMeshForTesting()
{
    Client->bReferenceFixture=true;
    if(Mesh)return;Mesh.Reset(NewObject<UDynamicMeshComponent>(GetTransientPackage()));Scene->AddComponent(Mesh.Get(),FTransform::Identity);
    Mesh->SetMaterial(0,LoadObject<UMaterial>(nullptr,TEXT("/CatalystLandform/Surfaces/M_GlobeGrid.M_GlobeGrid")));
}
#endif
TSharedRef<FEditorViewportClient> SCLPlanetPreview::MakeEditorViewportClient(){Client=MakeShared<FCLGlobeClient>(Scene.Get(),SharedThis(this));return Client.ToSharedRef();}
void SCLPlanetPreview::Rebuild(TSharedPtr<const FCLCompiledPlanetRecipe> Plan,TSharedPtr<const FCLWatershedDebugGeometry> Debug)
{
    if(!Plan){Cancel();return;}
    SetWalkLocation(FVector::ZeroVector);Client->Plan=Plan;PendingDebugPlan=Plan;PendingWatershedDebug=MoveTemp(Debug);
    if(!NativePreview)NativePreview=MakeUnique<FCLNativePreview>();FString Error;
    if(!NativePreview->Start(*Scene->GetWorld(),Plan,false,500,Error,bDraft))UE_LOG(LogTemp,Error,TEXT("Native globe: %s"),*Error);
    Client->NativePreview=NativePreview.Get();
    Client->RegionBoundaries=Plan->GeneratedRegions?Plan->GeneratedRegions->Boundaries:TArray<FCLGeneratedRegionBoundary>();
    Client->RegionCentres.Reset();
    if(Plan->GeneratedRegions)
    {
        auto Place=[&](const FVector& D){return D*((Plan->RadiusMeters+Plan->SampleHeightMeters(D))/Plan->RadiusMeters*100000+100);};
        for(auto& E:Client->RegionBoundaries){E.A=Place(E.A);E.B=Place(E.B);}
        for(const auto& Cell:Plan->GeneratedRegions->Cells)Client->RegionCentres.Add(Place(Cell.Centre));
    }
    Client->UpdatePlacementOverlay();
    if(!Framed){Framed=true;Frame();}Client->Apply();
}
#if WITH_DEV_AUTOMATION_TESTS
void SCLPlanetPreview::FrameDirectionForTesting(FVector D,double Zoom){const auto R=D.Rotation();Client->Navigation.Yaw=R.Yaw;Client->Navigation.Elevation=R.Pitch;Client->Navigation.Zoom=Zoom;Client->Apply();}
void SCLPlanetPreview::OrbitForTesting(FVector2D Delta){Client->Navigation.Orbit(Delta);Client->Apply();}
#endif
void SCLPlanetPreview::Cancel()
{
    CavePreview.Reset();CaveInput.Reset();NativePreview.Reset();PendingDebugPlan.Reset();DisplayedDebugPlan.Reset();PendingWatershedDebug.Reset();
    if(Client){Client->NativePreview=nullptr;Client->WatershedDebug.Reset();Client->DebugPlan.Reset();}
    if(DebugWater)DebugWater->SetVisibility(false);
}
FString SCLPlanetPreview::NativePreviewStatus()const{return NativePreview?NativePreview->Status():TEXT("No native preview");}
bool SCLPlanetPreview::IsUpdating()const{return bEvaluationPending||(NativePreview&&NativePreview->IsUpdating())||(Client&&Client->bCaveOverlay&&CavePreview&&CavePreview->IsApplyingInput());}
bool SCLPlanetPreview::IsApplyingInput()const{return bEvaluationPending||(NativePreview&&NativePreview->IsApplyingInput())||(Client&&Client->bCaveOverlay&&CavePreview&&CavePreview->IsApplyingInput());}
TOptional<float> SCLPlanetPreview::InputProgress()const{if(Client&&Client->bCaveOverlay&&CavePreview&&CavePreview->IsApplyingInput()&&NativePreview&&!NativePreview->IsApplyingInput())return CavePreview->InputProgress();return NativePreview?NativePreview->InputProgress():TOptional<float>();}
FString SCLPlanetPreview::InputProgressText()const{if(Client&&Client->bCaveOverlay&&CavePreview&&CavePreview->IsApplyingInput()&&NativePreview&&!NativePreview->IsApplyingInput())return CavePreview->InputProgressText();return NativePreview?NativePreview->InputProgressText():TEXT("Preparing preview…");}
void SCLPlanetPreview::SetEvaluationPending(bool Pending){bEvaluationPending=Pending;if(Pending)CancelPlacementDrag();}
void SCLPlanetPreview::SetAuthoringValid(bool Valid){bAuthoringInvalid=!Valid;if(!Valid)CancelPlacementDrag();}
void SCLPlanetPreview::SetRegionInspection(bool Inspect)
{
    if(Inspect)bWalkPicking=false;CancelPlacementDrag();Client->bInspectRegions=Inspect;if(Inspect)Client->Selected={};Client->UpdatePlacementOverlay();Client->Invalidate();}
bool SCLPlanetPreview::IsInspectingRegions() const{return Client->bInspectRegions&&Client->Plan&&Client->Plan->GeneratedRegions.IsValid();}
void SCLPlanetPreview::SelectGeneratedRegion(FGuid Id)
{
    SetRegionInspection(true);const auto G=Client->Plan?Client->Plan->GeneratedRegions:nullptr;
    Client->SelectedRegion=G&&G->Cells.ContainsByPredicate([&](const auto& C){return C.Id==Id;})?Id:FGuid();Client->Invalidate();OnRegionSelected.ExecuteIfBound(Client->SelectedRegion);
}
FGuid SCLPlanetPreview::GeneratedSelection() const{return Client->SelectedRegion;}
void SCLPlanetPreview::Frame(){if(Client){Client->Navigation.Zoom=1;Client->Navigation.Pivot=FVector::ZeroVector;Client->Navigation.Pan=FVector2D::ZeroVector;Client->Apply();}}
void SCLPlanetPreview::Dolly(double Steps){Client->Dolly(Steps);}
void SCLPlanetPreview::SetGuides(const FCLRadialGuideSet& Guides,bool Visible){Client->Guides=Guides;Client->bGuides=Visible;UpdateOcean();Client->UpdateIndicators();Client->Invalidate();}
void SCLPlanetPreview::SetOceanVisible(bool Visible){bOceanVisible=Visible;UpdateOcean();}
void SCLPlanetPreview::UpdateOcean()
{
    if(!Client||!Ocean)return;
    const auto* Guide=Client->Guides.Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("OceanHeight");});
    const bool Show=bOceanVisible&&!Client->bWatershedOverlay&&Guide&&FMath::IsFinite(Guide->NormalizedHeight)&&Guide->NormalizedHeight>0&&Guide->NormalizedHeight<=1;
    Ocean->SetVisibility(Show);
    if(Show)Ocean->SetWorldScale3D(FVector(Guide->NormalizedHeight*FCLPlanetNavigation::Radius/50.));
    Client->Invalidate();
}
double SCLPlanetPreview::NormalizedAtScreenY(double Y)const{return Client->NormalizedAtScreenY(Y);}
const TArray<FCLGuideIndicator>& SCLPlanetPreview::Indicators()const{return Client->Indicators;}
const TArray<TArray<FVector2D>>& SCLPlanetPreview::Rings()const{return Client->Rings;}
void SCLPlanetPreview::Pump()
{
    if(NativePreview&&NativePreview->Poll(true)){++AppliedBuilds;bLastPublishedDraft=NativePreview->IsDraft();Client->Invalidate();}
    UpdateWatershedDebug();UpdateCaveVolume();
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
    CancelPlacementDrag();if(Key.IsValid()){bWalkPicking=false;Client->bInspectRegions=false;Client->SelectedRegion.Invalidate();OnRegionSelected.ExecuteIfBound(FGuid());}if(Key!=Client->Selected)Client->Mode=ECLGlobePlacementMode::Move;Client->Selected=Client->Targets.ContainsByPredicate([&](const auto& T){return T.Key==Key;})?Key:FCLGlobeSelection();Client->UpdatePlacementOverlay();Client->Invalidate();
}
FCLGlobeSelection SCLPlanetPreview::Selection() const{return Client->Selected;}
FString SCLPlanetPreview::SelectionName() const{const auto* T=Client->SelectedTarget();return T?T->Name:TEXT("None");}
void SCLPlanetPreview::SetPlacementMode(ECLGlobePlacementMode Mode){if(DragAxis==EAxisList::None){bWalkPicking=false;Client->bInspectRegions=false;Client->Mode=Mode;Client->UpdatePlacementOverlay();Client->Invalidate();}}
ECLGlobePlacementMode SCLPlanetPreview::PlacementMode() const{return Client->Mode;}
FString SCLPlanetPreview::PlacementHint() const
{
    if(bWalkPicking)return TEXT("Click terrain to choose a walking location | Left drag: orbit");
    if(IsInspectingRegions())return TEXT("Click a generated region to inspect its Landform assignment | Left drag: orbit");
    if(DragAxis!=EAxisList::None)return TEXT("Release to apply placement and rebuild terrain | Esc: cancel");
    if(!Client->Selected.IsValid())return TEXT("Click a Landform / feature to select | Left drag: orbit");
    return Client->Mode==ECLGlobePlacementMode::Move?TEXT("Drag Unreal arrows / plane: move on surface | W: move | E: rotate | R: scale | Space: cycle"):
        Client->Mode==ECLGlobePlacementMode::Rotate?TEXT("Drag Unreal radial ring: rotate | Space: cycle | Esc: deselect"):
        TEXT("Drag Unreal X / Y scale handles; XY scales both | Space: cycle | Esc: deselect");
}
void SCLPlanetPreview::SetWalkPicking(bool Enabled){CancelPlacementDrag();bWalkPicking=Enabled;if(Enabled){Select({});SetRegionInspection(false);}Client->Invalidate();}
bool SCLPlanetPreview::IsWalkPicking() const{return bWalkPicking;}
void SCLPlanetPreview::SetWalkLocation(FVector D){WalkSurfacePoint.Reset();Client->WalkDirection=D.GetSafeNormal();Client->Invalidate();}
void SCLPlanetPreview::SetWalkPoint(FVector P){SetWalkLocation(P);WalkSurfacePoint=P;}
FVector SCLPlanetPreview::WalkLocation() const{return Client->WalkDirection;}
void SCLPlanetPreview::CancelPlacementDrag(){DragAxis=EAxisList::None;bPlacementMoved=false;if(Client){Client->Draft.Reset();Client->UpdatePlacementOverlay();Client->Invalidate();}}
FReply SCLPlanetPreview::OnPreviewMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
    const FKey Key=E.GetEffectingButton();if(Key!=EKeys::LeftMouseButton&&Key!=EKeys::MiddleMouseButton&&Key!=EKeys::RightMouseButton)return FReply::Unhandled();
    if(DragAxis!=EAxisList::None){CancelPlacementDrag();Button=FKey();return FReply::Handled().ReleaseMouseCapture();}
    Button=Key;Previous=MouseDown=E.GetScreenSpacePosition();bOrbitDrag=false;bPlacementMoved=false;bPressCanPick=CanPick();
    if(bPressCanPick&&Key==EKeys::LeftMouseButton&&bWalkPicking)
    {
        ClickWalkLocation=FVector::ZeroVector;Client->CursorDirection(PointerPosition(G,E),ClickWalkLocation);ClickWalkPoint=Client->LastPickPoint;
    }
    else if(bPressCanPick&&Key==EKeys::LeftMouseButton&&IsInspectingRegions())
    {
        FVector D;ClickGeneratedRegion.Invalidate();if(Client->CursorDirection(PointerPosition(G,E),D)){const auto& Cells=Client->Plan->GeneratedRegions->Cells;const int32 Index=Client->Plan->GeneratedRegions->Owner(D);if(Cells.IsValidIndex(Index))ClickGeneratedRegion=Cells[Index].Id;}
    }
    else if(bPressCanPick&&Key==EKeys::LeftMouseButton)
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
    else if(Button==EKeys::LeftMouseButton&&!bOrbitDrag&&bPressCanPick&&CanPick()){if(bWalkPicking){SetWalkLocation(ClickWalkLocation);WalkSurfacePoint=ClickWalkPoint;}else if(IsInspectingRegions())SelectGeneratedRegion(ClickGeneratedRegion);else Select(ClickSelection);}
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
    TAtomic<bool> Stop(true);const auto Cancelled=FCLGlobeBuffers::Build(*P,FCLGlobeBuffers::PreviewResolution,&Stop);
    TestTrue(TEXT("Cancelled builds return no partial render buffers"),Cancelled.bCancelled&&Cancelled.Vertices.IsEmpty()&&Cancelled.Triangles.IsEmpty());
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
    auto& App=FSlateApplication::Get();const bool PreviousInput=App.GetHandleDeviceInputWhenApplicationNotActive();App.SetHandleDeviceInputWhenApplicationNotActive(true);ON_SCOPE_EXIT{App.SetHandleDeviceInputWhenApplicationNotActive(PreviousInput);};const auto Widget=SNew(SCLPlanetPreview);
    const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Globe pointer routing test"))).ClientSize(FVector2D(800,600))[Widget];
    App.AddWindow(Window,false);Window->SlatePrepass();
    FWidgetPath WidgetPath;App.GeneratePathToWidgetUnchecked(Widget->ViewportWidget.ToSharedRef(),WidgetPath,EVisibility::All);
    // FindPathToWidget produces a focus path. Pointer routing also requires one
    // optional virtual-pointer slot per arranged widget (UE 5.7's modern ctor).
    TArray<FWidgetAndPointer> PointerWidgets;for(const auto& W:WidgetPath.Widgets.GetInternalArray())PointerWidgets.Emplace(W,TOptional<FVirtualPointerPosition>());
    const FWidgetPath Path(MakeArrayView(PointerWidgets));
    if(!TestTrue(TEXT("Actual inner viewport is in the routed widget path"),Path.IsValid())){App.RequestDestroyWindow(Window);return false;}
    const FVector2D Start(300,300),End(420,340);const auto InitialRotation=Widget->Client->GetViewRotation();const double InitialZoom=Widget->Client->Navigation.Zoom;const double InitialYaw=Widget->Client->Navigation.Yaw;
    const FPointerEvent Down(FSlateApplication::CursorPointerIndex,Start,Start,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());
    const auto Reply=App.RoutePointerDownEvent(Path,Down);
    TestTrue(TEXT("Globe intercepts the press before the inner viewport"),Reply.GetMouseCaptor()==Widget);
    TestTrue(TEXT("Globe owns pointer capture"),Widget->HasMouseCapture());
    const FPointerEvent Move(FSlateApplication::CursorPointerIndex,End,Start,TSet<FKey>{EKeys::LeftMouseButton},FKey(),0,FModifierKeysState());
    App.RoutePointerMoveEvent(Path,Move,false);
    TestFalse(TEXT("A routed drag rotates the actual camera"),Widget->Client->GetViewRotation().Equals(InitialRotation,.01));
    TestTrue(TEXT("Rightward drag increases globe yaw"),Widget->Client->Navigation.Yaw>InitialYaw);
    TestEqual(TEXT("Orbit does not change zoom"),Widget->Client->Navigation.Zoom,InitialZoom);
    const FPointerEvent Up(FSlateApplication::CursorPointerIndex,End,End,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState());
    App.RoutePointerUpEvent(Path,Up);TestFalse(TEXT("Release ends globe capture"),Widget->HasMouseCapture());
    TestFalse(TEXT("Uncaptured move is ignored"),Widget->OnMouseMove(FGeometry(),Move).IsEventHandled());
    Widget->Client->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseScrollUp,IE_Pressed,1));
    TestTrue(TEXT("The inner viewport wheel-key path zooms the globe"),Widget->Client->Navigation.Zoom<InitialZoom);
    const double WheelZoom=Widget->Client->Navigation.Zoom;
    Widget->Client->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseScrollUp,IE_Released,1));
    TestEqual(TEXT("Wheel release does not apply a second zoom step"),Widget->Client->Navigation.Zoom,WheelZoom);
    for(bool Pending:{true,false})
    {
        Widget->SetEvaluationPending(Pending);Widget->SetAuthoringValid(Pending);const double Before=Widget->Client->Navigation.Yaw;
        TestFalse(TEXT("Busy/stale geometry remains unpickable"),Widget->CanPick());
        App.RoutePointerDownEvent(Path,Down);TestTrue(TEXT("Busy/stale preview still admits camera capture"),Widget->HasMouseCapture());
        App.RoutePointerMoveEvent(Path,Move,false);App.RoutePointerUpEvent(Path,Up);
        TestTrue(TEXT("Busy/stale preview still orbits through routed pointer input"),Widget->Client->Navigation.Yaw>Before);
    }
    Widget->SetEvaluationPending(false);Widget->SetAuthoringValid(true);
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
    W->CreateReferenceMeshForTesting(); // Explicit isolated picking oracle.
    const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Native globe widget proof"))).ClientSize(FVector2D(800,600))[W];App.AddWindow(Window,false);Window->SlatePrepass();
    const auto V=W->GetSceneViewport();V->UpdateViewportRHI(false,800,600,EWindowMode::Windowed,PF_Unknown);FlushRenderingCommands();
    const auto C=W->Client;C->Plan=Plan;C->SetViewLocation(FVector(80000,-60000,200000));C->SetViewRotation((FVector(0,0,45000)-C->GetViewLocation()).Rotation());C->StudioScene->AimLights(C->GetViewRotation());const auto B=FCLGlobeBuffers::Build(*Plan,32);CLSetPreviewMesh(*W->Mesh.Get(),B.Vertices,B.Triangles,B.Normals,B.UV,B.Colors,B.Tangents);W->SetAuthoringRecipe(Recipe);W->Select({Region.Id,false});
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
    auto& App=FSlateApplication::Get();const auto W=SNew(SCLPlanetPreview);W->CreateReferenceMeshForTesting();const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Globe exposure orbit proof"))).ClientSize(FVector2D(640,480))[W];App.AddWindow(Window,false);Window->SlatePrepass();const auto V=W->GetSceneViewport();V->UpdateViewportRHI(false,640,480,EWindowMode::Windowed,PF_Unknown);FlushRenderingCommands();
    auto Recipe=FCLPlanetCompiler::BasePlanet();FString Error;const auto Plan=FCLPlanetCompiler::Compile(Recipe,Error);if(!Plan){AddError(Error);App.RequestDestroyWindow(Window);return false;}
    const auto B=FCLGlobeBuffers::Build(*Plan,48);W->Client->Plan=Plan;CLSetPreviewMesh(*W->Mesh.Get(),B.Vertices,B.Triangles,B.Normals,B.UV,B.Colors,B.Tangents);
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
    W->CreateReferenceMeshForTesting(); // Explicit isolated picking oracle.
    const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Generated region inspection proof"))).ClientSize(FVector2D(800,600))[W];App.AddWindow(Window,false);Window->SlatePrepass();
    const auto V=W->GetSceneViewport();V->UpdateViewportRHI(false,800,600,EWindowMode::Windowed,PF_Unknown);FlushRenderingCommands();
    const auto C=W->Client;C->Plan=Plan;const auto B=FCLGlobeBuffers::Build(*Plan,96);C->RegionBoundaries=B.RegionBoundaries;C->RegionCentres=B.RegionCentres;
    TestTrue(TEXT("Compiled regions have rendered surface contours"),B.RegionBoundaries.Num()>100&&B.RegionCentres.Num()==24);
    CLSetPreviewMesh(*W->Mesh.Get(),B.Vertices,B.Triangles,B.Normals,B.UV,B.Colors,B.Tangents);
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

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSurfaceGlobeTest,"Catalyst.Landform.Surfaces.RenderedGlobe",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLSurfaceGlobeTest::RunTest(const FString&)
{
    FString Json,Error;FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/TerrainSurfaces.json")));
    auto* Asset=NewObject<UCLPlanetRecipeAsset>();if(!Asset->ImportRecipeJson(Json,Error)){AddError(Error);return false;}const auto Plan=FCLPlanetCompiler::Compile(Asset->Recipe,Error);if(!Plan){AddError(Error);return false;}
    const auto B=FCLGlobeBuffers::Build(*Plan,48);TArray<FCLSurfaceWeights> Weights;Plan->SampleSurfaceWeights(B.SourceDirections,Weights);double Worst=0;
    for(int32 I=0;I<Weights.Num();++I){const auto W=CLPackSurfaceWeights(Weights[I]);Worst=FMath::Max(Worst,double(FMath::Abs(W.R-B.Colors[I].R)+FMath::Abs(W.G-B.Colors[I].G)+FMath::Abs(W.B-B.Colors[I].B)+FMath::Abs(W.A-B.Colors[I].A)));}
    TestTrue(TEXT("Globe packs the same final-field surface weights"),Worst<.00001);
    auto& App=FSlateApplication::Get();const auto W=SNew(SCLPlanetPreview);W->CreateReferenceMeshForTesting();const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Terrain surface appearance proof"))).ClientSize(FVector2D(800,600))[W];App.AddWindow(Window,false);Window->SlatePrepass();
    const auto V=W->GetSceneViewport();V->UpdateViewportRHI(false,800,600,EWindowMode::Windowed,PF_Unknown);FlushRenderingCommands();W->Client->Plan=Plan;
    W->Mesh->SetMaterial(0,CLMakeSurfacePreviewMaterial(W->Mesh.Get(),*Plan->Surfaces,true));CLSetPreviewMesh(*W->Mesh.Get(),B.Vertices,B.Triangles,B.Normals,B.UV,B.UV1,B.UV2,B.UV3,B.Colors,B.Tangents);W->SetGuides(Asset->Recipe.RadialGuides,false);
    if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
    // Let the editor publish loaded material resources and the preview scene proxy
    // before reading GPU pixels. Immediate draws can capture the fallback material.
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this,W,Window,V,B,Worst,Plan]
    {
    auto& App=FSlateApplication::Get();
    int32 Clipped=0,Changed=0,Earthy=0,GridPixels=0;
    for(int32 Frame=0;Frame<3;++Frame)
    {
        const FVector Eye=FVector(Frame==0?1:Frame==1?-1:0,-1,Frame==2?-1:.6).GetSafeNormal()*150000;W->Client->SetViewLocation(Eye);W->Client->SetViewRotation((-Eye).Rotation());W->Client->StudioScene->AimLights(W->Client->GetViewRotation());W->Scene->UpdateCaptureContents();TArray<FColor> Pixels;
        for(int32 K=0;K<3;++K){W->Scene->GetWorld()->SendAllEndOfFrameUpdates();V->Invalidate();V->Draw(false);FlushRenderingCommands();V->ReadPixels(Pixels);}if(Pixels.IsEmpty()){AddError(TEXT("No rendered surface frame"));continue;}
        TSet<uint32> Colors;for(const auto& C:Pixels){if(C.R>C.G+5&&C.G>C.B+3)++Earthy;if(C.R>250&&C.G>250&&C.B>250)++Clipped;if(C.R>40||C.G>40||C.B>50)Colors.Add(C.DWColor());}Changed=FMath::Max(Changed,Colors.Num());
        TArray<uint8> PNG;FImageUtils::CompressImageArray(800,600,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Verification/surface-globe-%d.png"),Frame)));
        if(Frame==0)
        {
            auto* Material=CastChecked<UMaterialInstanceDynamic>(W->Mesh->GetMaterial(0));Material->SetVectorParameterValue(TEXT("CLGrid"),FLinearColor(0,10,0,0));TArray<FColor> WithoutGrid;
            for(int32 K=0;K<3;++K){W->Scene->GetWorld()->SendAllEndOfFrameUpdates();V->Invalidate();V->Draw(false);FlushRenderingCommands();V->ReadPixels(WithoutGrid);}
            for(int32 I=0;I<FMath::Min(Pixels.Num(),WithoutGrid.Num());++I)if(FMath::Abs(int32(Pixels[I].R)-WithoutGrid[I].R)+FMath::Abs(int32(Pixels[I].G)-WithoutGrid[I].G)+FMath::Abs(int32(Pixels[I].B)-WithoutGrid[I].B)>30)++GridPixels;
            Material->SetVectorParameterValue(TEXT("CLGrid"),FLinearColor(1,10,0,0));
        }
    }
    TestTrue(TEXT("Geographic grid is visibly rendered on the terrain"),GridPixels>400);TestTrue(TEXT("Shared surface material actually renders tonal detail"),Changed>1000);TestTrue(TEXT("Soil and scree render their authored warm colors instead of the default grey material"),Earthy>1000);TestTrue(TEXT("Surface orbit frames retain stable exposure"),Clipped<3*800*600/100);
    AddInfo(FString::Printf(TEXT("Surface globe: %d vertices, %.9g weight disagreement, %d distinct lit colors, %d clipped pixels, %d grid pixels in 3 frames"),B.Vertices.Num(),Worst,Changed,Clipped,GridPixels));
    // VPP chunk LODs scale their local mesh coordinates. The native shader must
    // preserve physical texture size when the same world-space mesh is rescaled.
    auto* Native=UMaterialInstanceDynamic::Create(Plan->Surfaces->Owner->Library.Entries[0].SurfaceType->Material,W->Mesh.Get());
    Native->SetVectorParameterValue(TEXT("CLGrid"),FLinearColor(0,10,0,0));W->Mesh->SetMaterial(0,Native);
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this,W,Window,V,B]
    {
        auto Before=MakeShared<TArray<FColor>>();
        for(int32 K=0;K<3;++K){W->Scene->GetWorld()->SendAllEndOfFrameUpdates();V->Invalidate();V->Draw(false);FlushRenderingCommands();V->ReadPixels(*Before);}
        auto Vertices=B.Vertices;for(auto& P:Vertices)P/=25;
        W->Mesh->SetWorldScale3D(FVector(25));CLSetPreviewMesh(*W->Mesh.Get(),Vertices,B.Triangles,B.Normals,B.UV,B.UV1,B.UV2,B.UV3,B.Colors,B.Tangents);
        ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this,W,Window,V,Before]
        {
            TArray<FColor> After;
            for(int32 K=0;K<3;++K){W->Scene->GetWorld()->SendAllEndOfFrameUpdates();V->Invalidate();V->Draw(false);FlushRenderingCommands();V->ReadPixels(After);}
            double Difference=0;const int32 Count=FMath::Min(Before->Num(),After.Num());
            for(int32 I=0;I<Count;++I)Difference+=FMath::Abs(int32((*Before)[I].R)-After[I].R)+FMath::Abs(int32((*Before)[I].G)-After[I].G)+FMath::Abs(int32((*Before)[I].B)-After[I].B);
            const double Mean=Count?Difference/(Count*3):255;
            TestTrue(TEXT("Native artwork preserves metre scale across 25x chunk transforms"),Count==800*600&&Mean<3);
            AddInfo(FString::Printf(TEXT("Native scan projection: 1x versus 25x equivalent world mesh, mean RGB difference %.6f / 255"),Mean));
            FSlateApplication::Get().RequestDestroyWindow(Window);
        },.3f));
    },.3f));
    },.5f));return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGlobeWalkPickingTest,"Catalyst.Landform.PlanetBuilder.WalkingLocationPicking",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGlobeWalkPickingTest::RunTest(const FString&)
{
    auto& App=FSlateApplication::Get();const auto W=SNew(SCLPlanetPreview);W->CreateReferenceMeshForTesting();const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Walking location picking"))).ClientSize(FVector2D(640,480))[W];App.AddWindow(Window,false);Window->SlatePrepass();
    const auto V=W->GetSceneViewport();V->UpdateViewportRHI(false,640,480,EWindowMode::Windowed,PF_Unknown);FlushRenderingCommands();
    auto R=FCLPlanetCompiler::BasePlanet();R.RadiusMeters=1000;R.BaseSurfaceNormalized=.5;R.NoiseLayers.Reset();FString Error;const auto Plan=FCLPlanetCompiler::Compile(R,Error);
    if(!Plan){AddError(Error);App.RequestDestroyWindow(Window);return false;}
    W->Client->Plan=Plan;const auto B=FCLGlobeBuffers::Build(*Plan,8);CLSetPreviewMesh(*W->Mesh.Get(),B.Vertices,B.Triangles,B.Normals,B.UV,B.Colors,B.Tangents);
    W->SetWalkPicking(true);W->Client->SetViewLocation(FVector(0,0,220000));W->Client->SetViewRotation(FRotator(-90,0,0));V->Invalidate();V->Draw(false);FlushRenderingCommands();
    FWidgetPath Actual;App.GeneratePathToWidgetUnchecked(W->ViewportWidget.ToSharedRef(),Actual,EVisibility::All);const auto G=Actual.Widgets.Last().Geometry;
    TArray<FWidgetAndPointer> Arranged;for(const auto& A:Actual.Widgets.GetInternalArray())Arranged.Emplace(A,TOptional<FVirtualPointerPosition>());const FWidgetPath Path(MakeArrayView(Arranged));
    auto Click=[&](FVector2D P)
    {
        App.RoutePointerDownEvent(Path,FPointerEvent(FSlateApplication::CursorPointerIndex,P,P,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        App.RoutePointerUpEvent(Path,FPointerEvent(FSlateApplication::CursorPointerIndex,P,P,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState()));
    };
    Click(G.LocalToAbsolute(G.GetLocalSize()*.5));
    TestTrue(TEXT("Routed terrain click picks north pole in recipe space"),W->WalkLocation().Equals(FVector::ZAxisVector,.001));
    TestFalse(TEXT("Picking never starts a geometry rebuild"),W->IsUpdating());TestFalse(TEXT("Walking picker does not select a placement"),W->Selection().IsValid());
    W->SetWalkPicking(false);TestTrue(TEXT("Exiting picker retains the return marker"),W->WalkLocation().Equals(FVector::ZAxisVector,.001));
    W->SetWalkPicking(true);Click(G.LocalToAbsolute(FVector2D(1,1)));TestTrue(TEXT("Empty-space click clears walking destination"),W->WalkLocation().IsNearlyZero());
    App.RequestDestroyWindow(Window);return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTectonicGlobeTest,"Catalyst.Landform.Tectonics.RenderedProgression",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLTectonicGlobeTest::RunTest(const FString&)
{
    // The comparison owns its input; editing the user's planet may legitimately
    // remove its material stack or pin controls that invalidate a progression test.
    FString Json,ImportError;const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());
    if(!Plugin||!FFileHelper::LoadFileToString(Json,*(Plugin->GetBaseDir()/TEXT("Examples/TectonicBase.json")))||!A->ImportRecipeJson(Json,ImportError))
    {AddError(TEXT("Cannot load the owned tectonic comparison: ")+ImportError);return false;}
    auto& App=FSlateApplication::Get();const auto W=SNew(SCLPlanetPreview);W->CreateReferenceMeshForTesting();const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Tectonic activity progression"))).ClientSize(FVector2D(800,600))[W];App.AddWindow(Window,false);Window->SlatePrepass();
    const auto V=W->GetSceneViewport();V->UpdateViewportRHI(false,800,600,EWindowMode::Windowed,PF_Unknown);FlushRenderingCommands();
    auto Buffers=MakeShared<TArray<FCLGlobeBuffers>>();auto Plans=MakeShared<TArray<TSharedPtr<const FCLCompiledPlanetRecipe>>>();
    const double Started=FPlatformTime::Seconds();
    for(int32 I=0;I<4;++I)
    {
        auto R=A->Recipe;R.Tectonics.Activity=I==0?0:I==1?.55:1.;R.Tectonics.AlgorithmVersion=I==3?1:2;FString Error;const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);App.RequestDestroyWindow(Window);return false;}
        if(!P->Surfaces){AddError(TEXT("The owned comparison requires its geological material stack"));App.RequestDestroyWindow(Window);return false;}
        Plans->Add(P);Buffers->Add(FCLGlobeBuffers::Build(*P,192));
    }
    W->Mesh->SetMaterial(0,CLMakeSurfacePreviewMaterial(W->Mesh.Get(),*(*Plans)[0]->Surfaces,true));
    const auto& Initial=(*Buffers)[0];W->Client->Plan=(*Plans)[0];CLSetPreviewMesh(*W->Mesh.Get(),Initial.Vertices,Initial.Triangles,Initial.Normals,Initial.UV,Initial.UV1,Initial.UV2,Initial.UV3,Initial.Colors,Initial.Tangents);
    if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
    AddInfo(FString::Printf(TEXT("Four 192-cell tectonic globe buffers: %.3f seconds"),FPlatformTime::Seconds()-Started));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this,W,Window,V,Buffers,Plans]
    {
        IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("Verification/tectonic-preset")),true);
        TArray<FColor> First;int32 MostClipped=0,Earthy=0;double Difference=0;
        for(int32 I=0;I<3;++I)
        {
            const auto& B=(*Buffers)[I];W->Client->Plan=(*Plans)[I];CLSetPreviewMesh(*W->Mesh.Get(),B.Vertices,B.Triangles,B.Normals,B.UV,B.UV1,B.UV2,B.UV3,B.Colors,B.Tangents);
            const FVector Eye=FVector(1,-1,.65).GetSafeNormal()*165000;W->Client->SetViewLocation(Eye);W->Client->SetViewRotation((-Eye).Rotation());W->Client->StudioScene->AimLights(W->Client->GetViewRotation());W->Scene->UpdateCaptureContents();TArray<FColor> Pixels;
            for(int32 K=0;K<4;++K){W->Scene->GetWorld()->SendAllEndOfFrameUpdates();V->Invalidate();V->Draw(false);FlushRenderingCommands();V->ReadPixels(Pixels);}
            TestEqual(TEXT("Globe frame is rendered"),Pixels.Num(),800*600);int32 Clipped=0;for(const auto& C:Pixels){Clipped+=C.R>250&&C.G>250&&C.B>250;Earthy+=C.R>C.G+5&&C.G>C.B+3;}MostClipped=FMath::Max(MostClipped,Clipped);
            if(I==0)First=Pixels;else if(I==2)for(int32 K=0;K<Pixels.Num()&&K<First.Num();++K)Difference+=FMath::Abs(int32(Pixels[K].R)-First[K].R)+FMath::Abs(int32(Pixels[K].G)-First[K].G)+FMath::Abs(int32(Pixels[K].B)-First[K].B);
            TArray<uint8> PNG;FImageUtils::CompressImageArray(800,600,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Verification/tectonic-preset/globe-%d.png"),I)));
        }
        TestTrue(TEXT("Scanned geological materials actually render"),Earthy>1000);TestTrue(TEXT("Activity changes the rendered surface visibly"),Difference/(800*600*3)>2);TestTrue(TEXT("Exposure remains legible"),MostClipped<800*600/100);
        IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("Verification/tectonic-organic")),true);
        const FVector Views[]={FVector(1,-1,.65),FVector(-1,.2,-.8),FVector(.2,1,.3)};
        for(int32 Version=1;Version<=2;++Version)for(int32 Angle=0;Angle<3;++Angle)
        {
            const int32 I=Version==1?3:2;const auto& B=(*Buffers)[I];W->Client->Plan=(*Plans)[I];CLSetPreviewMesh(*W->Mesh.Get(),B.Vertices,B.Triangles,B.Normals,B.UV,B.UV1,B.UV2,B.UV3,B.Colors,B.Tangents);
            const FVector Eye=Views[Angle].GetSafeNormal()*165000;W->Client->SetViewLocation(Eye);W->Client->SetViewRotation((-Eye).Rotation());W->Client->StudioScene->AimLights(W->Client->GetViewRotation());W->Scene->UpdateCaptureContents();TArray<FColor> Pixels;
            for(int32 K=0;K<4;++K){W->Scene->GetWorld()->SendAllEndOfFrameUpdates();V->Invalidate();V->Draw(false);FlushRenderingCommands();V->ReadPixels(Pixels);}
            TestEqual(TEXT("Full-activity comparison frame is rendered"),Pixels.Num(),800*600);
            TArray<uint8> PNG;FImageUtils::CompressImageArray(800,600,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Verification/tectonic-organic/v%d-angle%d.png"),Version,Angle)));
        }
        AddInfo(FString::Printf(TEXT("Rendered quiet/intense mean difference %.3f/255; clipped pixels %d"),Difference/(800*600*3),MostClipped));FSlateApplication::Get().RequestDestroyWindow(Window);
    },.6f));return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLContinentalGlobeTest,"Catalyst.Landform.Continental.RenderedSettings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLContinentalGlobeTest::RunTest(const FString&)
{
    FString Json,Error;const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"));TStrongObjectPtr<UCLPlanetRecipeAsset> Source(NewObject<UCLPlanetRecipeAsset>());
    if(!Plugin||!FFileHelper::LoadFileToString(Json,*(Plugin->GetBaseDir()/TEXT("Examples/ContinentalBase.json")))||!Source->ImportRecipeJson(Json,Error)){AddError(Error);return false;}
    auto& App=FSlateApplication::Get();const auto W=SNew(SCLPlanetPreview);W->CreateReferenceMeshForTesting();const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Continental foundation settings"))).ClientSize(FVector2D(800,600))[W];App.AddWindow(Window,false);Window->SlatePrepass();
    const auto V=W->GetSceneViewport();V->UpdateViewportRHI(false,800,600,EWindowMode::Windowed,PF_Unknown);FlushRenderingCommands();
    auto Buffers=MakeShared<TArray<FCLGlobeBuffers>>();auto Plans=MakeShared<TArray<TSharedPtr<const FCLCompiledPlanetRecipe>>>();
    const double Started=FPlatformTime::Seconds();
    for(int32 I=0;I<6;++I)
    {
        auto R=Source->Recipe;R.Tectonics.MotionDirection=I==1?0:I==2?-1:1;R.Tectonics.Setting=I==3?ECLContinentalSetting::Subduction:I==4?ECLContinentalSetting::Rift:I==5?ECLContinentalSetting::Transform:ECLContinentalSetting::Collision;
        auto P=FCLPlanetCompiler::Compile(R,Error);if(!P||!P->Surfaces){AddError(Error);App.RequestDestroyWindow(Window);return false;}Plans->Add(P);Buffers->Add(FCLGlobeBuffers::Build(*P,192));
    }
    W->Mesh->SetMaterial(0,CLMakeSurfacePreviewMaterial(W->Mesh.Get(),*(*Plans)[0]->Surfaces,true));const auto& Initial=(*Buffers)[0];W->Client->Plan=(*Plans)[0];CLSetPreviewMesh(*W->Mesh.Get(),Initial.Vertices,Initial.Triangles,Initial.Normals,Initial.UV,Initial.UV1,Initial.UV2,Initial.UV3,Initial.Colors,Initial.Tangents);
    if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();AddInfo(FString::Printf(TEXT("Six continental 192-cell buffers: %.3f seconds"),FPlatformTime::Seconds()-Started));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this,W,Window,V,Buffers,Plans]
    {
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/continental-settings");IFileManager::Get().MakeDirectory(*Folder,true);const TCHAR* Names[]={TEXT("collision"),TEXT("stationary"),TEXT("reversed"),TEXT("subduction"),TEXT("rift"),TEXT("transform")};
        for(int32 I=0;I<6;++I)for(int32 Angle=0;Angle<2;++Angle)
        {
            const auto& B=(*Buffers)[I];W->Client->Plan=(*Plans)[I];CLSetPreviewMesh(*W->Mesh.Get(),B.Vertices,B.Triangles,B.Normals,B.UV,B.UV1,B.UV2,B.UV3,B.Colors,B.Tangents);
            const FVector Eye=(Angle==0?FVector(1,.35,.55):FVector(-1,-.35,.4)).GetSafeNormal()*130000;W->Client->SetViewLocation(Eye);W->Client->SetViewRotation((-Eye).Rotation());W->Client->StudioScene->AimLights(W->Client->GetViewRotation());W->Scene->UpdateCaptureContents();TArray<FColor> Pixels;
            for(int32 K=0;K<4;++K){W->Scene->GetWorld()->SendAllEndOfFrameUpdates();V->Invalidate();V->Draw(false);FlushRenderingCommands();V->ReadPixels(Pixels);}
            TestEqual(TEXT("Continental comparison renders a complete frame"),Pixels.Num(),800*600);TArray<uint8> PNG;FImageUtils::CompressImageArray(800,600,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Folder/FString::Printf(TEXT("%s-%d.png"),Names[I],Angle)));
        }
        FSlateApplication::Get().RequestDestroyWindow(Window);
    },.6f));return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGlobeOceanTest,"Catalyst.Landform.PlanetBuilder.DisplayOcean",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGlobeOceanTest::RunTest(const FString&)
{
    auto R=FCLPlanetCompiler::ContinentalBase();const auto Original=R;
    const auto W=SNew(SCLPlanetPreview);W->CreateReferenceMeshForTesting();W->SetGuides(R.RadialGuides,true);
    TestFalse(TEXT("Ocean starts hidden"),W->IsOceanVisible()||W->Ocean->IsVisible());
    W->SetOceanVisible(true);const auto* G=R.RadialGuides.Guides.FindByPredicate([](const auto& V){return V.Name==TEXT("OceanHeight");});
    if(!TestNotNull(TEXT("Fixture has OceanHeight"),G))return false;
    TestTrue(TEXT("Display ocean is visible"),W->Ocean->IsVisible());
    TestTrue(TEXT("Ocean radius follows normalized guide"),FMath::IsNearlyEqual(W->Ocean->GetComponentScale().X*50,G->NormalizedHeight*100000,1.e-6));
    auto Guides=R.RadialGuides;Guides.Guides.FindByPredicate([](const auto& V){return V.Name==TEXT("OceanHeight");})->NormalizedHeight=.47;W->SetGuides(Guides,false);
    TestTrue(TEXT("Hidden guide rings still update ocean geometry"),FMath::IsNearlyEqual(W->Ocean->GetComponentScale().X*50,47000.,1.e-6));
    Guides.Guides.RemoveAll([](const auto& V){return V.Name==TEXT("OceanHeight");});W->SetGuides(Guides,true);
    TestFalse(TEXT("Missing guide suppresses ocean"),W->Ocean->IsVisible());
    TestEqual(TEXT("Ocean has no collision"),W->Ocean->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
    TestTrue(TEXT("Ocean does not alter recipe"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&R,&Original,0));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGeologyRenderedTest,"Catalyst.Landform.PlanetLayers.RenderedRangeAndOcean",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGeologyRenderedTest::RunTest(const FString&)
{
    auto& App=FSlateApplication::Get();const auto W=SNew(SCLPlanetPreview);W->CreateReferenceMeshForTesting();
    const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Calibrated geology"))).ClientSize(FVector2D(800,600))[W];App.AddWindow(Window,false);Window->SlatePrepass();
    const auto V=W->GetSceneViewport();V->UpdateViewportRHI(false,800,600,EWindowMode::Windowed,PF_Unknown);FlushRenderingCommands();
    FString Json,Error;TStrongObjectPtr<UCLPlanetRecipeAsset> Source(NewObject<UCLPlanetRecipeAsset>());
    FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/GuidedGeology.json")));
    if(!Source->ImportRecipeJson(Json,Error)){AddError(Error);App.RequestDestroyWindow(Window);return false;}
    auto Base=Source->Recipe;Base.Continents.bOverrideExtent=false;Base.Continents.bOverrideRelief=false;Base.Tectonics.bOverrideMotion=false;Base.Tectonics.bOverrideRelief=false;
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/planet-tuning-frames");IFileManager::Get().MakeDirectory(*Folder,true);
    auto Plans=MakeShared<TArray<TSharedPtr<const FCLCompiledPlanetRecipe>>>();auto Buffers=MakeShared<TArray<FCLGlobeBuffers>>();
    for(int32 I=0;I<6;++I)
    {
        auto R=Base;const double Position=I==0?0:I==1?.5:1.;R.Continents.ApplyProgression(Position);R.Tectonics.ApplyProgression(Position);
        if(I==3)R.Continents.Seed=529;if(I==4)R.Tectonics.Seed=817;
        const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P||!P->Surfaces){AddError(Error);App.RequestDestroyWindow(Window);return false;}
        Plans->Add(P);Buffers->Add(FCLGlobeBuffers::Build(*P,128));
    }
    FVector EyeDirection=FVector(1,-1,.65).GetSafeNormal();double Best=1.e9;
    const auto* OceanGuide=Base.RadialGuides.Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("OceanHeight");});
    for(int32 K=0;OceanGuide&&K<256;++K)
    {
        const double Z=1-2*(K+.5)/256.,A=K*2.399963229728653;const FVector D(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z);
        const double Distance=FMath::Abs((*Plans)[2]->SampleHeightMeters(D)-(OceanGuide->NormalizedHeight-1)*Base.RadiusMeters);
        if(Distance<Best){Best=Distance;EyeDirection=D;}
    }
    auto Intense=MakeShared<TArray<FColor>>();auto Differences=MakeShared<TArray<double>>();Differences->Init(0,3);
    for(int32 I=0;I<6;++I)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([W,Plans,Buffers,Base,EyeDirection,I]
        {
            const auto& P=(*Plans)[I];const auto& B=(*Buffers)[I];W->Client->Plan=P;
            W->Mesh->SetMaterial(0,CLMakeSurfacePreviewMaterial(W->Mesh.Get(),*P->Surfaces,true));
            CLSetPreviewMesh(*W->Mesh.Get(),B.Vertices,B.Triangles,B.Normals,B.UV,B.UV1,B.UV2,B.UV3,B.Colors,B.Tangents);
            W->SetGuides(Base.RadialGuides,false);W->SetOceanVisible(I==5);
            const FVector Eye=EyeDirection*135000;W->Client->SetViewLocation(Eye);W->Client->SetViewRotation((-Eye).Rotation());W->Client->StudioScene->AimLights(W->Client->GetViewRotation());
        },.05f));
        ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this,W,Window,V,Folder,I,Intense,Differences]
        {
            // Visibility/render-state changes need an editor frame before readback.
            if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();W->Scene->UpdateCaptureContents();TArray<FColor> Pixels;
            for(int32 K=0;K<5;++K){W->Scene->GetWorld()->SendAllEndOfFrameUpdates();V->Invalidate();V->Draw(false);FlushRenderingCommands();V->ReadPixels(Pixels);}
            TestEqual(TEXT("Rendered tuning frame exists"),Pixels.Num(),800*600);
            if(I==2)*Intense=Pixels;
            if(I>=3)for(int32 K=0;K<Pixels.Num()&&K<Intense->Num();++K)
                (*Differences)[I-3]+=FMath::Abs(int32(Pixels[K].R)-(*Intense)[K].R)+FMath::Abs(int32(Pixels[K].G)-(*Intense)[K].G)+FMath::Abs(int32(Pixels[K].B)-(*Intense)[K].B);
            TArray<uint8> PNG;FImageUtils::CompressImageArray(800,600,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Folder/FString::Printf(TEXT("globe-%d.png"),I)));
            if(I==5)
            {
                TestTrue(TEXT("Continental variation changes rendered globe"),(*Differences)[0]/(800*600*3)>.2);
                TestTrue(TEXT("Tectonic variation changes rendered globe"),(*Differences)[1]/(800*600*3)>.2);
                TestTrue(TEXT("Ocean is visibly rendered over terrain"),(*Differences)[2]/(800*600*3)>1);
                int32 Blue=0;for(int32 K=0;K<Pixels.Num()&&K<Intense->Num();++K)Blue+=(int32(Pixels[K].B)-Pixels[K].R)-(int32((*Intense)[K].B)-(*Intense)[K].R)>10;
                TestTrue(TEXT("Glass is visibly blue rather than a neutral overlay"),Blue>1000);
                AddInfo(FString::Printf(TEXT("Rendered mean differences: continents %.3f, tectonics %.3f, ocean %.3f"),(*Differences)[0]/(800*600*3),(*Differences)[1]/(800*600*3),(*Differences)[2]/(800*600*3)));
                FSlateApplication::Get().RequestDestroyWindow(Window);
            }
        },.2f));
    }
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
class FCLGlobeAwait : public IAutomationLatentCommand
{
    TFunction<bool()> Check;
public:
    explicit FCLGlobeAwait(TFunction<bool()> In):Check(MoveTemp(In)){}
    virtual bool Update() override{return Check();}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGlobePublicationTest,"Catalyst.Landform.PlanetLayers.VariationPublication",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGlobePublicationTest::RunTest(const FString&)
{
    auto R=FCLPlanetCompiler::BasePlanet();R.SeparateGeologyLayers();R.Continents.bEnabled=true;R.Tectonics.bEnabled=true;R.Tectonics.AlgorithmVersion=2;
    FString Error;const auto Initial=FCLPlanetCompiler::Compile(R,Error);R.Continents.Seed=817;const auto Changed=FCLPlanetCompiler::Compile(R,Error);
    if(!Initial||!Changed){AddError(Error);return false;}
    const auto W=SNew(SCLPlanetPreview);const auto Window=SNew(SWindow).ClientSize(FVector2D(640,480))[W];FSlateApplication::Get().AddWindow(Window,false);Window->SlatePrepass();W->Rebuild(Initial);
    auto Owner=MakeShared<uint32>(0);auto Runtime=MakeShared<const void*>(nullptr);auto Phase=MakeShared<int32>(0);auto Before=MakeShared<uint64>(0);auto Started=MakeShared<double>(FPlatformTime::Seconds());
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FCLGlobeAwait>([this,W,Window,Initial,Changed,Phase,Before,Started,Owner,Runtime]
    {
        W->Pump();
        auto Close=[&]{FSlateApplication::Get().RequestDestroyWindow(Window);return true;};
        if(FPlatformTime::Seconds()-*Started>30){AddError(FString::Printf(TEXT("Globe variation failed to publish/refine within 30 seconds; phase %d, builds %llu"),*Phase,W->PublishedBuilds()));return Close();}
        if(*Phase==0)
        {
            if(W->IsUpdating())return false;
            TestTrue(TEXT("Initial plan is displayed"),W->Client->Plan==Initial);*Owner=W->NativePreview->WorldIdentity();*Runtime=W->NativePreview->RuntimeIdentity();*Before=W->PublishedBuilds();W->Rebuild(Changed);W->Rebuild(Initial);W->Rebuild(Changed);*Started=FPlatformTime::Seconds();*Phase=1;return false;
        }
        if(W->IsUpdating())return false;
        TestTrue(TEXT("Latest coalesced variation publishes through VPP"),W->PublishedBuilds()>*Before);
        TestTrue(TEXT("Newest variation owns the displayed plan"),W->Client->Plan==Changed);
        TestFalse(TEXT("Variation finishes at full quality"),W->HasDraftMesh());
        TestTrue(TEXT("Native VPP renders the accepted variation"),W->NativePreview&&W->NativePreview->PublishedPlan()==Changed&&W->NativePreview->UsesNativePresentation());
        TestTrue(TEXT("No application terrain mesh is constructed"),!W->Mesh);
        TestEqual(TEXT("Recipe changes retain the native world"),W->NativePreview->WorldIdentity(),*Owner);
        TestTrue(TEXT("Recipe changes retain the native runtime"),W->NativePreview->RuntimeIdentity()==*Runtime);
        AddInfo(W->NativePreview->Status());return Close();
    }));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLNativePreviewRadiiTest,"Catalyst.Landform.NativeGeneration.NativePreviewRadii",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLNativePreviewRadiiTest::RunTest(const FString&)
{
    TArray<TSharedPtr<const FCLCompiledPlanetRecipe>> Plans;FString Error;
    for(double Radius:{100.,1000.,6371000.})
    {auto R=FCLPlanetCompiler::BasePlanet();R.SchemaVersion=17;R.RadiusMeters=Radius;R.SurfaceStack=FCLSurfaceStack::MakeDefault();R.SurfaceStack.Layers.Reset();const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}Plans.Add(P);}
    const auto W=SNew(SCLPlanetPreview);const auto Window=SNew(SWindow).ClientSize(FVector2D(640,480))[W];
    FSlateApplication::Get().AddWindow(Window,false);Window->SlatePrepass();W->Rebuild(Plans[0]);
    auto Phase=MakeShared<int32>(0);auto Owner=MakeShared<uint32>(0);auto Runtime=MakeShared<const void*>(nullptr);auto Started=MakeShared<double>(FPlatformTime::Seconds());
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FCLGlobeAwait>([this,W,Window,Plans,Phase,Owner,Runtime,Started]
    {
        W->Pump();auto Close=[&]{FSlateApplication::Get().RequestDestroyWindow(Window);return true;};
        if(FPlatformTime::Seconds()-*Started>30){AddError(W->NativePreviewStatus());return Close();}
        if(W->IsUpdating())return false;
        const auto& Preview=*W->NativePreview;TestTrue(TEXT("VPP presents the accepted radius"),Preview.UsesNativePresentation()&&Preview.PublishedPlan()==Plans[*Phase]);
        TestTrue(TEXT("No custom terrain presenter"),!W->Mesh);
        FString MaterialError;TestTrue(TEXT("Native globe mapping follows each radius edit"),Preview.CheckMaterialCoordinates(MaterialError));AddInfo(MaterialError);
        if(*Phase==0){*Owner=Preview.WorldIdentity();*Runtime=Preview.RuntimeIdentity();}
        else {TestEqual(TEXT("Radius edit retains native world"),Preview.WorldIdentity(),*Owner);TestTrue(TEXT("Radius edit retains native runtime"),Preview.RuntimeIdentity()==*Runtime);}
        double Worst=0;for(const auto& P:Preview.Published().Vertices)Worst=FMath::Max(Worst,FMath::Abs(P.Length()-45000.));
        AddInfo(FString::Printf(TEXT("Radius %.0f m: %d native vertices, normalized sphere residual %.6f cm"),Plans[*Phase]->RadiusMeters,Preview.Published().Vertices.Num(),Worst));
        TestTrue(TEXT("Native normalized sphere stays within one 50 cm preview voxel"),Preview.Published().Vertices.Num()>0&&Worst<=50);
        if(++*Phase==Plans.Num())return Close();W->Rebuild(Plans[*Phase]);*Started=FPlatformTime::Seconds();return false;
    }));return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPreviewPrecisionTest,"Catalyst.Landform.Surfaces.LargeRadiusPreviewPrecision",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPreviewPrecisionTest::RunTest(const FString&)
{
    auto& App=FSlateApplication::Get();const auto W=SNew(SCLPlanetPreview);W->CreateReferenceMeshForTesting();
    const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Preview coordinate precision"))).ClientSize(FVector2D(800,600))[W];App.AddWindow(Window,false);Window->SlatePrepass();
    const auto V=W->GetSceneViewport();V->UpdateViewportRHI(false,800,600,EWindowMode::Windowed,PF_Unknown);FlushRenderingCommands();
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/preview-precision");IFileManager::Get().MakeDirectory(*Folder,true);
    auto Buffers=MakeShared<TArray<FCLGlobeBuffers>>();auto Plans=MakeShared<TArray<TSharedPtr<const FCLCompiledPlanetRecipe>>>();
    FString Error;
    for(double Radius:{1000.,10000.,6371000.})
    {
        auto R=FCLPlanetCompiler::BasePlanet();R.SchemaVersion=17;R.RadiusMeters=Radius;R.BaseSurfaceNormalized=.75;R.SurfaceStack=FCLSurfaceStack::MakeDefault();
        const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);App.RequestDestroyWindow(Window);return false;}
        Plans->Add(P);Buffers->Add(FCLGlobeBuffers::Build(*P,64));
    }
    auto GoldCounts=MakeShared<TArray<int32>>();GoldCounts->Init(0,4);
    for(int32 Case=0;Case<4;++Case)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this,W,Plans,Buffers,Case]
        {
            const int32 Index=FMath::Max(0,Case-1);const auto P=(*Plans)[Index];auto B=(*Buffers)[Index];
            if(Case==0)
            {
                // Reproduce the old PMC GPU conversion, now deliberately injected
                // into the full-precision renderer as a before/after control.
                for(auto& UV:B.UV){UV.X=FFloat16(float(UV.X)).GetFloat();UV.Y=FFloat16(float(UV.Y)).GetFloat();}
                for(auto& UV:B.UV1)UV.X=FFloat16(float(UV.X)).GetFloat();
            }
            W->Client->Plan=P;auto* Material=CastChecked<UMaterialInstanceDynamic>(CLMakeSurfacePreviewMaterial(W->Mesh.Get(),*P->Surfaces,true));
            for(int32 I=0;I<8;++I)
            {
                Material->SetVectorParameterValue(FName(*FString::Printf(TEXT("CLTextureDetail%d"),I)),FLinearColor(270,0,0,0));
                Material->SetVectorParameterValue(FName(*FString::Printf(TEXT("CLTint%d"),I)),FLinearColor(.5,.5,.5,.95));
                Material->SetVectorParameterValue(FName(*FString::Printf(TEXT("CLDetail%d"),I)),FLinearColor(12,0,0,0));
            }
            W->Mesh->SetMaterial(0,Material);CLSetPreviewMesh(*W->Mesh.Get(),B.Vertices,B.Triangles,B.Normals,B.UV,B.UV1,B.UV2,B.UV3,B.Colors,B.Tangents);
            W->SetGuides({},false);W->SetOceanVisible(false);
            const FVector Eye=FVector(1,-1,.6).GetSafeNormal()*165000;W->Client->SetViewLocation(Eye);W->Client->SetViewRotation((-Eye).Rotation());W->Client->StudioScene->AimLights(W->Client->GetViewRotation());
            if(Case>0)
            {
                const auto* UV=W->Mesh->GetMesh()->Attributes()->GetUVLayer(0);double Worst=0;
                for(int32 I=0;I<B.UV.Num();++I){const auto Actual=UV->GetElement(I);Worst=FMath::Max(Worst,FVector2D::Distance(FVector2D(Actual),B.UV[I]));TestTrue(TEXT("Large source attributes remain finite"),FMath::IsFinite(Actual.X)&&FMath::IsFinite(Actual.Y));}
                TestTrue(TEXT("Source channels retain float32 precision"),Worst<P->RadiusMeters*100*1.e-7);
                AddInfo(FString::Printf(TEXT("Radius %.0f m: maximum UV0 upload rounding %.6g cm"),P->RadiusMeters,Worst));
            }
        },.1f));
        ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this,W,V,Window,Folder,GoldCounts,Case]
        {
            if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();W->Scene->UpdateCaptureContents();
            auto Render=[&](TArray<FColor>& Pixels){for(int32 K=0;K<4;++K){W->Scene->GetWorld()->SendAllEndOfFrameUpdates();V->Invalidate();V->Draw(false);FlushRenderingCommands();V->ReadPixels(Pixels);}};
            TArray<FColor> WithGrid,WithoutGrid;Render(WithGrid);TestEqual(TEXT("Precision proof renders GPU pixels"),WithGrid.Num(),800*600);
            auto* Material=CastChecked<UMaterialInstanceDynamic>(W->Mesh->GetMaterial(0));Material->SetVectorParameterValue(TEXT("CLGrid"),FLinearColor(0,10,0,0));Render(WithoutGrid);
            int32 Gold=0;for(int32 I=0;I<WithGrid.Num()&&I<WithoutGrid.Num();++I)
            {const auto C=WithGrid[I],B=WithoutGrid[I];Gold+=int32(C.B)<int32(B.B)-10&&int32(C.R)>int32(C.B)+35&&int32(C.G)>int32(C.B)+20;}
            (*GoldCounts)[Case]=Gold;
            TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(800,600,WithGrid,PNG);FFileHelper::SaveArrayToFile(PNG,*(Folder/FString::Printf(TEXT("globe-%d.png"),Case)));
            AddInfo(FString::Printf(TEXT("Precision case %d: gold overlay pixels %d"),Case,Gold));
            if(Case>0)TestTrue(TEXT("Only a narrow gold equator is rendered"),Gold>100&&Gold<800*600*.03);
            if(Case==3)
            {
                TestTrue(TEXT("Old half-precision control reproduces giant yellow patches"),(*GoldCounts)[0]>(*GoldCounts)[1]*4);
                FSlateApplication::Get().RequestDestroyWindow(Window);
            }
        },.15f));
    }
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTectonicRenderedPortfolioTest,"Catalyst.Landform.TectonicCharacter.RenderedPortfolio",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLTectonicRenderedPortfolioTest::RunTest(const FString&)
{
    FString Json,Error;TSharedPtr<FJsonObject> Reference;
    const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"));
    if(!FFileHelper::LoadFileToString(Json,*(Plugin->GetBaseDir()/TEXT("conformance/tectonic-character-v5-reference.json")))||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Reference))
    {AddError(TEXT("Missing pre-change tectonic character reference"));return false;}
    auto& App=FSlateApplication::Get();const auto W=SNew(SCLPlanetPreview);W->CreateReferenceMeshForTesting();
    const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Tectonic character portfolio"))).ClientSize(FVector2D(800,600))[W];App.AddWindow(Window,false);Window->SlatePrepass();
    const auto V=W->GetSceneViewport();V->UpdateViewportRHI(false,800,600,EWindowMode::Windowed,PF_Unknown);FlushRenderingCommands();
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/tectonic-character/frames-final");IFileManager::Get().MakeDirectory(*Folder,true);
    auto Plans=MakeShared<TArray<TSharedPtr<const FCLCompiledPlanetRecipe>>>();auto Buffers=MakeShared<TArray<FCLGlobeBuffers>>();
    for(int32 Group=0;Group<4;++Group)for(const auto& Value:Reference->GetArrayField(TEXT("cases")))
    {
        const auto Case=Value->AsObject();FString Input;
        FJsonSerializer::Serialize(Case->GetObjectField(TEXT("recipe")).ToSharedRef(),TJsonWriterFactory<>::Create(&Input));
        TStrongObjectPtr<UCLPlanetRecipeAsset> Source(NewObject<UCLPlanetRecipeAsset>());
        if(!Source->ImportRecipeJson(Input,Error)){AddError(Error);App.RequestDestroyWindow(Window);return false;}
        auto R=Source->Recipe;
        if(Group>0)R.Tectonics.ApplyProgression(Case->GetNumberField(TEXT("position")));
        if(Group==2){R.Continents.bEnabled=true;R.Continents.ApplyProgression(.55);}
        if(Group==3)R.Tectonics.Seed+=817;
        if(Group==1&&FMath::IsNearlyEqual(Case->GetNumberField(TEXT("position")),.45))
        {
            R.Name=TEXT("Tectonic character");R.RadialGuides.Id=FGuid(0x434C5443,0,0,1);R.RadialGuides.Name=TEXT("Character review guides");Source->Recipe=R;FString Review;
            if(!Source->ExportJsonString(Review,Error)){AddError(Error);App.RequestDestroyWindow(Window);return false;}
            FFileHelper::SaveStringToFile(Review,*(FPaths::ProjectSavedDir()/TEXT("Verification/tectonic-character/review.json")));
        }
        const auto P=FCLPlanetCompiler::Compile(R,Error);
        if(!P||!P->Surfaces){AddError(Error);App.RequestDestroyWindow(Window);return false;}
        const int32 Resolution=Group==1&&Case->GetNumberField(TEXT("position"))>=.75?384:128;
        Plans->Add(P);Buffers->Add(FCLGlobeBuffers::Build(*P,Resolution));
    }
    auto Previous=MakeShared<TArray<FColor>>();auto Differences=MakeShared<FString>(TEXT("group,step,angle,mean_pixel_difference\n"));
    for(int32 I=0;I<Plans->Num();++I)for(int32 Angle=0;Angle<2;++Angle)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([W,Plans,Buffers,I,Angle]
        {
            const auto& P=(*Plans)[I];const auto& B=(*Buffers)[I];W->Client->Plan=P;
            auto* Material=CastChecked<UMaterialInstanceDynamic>(CLMakeSurfacePreviewMaterial(W->Mesh.Get(),*P->Surfaces,true));
            Material->SetVectorParameterValue(TEXT("CLGrid"),FLinearColor(0,10,0,0));W->Mesh->SetMaterial(0,Material);
            CLSetPreviewMesh(*W->Mesh.Get(),B.Vertices,B.Triangles,B.Normals,B.UV,B.UV1,B.UV2,B.UV3,B.Colors,B.Tangents);
            W->SetGuides({},false);W->SetOceanVisible(false);
            const FVector Eye=(Angle==0?FVector(1,-1,.65):FVector(-1,.4,.35)).GetSafeNormal()*220000;
            W->Client->SetViewLocation(Eye);W->Client->SetViewRotation((-Eye).Rotation());W->Client->StudioScene->AimLights(W->Client->GetViewRotation());
        },.025f));
        ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this,W,Window,V,Folder,Plans,I,Angle,Previous,Differences]
        {
            if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();W->Scene->UpdateCaptureContents();TArray<FColor> Pixels;
            for(int32 K=0;K<4;++K){W->Scene->GetWorld()->SendAllEndOfFrameUpdates();V->Invalidate();V->Draw(false);FlushRenderingCommands();V->ReadPixels(Pixels);}
            TestEqual(TEXT("Portfolio frame renders fully"),Pixels.Num(),800*600);
            const int32 Group=I/8,Step=I%8;
            if(Angle==0)
            {
                double Difference=0;
                if(Step>0&&Previous->Num()==Pixels.Num())for(int32 K=0;K<Pixels.Num();++K)
                    Difference+=FMath::Abs(int32(Pixels[K].R)-(*Previous)[K].R)+FMath::Abs(int32(Pixels[K].G)-(*Previous)[K].G)+FMath::Abs(int32(Pixels[K].B)-(*Previous)[K].B);
                Difference/=800*600*3;
                *Differences+=FString::Printf(TEXT("%d,%d,%d,%.6f\n"),Group,Step,Angle,Difference);
                if(Group>0&&Step>0)TestTrue(TEXT("Every reviewed new-profile interval visibly changes the rendered planet"),Difference>.2);
                *Previous=Pixels;
            }
            const TCHAR* Names[]={TEXT("v5"),TEXT("v6"),TEXT("v6-continents"),TEXT("v6-variation")};
            TArray<uint8> PNG;FImageUtils::CompressImageArray(800,600,Pixels,PNG);
            FFileHelper::SaveArrayToFile(PNG,*(Folder/FString::Printf(TEXT("%s-%d-%d.png"),Names[Group],Step,Angle)));
            if(Group==1&&Step==7&&Angle==0)
            {
                auto* Original=W->Mesh->GetMaterial(0);
                auto* Clay=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
                if(TestNotNull(TEXT("Neutral material is available for geometric review"),Clay))
                {
                    W->Mesh->SetMaterial(0,Clay);TArray<FColor> Neutral;
                    for(int32 K=0;K<4;++K){W->Scene->GetWorld()->SendAllEndOfFrameUpdates();V->Invalidate();V->Draw(false);FlushRenderingCommands();V->ReadPixels(Neutral);}
                    FImageUtils::CompressImageArray(800,600,Neutral,PNG);FFileHelper::SaveArrayToFile(PNG,*(Folder/TEXT("v6-100-neutral.png")));
                    W->Mesh->SetMaterial(0,Original);
                }
            }
            if(I==Plans->Num()-1&&Angle==1)
            {
                FFileHelper::SaveStringToFile(*Differences,*(Folder/TEXT("differences.csv")));
                FSlateApplication::Get().RequestDestroyWindow(Window);
            }
        },.10f));
    }
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLContinentalRenderedPortfolioTest,"Catalyst.Landform.ContinentalCharacter.NativeRenderedPortfolio",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLContinentalRenderedPortfolioTest::RunTest(const FString&)
{
    FString Json,Error;TSharedPtr<FJsonObject> Reference;const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"));
    if(!FFileHelper::LoadFileToString(Json,*(Plugin->GetBaseDir()/TEXT("conformance/continental-character-v2-reference.json")))||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Reference)){AddError(TEXT("Missing pre-change continental reference"));return false;}
    const auto W=SNew(SCLPlanetPreview);const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Native continental character review"))).ClientSize(FVector2D(800,600))[W];FSlateApplication::Get().AddWindow(Window,false);Window->SlatePrepass();
    const auto V=W->GetSceneViewport();V->UpdateViewportRHI(false,800,600,EWindowMode::Windowed,PF_Unknown);FlushRenderingCommands();
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/continental-character/frames");IFileManager::Get().MakeDirectory(*Folder,true);
    auto Plans=MakeShared<TArray<TSharedPtr<const FCLCompiledPlanetRecipe>>>();
    for(int32 Group=0;Group<5;++Group)for(const auto& Value:Reference->GetArrayField(TEXT("cases")))
    {
        const auto Case=Value->AsObject();FString Input;FJsonSerializer::Serialize(Case->GetObjectField(TEXT("recipe")).ToSharedRef(),TJsonWriterFactory<>::Create(&Input));
        TStrongObjectPtr<UCLPlanetRecipeAsset> Source(NewObject<UCLPlanetRecipeAsset>());if(!Source->ImportRecipeJson(Input,Error)){AddError(Error);FSlateApplication::Get().RequestDestroyWindow(Window);return false;}
        auto R=Source->Recipe;
        if(Group>0)
        {
            R.Continents.ApplyProgression(Case->GetNumberField(TEXT("position")));R.Tectonics.bEnabled=Group!=1;R.Tectonics.ApplyProgression(Group==3?.85:.45);
            if(Group>=3)R.Seed=Group==3?817:529;
        }
        if(Group==2&&FMath::IsNearlyEqual(Case->GetNumberField(TEXT("position")),.60))
        {
            R.Name=TEXT("Continental character");R.RadialGuides.Id=FGuid(0x434C4343,0,0,1);R.RadialGuides.Name=TEXT("Continental character guides");Source->Recipe=R;FString Review;
            if(!Source->ExportJsonString(Review,Error)){AddError(Error);FSlateApplication::Get().RequestDestroyWindow(Window);return false;}
            FFileHelper::SaveStringToFile(Review,*(FPaths::ProjectSavedDir()/TEXT("Verification/continental-character/review.json")));
        }
        const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);FSlateApplication::Get().RequestDestroyWindow(Window);return false;}Plans->Add(P);
    }
    auto Index=MakeShared<int32>(0);auto Phase=MakeShared<int32>(0);auto Started=MakeShared<double>(FPlatformTime::Seconds());auto Owner=MakeShared<uint32>(0);auto Runtime=MakeShared<const void*>(nullptr);
    auto Previous=MakeShared<TArray<FColor>>();auto CSV=MakeShared<FString>(TEXT("group,step,angle,mean_pixel_difference,native_vertices\n"));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FCLGlobeAwait>([this,W,Window,V,Folder,Plans,Index,Phase,Started,Owner,Runtime,Previous,CSV]
    {
        auto Close=[&]{W->Cancel();FSlateApplication::Get().RequestDestroyWindow(Window);return true;};
        if(*Index>=Plans->Num()){FFileHelper::SaveStringToFile(*CSV,*(Folder/TEXT("differences.csv")));return Close();}
        if(*Phase==0){W->Rebuild((*Plans)[*Index]);W->SetGuides((*Plans)[*Index]->SourceRecipe->RadialGuides,false);W->SetOceanVisible(true);*Started=FPlatformTime::Seconds();*Phase=1;}
        W->Pump();
        if(FPlatformTime::Seconds()-*Started>90){AddError(FString::Printf(TEXT("Native continental review timed out at %d: %s"),*Index,*W->NativePreviewStatus()));return Close();}
        if(W->IsUpdating())return false;
        if(!TestTrue(TEXT("Actual native VPP mesh and accepted plan are published"),W->NativePreview&&W->NativePreview->UsesNativePresentation()&&W->NativePreview->PublishedPlan()==(*Plans)[*Index]))return Close();
        TestFalse(TEXT("Portfolio constructs no application terrain mesh"),W->Mesh.IsValid());
        TestTrue(TEXT("Coast review retains the actual OceanHeight reference"),W->Ocean&&W->Ocean->IsVisible());
        if(*Index==0){*Owner=W->NativePreview->WorldIdentity();*Runtime=W->NativePreview->RuntimeIdentity();}
        TestTrue(TEXT("Knob and seed edits retain the native world/runtime"),*Owner==W->NativePreview->WorldIdentity()&&*Runtime==W->NativePreview->RuntimeIdentity());
        const int32 Group=*Index/8,Step=*Index%8;const TCHAR* Names[]={TEXT("v2"),TEXT("v3"),TEXT("v3-tectonic"),TEXT("v3-active-variation"),TEXT("v3-variation")};
        for(int32 Angle=0;Angle<2;++Angle)
        {
            const FVector Eye=(Angle==0?FVector(1,-1,.65):FVector(-1,.4,.35)).GetSafeNormal()*220000;W->Client->SetViewLocation(Eye);W->Client->SetViewRotation((-Eye).Rotation());W->Client->StudioScene->AimLights(W->Client->GetViewRotation());
            if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();W->Scene->UpdateCaptureContents();TArray<FColor> Pixels;
            for(int32 K=0;K<4;++K){W->Scene->GetWorld()->SendAllEndOfFrameUpdates();V->Invalidate();V->Draw(false);FlushRenderingCommands();V->ReadPixels(Pixels);}
            if(!TestEqual(TEXT("Native portfolio frame renders fully"),Pixels.Num(),800*600))return Close();
            double Difference=0;if(Angle==0)
            {
                if(Step>0&&Previous->Num()==Pixels.Num())for(int32 K=0;K<Pixels.Num();++K)Difference+=FMath::Abs(int32(Pixels[K].R)-(*Previous)[K].R)+FMath::Abs(int32(Pixels[K].G)-(*Previous)[K].G)+FMath::Abs(int32(Pixels[K].B)-(*Previous)[K].B);
                Difference/=800*600*3;if(Group>0&&Step>0)TestTrue(TEXT("Every reviewed interval visibly changes native output"),Difference>.15);*Previous=Pixels;
            }
            *CSV+=FString::Printf(TEXT("%d,%d,%d,%.8f,%d\n"),Group,Step,Angle,Difference,W->NativePreview->Published().Vertices.Num());
            TArray<uint8> PNG;FImageUtils::CompressImageArray(800,600,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Folder/FString::Printf(TEXT("%s-%d-%d.png"),Names[Group],Step,Angle)));
        }
        ++*Index;*Phase=0;return false;
    }));return true;
}
#endif

void SCLPlanetPreview::UpdateWatershedDebug()
{
    if(NativePreview&&PendingDebugPlan&&NativePreview->PublishedPlan()==PendingDebugPlan&&DisplayedDebugPlan!=PendingDebugPlan)
    {
        DisplayedDebugPlan=PendingDebugPlan;Client->DebugPlan=DisplayedDebugPlan;Client->WatershedDebug=PendingWatershedDebug;
        if(PendingWatershedDebug&&PendingWatershedDebug->Source==DisplayedDebugPlan->Watershed)
        {
            if(!DebugWater)
            {
                DebugWater.Reset(NewObject<UDynamicMeshComponent>(GetTransientPackage()));
                DebugWater->SetCollisionEnabled(ECollisionEnabled::NoCollision);DebugWater->SetCastShadow(false);DebugWater->bSelectable=false;
                DebugWater->SetMaterial(0,Ocean->GetMaterial(0));Scene->AddComponent(DebugWater.Get(),FTransform::Identity);
            }
            DebugWater->SetMesh(UE::Geometry::FDynamicMesh3(PendingWatershedDebug->Water));
            DebugWater->SetWorldScale3D(FVector(100000/DisplayedDebugPlan->RadiusMeters));
        }
        else Client->WatershedDebug.Reset();
        Client->Invalidate();
    }
    if(DebugWater)DebugWater->SetVisibility(Client->CanDrawWatershed());
}
void SCLPlanetPreview::UpdateCaveVolume()
{
    const auto P=NativePreview?NativePreview->PublishedPlan():nullptr;
    const bool Show=Client->bCaveOverlay&&P&&P==Client->Plan&&P->Caves&&P->Caves->Cutters;
    if(!Show)
    {
        if(CavePreview)CavePreview->SetVisible(false);
        return;
    }
    if(!CavePreview)CavePreview=MakeUnique<FCLNativePreview>(true);
    if(CaveInput!=P)
    {
        CavePreview->SetVisible(false);FString Error;
        if(CavePreview->Start(*Scene->GetWorld(),P,false,500,Error))CaveInput=P;
        else UE_LOG(LogTemp,Error,TEXT("Native cave inspection: %s"),*Error);
    }
    const bool Published=CavePreview->Poll(true);const bool Ready=CavePreview->PublishedPlan()==P;
    CavePreview->SetVisible(Ready);if(Published)Client->Invalidate();
}
void SCLPlanetPreview::SetCaveOverlay(bool Enabled)
{Client->bCaveOverlay=Enabled;UpdateCaveVolume();static_cast<FCLGlobeScene*>(Scene.Get())->SetCaveInspection(Enabled);Scene->UpdateCaptureContents();Client->Invalidate();}
bool SCLPlanetPreview::IsCaveOverlayEnabled()const{return Client->bCaveOverlay;}
void SCLPlanetPreview::SetWatershedOverlay(bool Enabled)
{Client->bWatershedOverlay=Enabled;UpdateWatershedDebug();UpdateOcean();Client->Invalidate();}
bool SCLPlanetPreview::IsWatershedOverlayEnabled()const{return Client->bWatershedOverlay;}

bool SCLPlanetPreview::FocusWatershed()
{
    if(!Client->Plan||!Client->Plan->Watershed||Client->Plan->Watershed->GetChannels().IsEmpty())return false;
    const auto& W=*Client->Plan->Watershed;const auto& Cells=W.GetCells();const FCLWatershedChannel* Best=&W.GetChannels()[0];
    for(const auto& C:W.GetChannels())if(Cells[C.From].AccumulatedAreaSquareMeters>Cells[Best->From].AccumulatedAreaSquareMeters)Best=&C;
    const FVector D=(Cells[Best->From].Direction+Cells[Best->To].Direction).GetSafeNormal();
    SetWalkLocation(D);Client->Navigation.Yaw=FMath::RadiansToDegrees(FMath::Atan2(D.Y,D.X));Client->Navigation.Elevation=FMath::Clamp(FMath::RadiansToDegrees(FMath::Asin(D.Z)),-85.,85.);Client->Navigation.Zoom=.55;Client->Navigation.Pan=FVector2D::ZeroVector;Client->Apply();return true;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "CLRecipeEvaluation.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLWatershedDebugRenderedTest,"Catalyst.Landform.Watershed.DebugRenderedNavigation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLWatershedDebugRenderedTest::RunTest(const FString&)
{
    FString Error;auto R=CLWatershedDebugRecipe(Error);if(!Error.IsEmpty()){AddError(Error);return false;}
    const auto W=SNew(SCLPlanetPreview);const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Watershed debug water and drainage"))).ClientSize(FVector2D(1000,800))[W];
    FSlateApplication::Get().AddWindow(Window,true);Window->SlatePrepass();
    struct FState
    {
        FCLRecipeEvaluation Evaluation;TSharedPtr<const FCLCompiledPlanetRecipe> Plan;TSharedPtr<const FCLWatershedDebugGeometry> Debug;
        int32 Phase=0,Frame=0,Busy=0;uint32 World=0;const void* Runtime=nullptr;FCLNativePreview::FMetrics Metrics;
        double Start=FPlatformTime::Seconds(),Stable=0,Last=0,MaxInputMs=0;TArray<double> Frames;TArray<FColor> Off;
    };
    const auto State=MakeShared<FState>();const auto Prepared=State->Evaluation.Prepare(R,Error);
    if(!Prepared){AddError(Error);FSlateApplication::Get().RequestDestroyWindow(Window);return false;}State->Evaluation.Request(R,Prepared);
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FCLGlobeAwait>([this,W,Window,State,R]
    {
        auto& S=*State;W->Pump();const double Now=FPlatformTime::Seconds();auto Close=[&]{S.Evaluation.Cancel();W->Cancel();FSlateApplication::Get().RequestDestroyWindow(Window);return true;};
        if(Now-S.Start>180){AddError(FString::Printf(TEXT("Debug view timed out at phase %d: %s"),S.Phase,*W->NativePreviewStatus()));return Close();}
        const FString Dir=FPaths::ProjectSavedDir()/TEXT("Verification/watershed-debug");IFileManager::Get().MakeDirectory(*Dir,true);
        auto Capture=[&](const TCHAR* Name,TArray<FColor>* Output=nullptr)
        {
            auto* V=W->Client->Viewport;if(!V)return;W->Scene->UpdateCaptureContents();TArray<FColor> Pixels;
            for(int32 K=0;K<3;++K){W->Scene->GetWorld()->SendAllEndOfFrameUpdates();V->Invalidate();V->Draw(false);FlushRenderingCommands();V->ReadPixels(Pixels);}
            TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(V->GetSizeXY().X,V->GetSizeXY().Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Dir/(FString(Name)+TEXT(".png"))));if(Output)*Output=Pixels;
        };
        if(S.Phase==0||S.Phase==6)
        {
            if(auto Result=S.Evaluation.Pump())
            {
                if(!Result->Plan||!Result->WatershedDebug){AddError(Result->Error);return Close();}
                TestTrue(TEXT("Water diagnostics build inside the existing background task"),Result->WorkerThread);
                if(S.Phase==6)TestTrue(TEXT("Old debug remains paired with the currently published terrain during authoring"),W->Client->DebugPlan==S.Plan&&W->Client->CanDrawWatershed());
                S.Plan=Result->Plan;S.Debug=Result->WatershedDebug;W->SetGuides(R.RadialGuides,false);W->Rebuild(S.Plan,S.Debug);W->SetWatershedOverlay(true);
                if(S.Phase==6)TestTrue(TEXT("Queued input cannot display the new debug over old terrain"),W->Client->DebugPlan!=S.Plan);
                S.Phase=S.Phase==0?1:7;S.Stable=0;
            }
            return false;
        }
        if(S.Phase==1||S.Phase==7)
        {
            if(W->IsUpdating()||!W->Client->CanDrawWatershed())return false;
            if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling()){GShaderCompilingManager->FinishAllCompilation();return false;}
            if(!S.Stable){S.Stable=Now;return false;}if(Now-S.Stable<.6)return false;
            TestTrue(TEXT("Water mesh is a visible collision-free UE debug component"),W->DebugWater&&W->DebugWater->IsVisible()&&W->DebugWater->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
            TestTrue(TEXT("Water presentation matches the native published terrain"),W->DisplayedDebugPlan==W->NativePreview->PublishedPlan()&&W->Client->WatershedDebug==S.Debug);
            if(S.Phase==7){Capture(TEXT("replacement"));return Close();}
            W->Client->Navigation.Yaw=0;W->Client->Navigation.Elevation=10;W->Client->Navigation.Zoom=.8;W->Client->Apply();
            W->SetWatershedOverlay(false);Capture(TEXT("off"),&S.Off);W->SetWatershedOverlay(true);S.Phase=2;return false;
        }
        if(S.Phase==2)
        {
            TArray<FColor> On;Capture(TEXT("ocean"),&On);double Difference=0;for(int32 I=0;I<FMath::Min(On.Num(),S.Off.Num());++I)Difference+=FMath::Abs(int32(On[I].B)-S.Off[I].B)+FMath::Abs(int32(On[I].R)-S.Off[I].R)+FMath::Abs(int32(On[I].G)-S.Off[I].G);
            Difference/=FMath::Max(1,On.Num()*3);TestTrue(TEXT("Transparent water and debug lines visibly change the same-camera native frame"),Difference>.3);
            const auto PathsOnly=MakeShared<FCLWatershedDebugGeometry>(*S.Debug);PathsOnly->Shores.Reset();W->Client->WatershedDebug=PathsOnly;W->DebugWater->SetVisibility(false);
            TArray<FColor> PathPixels;Capture(TEXT("paths-only"),&PathPixels);
            auto Cyan=[](const TArray<FColor>& Pixels){int32 Count=0;for(const auto& P:Pixels)Count+=P.R<180&&P.G>180&&P.B>180;return Count;};
            TestTrue(TEXT("Native spline paths themselves render with legible pixel widths"),Cyan(PathPixels)>Cyan(S.Off)+50);
            W->Client->WatershedDebug=S.Debug;W->UpdateWatershedDebug();
            W->SetOceanVisible(true);TestFalse(TEXT("Watershed debug suppresses the duplicate reference sphere"),W->Ocean->IsVisible());W->SetWatershedOverlay(false);TestTrue(TEXT("Turning debug off restores the requested ocean reference"),W->Ocean->IsVisible());W->SetWatershedOverlay(true);W->SetOceanVisible(false);
            W->Client->Navigation.Yaw=180;W->Client->Navigation.Elevation=0;W->Client->Navigation.Zoom=.65;W->Client->Apply();S.Phase=3;return false;
        }
        if(S.Phase==3){Capture(TEXT("lake"));W->Client->Navigation.Yaw=90;W->Client->Apply();S.Phase=4;return false;}
        if(S.Phase==4)
        {
            Capture(TEXT("raised-lake"));S.World=W->NativePreview->WorldIdentity();S.Runtime=W->NativePreview->RuntimeIdentity();S.Metrics=W->NativePreview->Metrics;S.Last=Now;UE_LOG(LogTemp,Display,TEXT("CL_WATERSHED_NAV_BEGIN"));S.Phase=5;return false;
        }
        if(S.Phase==5)
        {
            S.Frames.Add((Now-S.Last)*1000);S.Last=Now;const double Start=FPlatformTime::Seconds();W->Client->Navigation.Orbit(FVector2D(1.4,.15));W->Dolly(S.Frame%2?.02:-.02);S.MaxInputMs=FMath::Max(S.MaxInputMs,(FPlatformTime::Seconds()-Start)*1000);
            S.Busy+=W->NativePreview->IsUpdating();TestTrue(TEXT("Indicators remain visible while navigation drives native LOD"),W->Client->CanDrawWatershed()&&W->DebugWater->IsVisible());
            TestFalse(TEXT("Navigation creates no authoring-progress state"),W->IsApplyingInput());
            if(++S.Frame<180)return false;
            UE_LOG(LogTemp,Display,TEXT("CL_WATERSHED_NAV_END"));
            TestTrue(TEXT("Navigation exercised native LOD work"),S.Busy>0);TestTrue(TEXT("Spline and shore lines are emitted during navigation"),W->Client->LastWatershedSegments>0);
            TestTrue(TEXT("Navigation retains one native VPP owner"),W->NativePreview->WorldIdentity()==S.World&&W->NativePreview->RuntimeIdentity()==S.Runtime);
            TestEqual(TEXT("Navigation starts no terrain recipe input"),W->NativePreview->Metrics.Starts,S.Metrics.Starts);TestEqual(TEXT("Navigation copies no terrain inspection mesh"),W->NativePreview->Metrics.MeshCopies,S.Metrics.MeshCopies);
            S.Frames.RemoveAt(0);S.Frames.Sort();const double P95=S.Frames[FMath::Min(S.Frames.Num()-1,int32(S.Frames.Num()*.95))];TestTrue(TEXT("Debug navigation p95 remains below 50 ms in this bounded view"),P95<50);TestTrue(TEXT("Debug navigation input remains below 8 ms"),S.MaxInputMs<8);
            const FString JSON=FString::Printf(TEXT("{\"frames\":%d,\"nativeBusyFrames\":%d,\"p95Ms\":%.6f,\"maxInputMs\":%.6f,\"waterTriangles\":%d,\"shoreSegments\":%d,\"paths\":%d,\"buildSeconds\":%.6f}\n"),S.Frame,S.Busy,P95,S.MaxInputMs,S.Debug->Water.TriangleCount(),S.Debug->Shores.Num(),S.Debug->Paths.Num(),S.Debug->Seconds);FFileHelper::SaveStringToFile(JSON,*(Dir/TEXT("navigation.json")));AddInfo(JSON);Capture(TEXT("navigation"));
            auto Changed=R;Changed.Watershed.ApplyProgression(.85);FString Error;auto Prepared=S.Evaluation.Prepare(Changed,Error);if(!Prepared){AddError(Error);return Close();}S.Evaluation.Request(Changed,Prepared);S.Phase=6;return false;
        }
        return false;
    }));return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Testing/CLCaveIntersections.h"
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FCLCaveDebugRenderedTest,"Catalyst.Landform.Caves.PlanetWideNativeDebugAndNavigation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FCLCaveDebugRenderedTest::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{Names={TEXT("Reference2km"),TEXT("Saved500m100"),TEXT("Saved500m1"),TEXT("CarbonateKarst"),TEXT("MixedFamilies"),TEXT("IntersectingVolumes"),TEXT("MorphFissure"),TEXT("MorphKarst"),TEXT("MorphMixed")};Commands={TEXT("reference"),TEXT("small"),TEXT("small-low"),TEXT("karst"),TEXT("mixed"),TEXT("joined"),TEXT("morph-fissure"),TEXT("morph-karst"),TEXT("morph-mixed")};}
bool FCLCaveDebugRenderedTest::RunTest(const FString& Parameters)
{
    const bool Morph=Parameters.StartsWith(TEXT("morph-"));
    const bool Mixed=Parameters==TEXT("mixed"),Joined=Parameters==TEXT("joined"),Karst=Parameters==TEXT("karst"),Small=Parameters==TEXT("small")||Parameters==TEXT("small-low"),Low=Parameters==TEXT("small-low");
    FString J,E;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/(Morph?(Parameters==TEXT("morph-fissure")?TEXT("Examples/FissureMorphology.json"):Parameters==TEXT("morph-karst")?TEXT("Examples/KarstMorphology.json"):TEXT("Examples/CaveMorphology.json")):(Mixed||Joined)?TEXT("Examples/MixedCaves.json"):Karst?TEXT("Examples/KarstCaves.json"):Small?TEXT("Examples/CavesSmall.json"):TEXT("Examples/Caves.json"))));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());if(!A->ImportRecipeJson(J,E)){AddError(E);return false;}if(Low)A->Recipe.Caves.ApplyProgression(.01);
    const auto W=SNew(SCLPlanetPreview);const auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Planet-wide native cave surfaces"))).ClientSize(FVector2D(1000,800))[W];FSlateApplication::Get().AddWindow(Window,true);Window->SlatePrepass();
    struct FState {FCLRecipeEvaluation Evaluation;TSharedPtr<const FCLCompiledPlanetRecipe> Plan;int32 Phase=0,Frames=0,Networks=0,Vertices=0,Cyan=0;double Start=FPlatformTime::Seconds(),Last=0,OffLuma=0,DimLuma=0,Residual=0;TArray<double> Times;TArray<FColor> Off;uint32 Terrain=0;TArray<uint32> Inspection;TMap<FGuid,uint32> DomainOwners;uint64 Copies=0,Starts=0;FVector Eye;FRotator Rotation;};
    const auto State=MakeShared<FState>();const auto Prepared=State->Evaluation.Prepare(A->Recipe,E);if(!Prepared){AddError(E);return false;}State->Evaluation.Request(A->Recipe,Prepared);
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FCLGlobeAwait>([this,W,Window,State,Small,Low,Karst,Mixed,Joined,Morph]
    {
        auto& S=*State;W->Pump();const double Now=FPlatformTime::Seconds();auto Close=[&]{S.Evaluation.Cancel();W->Cancel();FSlateApplication::Get().RequestDestroyWindow(Window);return true;};
        if(Now-S.Start>300){AddError(W->NativePreviewStatus());if(W->CavePreview)AddError(W->CavePreview->Status());return Close();}
        const FString Dir=FPaths::ProjectSavedDir()/(Morph?(S.Plan&&S.Plan->Caves&&S.Plan->Caves->NetworkInfo.ContainsByPredicate([](const auto& N){return N.Family==FCLCompiledCaves::EFamily::CarbonateKarst;})?(S.Plan->Caves->NetworkInfo.ContainsByPredicate([](const auto& N){return N.Family==FCLCompiledCaves::EFamily::Structural;})?TEXT("Verification/morph-mixed"):TEXT("Verification/morph-karst")):TEXT("Verification/morph-fissure")):Joined?TEXT("Verification/cave-joined"):Mixed?TEXT("Verification/cave-mixed"):Karst?TEXT("Verification/cave-karst"):Low?TEXT("Verification/cave-small-low"):Small?TEXT("Verification/cave-small-render"):TEXT("Verification/cave-overview"));IFileManager::Get().MakeDirectory(*Dir,true);
        auto Capture=[&](const TCHAR* Name,TArray<FColor>& Pixels)
        {
            auto* V=W->Client->Viewport;if(!V)return;W->Scene->UpdateCaptureContents();
            for(int32 K=0;K<4;++K){W->Scene->GetWorld()->SendAllEndOfFrameUpdates();V->Invalidate();V->Draw(false);FlushRenderingCommands();V->ReadPixels(Pixels);}
            TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(V->GetSizeXY().X,V->GetSizeXY().Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Dir/(FString(Name)+TEXT(".png"))));
        };
        // At globe scale the cyan material is tone-mapped and blended with
        // terrain in subpixel coverage; it is not a pure RGB cyan swatch.
        auto Cyan=[](FColor C){return C.G>100&&C.B>100&&C.R<C.G*.9;};
        auto CameraUnchanged=[&]{TestTrue(TEXT("Cave display leaves camera position unchanged"),W->Client->GetViewLocation().Equals(S.Eye,1.e-7));TestTrue(TEXT("Cave display leaves camera rotation unchanged"),W->Client->GetViewRotation().Equals(S.Rotation,1.e-7));};
        auto CheckAllGeometry=[&]
        {
            const auto& M=W->CavePreview->Published();S.Vertices=M.Vertices.Num();
            TSet<FGuid> Represented;TArray<FVector> Body;TArray<TPair<FGuid,FBox>> NetworkBounds;for(const auto& D:S.Plan->Caves->InspectionDomains)NetworkBounds.Emplace(D.Id,D.Bounds);
            for(const auto& P:M.SourcePoints)
            {
                const FVector B=P*.01;Body.Add(B);
                for(const auto& N:NetworkBounds)if(!Represented.Contains(N.Key)&&N.Value.IsInsideOrOn(B))Represented.Add(N.Key);
            }
            S.Networks=Represented.Num();TestEqual(TEXT("Every joined diagnostic domain has native union surface vertices at globe scale"),S.Networks,S.Plan->Caves->InspectionDomains.Num());TestTrue(TEXT("Native VPP presents actual cave surface triangles at globe scale"),M.Vertices.Num()>100&&M.Triangles.Num()>300&&W->CavePreview->UsesNativePresentation());
            TArray<FCLSceneShapeSample> Fields;FString Error;FCLScene::FromPlanet(S.Plan,Error)->SampleCaveVoid({Body},Fields,Error);S.Residual=0;FVector WorstPoint;for(int32 I=0;I<Fields.Num();++I)if(FMath::Abs(Fields[I].FieldMeters)>S.Residual){S.Residual=FMath::Abs(Fields[I].FieldMeters);WorstPoint=Body[I];}if(S.Residual>=.5)AddInfo(FString::Printf(TEXT("Cave mesh worst residual %.6f m at %s"),S.Residual,*WorstPoint.ToString()));TestTrue(TEXT("Native vertices follow the real cave field"),S.Residual<.5);
        };
        if(S.Phase==0)
        {
            if(auto Done=S.Evaluation.Pump()){if(!Done->Plan||!Done->Plan->Caves||!Done->Plan->Caves->Cutters){AddError(Done->Error);return Close();}TestTrue(TEXT("Caves use the existing background worker"),Done->WorkerThread);S.Plan=Done->Plan;if(Joined){FString Error;S.Plan=CLTesting::IntersectingCaves(S.Plan,Error);if(!S.Plan){AddError(Error);return Close();}}W->Rebuild(S.Plan);W->SetGuides({},false);W->SetRegionInspection(false);W->SetOceanVisible(false);S.Phase=1;}return false;
        }
        if(S.Phase==1)
        {
            if(W->IsUpdating())return false;
            // A globe view containing both hemispheres in projection. No network focus.
            const FVector D=(S.Plan->Caves->Segments[0].A.GetSafeNormal()+S.Plan->Caves->Segments.Last().A.GetSafeNormal()).GetSafeNormal();W->Client->Navigation.Yaw=D.Rotation().Yaw;W->Client->Navigation.Elevation=D.Rotation().Pitch;W->Client->Navigation.Zoom=.8;W->Client->Apply();S.Eye=W->Client->GetViewLocation();S.Rotation=W->Client->GetViewRotation();Capture(TEXT("surface"),S.Off);W->SetCaveOverlay(true);CameraUnchanged();S.Phase=2;return false;
        }
        if(S.Phase==2)
        {
            if(W->IsUpdating()||!W->CavePreview||W->CavePreview->IsUpdating()||W->CavePreview->PublishedPlan()!=S.Plan||(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling()))return false;
            CameraUnchanged();CheckAllGeometry();W->SetCaveOverlay(false);Capture(TEXT("surface"),S.Off);W->SetCaveOverlay(true);CameraUnchanged();TArray<FColor> Control,Dim;W->CavePreview->SetVisible(false);Capture(TEXT("all-caves-control"),Control);W->CavePreview->SetVisible(true);Capture(TEXT("all-caves"),Dim);int32 Samples=0;S.Cyan=0;
            for(int32 I=0;I<FMath::Min(S.Off.Num(),Dim.Num());++I){const auto A=S.Off[I],B=Dim[I];S.Cyan+=Cyan(B);if(A.R>110&&A.G>100&&A.B>70&&!Cyan(B)){S.OffLuma+=(A.R+A.G+A.B)/3.;S.DimLuma+=(B.R+B.G+B.B)/3.;++Samples;}}
            if(Samples){S.OffLuma/=Samples;S.DimLuma/=Samples;}TestTrue(TEXT("Surface dims at the unchanged planet-wide view"),Samples>1000&&S.DimLuma<S.OffLuma*.7);TestTrue(TEXT("Actual cave surfaces rasterize in the planet-wide view"),S.Cyan>10);
            // Verify visible native surface pixels in every network's projected box;
            // the test deliberately does not use the old front-hemisphere culling.
            TMap<FGuid,FBox2D> Boxes;const double Scale=100000/S.Plan->RadiusMeters;
            for(const auto& D:S.Plan->Caves->InspectionDomains)for(int32 K=0;K<8;++K){const auto& B=D.Bounds;const FVector P((K&1)?B.Max.X:B.Min.X,(K&2)?B.Max.Y:B.Min.Y,(K&4)?B.Max.Z:B.Min.Z);FVector2D Pixel;if(FSceneView::ProjectWorldToScreen(P*Scale,W->Client->ViewRect,W->Client->ViewProjection,Pixel)){if(!Boxes.Contains(D.Id))Boxes.Add(D.Id,FBox2D(ForceInit));Boxes[D.Id]+=Pixel;}}
            const auto Size=W->Client->Viewport->GetSizeXY();for(const auto& Pair:Boxes)
            {
                int32 Count=0;const auto Box=Pair.Value.ExpandBy(5);for(int32 Y=FMath::Max(0,FMath::FloorToInt(Box.Min.Y));Y<=FMath::Min(Size.Y-1,FMath::CeilToInt(Box.Max.Y));++Y)for(int32 X=FMath::Max(0,FMath::FloorToInt(Box.Min.X));X<=FMath::Min(Size.X-1,FMath::CeilToInt(Box.Max.X));++X){const int32 I=Y*Size.X+X;const auto A=Control[I],B=Dim[I];Count+=B.B>A.B+8&&B.G>A.G+5&&B.R<B.G*.85;}if(!Count)AddInfo(FString::Printf(TEXT("No matched native coverage for network %s"),*Pair.Key.ToString()));TestTrue(TEXT("Every projected union domain has visible native surface pixels against its hidden control"),Count>0);
            }
            TestEqual(TEXT("All native union domains project in this bounded view"),Boxes.Num(),S.Plan->Caves->InspectionDomains.Num());

            W->SetCaveOverlay(false);CameraUnchanged();TArray<FColor> Restored;Capture(TEXT("restored"),Restored);double Difference=0;for(int32 I=0;I<FMath::Min(S.Off.Num(),Restored.Num());++I)Difference+=FMath::Abs(int(S.Off[I].R)-int(Restored[I].R))+FMath::Abs(int(S.Off[I].G)-int(Restored[I].G))+FMath::Abs(int(S.Off[I].B)-int(Restored[I].B));TestTrue(TEXT("Off restores ordinary native surface"),Difference/(3*Restored.Num())<5);
            W->SetCaveOverlay(true);CameraUnchanged();S.Terrain=W->NativePreview->WorldIdentity();S.Inspection=W->CavePreview->CaveWorldIdentities();S.DomainOwners=W->CavePreview->CaveDomainIdentities();TestEqual(TEXT("One native presenter for each overlapping support group"),S.Inspection.Num(),S.Plan->Caves->InspectionDomains.Num());S.Copies=W->CavePreview->Metrics.MeshCopies;S.Starts=W->CavePreview->Metrics.Starts;S.Last=Now;UE_LOG(LogTemp,Display,TEXT("CL_CAVE_NAV_BEGIN"));S.Phase=3;return false;
        }
        if(S.Phase==3)
        {
            if(S.Frames>=30)S.Times.Add((Now-S.Last)*1000);S.Last=Now;W->OrbitForTesting({.3,.08});W->Dolly(S.Frames%2?.008:-.008);if(auto* V=W->Client->Viewport){V->Invalidate();V->Draw(false);}TestTrue(TEXT("All-cave presentation survives navigation"),W->CavePreview->UsesNativePresentation());TestFalse(TEXT("Navigation shows no input progress"),W->IsApplyingInput());if(++S.Frames<120)return false;UE_LOG(LogTemp,Display,TEXT("CL_CAVE_NAV_END"));S.Phase=8;return false;
        }
        if(S.Phase==8)
        {
            // Native presentation remains visible during camera updates. The
            // explicit test-only mesh readback waits for a settled native state.
            if(W->CavePreview->IsUpdating())return false;
            S.Times.Sort();const double P95=S.Times[FMath::Min(S.Times.Num()-1,int32(S.Times.Num()*.95))];TestTrue(TEXT("Planet-wide navigation p95 is below 50 ms"),P95<50);TestEqual(TEXT("Navigation copies no cave mesh"),W->CavePreview->Metrics.MeshCopies,S.Copies);TestEqual(TEXT("Navigation starts no cave input"),W->CavePreview->Metrics.Starts,S.Starts);CheckAllGeometry();TArray<FColor> Pixels;Capture(TEXT("navigation"),Pixels);
            // Rotate the same globe to put the first network on the back hemisphere.
            const FVector Back=-S.Plan->Caves->Segments[0].A.GetSafeNormal();W->Client->Navigation.Yaw=Back.Rotation().Yaw;W->Client->Navigation.Elevation=Back.Rotation().Pitch;W->Client->Apply();
            TArray<FColor> BackControl;W->CavePreview->SetVisible(false);Capture(TEXT("back-hemisphere-control"),BackControl);W->CavePreview->SetVisible(true);Capture(TEXT("back-hemisphere"),Pixels);
            const auto Id=S.Plan->Caves->Segments[0].Network;FBox2D Box(ForceInit);const double Scale=100000/S.Plan->RadiusMeters;for(const auto& Segment:S.Plan->Caves->Segments)if(Segment.Network==Id)for(const auto& P:{Segment.A,Segment.B}){FVector2D Pixel;if(FSceneView::ProjectWorldToScreen(P*Scale,W->Client->ViewRect,W->Client->ViewProjection,Pixel))Box+=Pixel;}
            const auto Size=W->Client->Viewport->GetSizeXY();Box=Box.ExpandBy(5);int32 Count=0;for(int32 Y=FMath::Max(0,FMath::FloorToInt(Box.Min.Y));Y<=FMath::Min(Size.Y-1,FMath::CeilToInt(Box.Max.Y));++Y)for(int32 X=FMath::Max(0,FMath::FloorToInt(Box.Min.X));X<=FMath::Min(Size.X-1,FMath::CeilToInt(Box.Max.X));++X){const int32 I=Y*Size.X+X;const auto A=BackControl[I],B=Pixels[I];Count+=B.B>A.B+8&&B.G>A.G+5&&B.R<B.G*.85;}
            // Compare against an identical dimmed view with only native cave
            // components hidden. This distinguishes real subpixel cave coverage
            // from guide lines and does not assume a fixed shaded brightness.
            TestTrue(TEXT("A network on the far hemisphere still has actual visible surface pixels"),Count>0);
            const FString Metrics=FString::Printf(TEXT("{\"frames\":90,\"warmupFrames\":30,\"p95Ms\":%.6f,\"normalSurfaceLuma\":%.6f,\"debugSurfaceLuma\":%.6f,\"cyanPixels\":%d,\"networks\":%d,\"nativeDomains\":%d,\"nativeVertices\":%d,\"maxFieldResidualMeters\":%.6f,\"cameraMovedByToggle\":false,\"inputStartsDuringNavigation\":0,\"meshCopiesDuringNavigation\":0}\n"),P95,S.OffLuma,S.DimLuma,S.Cyan,S.Plan->Caves->Networks,S.Networks,S.Vertices,S.Residual);FFileHelper::SaveStringToFile(Metrics,*(Dir/TEXT("native-review.json")));AddInfo(Metrics);
            if(Morph)
            {
                const FVector OverviewEye=W->Client->GetViewLocation();const FRotator OverviewRotation=W->Client->GetViewRotation();
                const auto& Room=S.Plan->Caves->Chambers[0];const FVector Center=Room.Center,Up=Room.Rotation.GetAxisZ(),Along=Room.Rotation.GetAxisX();
                for(int32 View=0;View<2;++View)
                {
                    const FVector Eye=Center+Up*28+(View?Room.Rotation.GetAxisY():Along)*18;
                    W->Client->SetViewLocation(Eye*Scale);W->Client->SetViewRotation((Center-Eye).Rotation());TArray<FColor> Detail;Capture(View?TEXT("room-side-physical-scale"):TEXT("room-physical-scale"),Detail);
                }
                W->Client->SetViewLocation(OverviewEye);W->Client->SetViewRotation(OverviewRotation);
            }
            if(Joined)
            {
                // Test-only junction anatomy capture, with no product focus UI.
                const FVector Center=(S.Plan->Caves->Passages[0].Points[0]+S.Plan->Caves->Passages[0].Points[1])*.5;
                const FVector Up=Center.GetSafeNormal(),Eye=Center+Up*90;
                W->Client->SetViewLocation(Eye*(100000/S.Plan->RadiusMeters));W->Client->SetViewRotation((Center-Eye).Rotation());TArray<FColor> Detail;Capture(TEXT("joined-detail"),Detail);
                return Close();
            }
            S.Eye=W->Client->GetViewLocation();S.Rotation=W->Client->GetViewRotation();auto Changed=*S.Plan->SourceRecipe;Changed.Caves.bSurfaceEntrances=false;FString Error;const auto Prepared=S.Evaluation.Prepare(Changed,Error);if(!Prepared){AddError(Error);return Close();}S.Evaluation.Request(Changed,Prepared);S.Phase=4;return false;
        }
        if(S.Phase==4){if(auto Done=S.Evaluation.Pump()){if(!Done->Plan){AddError(Done->Error);return Close();}S.Plan=Done->Plan;W->Rebuild(S.Plan);S.Phase=5;}return false;}
        if(S.Phase==5)
        {
            if(W->IsUpdating()||W->CavePreview->IsUpdating()||W->CavePreview->PublishedPlan()!=S.Plan)return false;CameraUnchanged();CheckAllGeometry();TestEqual(TEXT("Recipe edit retains native terrain owner"),W->NativePreview->WorldIdentity(),S.Terrain);const auto Owners=W->CavePreview->CaveDomainIdentities();TestEqual(TEXT("Edited domains each have a native presenter"),Owners.Num(),S.Plan->Caves->InspectionDomains.Num());for(const auto& Pair:Owners)if(const auto* Before=S.DomainOwners.Find(Pair.Key))TestEqual(TEXT("Recipe edit retains every unchanged domain owner"),Pair.Value,*Before);TestEqual(TEXT("One diagnostic input per accepted plan"),W->CavePreview->Metrics.Starts,uint64(2));TestEqual(TEXT("Edited recipe removes entrances"),S.Plan->Caves->Entrances,0);TArray<FColor> Pixels;Capture(TEXT("edited-network"),Pixels);
            auto Changed=*S.Plan->SourceRecipe;Changed.Caves.ApplyProgression(1.);FString Error;const auto Prepared=S.Evaluation.Prepare(Changed,Error);if(!Prepared){AddError(Error);return Close();}S.Evaluation.Request(Changed,Prepared);S.Phase=6;return false;
        }
        if(S.Phase==6){if(auto Done=S.Evaluation.Pump()){if(!Done->Plan){AddError(Done->Error);return Close();}S.Plan=Done->Plan;W->Rebuild(S.Plan);S.Phase=7;}return false;}
        if(S.Phase==7)
        {
            if(W->IsUpdating()||W->CavePreview->IsUpdating()||W->CavePreview->PublishedPlan()!=S.Plan||(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling()))return false;CameraUnchanged();CheckAllGeometry();TestEqual(TEXT("Maximum knob still presents every network"),W->CavePreview->CaveWorldIdentities().Num(),S.Plan->Caves->InspectionDomains.Num());TestEqual(TEXT("Maximum knob is one accepted diagnostic input"),W->CavePreview->Metrics.Starts,uint64(3));TArray<FColor> Pixels;Capture(TEXT("maximum-knob"),Pixels);
            const FString JSON=FString::Printf(TEXT("{\"character\":100,\"networks\":%d,\"nativeDomains\":%d,\"nativeVertices\":%d,\"maxFieldResidualMeters\":%.6f,\"cameraMovedByInput\":false}\n"),S.Plan->Caves->Networks,S.Networks,S.Vertices,S.Residual);FFileHelper::SaveStringToFile(JSON,*(Dir/TEXT("maximum-knob.json")));AddInfo(JSON);
            // Qualification views only: inspect the complete first native
            // network from two angles, then restore the unchanged user view.
            const FBox NetworkBounds=S.Plan->Caves->Cutters->Bounds(S.Plan->Caves->Nodes[0].Network);
            const FVector Center=NetworkBounds.GetCenter(),Up=Center.GetSafeNormal();const double Distance=NetworkBounds.GetExtent().Length()*2.8;
            for(int32 View=0;View<2;++View)
            {
                const FVector Eye=Center+(View==0?Up:(Up+FVector::CrossProduct(Up,FVector::ZAxisVector).GetSafeNormal()).GetSafeNormal())*Distance;
                W->Client->SetViewLocation(Eye*(100000/S.Plan->RadiusMeters));W->Client->SetViewRotation((Center-Eye).Rotation());TArray<FColor> Detail;Capture(View==0?TEXT("network-detail"):TEXT("network-side"),Detail);
            }
            W->Client->SetViewLocation(S.Eye);W->Client->SetViewRotation(S.Rotation);

            return Close();
        }
        return Close();
    }));return true;
}
#endif

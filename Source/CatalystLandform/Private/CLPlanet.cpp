#include "CLPlanet.h"
#include "Misc/SecureHash.h"
#include "Scene/CLGroundSurface.h"
#include "Surface/CLNativeMaterialCoordinates.h"
#include "Surface/CLSurfaceLibrary.h"
#include "MegaMaterial/VoxelMegaMaterial.h"
#include "Components/ArrowComponent.h"
#include "Components/SplineComponent.h"
#include "Containers/Ticker.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Engine/Texture2D.h"
#include "Heightmap/VoxelHeightmap.h"
#include "Heightmap/VoxelHeightmap_Height.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "VoxelLayer.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
#include "VoxelStampComponent.h"
#include "Scene/CLScene.h"

bool UCLHeightmapLibrary::ConfigureTextureSource(UVoxelHeightmap* Heightmap, UTexture2D* Texture,
    float ScaleXY, float ScaleZ, float OffsetZ, bool bUseBicubic)
{
#if WITH_EDITOR
    if (!IsInGameThread() || !Heightmap || !Heightmap->Height || !Texture || !FMath::IsFinite(ScaleXY) || ScaleXY <= 0 ||
        !FMath::IsFinite(ScaleZ) || !FMath::IsFinite(OffsetZ)) return false;
    Heightmap->Modify();
    Heightmap->Height->Modify();
    Heightmap->ScaleXY = ScaleXY;
    Heightmap->Height->Texture = Texture;
    Heightmap->Height->TextureChannel = EVoxelTextureChannel::R;
    Heightmap->Height->bCompressTo16Bits = true;
    Heightmap->Height->bUseBicubic = bUseBicubic;
    Heightmap->Height->ScaleZ = ScaleZ;
    Heightmap->Height->OffsetZ = OffsetZ;
    Heightmap->Height->OnReimport();
    Heightmap->OnChanged_EditorOnly.Broadcast();
    Heightmap->MarkPackageDirty();
    return Heightmap->Height->GetData().IsValid();
#else
    return false;
#endif
}

FIntPoint UCLHeightmapLibrary::GetSourceResolution(UVoxelHeightmap* Heightmap)
{
    if (!Heightmap || !Heightmap->Height) return FIntPoint::ZeroValue;
    const auto Data = Heightmap->Height->GetData();
    return Data ? FIntPoint(Data->SizeX, Data->SizeY) : FIntPoint::ZeroValue;
}

ACLPlanet::ACLPlanet()
{
    TerrainStamp = CreateDefaultSubobject<UVoxelStampComponent>(TEXT("SphericalLandform"));
    TerrainStamp->SetupAttachment(RootComponent);
}

bool ACLPlanet::ApplyRecipeJson(const FString& Json, FString& Error)
{
    Error.Reset();
    if(!IsInGameThread()||!GetWorld()||GetWorld()->bIsTearingDown||IsTemplate()||bRebuilding)
    {Error=TEXT("Recipe application requires an available planet actor on the game thread");return false;}
    if(SamplingWindow.bEnabled&&!SamplingWindow.Validate(Error))return false;
    // Import owns the strict parser and complete compiler preflight. Do not change
    // the live actor, its retained recipe, or its stamp on a rejected document.
    auto Candidate=TStrongObjectPtr<UCLPlanetRecipeAsset>(NewObject<UCLPlanetRecipeAsset>(this,NAME_None,RF_Transient));
    TSharedPtr<const FCLCompiledPlanetRecipe> Plan;
    if(!Candidate->ImportRecipeJsonPrepared(Json,Error,Plan))return false;
    if(SamplingWindow.bEnabled)
    {
        if(!Plan||!FCLScene::FromPlanet(Plan,Error))return false;
        for(TActorIterator<ACLSphericalHeightmapStamp> It(GetWorld());It;++It)
            if(It->Planet==this&&!It->IsActorBeingDestroyed())
            {Error=TEXT("A native scene window requires recipe-owned geometry, without authored stamp actors");return false;}
    }
    UVoxelMegaMaterial* Material=MegaMaterial;
    if(Candidate->Recipe.SurfaceStack.IsActive())
    {
        if(!Plan)return false;
        Material=Plan->MeasuredTerrain&&Plan->MeasuredTerrain->GetMaterial()?Plan->MeasuredTerrain->GetMaterial():GetDefault<UCLSurfaceSettings>()->PlanetMaterial.LoadSynchronous();
        if(!Material){Error=TEXT("Configured client planet material is missing or was not cooked");return false;}
    }
    RecipeAsset=Candidate.Get();MegaMaterial=Material;bRuntimeRecipeApplied=true;
    TGuardValue<TSharedPtr<const FCLCompiledPlanetRecipe>> Applying(ApplyingRecipePlan,Plan);RebuildTerrain();return true;
}

void ACLPlanet::RebuildTerrain()
{
    if (bRebuilding || !GetWorld() || GetWorld()->bIsTearingDown) return;
    TGuardValue<bool> Guard(bRebuilding, true);
#if WITH_EDITOR
    if (BoundRecipe.Get() != RecipeAsset)
    {
        if (BoundRecipe.IsValid()) BoundRecipe->OnRecipeChanged.Remove(RecipeChangedHandle);
        BoundRecipe = RecipeAsset;
        if (RecipeAsset) RecipeChangedHandle = RecipeAsset->OnRecipeChanged.AddUObject(this, &ACLPlanet::RebuildTerrain);
    }
#endif
    FCLPlanetStamp Stamp;
    Stamp.Window=SamplingWindow;
    if (RecipeAsset)
    {
        if(!RecipeAsset->SynchronizeRadialGuides(RecipeStatus)){RecipeStatus=TEXT("Keeping last valid terrain: ")+RecipeStatus;return;}
        const bool SameInput=ApplyingRecipePlan&&ApplyingRecipePlan->SourceRecipe&&FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&RecipeAsset->Recipe,ApplyingRecipePlan->SourceRecipe.Get(),0);
        const auto Plan=SameInput?ApplyingRecipePlan:FCLPlanetCompiler::Compile(RecipeAsset->Recipe,RecipeStatus);
        if (!Plan) { RecipeStatus = TEXT("Keeping last valid terrain: ") + RecipeStatus; return; }
        RadiusMeters = Plan->RadiusMeters;
        if(Plan->MeasuredTerrain&&Plan->MeasuredTerrain->GetMaterial())MegaMaterial=Plan->MeasuredTerrain->GetMaterial();
        Stamp.bUseRecipe = true;
        Stamp.Recipe = RecipeAsset->Recipe;
        Stamp.CompiledRecipe = Plan;
        RecipeStatus = TEXT("Compiled recipe: ") + RecipeAsset->Recipe.Name;
    }
    else RecipeStatus = TEXT("No recipe: spherical base with authored stamps");
    Stamp.Radius = FMath::IsFinite(RadiusMeters) ? FMath::Max(10.0, RadiusMeters) * 100 : 10000;
    Stamp.Layer = TerrainLayer ? TerrainLayer.Get() : UVoxelVolumeLayer::Default();
    Stamp.Behavior = RecipeAsset && RecipeAsset->Recipe.SurfaceStack.IsActive() ? EVoxelStampBehavior::AffectAll : EVoxelStampBehavior::AffectShape;
    Stamp.Smoothness = 0;

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
    Stamp.BoundsExtensionMultiplier=.05;Stamp.MaximumBoundsExtension=TNumericLimits<float>::Max();
#else
    Stamp.BoundsExtension=.05;
#endif

    TArray<ACLSphericalHeightmapStamp*> Actors;
    for (TActorIterator<ACLSphericalHeightmapStamp> It(GetWorld()); It; ++It)
        if (It->Planet == this && !It->IsActorBeingDestroyed()) Actors.Add(*It);
    Actors.Sort([](const ACLSphericalHeightmapStamp& A, const ACLSphericalHeightmapStamp& B)
    {
        if (A.Priority != B.Priority) return A.Priority < B.Priority;
        return A.StampId.ToString() < B.StampId.ToString();
    });
    for (ACLSphericalHeightmapStamp* Actor : Actors)
    {
        Actor->SnapToSurface();
        FCLSphericalHeightmapPlacement Placement;
        if (Actor->BuildPlacement(Placement)) Stamp.Placements.Add(Placement);
    }
    const double DisplayScale=DisplayRadiusMeters>0?DisplayRadiusMeters/RadiusMeters:1.;
    if(!FMath::IsFinite(DisplayRadiusMeters)||DisplayRadiusMeters<0||!FMath::IsFinite(DisplayScale)||DisplayScale<=0||(DisplayRadiusMeters>0&&SamplingWindow.bEnabled))
    {RecipeStatus=TEXT("Display scaling requires a finite positive scale and canonical body coordinates");return;}
    if(DisplayRadiusMeters>0){TerrainStamp->SetRelativeScale3D(FVector(DisplayScale));bDisplayScaleApplied=true;}
    else if(bDisplayScaleApplied){TerrainStamp->SetRelativeScale3D(FVector::OneVector);bDisplayScaleApplied=false;}
    if(DisplayRadiusMeters>0)
    {
        const auto Transform=FCLNativeMaterialCoordinates::Transform(TerrainStamp->GetRelativeTransform());
        if(!FCLNativeMaterialCoordinates::Make(Transform,false,RadiusMeters).Apply(Stamp,RecipeStatus))return;
    }
    TerrainStamp->SetStamp(Stamp);
    SamplingRuntime = StaticCastSharedPtr<FCLPlanetStampRuntime>(
        FVoxelStampRuntime::Create(GetWorld(), TerrainStamp->GetStamp(), TerrainStamp));
    FString GroundError;GroundSampler=SamplingRuntime&&SamplingRuntime->RecipePlan?FCLGroundSurfaceSampler::Make(SamplingRuntime->RecipePlan,GroundError):nullptr;
    FString SurfaceInput=FString::Printf(TEXT("GroundSurface=%d|"),FCLGroundSurfaceSampler::Profile);
    FCLPlanetRecipe::StaticStruct()->ExportText(SurfaceInput,&Stamp.Recipe,nullptr,this,PPF_None,nullptr);
    SurfaceInput+=FString::Printf(TEXT("|%.17g|"),Stamp.Radius);
    for(const auto& P:Stamp.Placements)FCLSphericalHeightmapPlacement::StaticStruct()->ExportText(SurfaceInput,&P,nullptr,this,PPF_None,nullptr);
    SurfaceInput+=GetActorTransform().ToString();
    SurfaceGenerationKey=FMD5::HashAnsiString(*SurfaceInput);
    for (ACLSphericalHeightmapStamp* Actor : Actors) Actor->UpdateFootprint();
}

void ACLPlanet::PostRegisterAllComponents()
{
    Super::PostRegisterAllComponents();
    if (IsTemplate()) return;
    // Recreate the nonserialized sampling/preview snapshot after all level
    // actors have registered, rather than requiring an edit after reopening.
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
    {
        if (!bRuntimeRecipeApplied && GetWorld() && !GetWorld()->bIsTearingDown) RebuildTerrain();
        return false;
    }));
}

void ACLPlanet::OnConstruction(const FTransform& Transform)
{
    RebuildTerrain();
    Super::OnConstruction(Transform);
}

void ACLPlanet::BeginPlay()
{
    if(!bRuntimeRecipeApplied)RebuildTerrain();
    Super::BeginPlay();
}

bool ACLPlanet::SampleRadialHeights(const TArray<FVector>& LocalDirections, TArray<double>& Heights) const
{
    Heights.Reset();
    if (!SamplingRuntime || !IsInGameThread() || LocalDirections.IsEmpty()) return false;
    FVoxelDoubleVectorBuffer Directions;
    Directions.Allocate(LocalDirections.Num());
    for (int32 I = 0; I < LocalDirections.Num(); ++I)
    {
        Directions.X.Set(I, LocalDirections[I].X);
        Directions.Y.Set(I, LocalDirections[I].Y);
        Directions.Z.Set(I, LocalDirections[I].Z);
    }
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.SampleRadialHeights"));
    const auto Surfaces = FVoxelSurfaceTypeTable::Get();
    const FVoxelQuery Query(0, FVoxelLayers::Empty(), *Surfaces, Dependencies);
    const FVoxelFloatBuffer Result = SamplingRuntime->SampleHeights(Query, Directions);
    for (int32 I = 0; I < Result.Num(); ++I) Heights.Add(Result[I]);
    return true;
}

int32 ACLPlanet::GetNativeStampCount() const { return SamplingRuntime ? SamplingRuntime->Sources.Num() + (SamplingRuntime->RecipePlan ? SamplingRuntime->RecipePlan->GetDistributedStampCount() : 0) : 0; }

TArray<FCLResolvedStampPlacement> ACLPlanet::GetDistributedStampPlacements() const
{
    TArray<FCLResolvedStampPlacement> Result;
    if(SamplingRuntime && SamplingRuntime->RecipePlan)SamplingRuntime->RecipePlan->GetStampPlacements(Result);
    return Result;
}

uint32 ACLPlanet::GetFootprintMask(const FVector& Direction) const
{
    uint32 Mask = 0;
    if (!SamplingRuntime) return Mask;
    for (int32 I = 0; I < FMath::Min(SamplingRuntime->Sources.Num(), 32); ++I)
    {
        FVector2D Chart;
        if (SamplingRuntime->Sources[I].Weight(RadiusMeters * 100, Direction, Chart) > .05) Mask |= 1u << I;
    }
    return Mask;
}

ACLSphericalHeightmapStamp::ACLSphericalHeightmapStamp()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SphericalPlacement"));
    Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("StampForward"));
    Arrow->SetupAttachment(RootComponent);
    Arrow->ArrowColor = FColor(255, 180, 60);
    Arrow->ArrowSize = 2;
    Arrow->SetHiddenInGame(true);
#if WITH_EDITORONLY_DATA
    EditorFootprint = CreateEditorOnlyDefaultSubobject<USplineComponent>(TEXT("CurvedFootprint"));
    if (EditorFootprint)
    {
        EditorFootprint->SetupAttachment(RootComponent);
        EditorFootprint->SetIsVisualizationComponent(true);
        EditorFootprint->SetDrawDebug(true);
        EditorFootprint->SetHiddenInGame(true);
    }
#endif
}

bool ACLSphericalHeightmapStamp::BuildPlacement(FCLSphericalHeightmapPlacement& Out)
{
    ValidationMessage.Reset();
    if (!bEnabled) { ValidationMessage = TEXT("Disabled"); return false; }
    if (!Planet || !Heightmap || !Heightmap->Height) { ValidationMessage = TEXT("Choose a planet and VPP heightmap"); return false; }
    const FVector Scale = GetActorScale3D();
    if (Scale.ContainsNaN() || Scale.GetMin() <= 0 || !FMath::IsFinite(HeightOffsetMeters) ||
        !FMath::IsFinite(EdgeFalloffMeters) || EdgeFalloffMeters < .01 || !FMath::IsFinite(Smoothness) || Smoothness < 0)
    { ValidationMessage = TEXT("Use positive scales/falloff and finite height settings"); return false; }
    if (!Planet->GetActorScale3D().Equals(FVector::OneVector))
    { ValidationMessage = TEXT("Planet actor scale must be 1; edit its radius instead"); return false; }
    const auto Data = Heightmap->Height->GetData();
    if (!Data || Data->SizeX < 4 || Data->SizeY < 4 || !FMath::IsFinite(Heightmap->ScaleXY) || Heightmap->ScaleXY <= 0)
    { ValidationMessage = TEXT("Heightmap needs valid source data and positive ScaleXY"); return false; }
    if (!FMath::IsFinite(Heightmap->Height->ScaleZ) || !FMath::IsFinite(Heightmap->Height->OffsetZ))
    { ValidationMessage = TEXT("Heightmap vertical scale/offset must be finite"); return false; }
    const double Radius = Planet->RadiusMeters * 100;
    if (!FMath::IsFinite(Radius) || Radius < 1000 || FVector2D(Data->SizeX * Heightmap->ScaleXY * Scale.X,
        Data->SizeY * Heightmap->ScaleXY * Scale.Y).Size() * .5 >= UE_PI * Radius - 1)
    { ValidationMessage = TEXT("Reduce footprint scale: a spherical chart cannot contain its antipode"); return false; }
    if (!StampId.IsValid()) StampId = FGuid::NewGuid();
    const FTransform PlanetTransform = Planet->GetActorTransform();
    Out.Id = StampId;
    Out.Anchor = PlanetTransform.InverseTransformPosition(GetActorLocation()).GetSafeNormal(UE_SMALL_NUMBER, FVector::ZAxisVector);
    Out.TangentX = PlanetTransform.InverseTransformVectorNoScale(GetActorForwardVector());
    Out.Scale = Scale;
    Out.HeightOffset = HeightOffsetMeters * 100;
    Out.EdgeFalloff = EdgeFalloffMeters * 100;
    Out.Source.Heightmap = Heightmap;
    Out.Source.BlendMode = BlendMode;
    Out.Source.Priority = Priority;
    Out.Source.Smoothness = Smoothness;
    Out.Source.Behavior = EVoxelStampBehavior::AffectShape;
    Out.Source.bApplyOnVoid = false;
    ValidationMessage = TEXT("Ready: native VPP heightmap runtime");
    return true;
}

void ACLSphericalHeightmapStamp::SnapToSurface()
{
    if (!Planet || !GetWorld() || GetWorld()->bIsTearingDown) return;
    if (!StampId.IsValid()) StampId = FGuid::NewGuid();
    const FVector Up = (GetActorLocation() - Planet->GetActorLocation()).GetSafeNormal(UE_SMALL_NUMBER, FVector::ZAxisVector);
    FVector Forward = FVector::VectorPlaneProject(GetActorForwardVector(), Up).GetSafeNormal();
    if (Forward.IsNearlyZero()) { FVector Other; Up.FindBestAxisVectors(Forward, Other); }
    SetActorLocationAndRotation(Planet->GetActorLocation() + Up * Planet->RadiusMeters * 100,
        FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat());
    if (GetAttachParentActor() != Planet) AttachToActor(Planet, FAttachmentTransformRules::KeepWorldTransform);
}

void ACLSphericalHeightmapStamp::RefreshStamp()
{
    if (!Planet || !GetWorld() || GetWorld()->bIsTearingDown) return;
    SnapToSurface();
#if WITH_EDITOR
    BindHeightmap();
#endif
    Planet->RebuildTerrain();
}

void ACLSphericalHeightmapStamp::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RefreshStamp();
}

void ACLSphericalHeightmapStamp::PostRegisterAllComponents()
{
    Super::PostRegisterAllComponents();
#if WITH_EDITOR
    BindHeightmap();
#endif
}

void ACLSphericalHeightmapStamp::Destroyed()
{
#if WITH_EDITOR
    if (BoundHeightmap.IsValid()) BoundHeightmap->OnChanged_EditorOnly.Remove(HeightmapChangedHandle);
#endif
    Super::Destroyed();
    if (Planet && GetWorld() && !GetWorld()->bIsTearingDown) Planet->RebuildTerrain();
}

#if WITH_EDITOR
void ACLSphericalHeightmapStamp::BindHeightmap()
{
    if (BoundHeightmap.Get() == Heightmap) return;
    if (BoundHeightmap.IsValid()) BoundHeightmap->OnChanged_EditorOnly.Remove(HeightmapChangedHandle);
    BoundHeightmap = Heightmap;
    if (Heightmap) HeightmapChangedHandle = Heightmap->OnChanged_EditorOnly.AddUObject(this, &ACLSphericalHeightmapStamp::RefreshStamp);
}
void ACLSphericalHeightmapStamp::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);
    RefreshStamp();
}
void ACLSphericalHeightmapStamp::PostEditUndo()
{
    Super::PostEditUndo();
    RefreshStamp();
}
void ACLSphericalHeightmapStamp::PostDuplicate(EDuplicateMode::Type Mode)
{
    Super::PostDuplicate(Mode);
    if (Mode != EDuplicateMode::PIE) StampId = FGuid::NewGuid();
}
#endif

void ACLSphericalHeightmapStamp::UpdateFootprint()
{
    FootprintPoints.Reset();
#if WITH_EDITORONLY_DATA
    if (EditorFootprint) EditorFootprint->SetVisibility(false);
#endif
    if (!bShowFootprint || !Planet) return;
    FCLSphericalHeightmapPlacement Placement;
    if (!BuildPlacement(Placement)) return;
    const auto Data = Heightmap->Height->GetData();
    const FVector2D Min = (FVector2D(1, 1) - FVector2D(Data->SizeX, Data->SizeY) * .5) * Heightmap->ScaleXY;
    const FVector2D Max = (FVector2D(Data->SizeX - 2.0001, Data->SizeY - 2.0001) -
        FVector2D(Data->SizeX, Data->SizeY) * .5) * Heightmap->ScaleXY;
    TArray<FVector> Directions;
    for (int32 Side = 0; Side < 4; ++Side)
        for (int32 I = 0; I <= 24; ++I)
        {
            const double T = I / 24.0;
            const FVector2D Point = Side == 0 ? FVector2D(FMath::Lerp(Min.X, Max.X, T), Min.Y) :
                Side == 1 ? FVector2D(Max.X, FMath::Lerp(Min.Y, Max.Y, T)) :
                Side == 2 ? FVector2D(FMath::Lerp(Max.X, Min.X, T), Max.Y) : FVector2D(Min.X, FMath::Lerp(Max.Y, Min.Y, T));
            Directions.Add(Placement.FromChart(Planet->RadiusMeters * 100,
                FVector2D(Point.X * Placement.Scale.X, Point.Y * Placement.Scale.Y)));
        }
    TArray<double> Heights;
    if (!Planet->SampleRadialHeights(Directions, Heights)) return;
    for (int32 I = 0; I < Directions.Num(); ++I)
        FootprintPoints.Add(Planet->GetActorTransform().TransformPosition(Directions[I] * (Planet->RadiusMeters * 100 + Heights[I] + 25)));
#if WITH_EDITORONLY_DATA
    if (EditorFootprint)
    {
        TArray<FVector> Points;
        for (int32 Side = 0; Side < 4; ++Side)
            for (int32 I = 0; I < 24; ++I) Points.Add(FootprintPoints[Side * 25 + I]);
        EditorFootprint->SetSplinePoints(Points, ESplineCoordinateSpace::World, false);
        for (int32 I = 0; I < Points.Num(); ++I) EditorFootprint->SetSplinePointType(I, ESplinePointType::Linear, false);
        EditorFootprint->SetClosedLoop(true, false);
        EditorFootprint->EditorUnselectedSplineSegmentColor = FLinearColor(FootprintColor);
        EditorFootprint->EditorSelectedSplineSegmentColor = FLinearColor(FootprintColor);
        EditorFootprint->UpdateSpline();
        EditorFootprint->SetVisibility(true);
    }
#endif
}

void ACLSphericalHeightmapStamp::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bShowFootprint || !bShowFootprintInGame || !GetWorld()->IsGameWorld() || FootprintPoints.Num() != 100) return;
    for (int32 Side = 0; Side < 4; ++Side)
        for (int32 I = 0; I < 24; ++I)
        {
            const int32 A = Side * 25 + I, B = A + 1;
            DrawDebugLine(GetWorld(), FootprintPoints[A], FootprintPoints[B],
                FootprintColor, false, -1, 0, 3);
        }
}


bool ACLPlanet::SampleGroundSurface(const TArray<FVector>& D,TArray<FVector>& Positions,TArray<FVector>& Normals,TArray<bool>& Supported,FString& Error)const
{
    Positions.Reset();Normals.Reset();Supported.Reset();
    if(!GroundSampler){Error=TEXT("No accepted whole-body ground sampler");return false;}
    TArray<FCLGroundSurfaceSample> Samples;if(!GroundSampler->Sample(D,Samples,Error))return false;
    for(const auto& S:Samples){Positions.Add(S.PositionMeters);Normals.Add(S.Normal);Supported.Add(S.bSupported);}return true;
}

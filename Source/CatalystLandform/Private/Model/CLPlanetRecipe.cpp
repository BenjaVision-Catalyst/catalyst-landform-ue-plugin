#include "Model/CLSurfaceRange.h"
#include "Model/CLCompileProgress.h"
#include "Misc/ScopeExit.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLRegionGeneration.h"
#include "Model/CLTerrainType.h"
#include "CLModelJson.h"
#include "CLStampDistribution.h"
#include "CLPlanetShape.h"
#include "Model/CLSphericalPatches.h"
#include "CLNativeNoiseKernel.h"
#include "VoxelISPCNodeHelpers.h"
#include "Shape/VoxelShapeFunctionLibrary.h"
#include "JsonObjectConverter.h"
#include "Validation/CFValidationTypes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "Serialization/CustomVersion.h"

// The native graph's shape library is stateless: MakeSphere reads only its
// double buffers and calls VPP's ISPC kernel. Bind its immortal CDO at compile
// time; workers do no reflection, UObject lookup or authoring-state access.
class FCLNativeFigureKernel
{
public:
    explicit FCLNativeFigureKernel(const UVoxelShapeFunctionLibrary& In):Library(&In){}
    void Sample(TConstArrayView<FVector> Positions,TConstArrayView<double> Heights,double Radius,TArray<double>& Fields)const
    {
        if(Positions.IsEmpty()){Fields.Reset();return;}
        FVoxelDoubleVectorBuffer P;P.Allocate(Positions.Num());FVoxelDoubleBuffer R;
        const double FirstRadius=Radius+FMath::Min(Heights[0],0.);bool Uniform=true;
        for(int32 I=0;I<Positions.Num();++I){P.Set(I,Positions[I]);Uniform&=Radius+FMath::Min(Heights[I],0.)==FirstRadius;}
        if(Uniform)R=FVoxelDoubleBuffer(FirstRadius);
        else {R.Allocate(Positions.Num());for(int32 I=0;I<Positions.Num();++I)R.Set(I,Radius+FMath::Min(Heights[I],0.));}
        const auto Values=Library->MakeSphere(P,R);Fields.SetNumUninitialized(Positions.Num());
        for(int32 I=0;I<Positions.Num();++I)Fields[I]=Values[I];
    }
private:
    const UVoxelShapeFunctionLibrary* const Library;
};
struct FCLPreparedPlanetCompile
{
    FCLPlanetRecipe Recipe;
    TOptional<FBox2D> Region;
    int32 Patch=INDEX_NONE,ReusedInputs=0;
    struct FNativeSnapshot
    {
        FCLPlanetRecipe Recipe;TOptional<FBox2D> Region;int32 Patch;
        const UCLStampLibraryAsset* Stamps;const UCLSurfaceLibraryAsset* Surfaces;
        TSharedPtr<const FCLCompiledPlanetRecipe> Field;
    };
    TArray<FNativeSnapshot> Raw;
    TMap<const FCLCompiledPlanetRecipe*,TFunction<TSharedPtr<const FCLCompiledPlanetRecipe>(FString&)>> DeferredRegions;
    TSharedPtr<const FCLNativeFigureKernel> Figure;
    TSharedPtr<const FCLCompiledMeasuredTerrain> Measured;
};
namespace
{
struct FCompileRun {bool Preparing=false;FCLPreparedPlanetCompile* Prepared=nullptr;int32 Next=0,RawDepth=0;FCLCompileProgress* Progress=nullptr;const FCLPreparedPlanetCompile* Previous=nullptr;};
thread_local FCompileRun* CompileRun=nullptr;
thread_local bool CompilingCaveBase=false;
TSharedPtr<const FCLCompiledPlanetRecipe> CompileUnmappedBody(const FCLPlanetRecipe&,FString&,UCLStampLibraryAsset*,const FBox2D*,int32,UCLSurfaceLibraryAsset*);
}
bool FCLCompileProgress::IsPreparing(){return CompileRun&&CompileRun->Preparing;}
bool FCLCompileProgress::Check(FString& E)
{if(CompileRun&&CompileRun->Progress&&CompileRun->Progress->Cancelled.load()){E=TEXT("Evaluation superseded");return false;}return true;}
void FCLCompileProgress::Report(ECLCompileStage S){if(CompileRun&&CompileRun->Progress){CompileRun->Progress->Work=0;CompileRun->Progress->Stage.store(S);}}
void FCLCompileProgress::Count(uint32 Done,uint32 Total){if(CompileRun&&CompileRun->Progress)CompileRun->Progress->Work=(uint64(Total)<<32)|FMath::Min(Done,Total);}
bool FCLCompileProgress::Sample(const FCLCompiledPlanetRecipe& P,TConstArrayView<FVector> D,TArray<double>& H,FString& E)
{
    if(!CompileRun){P.SampleHeightsMeters(D,H);return true;}
    const bool OwnProgress=!CompileRun->Progress||!uint32(CompileRun->Progress->Work.load()>>32);
    if(OwnProgress)Count(0,D.Num());ON_SCOPE_EXIT{if(OwnProgress)Count(0,0);};
    H.Reset(D.Num());for(int32 I=0;I<D.Num();I+=4096){if(!Check(E))return false;TArray<double> Part;P.SampleHeightsMeters(D.Slice(I,FMath::Min(4096,D.Num()-I)),Part);H.Append(Part);if(OwnProgress)Count(H.Num(),D.Num());}return Check(E);
}
bool FCLPlanetCompiler::WithAuthoringValidation(TFunctionRef<bool()> Operation)
{
    check(IsInGameThread());FCLPreparedPlanetCompile P;P.Figure=MakeShared<FCLNativeFigureKernel>(*GetDefault<UVoxelShapeFunctionLibrary>());
    FCompileRun Run;Run.Preparing=true;Run.Prepared=&P;auto* Previous=CompileRun;CompileRun=&Run;ON_SCOPE_EXIT{CompileRun=Previous;};return Operation();
}
TSharedPtr<const FCLPreparedPlanetCompile> FCLPlanetCompiler::PrepareBackground(const FCLPlanetRecipe& R,FString& E,const FBox2D* Region,int32 Patch,const FCLPreparedPlanetCompile* PreviousInputs)
{
    check(IsInGameThread());auto P=MakeShared<FCLPreparedPlanetCompile>();P->Recipe=R;if(Region)P->Region=*Region;P->Patch=Patch;
    P->Figure=MakeShared<FCLNativeFigureKernel>(*GetDefault<UVoxelShapeFunctionLibrary>());
    if((R.AlgorithmVersion==2||(R.Rock.bEnabled&&R.Rock.AlgorithmVersion==1))&&!FCLNativeNoiseKernel::Create(E))return nullptr;
    FCompileRun Run;Run.Preparing=true;Run.Prepared=&P.Get();Run.Previous=PreviousInputs;auto* Previous=CompileRun;CompileRun=&Run;ON_SCOPE_EXIT{CompileRun=Previous;};
    if(!Compile(R,E,nullptr,Region,Patch))return nullptr;return P;
}
int32 FCLCompileProgress::ReusedNativeInputs(const FCLPreparedPlanetCompile& P){return P.ReusedInputs;}
TSharedPtr<const FCLCompiledPlanetRecipe> FCLPlanetCompiler::CompleteBackground(const FCLPreparedPlanetCompile& P,const TSharedRef<FCLCompileProgress>& Progress,FString& E)
{
    FCompileRun Run;Run.Prepared=const_cast<FCLPreparedPlanetCompile*>(&P);Run.Progress=&Progress.Get();auto* Previous=CompileRun;CompileRun=&Run;ON_SCOPE_EXIT{CompileRun=Previous;};
    if(!FCLCompileProgress::Check(E))return nullptr;auto Out=Compile(P.Recipe,E,nullptr,P.Region.IsSet()?&P.Region.GetValue():nullptr,P.Patch);
    if(Out&&Run.Next!=P.Raw.Num()){E=TEXT("Prepared native field sequence did not match completion");return nullptr;}
    if(!FCLCompileProgress::Check(E))return nullptr;Progress->Stage=ECLCompileStage::Complete;return Out;
}
void FCLCompiledPlanetRecipe::SampleRadialFields(TConstArrayView<FVector> Positions,TConstArrayView<double> Heights,TArray<double>& Fields)const
{
    check(NativeFigure&&Positions.Num()==Heights.Num());NativeFigure->Sample(Positions,Heights,RadiusMeters,Fields);
}

namespace
{
const FGuid RecipeAssetVersion(0x434C5250, 0x82B741FA, 0xA3482664, 0xC96741B2);
FCustomVersionRegistration RegisterRecipeAssetVersion(RecipeAssetVersion, 3, TEXT("CLRecipeOuterSkyRadius"));
uint32 Mix(uint32 V)
{
    V ^= V >> 16; V *= 0x7feb352du; V ^= V >> 15; V *= 0x846ca68bu; return V ^ (V >> 16);
}
}

bool FCLHeightReference::Validate(FString& Error) const
{
    const bool bHeight = Mode == ECLHeightReferenceMode::Height;
    if (!FMath::IsFinite(OffsetMeters) || !FMath::IsFinite(Fraction) || uint8(Mode)>uint8(ECLHeightReferenceMode::IntervalDelta) ||
        (bHeight && (!UpperGuide.IsNone() || Fraction != 0)) ||
        (!bHeight && (Guide.IsNone() || UpperGuide.IsNone() || Fraction>1 || Fraction<(Mode==ECLHeightReferenceMode::IntervalHeight?0:-1))))
    { Error=TEXT("Invalid height reference"); return false; }
    return true;
}

bool FCLTerrainRegionMask::Validate(FString& Error) const
{
    if(uint8(Shape)>uint8(ECLTerrainRegionShape::HalfPlane) || CenterMeters.ContainsNaN() ||
       CenterMeters.GetAbs().GetMax()>1.e7 || !FMath::IsFinite(HeadingDegrees) ||
       !FMath::IsFinite(HalfWidthMeters) || HalfWidthMeters<.1 || HalfWidthMeters>4000 ||
       !FMath::IsFinite(TransitionMeters) || TransitionMeters<.1 || TransitionMeters>4000 ||
       (Shape!=ECLTerrainRegionShape::HalfPlane && TransitionMeters>2*HalfWidthMeters))
    {Error=TEXT("Region mask needs finite coordinates, a positive extent and transition no wider than its diameter");return false;}
    return true;
}
double FCLTerrainRegionMask::Weight(const FVector2D& Meters) const
{
    const FVector2D P=Meters-CenterMeters;
    const double A=FMath::DegreesToRadians(HeadingDegrees);
    const double Along=P.X*FMath::Cos(A)+P.Y*FMath::Sin(A);
    const double Distance=Shape==ECLTerrainRegionShape::Disc ? HalfWidthMeters-P.Size() :
        Shape==ECLTerrainRegionShape::Band ? HalfWidthMeters-FMath::Abs(Along) : Along;
    const double T=FMath::Clamp(.5+Distance/TransitionMeters,0.,1.);
    // Quintic endpoints have zero first and second derivatives.
    return T*T*T*(T*(T*6-15)+10);
}

bool FCLSphericalRegionMask::Validate(double Radius,FString& Error) const
{
    if(uint8(Shape)>uint8(ECLTerrainRegionShape::HalfPlane)||Axis.ContainsNaN()||Center.ContainsNaN()||
       Axis.IsNearlyZero()||Center.IsNearlyZero()||!FMath::IsFinite(OffsetMeters)||
       !FMath::IsFinite(HalfWidthMeters)||HalfWidthMeters<.1||HalfWidthMeters>=Radius*UE_PI*.5||
       !FMath::IsFinite(TransitionMeters)||TransitionMeters<.1||TransitionMeters>2*HalfWidthMeters)
    {Error=TEXT("Spherical mask requires valid directions, positive extent below a hemisphere and a positive bounded transition");return false;}
    return true;
}
double FCLSphericalRegionMask::Weight(const FVector& D,double Radius) const
{
    const FVector Unit=D.GetSafeNormal();
    const double Along=Radius*FMath::Asin(FMath::Clamp(FVector::DotProduct(Unit,Axis.GetSafeNormal()),-1.,1.))-OffsetMeters;
    const double Distance=Shape==ECLTerrainRegionShape::Disc ? HalfWidthMeters-Radius*FMath::Acos(FMath::Clamp(FVector::DotProduct(Unit,Center.GetSafeNormal()),-1.,1.)) :
        Shape==ECLTerrainRegionShape::Band ? HalfWidthMeters-FMath::Abs(Along) : Along;
    const double T=FMath::Clamp(.5+Distance/TransitionMeters,0.,1.);return T*T*T*(T*(T*6-15)+10);
}

int32 FCLCompiledPlanetRecipe::GetDistributedStampCount()const
{if(!SurfacePatches.IsEmpty()){TArray<FCLResolvedStampPlacement> P;GetStampPlacements(P);return P.Num();}int32 N=0;for(const auto& L:Layers)if(L.Distribution)N+=L.Distribution->Placements.Num();for(const auto& R:Regions)N+=R.Field->GetDistributedStampCount();if(GeneratedRegions)for(const auto& F:GeneratedRegions->Fields)if(F)N+=F->GetDistributedStampCount();return N;}
void FCLCompiledPlanetRecipe::GetStampCoverage(const FVector& Direction,int32& Count,double& MinimumWeight)const
{
    if(!SurfacePatches.IsEmpty()){SurfacePatches[FCLSphericalPatches::Owner(Direction)]->GetStampCoverage(Direction,Count,MinimumWeight);return;}
    Count=0;MinimumWeight=1;
    FVector Position=Direction;
    if(Coordinates==ECLTerrainCoordinates::LocalPatch){FVector2D P;if(!TerrainPosition(Direction,P))return;Position=FVector(P.X,P.Y,0);}
    TArray<double,TInlineAllocator<8>> Contributions;Contributions.SetNum(Regions.Num());
    double BaseWeight=1;
    for(int32 I=Regions.Num()-1;I>=0;--I)
    {
        const double W=Coordinates==ECLTerrainCoordinates::SphericalSurface ? Regions[I].Weight(Direction,RadiusMeters+BaseHeightMeters) : Regions[I].Mask.Weight(FVector2D(Position));Contributions[I]=W*BaseWeight;BaseWeight*=1-W;
    }
    if(BaseWeight>0)
    {
        for(const auto& L:Layers)if(L.Distribution)L.Distribution->Coverage(Position,Count,MinimumWeight);
        if(Count)MinimumWeight*=BaseWeight;
    }
    for(int32 I=0;I<Regions.Num();++I)
    {
        if(Contributions[I]<=0)continue;
        int32 C=0;double M=0;Regions[I].Field->GetStampCoverage(Regions[I].SourceDirection(Direction),C,M);
        Count+=C;if(C)MinimumWeight=FMath::Min(MinimumWeight,M*Contributions[I]);
    }
    if(GeneratedRegions&&BaseWeight>0)
    {
        TArray<double> W;GeneratedRegions->PaletteWeights(Direction,W);
        for(int32 I=0;I<W.Num();++I)if(W[I]>0&&GeneratedRegions->Fields[I])
        {int32 C=0;double M=0;GeneratedRegions->Fields[I]->GetStampCoverage(Direction,C,M);Count+=C;if(C)MinimumWeight=FMath::Min(MinimumWeight,M*W[I]*BaseWeight*GeneratedRegions->Strength);}
    }
    if(!Count)MinimumWeight=0;
}
void FCLCompiledPlanetRecipe::GetStampPlacements(TArray<FCLResolvedStampPlacement>& Placements) const
{
    if(!SurfacePatches.IsEmpty())
    {
        Placements.Reset();TSet<FGuid> Seen;
        for(const auto& Patch:SurfacePatches)
        {
            TArray<FCLResolvedStampPlacement> P;Patch->GetStampPlacements(P);
            for(auto& V:P)if(!Seen.Contains(V.Id)){Seen.Add(V.Id);Placements.Add(MoveTemp(V));}
        }
        Placements.Sort([](const auto& A,const auto& B){return A.Id.ToString(EGuidFormats::Digits)<B.Id.ToString(EGuidFormats::Digits);});return;
    }
    Placements.Reset();for(const auto& L:Layers)if(L.Distribution)Placements.Append(L.Distribution->Placements);
    for(const auto& R:Regions)
    {
        TArray<FCLResolvedStampPlacement> Child;R.Field->GetStampPlacements(Child);
        for(auto& P:Child){if(R.bPlacedLandform){P.Anchor=R.Placement.Orientation.RotateVector(P.Anchor);P.TangentX=R.Placement.Orientation.RotateVector(P.TangentX);}P.RegionId=R.Id;P.Id=FGuid::Combine(R.Id,P.Id);Placements.Add(MoveTemp(P));}
    }
    if(GeneratedRegions)for(int32 I=0;I<GeneratedRegions->Fields.Num();++I)if(GeneratedRegions->Fields[I])
    {
        TArray<FCLResolvedStampPlacement> Child;GeneratedRegions->Fields[I]->GetStampPlacements(Child);
        for(auto& P:Child){P.RegionId=GeneratedRegions->Palette[I].Id;P.Id=FGuid::Combine(P.RegionId,P.Id);Placements.Add(MoveTemp(P));}
    }

}

double FCLCompiledPlanetRecipe::FLayer::TerraceHeight(double Height) const
{
    if (Strength == 0) return Height;
    const double Position = (Height - Origin) / StepHeight;
    const double Level = FMath::FloorToDouble(Position), Fraction = Position - Level;
    const double Alpha = Softness > 0 ? FMath::Clamp((Fraction - (1 - Softness)) / Softness, 0.0, 1.0) : 0;
    const double Transition = Alpha * Alpha * (3 - 2 * Alpha);
    return FMath::Lerp(Height, Origin + StepHeight * (Level + Transition), Strength);
}

double FCLCompiledPlanetRecipe::SampleHeightMeters(const FVector& Direction) const
{
    if(MeasuredTerrain){TArray<double> H;MeasuredTerrain->SampleHeights(MakeArrayView(&Direction,1),H);return H[0];}

    if (Coordinates != ECLTerrainCoordinates::Sphere || NativePerlin || Layers.ContainsByPredicate([](const auto& L){return L.Operation == ECLTerrainOperation::StampDistribution;}))
    {
        TArray<double> Heights; SampleHeightsMeters(MakeArrayView(&Direction, 1), Heights); return Heights[0];
    }
    if (Direction.ContainsNaN() || Direction.IsNearlyZero()) return 0;
    const FVector Position = Direction.GetSafeNormal() * RadiusMeters;
    double Height = BaseHeightMeters;
    for (const FLayer& Layer : Layers)
    {
        if (Layer.Operation == ECLTerrainOperation::Terrace) { Height = Layer.TerraceHeight(Height); continue; }
        double Sum = 0, Weight = 1, Total = 0, Frequency = 1;
        for (int32 I = 0; I < Layer.Octaves; ++I)
        {
            const double N = FMath::PerlinNoise3D(Position * (Frequency / Layer.Wavelength) + Layer.Offset);
            Sum += (Layer.Shape == ECLNoiseShape::Ridged ? 1 - FMath::Abs(N) : .5 + .5 * N) * Weight;
            Total += Weight; Weight *= Layer.Persistence; Frequency *= 2;
        }
        const double Value = FMath::Lerp(Layer.Minimum, Layer.Maximum, FMath::Clamp(Sum / Total, 0.0, 1.0));
        switch (Layer.Blend)
        {
        case ECLNoiseBlend::Override: Height = Value; break;
        case ECLNoiseBlend::Add: Height += Value; break;
        case ECLNoiseBlend::Max: Height = FMath::Max(Height, Value); break;
        case ECLNoiseBlend::Min: Height = FMath::Min(Height, Value); break;
        }
    }
    return FMath::Min(Height, 0.0);
}

FVector FCLCompiledPlanetRecipe::TerrainDirection(const FVector2D& Meters) const
{
    FCLPlanetShape Map; Map.Radius = RadiusMeters + BaseHeightMeters;
    Map.AnchorDirection = TerrainPatch.Anchor; Map.ChartXAxis = TerrainPatch.TangentX;
    return Map.FromChart(Meters);
}
bool FCLCompiledPlanetRecipe::TerrainPosition(const FVector& Direction, FVector2D& Meters) const
{
    Meters=FVector2D::ZeroVector;
    if(Direction.ContainsNaN() || Direction.IsNearlyZero()) return false;
    FCLPlanetShape Map; Map.Radius = RadiusMeters + BaseHeightMeters;
    Map.AnchorDirection = TerrainPatch.Anchor; Map.ChartXAxis = TerrainPatch.TangentX;
    return Map.ToChart(Direction, Meters);
}
void FCLCompiledPlanetRecipe::SampleHeightsMeters(TConstArrayView<FVector> Directions, TArray<double>& Heights) const
{
    if(WatershedSurface){WatershedSurface->SampleHeightsMeters(Directions,Heights);Watershed->MapHeights(Directions,Heights);return;}
    if(MeasuredTerrain){MeasuredTerrain->SampleHeights(Directions,Heights);return;}
    if(UnmappedSurface)
    {UnmappedSurface->SampleHeightsMeters(Directions,Heights);SurfaceRange->MapHeights(Directions,Heights);return;}
    if(!SurfacePatches.IsEmpty())
    {
        Heights.SetNumUninitialized(Directions.Num());
        for(int32 Face=0;Face<6;++Face)
        {
            TArray<FVector> D;TArray<int32> Indices;
            for(int32 I=0;I<Directions.Num();++I)if(FCLSphericalPatches::Owner(Directions[I])==Face){D.Add(Directions[I]);Indices.Add(I);}
            if(D.IsEmpty())continue;
            TArray<double> H;SurfacePatches[Face]->SampleHeightsMeters(D,H);
            for(int32 I=0;I<Indices.Num();++I)Heights[Indices[I]]=H[I];
        }
        return;
    }
    if(Coordinates==ECLTerrainCoordinates::SphericalSurface)
    {EvaluateHeightsMeters(Directions,Heights,false);if(GeneratedRegions)GeneratedRegions->Blend(Directions,Heights);BlendRegionHeights(Directions,Heights,false);ApplyBaseStructure(Directions,Heights);return;}
    if (Coordinates == ECLTerrainCoordinates::LocalPatch)
    {
        TArray<FVector> Positions;
        TArray<double> Weights;
        TArray<int32> Indices;
        Heights.Init(BaseHeightMeters,Directions.Num());
        for (int32 I=0;I<Directions.Num();++I)
        {
            FVector2D P; if(!TerrainPosition(Directions[I], P)) continue;
            const double Margin = TerrainPatch.HalfWidthMeters - FMath::Max(FMath::Abs(P.X),FMath::Abs(P.Y));
            if(Margin<=0) continue;
            const double T = FMath::Clamp(Margin / TerrainPatch.TransitionMeters,0.,1.);
            Positions.Add(FVector(P.X,P.Y,0)); Weights.Add(T*T*(3-2*T)); Indices.Add(I);
        }
        if(!Positions.IsEmpty())
        {
            TArray<double> LocalHeights;SampleLocalHeightsMeters(Positions,LocalHeights);
            for(int32 I=0;I<Indices.Num();++I)Heights[Indices[I]]=FMath::Lerp(BaseHeightMeters,LocalHeights[I],Weights[I]);
        }
        return;
    }
    Heights.SetNumUninitialized(Directions.Num());
    if (!NativePerlin && !Layers.ContainsByPredicate([](const auto& L){return L.Operation == ECLTerrainOperation::StampDistribution;}))
    {
        for (int32 I = 0; I < Directions.Num(); ++I) Heights[I] = SampleHeightMeters(Directions[I]);
        return;
    }
    EvaluateHeightsMeters(Directions, Heights, false);
}
void FCLCompiledPlanetRecipe::ApplyBaseStructure(TConstArrayView<FVector> Directions,TArray<double>& Heights) const
{
    if(BaseStructure.IsEmpty()&&!Tectonics&&!Continents)return;
    TArray<double> Delta;Delta.Init(0,Directions.Num());
    if(Tectonics)for(int32 I=0;I<Directions.Num();++I)Delta[I]=Tectonics->SampleDeltaMeters(Directions[I]);
    if(Continents)for(int32 I=0;I<Directions.Num();++I)Delta[I]+=Continents->SampleDeltaMeters(Directions[I]);
    for(const auto& F:BaseStructure)
    {
        if(F.ReliefMeters==0)continue;
        if(F.Heightmap)
        {
            TArray<double> Values;Values.Init(0,Directions.Num());F.Heightmap->Apply(Directions,Values);
            for(int32 I=0;I<Directions.Num();++I)Delta[I]+=(F.ReliefMeters<0?-1:1)*Values[I];
        }
        else for(int32 I=0;I<Directions.Num();++I)Delta[I]+=F.Weight(Directions[I])*F.ReliefMeters;
    }
    for(int32 I=0;I<Directions.Num();++I)Heights[I]=bDeferRadialBounds?Heights[I]+Delta[I]:FMath::Clamp(Heights[I]+Delta[I],-RadiusMeters,0.);
}
void FCLCompiledPlanetRecipe::SampleLocalHeightsMeters(TConstArrayView<FVector> Directions, TArray<double>& Heights) const
{
    check(Coordinates==ECLTerrainCoordinates::LocalPatch);
    EvaluateHeightsMeters(Directions, Heights, true);
    BlendRegionHeights(Directions,Heights,true);
}
void FCLCompiledPlanetRecipe::BlendRegionHeights(TConstArrayView<FVector> Directions,TArray<double>& Heights,bool bLocal) const
{
    for(const FRegion& Region:Regions)
    {
        TArray<FVector> Positions;TArray<int32> Indices;TArray<double> Weights;
        for(int32 I=0;I<Directions.Num();++I)
        {
            const double W=bLocal ? Region.Mask.Weight(FVector2D(Directions[I])) : Region.Weight(Directions[I],RadiusMeters+BaseHeightMeters);
            if(W>0){Positions.Add(Region.SourceDirection(Directions[I]));Indices.Add(I);Weights.Add(W);}
        }
        if(Positions.IsEmpty())continue;
        TArray<double> Values;if(bLocal)Region.Field->SampleLocalHeightsMeters(Positions,Values);else Region.Field->SampleHeightsMeters(Positions,Values);
        for(int32 I=0;I<Indices.Num();++I)Heights[Indices[I]]=FMath::Lerp(Heights[Indices[I]],Region.bPlacedLandform?BaseHeightMeters+(Values[I]-BaseHeightMeters)*Region.Placement.Strength:Values[I],Weights[I]);
    }
}
void FCLCompiledPlanetRecipe::EvaluateHeightsMeters(TConstArrayView<FVector> Directions, TArray<double>& Heights, bool bLocal) const
{
    Heights.SetNumUninitialized(Directions.Num());
    // VPP 2.0p8's public PerlinNoise3D compute-node ABI: XYZ, amplitude,
    // feature scale, seed -> value. Keep the adapter here; never copy vendor noise.
    constexpr int32 BatchSize = 512;
    float X[BatchSize], Y[BatchSize], Z[BatchSize], Noise[BatchSize];
    FVector Unit[BatchSize]; double Sum[BatchSize];
    const float One = 1;
    for (int32 Start = 0; Start < Directions.Num(); Start += BatchSize)
    {
        const int32 Num = FMath::Min(BatchSize, Directions.Num() - Start);
        for (int32 I = 0; I < Num; ++I)
        {
            const FVector& D = Directions[Start + I];
            Unit[I] = D.ContainsNaN() ? FVector::ZeroVector : bLocal ? FVector(D.X,D.Y,0) : D.GetSafeNormal();
            Heights[Start + I] = BaseHeightMeters;
        }
        for (const FLayer& Layer : Layers)
        {
            if (Layer.Operation == ECLTerrainOperation::StampDistribution)
            {
                Layer.Distribution->Apply(MakeArrayView(Unit,Num),MakeArrayView(Heights.GetData()+Start,Num));
                continue;
            }
            if (Layer.Operation == ECLTerrainOperation::Terrace)
            {
                for (int32 I = 0; I < Num; ++I) Heights[Start + I] = Layer.TerraceHeight(Heights[Start + I]);
                continue;
            }
            for (int32 I = 0; I < Num; ++I) Sum[I] = 0;
            double Weight = 1, Total = 0, Frequency = 1;
            for (int32 Octave = 0; Octave < Layer.Octaves; ++Octave)
            {
                for (int32 I = 0; I < Num; ++I)
                {
                    const FVector P = Unit[I] * ((bLocal ? 1. : Coordinates==ECLTerrainCoordinates::SphericalSurface ? RadiusMeters+BaseHeightMeters : RadiusMeters) * Frequency / Layer.Wavelength);
                    X[I] = P.X; Y[I] = P.Y; Z[I] = P.Z;
                }
                const ispc::FVoxelInputBuffer Inputs[] = {
                    {X, false}, {Y, false}, {Z, false}, {&One, true}, {&One, true}, {&Layer.Seed, true}};
                if(NativePerlin) { void* Outputs[] = {Noise}; NativePerlin->Evaluate(Inputs, Outputs, Num); }
                for (int32 I = 0; I < Num; ++I)
                {
                    const double N = NativePerlin ? Noise[I] : FMath::PerlinNoise3D(
                        (Unit[I] * (bLocal ? 1. : Coordinates==ECLTerrainCoordinates::SphericalSurface ? RadiusMeters+BaseHeightMeters : RadiusMeters)) * (Frequency / Layer.Wavelength) + Layer.Offset);
                    Sum[I] += (Layer.Shape == ECLNoiseShape::Ridged ? 1 - FMath::Abs(N) : .5 + .5 * N) * Weight;
                }
                Total += Weight; Weight *= Layer.Persistence; Frequency *= 2;
            }
            for (int32 I = 0; I < Num; ++I)
            {
                const double Value = FMath::Lerp(Layer.Minimum, Layer.Maximum, FMath::Clamp(Sum[I] / Total, 0.0, 1.0));
                double& H = Heights[Start + I];
                switch (Layer.Blend)
                {
                case ECLNoiseBlend::Override: H = Value; break;
                case ECLNoiseBlend::Add: H += Value; break;
                case ECLNoiseBlend::Max: H = FMath::Max(H, Value); break;
                case ECLNoiseBlend::Min: H = FMath::Min(H, Value); break;
                }
            }
        }
        for (int32 I = 0; I < Num; ++I)
            Heights[Start + I] = (!bLocal && Unit[I].IsNearlyZero()) ? 0 : (bDeferRadialBounds?Heights[Start+I]:FMath::Min(Heights[Start + I], 0.0));
    }
}

bool FCLPlanetCompiler::ValidateNoiseLayers(const TArray<FCLNoiseLayer>& Layers, FString& Error)
{
    Error.Reset();
    if (Layers.Num() > 32) { Error = TEXT("At most 32 terrain layers are supported"); return false; }
    TSet<FName> Names;
    for (const FCLNoiseLayer& Layer : Layers)
    {
        if (Layer.Name.IsNone() || Names.Contains(Layer.Name)) { Error = TEXT("Noise layer names must be nonempty and unique"); return false; }
        Names.Add(Layer.Name);
        if (static_cast<uint8>(Layer.Operation) > static_cast<uint8>(ECLTerrainOperation::StampDistribution))
        { Error = TEXT("Unknown terrain operation: ") + Layer.Name.ToString(); return false; }
        if (!Layer.bEnabled) continue;
        if(Layer.Operation==ECLTerrainOperation::StampDistribution){if(!Layer.Distribution.Validate(Error))return false;continue;}
        const bool bTerrace = Layer.Operation == ECLTerrainOperation::Terrace;
        for (const FCLHeightReference* Ref : {bTerrace ? &Layer.Terrace.StepHeight : &Layer.Minimum, bTerrace ? &Layer.Terrace.Origin : &Layer.Maximum})
        {
            if (!Ref->Validate(Error)) { Error += TEXT(" in layer: ") + Layer.Name.ToString(); return false; }
        }
        if (bTerrace)
        {
            if (!FMath::IsFinite(Layer.Terrace.Softness) || Layer.Terrace.Softness < 0 || Layer.Terrace.Softness > 1 ||
                !FMath::IsFinite(Layer.Terrace.Strength) || Layer.Terrace.Strength < 0 || Layer.Terrace.Strength > 1)
            { Error = TEXT("Terrace softness and strength must be between 0 and 1: ") + Layer.Name.ToString(); return false; }
            continue;
        }
        if (!FMath::IsFinite(Layer.Persistence) || Layer.Persistence < 0 || Layer.Persistence > 1 || Layer.Octaves < 1 || Layer.Octaves > 8 ||
            static_cast<uint8>(Layer.Shape) > static_cast<uint8>(ECLNoiseShape::Ridged) || static_cast<uint8>(Layer.Blend) > static_cast<uint8>(ECLNoiseBlend::Min) ||
            static_cast<uint8>(Layer.Scale) > static_cast<uint8>(ECLNoiseScale::PlanetRadius) ||
            (Layer.Scale == ECLNoiseScale::Meters && (!FMath::IsFinite(Layer.WavelengthMeters) || Layer.WavelengthMeters < .1)) ||
            (Layer.Scale == ECLNoiseScale::PlanetRadius && (!FMath::IsFinite(Layer.WavelengthRadiusFraction) || Layer.WavelengthRadiusFraction <= 0)))
        { Error = TEXT("Invalid noise settings: ") + Layer.Name.ToString(); return false; }
    }
    return true;
}

TSharedPtr<const FCLCompiledPlanetRecipe> FCLPlanetCompiler::Compile(const FCLPlanetRecipe& Recipe,FString& Error,UCLStampLibraryAsset* Library,const FBox2D* LocalRegion,int32 SurfacePatch,UCLSurfaceLibraryAsset* SurfaceLibraryOverride)
{
    if(!FCLCompileProgress::Check(Error))return nullptr;
    const auto RetainInput=[&](TSharedPtr<const FCLCompiledPlanetRecipe> Input)->TSharedPtr<const FCLCompiledPlanetRecipe>
    {
        if(!Input)return nullptr;auto Out=MakeShared<FCLCompiledPlanetRecipe>(*Input);Out->SourceRecipe=MakeShared<const FCLPlanetRecipe>(Recipe);Out->NativeFigure=CompileRun?CompileRun->Prepared->Figure:MakeShared<FCLNativeFigureKernel>(*GetDefault<UVoxelShapeFunctionLibrary>());return Out;
    };

    const bool TypedHosts=Recipe.Rock.Units.ContainsByPredicate([](const auto& U){return U.Lithology!=ECLRockLithology::Unknown||U.FracturePotential!=0||U.DissolutionPotential!=0;});
    if(Recipe.SchemaVersion<23&&TypedHosts&&!CompilingCaveBase&&!Recipe.Caves.bEnabled){Error=TEXT("Typed cave-host rock requires recipe schema 23");return nullptr;}
    if(Recipe.SchemaVersion==23||Recipe.Caves.bEnabled)
    {
        if(Recipe.Coordinates!=ECLTerrainCoordinates::SphericalSurface&&Recipe.Coordinates!=ECLTerrainCoordinates::Sphere)
        {Error=TEXT("Planet caves require spherical body coordinates");return nullptr;}
        if(!Recipe.Caves.Validate(Error))return nullptr;
        if(Recipe.Rock.bEnabled&&!Recipe.Volumes.GeologicalUnits.IsEmpty()){Error=TEXT("Caves retain one canonical geological source");return nullptr;}
        auto Base=Recipe;Base.SchemaVersion=Recipe.MeasuredTerrain.IsActive()?19:
            Recipe.AlgorithmVersion==1?18:Recipe.SchemaVersion==23?22:Recipe.SchemaVersion;
        Base.Caves=FCLCaveSettings();Base.Volumes=FCLVolumeSettings();
        TSharedPtr<const FCLCompiledPlanetRecipe> Core;
        {TGuardValue<bool> Scope(CompilingCaveBase,true);Core=Compile(Base,Error,Library,nullptr,INDEX_NONE,SurfaceLibraryOverride);}if(!Core)return nullptr;
        auto Out=MakeShared<FCLCompiledPlanetRecipe>(*Core);
        if(FCLCompileProgress::IsPreparing())return RetainInput(Out);
        if(Recipe.Caves.bEnabled){FCLCompileProgress::Report(ECLCompileStage::Caves);Out->Caves=FCLCompiledCaves::Compile(Recipe.Caves,*Out,Recipe.Seed,Error);if(!Out->Caves)return nullptr;}
        if(Recipe.Volumes.bEnabled){Out->Volumes=FCLCompiledVolumes::Compile(Recipe.Volumes,Recipe.RadiusMeters,Out->Surfaces.Get(),Error);if(!Out->Volumes)return nullptr;}
        return RetainInput(Out);
    }
    if(Recipe.SchemaVersion==22||Recipe.Watershed.bEnabled)
    {
        if(Recipe.SchemaVersion!=22||Recipe.AlgorithmVersion!=2||Recipe.Coordinates!=ECLTerrainCoordinates::SphericalSurface||Recipe.MeasuredTerrain.IsActive()||LocalRegion||FCLSphericalPatches::IsFace(SurfacePatch))
        {Error=TEXT("Watershed schema 22 requires a whole native procedural sphere; measured terrain remains source-pinned");return nullptr;}
        if(Recipe.Rock.bEnabled&&!Recipe.Volumes.GeologicalUnits.IsEmpty()){Error=TEXT("Watersheds retain one geological source");return nullptr;}
        if(Recipe.Watershed.bEnabled&&!Recipe.Watershed.Validate(Error))return nullptr;
        auto Base=Recipe;Base.SchemaVersion=21;Base.Watershed=FCLWatershedSettings();Base.Volumes=FCLVolumeSettings();
        const auto Core=Compile(Base,Error,Library,nullptr,SurfacePatch,SurfaceLibraryOverride);if(!Core)return nullptr;
        auto Out=MakeShared<FCLCompiledPlanetRecipe>(*Core);
        if(FCLCompileProgress::IsPreparing())return RetainInput(Out);
        if(Recipe.Watershed.bEnabled)
        {
            FCLCompileProgress::Report(ECLCompileStage::Drainage);
            Out->Watershed=FCLCompiledWatershed::Compile(Recipe.Watershed,*Core,Error);if(!Out->Watershed)return nullptr;Out->WatershedSurface=Core;
        }
        if(Recipe.Volumes.bEnabled){const auto V=FCLCompiledVolumes::Compile(Recipe.Volumes,Recipe.RadiusMeters,Out->Surfaces.Get(),Error);if(!V)return nullptr;Out->Volumes=V;}
        return RetainInput(Out);
    }

    const bool RegionalElevation=Recipe.TerrainRegions.ContainsByPredicate([](const auto& R){return R.bEnabled&&R.ElevationDistribution.bEnabled;});
    if(Recipe.SchemaVersion==21||Recipe.ElevationDistribution.bEnabled||RegionalElevation)
    {
        if(Recipe.SchemaVersion!=21||Recipe.AlgorithmVersion!=2||Recipe.Coordinates!=ECLTerrainCoordinates::SphericalSurface||Recipe.MeasuredTerrain.IsActive()||LocalRegion||FCLSphericalPatches::IsFace(SurfacePatch))
        {Error=TEXT("Elevation schema 21 requires native VPP algorithm 2 and a whole spherical procedural scene; measured terrain retains its measured elevations");return nullptr;}
        if(Recipe.Rock.bEnabled&&!Recipe.Volumes.GeologicalUnits.IsEmpty())
        {Error=TEXT("Elevation schema 21 retains one geological source; Rock and legacy radial geological units cannot be composed");return nullptr;}
        auto Base=Recipe;Base.SchemaVersion=Recipe.Rock.bEnabled?20:18;Base.ElevationDistribution=FCLSurfaceDistributionSettings();Base.Volumes=FCLVolumeSettings();
        for(auto& R:Base.TerrainRegions)R.ElevationDistribution=FCLSurfaceDistributionSettings();
        const auto Core=Compile(Base,Error,Library,nullptr,SurfacePatch,SurfaceLibraryOverride);if(!Core)return nullptr;
        auto Out=MakeShared<FCLCompiledPlanetRecipe>(*Core);
        if(Recipe.ElevationDistribution.bEnabled&&!Recipe.ElevationDistribution.Validate(Error))return nullptr;
        for(const auto& Region:Recipe.TerrainRegions)if(Region.bEnabled&&Region.ElevationDistribution.bEnabled&&!Region.ElevationDistribution.Validate(Error))return nullptr;
        if(FCLCompileProgress::IsPreparing())return RetainInput(Out);
        if(Recipe.ElevationDistribution.bEnabled||RegionalElevation)
        {
            FCLCompileProgress::Report(ECLCompileStage::Elevation);
            if(!Core->UnmappedSurface||!Core->SurfaceRange){Error=TEXT("Elevation distribution requires a non-constant base surface");return nullptr;}
            const auto Range=FCLSurfaceRange::Compile(*Core->UnmappedSurface,Core->SurfaceRange->TargetMin,Core->SurfaceRange->TargetMax,Error,&Recipe);if(!Range)return nullptr;
            TFunction<TSharedPtr<const FCLCompiledPlanetRecipe>(TSharedPtr<const FCLCompiledPlanetRecipe>)> Replace;
            Replace=[&](auto Input)->TSharedPtr<const FCLCompiledPlanetRecipe>
            {
                auto P=MakeShared<FCLCompiledPlanetRecipe>(*Input);P->SurfaceRange=Range;P->SurfacePatches.Reset();
                for(const auto& Patch:Input->SurfacePatches)P->SurfacePatches.Add(Replace(Patch));return P;
            };
            Out=MakeShared<FCLCompiledPlanetRecipe>(*Replace(Core));
        }
        if(Recipe.Volumes.bEnabled)
        {
            const auto Volumes=FCLCompiledVolumes::Compile(Recipe.Volumes,Recipe.RadiusMeters,Out->Surfaces.Get(),Error);if(!Volumes)return nullptr;Out->Volumes=Volumes;
        }
        return RetainInput(Out);
    }
    if(Recipe.RegionGeneration.bEnabled&&Recipe.RegionGeneration.AlgorithmVersion==4)
    {
        if(Recipe.AlgorithmVersion!=2||Recipe.Coordinates!=ECLTerrainCoordinates::SphericalSurface||Recipe.MeasuredTerrain.IsActive()||LocalRegion||FCLSphericalPatches::IsFace(SurfacePatch)||!Recipe.Volumes.GeologicalUnits.IsEmpty()||(Recipe.Rock.bEnabled&&Recipe.SchemaVersion!=20))
        {Error=TEXT("Geology-first provinces require a whole native procedural sphere and the canonical Rock geological source");return nullptr;}
        if(!FCLCompiledRegionGeneration::ValidateSettings(Recipe.RegionGeneration,Error))return nullptr;
        // Resolve the unmodified base and Rock before any physical province is
        // assigned. The old neutral-placeholder path and native CDOs are frozen.
        auto Base=Recipe;Base.RegionGeneration=FCLRegionGenerationSettings();Base.Rock=FCLRockLayer();Base.SchemaVersion=18;Base.Volumes=FCLVolumeSettings();
        const auto Core=Compile(Base,Error,Library,nullptr,SurfacePatch,SurfaceLibraryOverride);if(!Core)return nullptr;
        const auto* Min=Core->Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMin");});
        const auto* Max=Core->Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMax");});
        if(!Min||!Max){Error=TEXT("Physical provinces require SurfaceMin and SurfaceMax");return nullptr;}
        auto Geology=MakeShared<FCLCompiledPlanetRecipe>(*Core);
        if(Recipe.Rock.bEnabled)
        {
            Geology->Rock=FCLCompiledRock::Compile(Recipe.Rock,Recipe.Seed,Recipe.RadiusMeters,Recipe.RadiusMeters+Min->HeightMeters,Recipe.RadiusMeters+Max->HeightMeters,Core->Surfaces.Get(),Core->Tectonics,Error,Core->Continents);
            if(!Geology->Rock)return nullptr;
        }
        const auto Generated=FCLCompiledRegionGeneration::Compile(Recipe,*Geology,Library,Error);if(!Generated)return nullptr;
        if(FCLCompileProgress::IsPreparing())return RetainInput(Geology);
        // A neutral/no-eligible selection must retain the exact prior field.
        if(!Generated->HasPhysicalRelief())
        {Geology->GeneratedRegions=Generated;return RetainInput(Geology);}
        auto Raw=MakeShared<FCLCompiledPlanetRecipe>(*(Core->UnmappedSurface?Core->UnmappedSurface:Core));
        Raw->GeneratedRegions=Generated;Raw->Rock=Geology->Rock?Geology->Rock->WithProvinceDeformation(Generated):nullptr;
        double Low=0,High=0;for(const auto& Field:Generated->Fields)if(Field){Low=FMath::Min(Low,Field->MinimumHeightMeters-Generated->NeutralHeight);High=FMath::Max(High,Field->MaximumHeightMeters-Generated->NeutralHeight);}
        const double Displacement=Generated->MaximumProvinceDisplacementMeters();
        Raw->MinimumHeightMeters=FMath::Max(-Raw->RadiusMeters,Raw->MinimumHeightMeters+Low*Generated->Strength-Displacement);
        Raw->MaximumHeightMeters=FMath::Min(0.,Raw->MaximumHeightMeters+High*Generated->Strength+Displacement);
        FCLCompileProgress::Report(ECLCompileStage::Elevation);
        const auto Range=FCLSurfaceRange::Compile(*Raw,Min->HeightMeters,Max->HeightMeters,Error);if(!Range)return nullptr;
        auto Out=MakeShared<FCLCompiledPlanetRecipe>(*Raw);Out->UnmappedSurface=Raw;Out->SurfaceRange=Range;
        Out->MinimumHeightMeters=Range->TargetMin;Out->MaximumHeightMeters=Range->TargetMax;
        return RetainInput(Out);
    }
    if(Recipe.SchemaVersion==20||Recipe.Rock.bEnabled)
    {
        if(Recipe.SchemaVersion!=20||Recipe.MeasuredTerrain.IsActive()||Recipe.Coordinates!=ECLTerrainCoordinates::SphericalSurface||LocalRegion||FCLSphericalPatches::IsFace(SurfacePatch)||!Recipe.Volumes.GeologicalUnits.IsEmpty())
        {Error=TEXT("Rock schema 20 requires a whole spherical scene and one geological source; legacy radial units and measured profiles retain their versions");return nullptr;}
        auto Base=Recipe;Base.Rock=FCLRockLayer();Base.SchemaVersion=18;
        const auto Core=Compile(Base,Error,Library,nullptr,SurfacePatch,SurfaceLibraryOverride);if(!Core)return nullptr;
        if(!Recipe.Rock.bEnabled)return RetainInput(Core);
        const auto* Min=Core->Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMin");});
        const auto* Max=Core->Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMax");});
        if(!Min||!Max){Error=TEXT("Rock Character requires the planet surface guides");return nullptr;}
        FCLCompileProgress::Report(ECLCompileStage::Geology);
        const auto Rock=FCLCompiledRock::Compile(Recipe.Rock,Recipe.Seed,Recipe.RadiusMeters,Recipe.RadiusMeters+Min->HeightMeters,Recipe.RadiusMeters+Max->HeightMeters,Core->Surfaces.Get(),Core->Tectonics,Error,Core->Continents);if(!Rock)return nullptr;
        auto Out=MakeShared<FCLCompiledPlanetRecipe>(*Core);Out->Rock=Rock;
        if(!FCLCompileProgress::IsPreparing()&&Rock->HasCarbonateSequence()&&Core->GeneratedRegions&&Recipe.RegionGeneration.AlgorithmVersion==3)
        {
            // Province placeholders have no physical relief/material, so their
            // selection can safely wait for canonical Rock evidence. Reuse the
            // same classifier and captured native neutral child; no second
            // terrain generation or mutable UObject access on the worker.
            const bool HasCarbonate=Recipe.RegionGeneration.Palette.ContainsByPredicate([](const auto& P){return P.bEnabled&&P.Suitability.Province.Type==ECLProvinceType::CarbonatePlatform;});
            if(HasCarbonate)
            {
                if(Recipe.RegionGeneration.Palette.ContainsByPredicate([](const auto& P){return P.bEnabled&&!P.Suitability.Province.bPlaceholder;}))
                {Error=TEXT("Carbonate evidence reconciliation currently requires neutral province placeholders; physical regional forms need geology-before-relief ordering");return nullptr;}
                auto Inputs=*Core->GeneratedRegions;
                const auto* Neutral=Inputs.Fields.FindByPredicate([](const auto& F){return F.IsValid();});
                if(!Neutral){Error=TEXT("Province reconciliation requires the captured native neutral field");return nullptr;}
                const auto NativeNeutral=*Neutral;for(auto& F:Inputs.Fields)if(!F)F=NativeNeutral;
                auto PlacementBase=*Out;PlacementBase.GeneratedRegions.Reset();
                Out->GeneratedRegions=FCLCompiledRegionGeneration::Compile(Recipe,PlacementBase,nullptr,Error,&Inputs);if(!Out->GeneratedRegions)return nullptr;
            }
        }
        return RetainInput(Out);
    }
    if(Recipe.MeasuredTerrain.IsActive()||!Recipe.MeasuredTerrain.Digest.IsEmpty()||Recipe.MeasuredTerrain.Revision!=1)
    {
        if(Recipe.SchemaVersion!=19||Recipe.Coordinates!=ECLTerrainCoordinates::SphericalSurface||LocalRegion||FCLSphericalPatches::IsFace(SurfacePatch)||!Recipe.NoiseLayers.IsEmpty()||!Recipe.TerrainRegions.IsEmpty()||!Recipe.BaseStructure.IsEmpty()||Recipe.Continents.bEnabled||Recipe.Tectonics.bEnabled||Recipe.RegionGeneration.bEnabled)
        {Error=TEXT("Measured schema 19 requires a whole spherical scene without height remapping/procedural terrain layers");return nullptr;}
        auto BaseRecipe=Recipe;BaseRecipe.MeasuredTerrain=FCLMeasuredTerrainReference();BaseRecipe.SchemaVersion=18;
        TSharedPtr<const FCLCompiledMeasuredTerrain> Measured;
        if(CompileRun&&!CompileRun->Preparing)Measured=CompileRun->Prepared->Measured;
        else if(CompileRun&&CompileRun->Previous&&CompileRun->Previous->Measured&&Recipe.RadiusMeters==CompileRun->Previous->Recipe.RadiusMeters&&FCLMeasuredTerrainReference::StaticStruct()->CompareScriptStruct(&Recipe.MeasuredTerrain,&CompileRun->Previous->Recipe.MeasuredTerrain,0))Measured=CompileRun->Previous->Measured;
        else Measured=FCLCompiledMeasuredTerrain::Compile(Recipe.MeasuredTerrain,Recipe.RadiusMeters,Error);
        if(!Measured)return nullptr;
        if(CompileRun&&CompileRun->Preparing)CompileRun->Prepared->Measured=Measured;
        // Native library access stays in preparation; completion replays the
        // already-resolved raw field and needs no UObject-backed override.
        auto* Surfaces=CompileRun&&!CompileRun->Preparing?nullptr:Measured->GetSurfaceLibrary();
        auto Core=Compile(BaseRecipe,Error,Library,nullptr,SurfacePatch,Surfaces);if(!Core)return nullptr;
        auto Out=MakeShared<FCLCompiledPlanetRecipe>(*Core);Out->MeasuredTerrain=Measured;Out->MinimumHeightMeters=Measured->MinimumHeightMeters;Out->MaximumHeightMeters=Measured->MaximumHeightMeters;
        return RetainInput(Out);
    }
    if(Recipe.SchemaVersion==19){Error=TEXT("Measured schema 19 requires a pinned measured resource");return nullptr;}
    if(Recipe.Volumes.bEnabled||!Recipe.Volumes.Operations.IsEmpty()||!Recipe.Volumes.GeologicalUnits.IsEmpty())
    {
        if(Recipe.SchemaVersion!=18||Recipe.Coordinates!=ECLTerrainCoordinates::SphericalSurface||LocalRegion||FCLSphericalPatches::IsFace(SurfacePatch))
        {Error=TEXT("Volume scenes require schema 18 and whole-body spherical coordinates");return nullptr;}
        auto BaseRecipe=Recipe;BaseRecipe.Volumes=FCLVolumeSettings();
        const auto Base=Compile(BaseRecipe,Error,Library,nullptr,SurfacePatch,SurfaceLibraryOverride);if(!Base)return nullptr;
        if(!Recipe.Volumes.bEnabled)return RetainInput(Base);
        const auto Volumes=FCLCompiledVolumes::Compile(Recipe.Volumes,Recipe.RadiusMeters,Base->Surfaces.Get(),Error);if(!Volumes)return nullptr;
        auto Out=MakeShared<FCLCompiledPlanetRecipe>(*Base);Out->Volumes=Volumes;return RetainInput(Out);
    }
    const auto Raw=CompileUnmapped(Recipe,Error,Library,LocalRegion,SurfacePatch,SurfaceLibraryOverride);if(!Raw)return nullptr;
    if(Recipe.SchemaVersion<17||Recipe.Coordinates!=ECLTerrainCoordinates::SphericalSurface)return RetainInput(Raw);
    const bool TectonicShape=Raw->Tectonics&&Raw->Tectonics->HasEffect();
    const bool RegionalShape=Recipe.RegionGeneration.bEnabled&&(Recipe.RegionGeneration.AlgorithmVersion<3||Recipe.RegionGeneration.Palette.ContainsByPredicate([](const auto& P){return P.bEnabled&&!P.Suitability.Province.bPlaceholder&&P.NoiseLayers.ContainsByPredicate([](const auto& L){return L.bEnabled;});}));
    const bool HasShape=(Raw->Continents&&Raw->Continents->HasEffect())||TectonicShape||!Recipe.NoiseLayers.IsEmpty()||!Recipe.BaseStructure.IsEmpty()||!Recipe.TerrainRegions.IsEmpty()||RegionalShape;
    if(!HasShape)return RetainInput(Raw); // A newly created, empty planet is a perfect sphere.
    const auto* Low=Raw->Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMin");});
    const auto* High=Raw->Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMax");});
    if(!Low||!High){Error=TEXT("Planet surface generation requires SurfaceMin and SurfaceMax");return nullptr;}
    auto Whole=Raw;
    if(FCLSphericalPatches::IsFace(SurfacePatch))Whole=CompileUnmapped(Recipe,Error,Library,nullptr,FCLSphericalPatches::WholeSphere,SurfaceLibraryOverride);
    if(!Whole)return nullptr;
    if(FCLCompileProgress::IsPreparing())return RetainInput(Raw);
    FCLCompileProgress::Report(ECLCompileStage::Elevation);
    const auto Range=FCLSurfaceRange::Compile(*Whole,Low->HeightMeters,High->HeightMeters,Error);if(!Range)return nullptr;
    TFunction<TSharedPtr<const FCLCompiledPlanetRecipe>(const TSharedPtr<const FCLCompiledPlanetRecipe>&)> Wrap;
    Wrap=[&](const auto& Input)->TSharedPtr<const FCLCompiledPlanetRecipe>
    {
        auto Out=MakeShared<FCLCompiledPlanetRecipe>(*Input);Out->UnmappedSurface=Input;Out->SurfaceRange=Range;
        Out->MinimumHeightMeters=Range->TargetMin;Out->MaximumHeightMeters=Range->TargetMax;
        Out->SurfacePatches.Reset();for(const auto& Patch:Input->SurfacePatches)Out->SurfacePatches.Add(Wrap(Patch));return Out;
    };
    return RetainInput(Wrap(Raw));
}

TSharedPtr<const FCLCompiledPlanetRecipe> FCLPlanetCompiler::CompileUnmapped(const FCLPlanetRecipe& Recipe, FString& Error, UCLStampLibraryAsset* StampLibrary, const FBox2D* LocalRegion, int32 SurfacePatch,UCLSurfaceLibraryAsset* SurfaceLibraryOverride)
{
    if(CompileRun&&!CompileRun->Preparing)
    {
        if(!CompileRun->Prepared->Raw.IsValidIndex(CompileRun->Next)){Error=TEXT("Native field snapshot is missing");return nullptr;}
        auto Raw=CompileRun->Prepared->Raw[CompileRun->Next++].Field;
        if(const auto* Deferred=CompileRun->Prepared->DeferredRegions.Find(Raw.Get()))return (*Deferred)(Error);
        return Raw;
    }
    const bool Root=!CompileRun||CompileRun->RawDepth++==0;ON_SCOPE_EXIT{if(CompileRun)--CompileRun->RawDepth;};
    if(CompileRun&&CompileRun->Preparing&&Root&&CompileRun->Previous)
    {
        for(const auto& Native:CompileRun->Previous->Raw)
        {
            const bool SameRegion=Native.Region.IsSet()==(LocalRegion!=nullptr)&&(!LocalRegion||(Native.Region->Min==LocalRegion->Min&&Native.Region->Max==LocalRegion->Max));
            if(Native.Patch!=SurfacePatch||Native.Stamps!=StampLibrary||Native.Surfaces!=SurfaceLibraryOverride||!SameRegion||!FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Native.Recipe,&Recipe,0))continue;
            CompileRun->Prepared->Raw.Add(Native);++CompileRun->Prepared->ReusedInputs;
            if(const auto* Deferred=CompileRun->Previous->DeferredRegions.Find(Native.Field.Get()))CompileRun->Prepared->DeferredRegions.Add(Native.Field.Get(),*Deferred);
            return Native.Field;
        }
    }
    auto Raw=CompileUnmappedBody(Recipe,Error,StampLibrary,LocalRegion,SurfacePatch,SurfaceLibraryOverride);
    if(Raw&&CompileRun&&CompileRun->Preparing&&Root)
    {
        FCLPreparedPlanetCompile::FNativeSnapshot Native{Recipe,{},SurfacePatch,StampLibrary,SurfaceLibraryOverride,Raw};if(LocalRegion)Native.Region=*LocalRegion;
        CompileRun->Prepared->Raw.Add(MoveTemp(Native));
    }
    return Raw;
}
namespace
{
TSharedPtr<const FCLCompiledPlanetRecipe> CompileUnmappedBody(const FCLPlanetRecipe& Recipe, FString& Error, UCLStampLibraryAsset* StampLibrary, const FBox2D* LocalRegion, int32 SurfacePatch,UCLSurfaceLibraryAsset* SurfaceLibraryOverride)
{
    Error.Reset();
    auto Fail = [&Error](const FString& Message) -> TSharedPtr<const FCLCompiledPlanetRecipe> { Error = Message; return nullptr; };
    if ((Recipe.SchemaVersion < 2 || Recipe.SchemaVersion > 18) || (Recipe.AlgorithmVersion != 1 && Recipe.AlgorithmVersion != 2) || (Recipe.AlgorithmVersion == 2 && Recipe.SchemaVersion < 3)) return Fail(TEXT("Unsupported recipe schema or algorithm version"));
    const bool HasSurfaces=Recipe.SurfaceStack.IsActive()||Recipe.RegionGeneration.Palette.ContainsByPredicate([](const auto& P){return P.SurfaceStack.IsActive();})||Recipe.TerrainRegions.ContainsByPredicate([](const auto& R){return R.SurfaceStack.IsActive();});
    if(HasSurfaces&&(Recipe.SchemaVersion<13||Recipe.Coordinates!=ECLTerrainCoordinates::SphericalSurface||!Recipe.SurfaceStack.IsActive()))return Fail(TEXT("Surface rules require schema 13 spherical terrain and a planet default surface"));
    if(!Recipe.SurfaceStack.Validate(Error))return nullptr;
    if(Recipe.RegionGeneration.bEnabled || !Recipe.RegionGeneration.Palette.IsEmpty() || !Recipe.RegionGeneration.Overrides.IsEmpty())
    {
        if(Recipe.SchemaVersion<12)return Fail(TEXT("Generated regions and Landform palettes require recipe schema 12"));
        if(!FCLCompiledRegionGeneration::ValidateSettings(Recipe.RegionGeneration,Error))return nullptr;
    }
    if(Recipe.RegionGeneration.bEnabled)
    {
        if(Recipe.Coordinates!=ECLTerrainCoordinates::SphericalSurface)return Fail(TEXT("Generated regions require spherical surface coordinates"));
        auto Plain=Recipe;Plain.RegionGeneration=FCLRegionGenerationSettings();
        const auto Core=CompileUnmappedBody(Plain,Error,StampLibrary,LocalRegion,SurfacePatch,SurfaceLibraryOverride);if(!Core)return nullptr;
        auto BaseRecipe=Plain;BaseRecipe.TerrainRegions.Reset();
        const auto Base=CompileUnmappedBody(BaseRecipe,Error,StampLibrary,nullptr,FCLSphericalPatches::WholeSphere,SurfaceLibraryOverride);if(!Base)return nullptr;
        auto AttachGenerated=[Core](const TSharedPtr<const FCLCompiledRegionGeneration>& Generated,FString& Error)->TSharedPtr<const FCLCompiledPlanetRecipe>
        {
        double Low=0,High=0;
        for(const auto& Field:Generated->Fields)if(Field){Low=FMath::Min(Low,Field->MinimumHeightMeters-Generated->NeutralHeight);High=FMath::Max(High,Field->MaximumHeightMeters-Generated->NeutralHeight);}
        TFunction<TSharedPtr<const FCLCompiledPlanetRecipe>(const TSharedPtr<const FCLCompiledPlanetRecipe>&)> Attach;
        Attach=[&](const TSharedPtr<const FCLCompiledPlanetRecipe>& In)->TSharedPtr<const FCLCompiledPlanetRecipe>
        {
            auto Out=MakeShared<FCLCompiledPlanetRecipe>(*In);Out->GeneratedRegions=Generated;Out->SurfacePatches.Reset();
            for(const auto& Patch:In->SurfacePatches)Out->SurfacePatches.Add(Attach(Patch));
            Out->MinimumHeightMeters=FMath::Clamp(In->MinimumHeightMeters+Low*Generated->Strength,-In->RadiusMeters,0.);
            Out->MaximumHeightMeters=FMath::Clamp(In->MaximumHeightMeters+High*Generated->Strength,-In->RadiusMeters,0.);return Out;
        };
        const auto Result=Attach(Core);if(Result->GetDistributedStampCount()>1024){Error=TEXT("Regional palette exceeds the 1024 unique source stamp-placement proof budget");return nullptr;};return Result;
        };
        const auto Generated=FCLCompiledRegionGeneration::Compile(Recipe,*Base,StampLibrary,Error);if(!Generated)return nullptr;
        if(FCLCompileProgress::IsPreparing())
        {
            // The placeholder never leaves preparation. Only plain native
            // children and captured recipe values enter the background task.
            auto Placeholder=MakeShared<FCLCompiledPlanetRecipe>(*Core);
            CompileRun->Prepared->DeferredRegions.Add(&Placeholder.Get(),[Recipe,Base,Generated,AttachGenerated](FString& E)
            {
                auto Regions=FCLCompiledRegionGeneration::Compile(Recipe,*Base,nullptr,E,Generated.Get());
                return Regions?AttachGenerated(Regions,E):nullptr;
            });
            return Placeholder;
        }
        return AttachGenerated(Generated,Error);
    }
    if (!FMath::IsFinite(Recipe.RadiusMeters) || Recipe.RadiusMeters < 10 || Recipe.RadiusMeters > 1.e7)
        return Fail(TEXT("RadiusMeters must be finite and between 10 and 10000000"));
    if (Recipe.RadialGuides.Guides.Num() > 128 || Recipe.NoiseLayers.Num() > 32) return Fail(TEXT("Recipe exceeds 128 guides or 32 noise layers"));
    if (!FCLPlanetCompiler::ValidateNoiseLayers(Recipe.NoiseLayers, Error)) return nullptr;
    if(Recipe.TerrainRegions.Num()>8)return Fail(TEXT("At most eight terrain regions are supported"));
    if(!Recipe.TerrainRegions.IsEmpty() && (Recipe.SchemaVersion<7 || Recipe.Coordinates==ECLTerrainCoordinates::Sphere))
        return Fail(TEXT("Terrain regions require recipe schema 7 and local coordinates"));
    if(!Recipe.BaseStructure.IsEmpty() && (Recipe.SchemaVersion<10 || Recipe.Coordinates!=ECLTerrainCoordinates::SphericalSurface))return Fail(TEXT("Base structure requires schema 10 spherical surface coordinates"));
    if(Recipe.BaseStructure.Num()>64)return Fail(TEXT("Base structure exceeds 64 broad features"));
    auto Plan = MakeShared<FCLCompiledPlanetRecipe>();
    if (Recipe.AlgorithmVersion == 2)
    {
        Plan->NativePerlin = FCLNativeNoiseKernel::Create(Error);
        if (!Plan->NativePerlin) return nullptr;
    }
    Plan->bDeferRadialBounds=Recipe.SchemaVersion>=17;
    Plan->RadiusMeters = Recipe.RadiusMeters;
    if (!FMath::IsFinite(Recipe.BaseSurfaceNormalized) || Recipe.BaseSurfaceNormalized <= 0 || Recipe.BaseSurfaceNormalized > 1)
        return Fail(TEXT("BaseSurfaceNormalized must be in (0, 1]"));
    Plan->BaseHeightMeters = (Recipe.BaseSurfaceNormalized - 1) * Recipe.RadiusMeters;
    Plan->MinimumHeightMeters = Plan->MaximumHeightMeters = Plan->BaseHeightMeters;
    Plan->Coordinates = Recipe.Coordinates; Plan->TerrainPatch = Recipe.TerrainPatch;
    if (uint8(Recipe.Coordinates) > uint8(ECLTerrainCoordinates::SphericalSurface)) return Fail(TEXT("Unknown terrain coordinates"));
    if (Recipe.Coordinates == ECLTerrainCoordinates::LocalPatch)
    {
        const auto& P = Recipe.TerrainPatch;
        if (Recipe.SchemaVersion < 6 || P.Anchor.ContainsNaN() || P.TangentX.ContainsNaN() ||
            P.Anchor.IsNearlyZero() || FVector::CrossProduct(P.Anchor,P.TangentX).IsNearlyZero() ||
            !FMath::IsFinite(P.HalfWidthMeters) || P.HalfWidthMeters < 1 ||
            !FMath::IsFinite(P.TransitionMeters) || P.TransitionMeters <= 0 || P.TransitionMeters >= P.HalfWidthMeters ||
            (!LocalRegion && P.HalfWidthMeters * FMath::Sqrt(2.) >= (Plan->RadiusMeters+Plan->BaseHeightMeters)*UE_PI))
            return Fail(TEXT("Local patch requires schema 6, a valid tangent frame, positive transition and extent before the chart antipode"));
        Plan->EvaluationRegion = LocalRegion ? *LocalRegion : FBox2D(FVector2D(-P.HalfWidthMeters),FVector2D(P.HalfWidthMeters));
        if (!Plan->EvaluationRegion.bIsValid || Plan->EvaluationRegion.Min.ContainsNaN() || Plan->EvaluationRegion.Max.ContainsNaN() ||
            Plan->EvaluationRegion.GetSize().GetMin() <= 0 || Plan->EvaluationRegion.GetSize().GetMax() > 4000 ||
            Plan->EvaluationRegion.Min.GetAbs().GetMax() > 1.e7 || Plan->EvaluationRegion.Max.GetAbs().GetMax() > 1.e7)
            return Fail(TEXT("Local evaluation region must be finite, positive and at most 4km per side"));
    }
    TMap<FName, double> Guides;
    if (!Recipe.RadialGuides.Validate(Error)) return nullptr;
    for (const FCLRadialGuide& RadialGuide : Recipe.RadialGuides.Guides)
    {
        FCLHeightGuide Guide; Guide.Name = RadialGuide.Name; Guide.Color = RadialGuide.Color;
        Guide.HeightMeters = (RadialGuide.NormalizedHeight - 1) * Recipe.RadiusMeters;
        if (Guide.Name.IsNone() || Guides.Contains(Guide.Name)) return Fail(TEXT("Height Guide names must be nonempty and unique"));
        if (!FMath::IsFinite(Guide.HeightMeters) || FMath::Abs(Guide.HeightMeters) > 1.e7 || Recipe.RadiusMeters + Guide.HeightMeters < 0 ||
            !FMath::IsFinite(Guide.Color.R) || !FMath::IsFinite(Guide.Color.G) ||
            !FMath::IsFinite(Guide.Color.B) || !FMath::IsFinite(Guide.Color.A)) return Fail(FString::Printf(TEXT("Invalid Height Guide: %s"), *Guide.Name.ToString()));
        Guides.Add(Guide.Name, Guide.HeightMeters);
        Plan->Guides.Add(Guide);
    }
    if(HasSurfaces)
    {
        Plan->Surfaces=FCLCompiledSurfaceLibrary::Resolve(Error,SurfaceLibraryOverride);if(!Plan->Surfaces)return nullptr;
        if(!FCLCompiledSurfaceStack::Compile(Recipe.SurfaceStack,*Plan->Surfaces,Plan->Guides,Recipe.RadiusMeters,Recipe.Seed,Plan->SurfaceStack,Error))return nullptr;
    }
    if(Recipe.Continents.bEnabled)
    {
        if(Recipe.Continents.AlgorithmVersion>=2&&Recipe.SchemaVersion<17)return Fail(TEXT("Guided continents requires recipe schema 17"));
        if(Recipe.SchemaVersion<15||Recipe.Coordinates!=ECLTerrainCoordinates::SphericalSurface)return Fail(TEXT("Continental layer requires schema 15 spherical coordinates"));
        const auto* L=Guides.Find(Recipe.Continents.LowerGuide);const auto* U=Guides.Find(Recipe.Continents.UpperGuide);
        if(!L||!U)return Fail(TEXT("Continental layer requires both named radial guides"));
        const double Span=Recipe.SchemaVersion==16?FMath::Min(*U-*L,Recipe.RadiusMeters*Recipe.BaseSurfaceNormalized*.25):*U-*L;
        const auto* Sea=Guides.Find(TEXT("OceanHeight"));const double Datum=Recipe.SchemaVersion>=16&&Sea?*Sea-Plan->BaseHeightMeters:0;
        Plan->Continents=FCLCompiledContinents::Compile(Recipe.Continents,Recipe.Seed,Span,Error,Recipe.SchemaVersion>=16,Datum);if(!Plan->Continents)return nullptr;
    }
    if(Recipe.Tectonics.bEnabled)
    {
        if(Recipe.Tectonics.AlgorithmVersion>=4&&Recipe.SchemaVersion<17)return Fail(TEXT("Guided tectonics requires recipe schema 17"));
        if(Recipe.SchemaVersion<14||Recipe.Coordinates!=ECLTerrainCoordinates::SphericalSurface)return Fail(TEXT("Tectonic preset requires schema 14 spherical coordinates"));
        const auto* L=Guides.Find(Recipe.Tectonics.LowerGuide);const auto* U=Guides.Find(Recipe.Tectonics.UpperGuide);
        if(!L||!U)return Fail(TEXT("Tectonic preset requires both named radial guides"));
        const double Span=Recipe.SchemaVersion==16?FMath::Min(*U-*L,Recipe.RadiusMeters*Recipe.BaseSurfaceNormalized*.25):*U-*L;
        Plan->Tectonics=FCLCompiledTectonics::Compile(Recipe.Tectonics,int32(uint32(Recipe.Seed)^(Recipe.SchemaVersion>=15?uint32(Recipe.Tectonics.Seed):0U)),Span,Error,Recipe.SchemaVersion>=15,Plan->Continents&&Plan->Continents->HasEffect()?Plan->Continents:nullptr,Recipe.SchemaVersion>=16);if(!Plan->Tectonics)return nullptr;
    }
    Plan->SurfacePatch=SurfacePatch;
    if(Recipe.Coordinates==ECLTerrainCoordinates::SphericalSurface)
    {
        if(Recipe.SchemaVersion<8 || (SurfacePatch!=INDEX_NONE && SurfacePatch!=FCLSphericalPatches::WholeSphere && !FCLSphericalPatches::IsFace(SurfacePatch)))
            return Fail(TEXT("Spherical surface fields require schema 8 and a valid patch request"));
        if(SurfacePatch==INDEX_NONE)
        {
            for(int32 Face=0;Face<6;++Face)
            {
                const auto Patch=CompileUnmappedBody(Recipe,Error,StampLibrary,nullptr,Face,SurfaceLibraryOverride);if(!Patch)return nullptr;
                Plan->SurfacePatches.Add(Patch);Plan->MinimumHeightMeters=FMath::Min(Plan->MinimumHeightMeters,Patch->MinimumHeightMeters);
                Plan->MaximumHeightMeters=FMath::Max(Plan->MaximumHeightMeters,Patch->MaximumHeightMeters);
            }
            if(Plan->GetDistributedStampCount()>1024)return Fail(TEXT("Spherical atlas exceeds the 1024 unique stamp-placement budget"));
            return Plan;
        }
    }
    auto Resolve = [&](const FCLHeightReference& Ref, double& Value) -> bool
    {
        Value = Ref.OffsetMeters;
        const double* Lower = Ref.Guide.IsNone() ? nullptr : Guides.Find(Ref.Guide);
        if (!Ref.Guide.IsNone() && !Lower) { Error = TEXT("Missing required Height Guide: ") + Ref.Guide.ToString(); return false; }
        if (Ref.Mode == ECLHeightReferenceMode::Height) { if (Lower) Value += *Lower; }
        else
        {
            if (Recipe.SchemaVersion < 3) { Error = TEXT("Guide intervals require recipe schema 3"); return false; }
            const double* Upper = Guides.Find(Ref.UpperGuide);
            if (!Upper) { Error = TEXT("Missing required Height Guide: ") + Ref.UpperGuide.ToString(); return false; }
            if (*Lower > *Upper) { Error = TEXT("Guide interval is reversed: ") + Ref.Guide.ToString() + TEXT(" / ") + Ref.UpperGuide.ToString(); return false; }
            Value += Ref.Mode == ECLHeightReferenceMode::IntervalHeight ? FMath::Lerp(*Lower, *Upper, Ref.Fraction) : (*Upper - *Lower) * Ref.Fraction;
        }
        if (!FMath::IsFinite(Value)) { Error = TEXT("Height reference overflow"); return false; }
        return true;
    };
    TSet<FGuid> FeatureIds;
    for(const auto& Feature:Recipe.BaseStructure)
    {
        if(!Feature.Id.IsValid() || FeatureIds.Contains(Feature.Id) || Feature.Name.TrimStartAndEnd().IsEmpty() ||
           Feature.LocationDegrees.ContainsNaN() || FMath::Abs(Feature.LocationDegrees.X)>180 || FMath::Abs(Feature.LocationDegrees.Y)>90 ||
           !FMath::IsFinite(Feature.HeadingDegrees) || Feature.HalfExtentDegrees.ContainsNaN() || Feature.HalfExtentDegrees.GetMin()<.1 || Feature.HalfExtentDegrees.GetMax()>=85 ||
           !Feature.Relief.Validate(Error) || Feature.Relief.Mode!=ECLHeightReferenceMode::IntervalDelta)
            return Fail(TEXT("Invalid broad feature identity, spherical placement, extent or guide-relative delta"));
        FeatureIds.Add(Feature.Id);if(!Feature.bEnabled)continue;
        FCLCompiledPlanetFeature F;F.Id=Feature.Id;F.Orientation=FCLCompiledPlanetFeature::Frame(Feature.LocationDegrees,Feature.HeadingDegrees);
        F.HalfExtentRadians=Feature.HalfExtentDegrees*(UE_PI/180);
        if(!Resolve(Feature.Relief,F.ReliefMeters))return nullptr;
        if(Feature.Profile==ECLPlanetFeatureProfile::Heightmap)
        {
            if(Recipe.SchemaVersion<11)return Fail(TEXT("Heightmap structure requires recipe schema 11"));
            F.Heightmap=FCLStampDistributionCompiler::CompileFixed(Feature,*Plan,StampLibrary,F.ReliefMeters,Error);if(!F.Heightmap)return nullptr;
        }
        else if(Feature.Profile!=ECLPlanetFeatureProfile::Oval)return Fail(TEXT("Unknown planet structure profile"));
        Plan->BaseStructure.Add(F);
    }
    Plan->BaseStructure.Sort([](const auto& A,const auto& B){return A.Id.ToString(EGuidFormats::Digits)<B.Id.ToString(EGuidFormats::Digits);});
    for (const FCLNoiseLayer& Layer : Recipe.NoiseLayers)
    {
        if (!Layer.bEnabled) continue;
        FCLCompiledPlanetRecipe::FLayer Compiled; Compiled.Operation = Layer.Operation;
        double& Low = Plan->MinimumHeightMeters; double& High = Plan->MaximumHeightMeters;
        if(Layer.Operation==ECLTerrainOperation::StampDistribution)
        {
            if(Recipe.SchemaVersion<5)return Fail(TEXT("Stamp distributions require recipe schema 5"));
            uint32 Seed=Mix(uint32(Recipe.Seed)^Mix(uint32(Layer.Distribution.Seed)));
            for(TCHAR C:Layer.Name.ToString().ToLower())Seed=Mix(Seed^uint32(C));
            auto Distribution = Layer.Distribution;
            double ReliefLow=0,ReliefHigh=0;
            if (Distribution.ReliefMode == ECLStampReliefMode::GuideInterval)
            {
                if (Recipe.SchemaVersion < 6) return Fail(TEXT("Guide-based stamp relief requires recipe schema 6"));
                if (!Resolve(Distribution.MinimumRelief,ReliefLow) || !Resolve(Distribution.MaximumRelief,ReliefHigh)) return nullptr;
                if (ReliefLow <= 0 || ReliefHigh < ReliefLow) return Fail(TEXT("Stamp relief must resolve to positive, ordered deltas"));
            }
            Compiled.Distribution=FCLStampDistributionCompiler::Compile(Distribution,int32(Seed),*Plan,StampLibrary,Error,ReliefLow,ReliefHigh);
            if(!Compiled.Distribution)return nullptr;
            Low+=Compiled.Distribution->MinimumDeltaMeters;High+=Compiled.Distribution->MaximumDeltaMeters;
            int32 Total=Compiled.Distribution->Placements.Num();
            for(const auto& Existing:Plan->Layers)if(Existing.Distribution)Total+=Existing.Distribution->Placements.Num();
            if(Total>1024)return Fail(TEXT("Recipe exceeds the 1024 resolved stamp-placement proof budget"));
        }
        else if (Layer.Operation == ECLTerrainOperation::Terrace)
        {
            if (Recipe.SchemaVersion < 4) return Fail(TEXT("Terracing requires recipe schema 4"));
            if (!Resolve(Layer.Terrace.StepHeight, Compiled.StepHeight) || !Resolve(Layer.Terrace.Origin, Compiled.Origin)) return nullptr;
            if (Compiled.StepHeight < .1 || Compiled.StepHeight > 1.e7)
                return Fail(TEXT("Terrace step height must resolve to between 0.1 and 10000000 metres: ") + Layer.Name.ToString());
            Compiled.Softness = Layer.Terrace.Softness; Compiled.Strength = Layer.Terrace.Strength;
            Low = Compiled.TerraceHeight(Low); High = Compiled.TerraceHeight(High);
        }
        else
        {
            if (!Resolve(Layer.Minimum, Compiled.Minimum) || !Resolve(Layer.Maximum, Compiled.Maximum)) return nullptr;
            if (Compiled.Minimum > Compiled.Maximum) return Fail(TEXT("Noise minimum must not exceed maximum: ") + Layer.Name.ToString());
            if (Layer.Scale == ECLNoiseScale::PlanetRadius && Recipe.SchemaVersion < 3)
                return Fail(TEXT("Planet-relative wavelengths require recipe schema 3"));
            const double Wavelength = Layer.Scale == ECLNoiseScale::PlanetRadius ? Layer.WavelengthRadiusFraction * Recipe.RadiusMeters : Layer.WavelengthMeters;
            if (!FMath::IsFinite(Wavelength) || Wavelength < .1 || Recipe.RadiusMeters / Wavelength * (1 << (Layer.Octaves - 1)) > 1.e6)
                return Fail(TEXT("Noise wavelength/octaves exceed supported precision or minimum wavelength"));
            Compiled.Wavelength = Wavelength; Compiled.Persistence = Layer.Persistence;
            Compiled.Octaves = Layer.Octaves; Compiled.Shape = Layer.Shape; Compiled.Blend = Layer.Blend;
            uint32 Seed = Mix(uint32(Recipe.Seed) ^ Mix(uint32(Layer.Seed)));
            // Explicit seed, independent of array order or process-local FName indices.
            for (TCHAR C : Layer.Name.ToString().ToLower()) Seed = Mix(Seed ^ uint32(C));
            Compiled.Seed = static_cast<int32>(Seed);
            Compiled.Offset = FVector(Mix(Seed) % 8192 + .37, Mix(Seed + 1) % 8192 + .59, Mix(Seed + 2) % 8192 + .83);
            switch (Compiled.Blend)
            {
            case ECLNoiseBlend::Override: Low = Compiled.Minimum; High = Compiled.Maximum; break;
            case ECLNoiseBlend::Add: Low += Compiled.Minimum; High += Compiled.Maximum; break;
            case ECLNoiseBlend::Max: Low = FMath::Max(Low, Compiled.Minimum); High = FMath::Max(High, Compiled.Maximum); break;
            case ECLNoiseBlend::Min: Low = FMath::Min(Low, Compiled.Minimum); High = FMath::Min(High, Compiled.Maximum); break;
            }
        }
        if (!FMath::IsFinite(Low) || !FMath::IsFinite(High) || Low + Plan->RadiusMeters <= .01 || FMath::Max(FMath::Abs(Low), FMath::Abs(High)) > 1.e7)
            return Fail(TEXT("Noise stack exceeds supported radial bounds"));
        Plan->Layers.Add(Compiled);
        if(Plan->GetDistributedStampCount()>FCLStampDistributionSettings::MaximumPlacements)return Fail(TEXT("Terrain exceeds the 1024 total stamp-placement budget"));
    }
    auto Regions=Recipe.TerrainRegions;
    Regions.Sort([](const auto& A,const auto& B){return A.Priority!=B.Priority ? A.Priority<B.Priority : A.Id.ToString(EGuidFormats::Digits)<B.Id.ToString(EGuidFormats::Digits);});
    TSet<FGuid> RegionIds;
    for(const auto& Region:Regions)
    {
        if(!Region.Id.IsValid() || RegionIds.Contains(Region.Id) || Region.Name.TrimStartAndEnd().IsEmpty())
            return Fail(TEXT("Terrain region IDs must be valid and unique, with nonempty names"));
        RegionIds.Add(Region.Id);
        if(Recipe.Coordinates==ECLTerrainCoordinates::SphericalSurface)
        {if(Region.bPlacedLandform){FCLCompiledLandformPlacement P;if(Recipe.SchemaVersion<11){Error=TEXT("Placed Landforms require schema 11");return nullptr;}if(!FCLCompiledLandformPlacement::Resolve(Region.Placement,Recipe.BaseStructure,P,Error))return nullptr;}
        else if(!Region.bSphericalMask||!Region.SphericalMask.Validate(Plan->RadiusMeters+Plan->BaseHeightMeters,Error)){if(Error.IsEmpty())Error=TEXT("Spherical regions need spherical masks");return nullptr;}}
        else if(Region.bPlacedLandform || Region.bSphericalMask || !Region.Mask.Validate(Error)){if(Error.IsEmpty())Error=TEXT("Local regions need local masks");return nullptr;}
        if(!Region.bEnabled)continue;
        if(!Region.TerrainTypeId.IsValid() || Region.TerrainTypeName.IsEmpty())return Fail(TEXT("Terrain region requires an applied source snapshot: ")+Region.Name);
        auto Child=Recipe;Child.BaseStructure.Reset();Child.Tectonics=FCLTectonicPreset();Child.Continents=FCLContinentalLayer();Child.SchemaVersion=Recipe.Coordinates==ECLTerrainCoordinates::SphericalSurface?8:6;Child.TerrainRegions.Reset();Child.AlgorithmVersion=Region.AlgorithmVersion;Child.SurfaceStack=Region.SurfaceStack;if(Child.SurfaceStack.IsActive())Child.SchemaVersion=13;
        Child.Seed=int32(uint32(Recipe.Seed)+uint32(Region.SeedOffset));Child.TerrainTypeId=Region.TerrainTypeId;
        Child.TerrainTypeName=Region.TerrainTypeName;Child.NoiseLayers=Region.NoiseLayers;
        const auto Field=CompileUnmappedBody(Child,Error,StampLibrary,Recipe.Coordinates==ECLTerrainCoordinates::LocalPatch?&Plan->EvaluationRegion:nullptr,Region.bPlacedLandform?FCLSphericalPatches::WholeSphere:SurfacePatch,SurfaceLibraryOverride);
        if(!Field){Error=Region.Name+TEXT(": ")+Error;return nullptr;}
        FCLCompiledPlanetRecipe::FRegion CompiledRegion;CompiledRegion.Id=Region.Id;CompiledRegion.Mask=Region.Mask;CompiledRegion.SphericalMask=Region.SphericalMask;CompiledRegion.Field=Field;
        CompiledRegion.bPlacedLandform=Region.bPlacedLandform;
        if(Region.bPlacedLandform && !FCLCompiledLandformPlacement::Resolve(Region.Placement,Recipe.BaseStructure,CompiledRegion.Placement,Error))return nullptr;
        Plan->Regions.Add(MoveTemp(CompiledRegion));
        Plan->MinimumHeightMeters=FMath::Min(Plan->MinimumHeightMeters,Field->MinimumHeightMeters);
        Plan->MaximumHeightMeters=FMath::Max(Plan->MaximumHeightMeters,Field->MaximumHeightMeters);
        if(Plan->GetDistributedStampCount()>1024)return Fail(TEXT("Composed recipe exceeds the 1024 stamp-placement budget"));
    }
    if (Plan->Coordinates==ECLTerrainCoordinates::LocalPatch)
    { Plan->MinimumHeightMeters=FMath::Min(Plan->MinimumHeightMeters,Plan->BaseHeightMeters); Plan->MaximumHeightMeters=FMath::Max(Plan->MaximumHeightMeters,Plan->BaseHeightMeters); }
    Plan->MinimumHeightMeters = FMath::Min(Plan->MinimumHeightMeters, 0.0);
    Plan->MaximumHeightMeters = FMath::Min(Plan->MaximumHeightMeters, 0.0);
    if(Plan->Tectonics){Plan->MinimumHeightMeters-=Plan->Tectonics->MaximumDeltaMeters;Plan->MaximumHeightMeters+=Plan->Tectonics->MaximumDeltaMeters;}
    if(Plan->Continents){Plan->MinimumHeightMeters-=Plan->Continents->MaximumDeltaMeters;Plan->MaximumHeightMeters+=Plan->Continents->MaximumDeltaMeters;}
    for(const auto& F:Plan->BaseStructure){Plan->MinimumHeightMeters+=FMath::Min(0.,F.ReliefMeters);Plan->MaximumHeightMeters+=FMath::Max(0.,F.ReliefMeters);}
    Plan->MinimumHeightMeters=FMath::Clamp(Plan->MinimumHeightMeters,-Plan->RadiusMeters,0.);Plan->MaximumHeightMeters=FMath::Clamp(Plan->MaximumHeightMeters,-Plan->RadiusMeters,0.);
    return Plan;
}
}


FCLPlanetRecipe FCLPlanetCompiler::RollingPlains()
{
    FCLPlanetRecipe Recipe;
    Recipe.RadialGuides = FCLRadialGuidePresets::Defaults()[0];
    FCLNoiseLayer Broad; Broad.Name = TEXT("Broad undulations"); Broad.Minimum.Guide = TEXT("SurfaceMin");
    Broad.Maximum.Guide = TEXT("SurfaceMax"); Broad.WavelengthMeters = 60; Recipe.NoiseLayers.Add(Broad);
    FCLNoiseLayer Detail; Detail.Name = TEXT("Ground detail"); Detail.Seed = 12; Detail.Blend = ECLNoiseBlend::Add;
    Detail.Minimum.OffsetMeters = -.2; Detail.Maximum.OffsetMeters = .2; Detail.WavelengthMeters = 8; Detail.Octaves = 2;
    Recipe.NoiseLayers.Add(Detail);
    return Recipe;
}

FCLPlanetRecipe FCLPlanetCompiler::BasePlanet()
{
    FCLPlanetRecipe R;R.Name=TEXT("New planet");R.RadiusMeters=1000;R.BaseSurfaceNormalized=.45;R.Seed=1729;
    R.Coordinates=ECLTerrainCoordinates::SphericalSurface;R.AlgorithmVersion=2;R.RadialGuides=FCLRadialGuidePresets::Defaults()[0];return R;
}

FCLPlanetRecipe FCLPlanetCompiler::TectonicBase()
{auto R=BasePlanet();R.SchemaVersion=14;R.Name=TEXT("Tectonic rocky world");R.Tectonics.bEnabled=true;R.Tectonics.AlgorithmVersion=2;return R;}

FCLPlanetRecipe FCLPlanetCompiler::ContinentalBase()
{auto R=TectonicBase();R.Name=TEXT("Continental rocky world");R.Tectonics.AlgorithmVersion=3;R.Tectonics.Activity=.7;R.SurfaceStack=FCLSurfaceStack::MakeDefault();return R;}

void FCLPlanetRecipe::MigrateLegacyGuides()
{
    if (HeightGuides_DEPRECATED.IsEmpty() && SchemaVersion != 1) return;
    const double OldRadius = ReferenceRadiusMeters_DEPRECATED;
    TMap<FName, double> Old; double Top = 0;
    for (const auto& G : HeightGuides_DEPRECATED) { Old.Add(G.Name, G.HeightMeters); Top = FMath::Max(Top, G.HeightMeters); }
    auto Resolve = [&](const FCLHeightReference& Ref) { return Ref.OffsetMeters + Old.FindRef(Ref.Guide); };
    double High = 0;
    for (const FCLNoiseLayer& L : NoiseLayers) if (L.bEnabled)
    {
        const double V = Resolve(L.Maximum);
        switch (L.Blend)
        {
        case ECLNoiseBlend::Override: High = V; break;
        case ECLNoiseBlend::Add: High += V; break;
        case ECLNoiseBlend::Min: High = FMath::Min(High, V); break;
        case ECLNoiseBlend::Max: High = FMath::Max(High, V); break;
        }
    }
    // Retain the old physical terrain while moving its radius to a sky boundary.
    RadiusMeters = OldRadius + FMath::Max(Top, High) + OldRadius * .05;
    const double Shift = RadiusMeters - OldRadius;
    BaseSurfaceNormalized = OldRadius / RadiusMeters;
    RadialGuides.Name = TEXT("Lab plains"); RadialGuides.Id = FGuid(0x434C0001, 0, 0, 2); RadialGuides.Guides.Reset();
    for (const FCLHeightGuide& G : HeightGuides_DEPRECATED)
    { FCLRadialGuide S; S.Name = G.Name; S.NormalizedHeight = (OldRadius + G.HeightMeters) / RadiusMeters;
      S.Color = G.Color; RadialGuides.Guides.Add(S); }
    for (FCLNoiseLayer& L : NoiseLayers)
    {
        L.WavelengthMeters *= RadiusMeters / OldRadius;
        for (FCLHeightReference* Ref : {&L.Minimum, &L.Maximum})
        {
            if (L.Blend == ECLNoiseBlend::Add && !Ref->Guide.IsNone()) Ref->OffsetMeters += Shift;
            if (L.Blend != ECLNoiseBlend::Add && Ref->Guide.IsNone()) Ref->OffsetMeters -= Shift;
        }
    }
    HeightGuides_DEPRECATED.Reset(); SchemaVersion = 2;
}
void UCLPlanetRecipeAsset::Serialize(FArchive& Ar)
{
    if (Ar.IsLoading() && Ar.IsPersistent() && !Ar.IsTransacting() && Ar.CustomVer(RecipeAssetVersion) < 1 && !HasAnyFlags(RF_ClassDefaultObject))
    {
        // UE omits fields equal to native defaults. Restore the old defaults
        // before tagged properties load, so missing tags do not adopt new ones.
        Recipe = FCLPlanetCompiler::RollingPlains(); Recipe.SchemaVersion = 1;
        Recipe.RadialGuides = FCLRadialGuideSet(); Recipe.ReferenceRadiusMeters_DEPRECATED = 100;
        Recipe.HeightGuides_DEPRECATED.Reset();
        const FName Names[] = {TEXT("OceanHeight"), TEXT("SurfaceMin"), TEXT("SurfaceMax"), TEXT("AtmosphereMax")};
        const double Heights[] = {0, 1, 7, 20};
        const FLinearColor Colors[] = {FLinearColor(.1, .55, 1), FLinearColor(.8, .65, .3), FLinearColor(.4, .9, .45), FLinearColor(.5, .7, 1)};
        for (int32 I = 0; I < 4; ++I)
        { FCLHeightGuide G; G.Name = Names[I]; G.HeightMeters = Heights[I]; G.Color = Colors[I]; Recipe.HeightGuides_DEPRECATED.Add(G); }
    }
    if(Ar.IsLoading() && Ar.IsPersistent() && !Ar.IsTransacting() && Ar.CustomVer(RecipeAssetVersion)>=1 && Ar.CustomVer(RecipeAssetVersion)<2 && !HasAnyFlags(RF_ClassDefaultObject))
    {
        // Pre-structure assets omitted schema 6 when equal to the old native default.
        Recipe=FCLPlanetCompiler::RollingPlains();Recipe.SchemaVersion=6;
    }
    if(Ar.IsLoading() && Ar.IsPersistent() && !Ar.IsTransacting() && Ar.CustomVer(RecipeAssetVersion)==2 && !HasAnyFlags(RF_ClassDefaultObject))
    {Recipe=FCLPlanetCompiler::RollingPlains();Recipe.SchemaVersion=10;}
    Ar.UsingCustomVersion(RecipeAssetVersion);
    Super::Serialize(Ar);
}
void UCLPlanetRecipeAsset::PostLoad()
{
    Super::PostLoad();Recipe.MigrateLegacyGuides();Recipe.Caves.UpgradeAbundanceProfile();if(Recipe.SchemaVersion>=2&&Recipe.SchemaVersion<9)Recipe.SchemaVersion=9;Version.SchemaVersion=Recipe.SchemaVersion;
    FString Error;SynchronizeRadialGuides(Error);
#if WITH_EDITOR
    BindGuideLibrary();
#endif
}
UCLPlanetRecipeAsset::UCLPlanetRecipeAsset()
{
    Recipe=FCLPlanetCompiler::RollingPlains();
    // UE delta-serializes against the CDO. Keep its historical cave default so
    // an omitted version-2 property in an existing package still loads as 2.
    // Fresh factories assign BasePlanet (current profile); knob changes use 3.
    Recipe.Caves.AlgorithmVersion=2;
}
FString UCLPlanetRecipeAsset::GetSummaryText() const
{ return FString::Printf(TEXT("%s: %d guides, %d terrain layers"), *Recipe.Name, Recipe.RadialGuides.Guides.Num(), Recipe.NoiseLayers.Num()); }
bool UCLPlanetRecipeAsset::ValidatePayload(FString& Error) const
{ return const_cast<UCLPlanetRecipeAsset*>(this)->SynchronizeRadialGuides(Error) && FCLPlanetCompiler::Compile(Recipe,Error).IsValid(); }
void UCLPlanetRecipeAsset::CollectValidationMessages(TArray<FCFValidationMessage>& Messages) const
{
    Super::CollectValidationMessages(Messages);
    FString Error;
    if (!ValidatePayload(Error)) Messages.Add(FCFValidationMessage::Make(ECFValidationSeverity::Error, TEXT("CL.Recipe.Invalid"),
        FText::FromString(Error), FText(), TEXT("/Recipe")));
}
bool UCLPlanetRecipeAsset::ImportRecipeJson(const FString& Json,FString& Error)
{TSharedPtr<const FCLCompiledPlanetRecipe> Plan;return ImportRecipeJsonPrepared(Json,Error,Plan);}
bool UCLPlanetRecipeAsset::ImportRecipeJsonPrepared(const FString& Json,FString& Error,TSharedPtr<const FCLCompiledPlanetRecipe>& Plan)
{
    Plan.Reset();Error.Reset();
    if (Json.Len() > 256 * 1024) { Error = TEXT("Recipe JSON exceeds 256 KiB"); return false; }
    TSharedPtr<FJsonObject> Object;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object)
    { Error = TEXT("Invalid recipe JSON object"); return false; }
    double Schema = 0, Algorithm = 0;
    if (!Object->TryGetNumberField(TEXT("schemaVersion"), Schema) || !Object->TryGetNumberField(TEXT("algorithmVersion"), Algorithm) || (Schema != 1 && Schema != 2 && Schema != 3 && Schema != 4 && Schema != 5 && Schema != 6 && Schema != 7 && Schema != 8 && Schema != 9 && Schema != 10 && Schema != 11 && Schema != 12 && Schema != 13 && Schema != 14 && Schema != 15 && Schema != 16 && Schema != 17 && Schema != 18 && Schema != 19 && Schema != 20 && Schema != 21 && Schema != 22 && Schema != 23) || (Algorithm != 1 && Algorithm != 2) || (Algorithm == 2 && Schema < 3))
    { Error = TEXT("Recipe requires schemaVersion 1..23 with algorithm 1, or schemaVersion 3..23 with algorithm 2"); return false; }
    // Schema 9 gives the guide model its canonical names. Legacy spelling is
    // accepted only for older documents, with ambiguous aliases rejected.
    if (Schema < 9)
    {
        auto Rename = [&](FJsonObject& O,const TCHAR* Old,const TCHAR* New) -> bool
        {
            FString OldKey,NewKey;
            for(const auto& Pair:O.Values)
            {
                if(Pair.Key.Equals(Old,ESearchCase::IgnoreCase))OldKey=Pair.Key;
                if(Pair.Key.Equals(New,ESearchCase::IgnoreCase))NewKey=Pair.Key;
            }
            if(OldKey.IsEmpty())return true;
            if(!NewKey.IsEmpty()){Error=TEXT("Ambiguous legacy and canonical radial guide fields");return false;}
            auto Value=O.Values[OldKey];O.RemoveField(OldKey);O.SetField(New,Value);return true;
        };
        if(!Rename(*Object,TEXT("strata"),TEXT("radialGuides")))return false;
        const TSharedPtr<FJsonObject>* Set=nullptr;
        if(Object->TryGetObjectField(TEXT("radialGuides"),Set) && !Rename(**Set,TEXT("stratum"),TEXT("guides")))return false;
    }
    if (Schema == 1)
    {
        // Explicit, lossless conversion of the old metre-based document.
        const TArray<TSharedPtr<FJsonValue>>* Old = nullptr;
        FCLPlanetRecipe Legacy; Legacy.SchemaVersion = 1; Legacy.RadialGuides.Guides.Reset();
        if (Object->TryGetArrayField(TEXT("heightGuides"), Old))
            for (const auto& V : *Old)
            {
                FCLHeightGuide G;
                if (V->Type != EJson::Object || !CLModelJson::CheckFields(*V->AsObject(), *FCLHeightGuide::StaticStruct(), Error, TEXT("heightGuides")) ||
                    !FJsonObjectConverter::JsonObjectToUStruct(V->AsObject().ToSharedRef(), &G)) return false;
                Legacy.HeightGuides_DEPRECATED.Add(G);
            }
        Object->TryGetNumberField(TEXT("referenceRadiusMeters"), Legacy.ReferenceRadiusMeters_DEPRECATED);
        // Convert the entire v1 payload so base, noise scales and absolute references migrate together.
        TSharedPtr<FJsonObject> OldObject = MakeShared<FJsonObject>(*Object);
        OldObject->RemoveField(TEXT("heightGuides")); OldObject->RemoveField(TEXT("referenceRadiusMeters"));
        if (!CLModelJson::CheckFields(*OldObject, *FCLPlanetRecipe::StaticStruct(), Error, TEXT("Recipe"))) return false;
        const auto SavedGuides = Legacy.HeightGuides_DEPRECATED;
        if (!FJsonObjectConverter::JsonObjectToUStruct(OldObject.ToSharedRef(), &Legacy)) return false;
        Legacy.HeightGuides_DEPRECATED = SavedGuides;
        Legacy.MigrateLegacyGuides();
        Object = FJsonObjectConverter::UStructToJsonObject(Legacy);
    }
    if (!CLModelJson::CheckFields(*Object, *FCLPlanetRecipe::StaticStruct(), Error, TEXT("Recipe"))) return false;
    // Fresh candidate prevents omitted fields inheriting previous document values.
    FCLPlanetRecipe Candidate;
    FText Reason;
    if (!FJsonObjectConverter::JsonObjectToUStruct(Object.ToSharedRef(), &Candidate, 0, 0, false, &Reason))
    { Error = Reason.ToString(); return false; }
    Candidate.Caves.UpgradeAbundanceProfile();
    const auto Accepted=FCLPlanetCompiler::Compile(Candidate,Error);if(!Accepted)return false;
    if (Candidate.SchemaVersion >= 2 && Candidate.SchemaVersion < 9) Candidate.SchemaVersion = 9;
    Modify();PaletteSources.Reset();LandformSources.Reset(); RadialGuideLibrary=nullptr;RadialGuideSetId.Invalidate();Recipe = MoveTemp(Candidate);
#if WITH_EDITOR
    BindGuideLibrary();
#endif
#if WITH_EDITOR
    PostEditChange(); MarkPackageDirty();
#endif
    Plan=Accepted;return true;
}
#if WITH_EDITOR
void UCLPlanetRecipeAsset::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
    if(Event.ChangeType & (EPropertyChangeType::ArrayAdd|EPropertyChangeType::Duplicate))
    {
        const int32 I=Event.GetArrayIndex(TEXT("BaseStructure"));
        if(Recipe.BaseStructure.IsValidIndex(I)){Recipe.BaseStructure[I].Id=FGuid::NewGuid();Recipe.SchemaVersion=FMath::Max(11,Recipe.SchemaVersion);}
        const int32 Solid=Event.GetArrayIndex(TEXT("Operations"));
        if(Recipe.Volumes.Operations.IsValidIndex(Solid))
        {auto& V=Recipe.Volumes.Operations[Solid];V.Id=FGuid::NewGuid();if(V.Name.IsNone())V.Name=TEXT("Solid form");}
    }
    FString GuideError;SynchronizeRadialGuides(GuideError);BindGuideLibrary();
    Super::PostEditChangeProperty(Event);if(!(Event.ChangeType & EPropertyChangeType::Interactive))OnRecipeChanged.Broadcast();
}
void UCLPlanetRecipeAsset::PostEditUndo()
{ Super::PostEditUndo(); FString Error;SynchronizeRadialGuides(Error);BindGuideLibrary();OnRecipeChanged.Broadcast(); }
#endif

void FCLPlanetRecipe::RenameGuideReferences(FName Old,FName New)
{
    auto Layers=[&](TArray<FCLNoiseLayer>& Values)
    {
        for(auto& L:Values)for(auto* Ref:{&L.Minimum,&L.Maximum,&L.Terrace.StepHeight,&L.Terrace.Origin,&L.Distribution.MinimumRelief,&L.Distribution.MaximumRelief})
        {if(Ref->Guide==Old)Ref->Guide=New;if(Ref->UpperGuide==Old)Ref->UpperGuide=New;}
    };
    SurfaceStack.RenameGuide(Old,New);Layers(NoiseLayers);for(auto& R:TerrainRegions){Layers(R.NoiseLayers);R.SurfaceStack.RenameGuide(Old,New);}
    for(auto& P:RegionGeneration.Palette){P.SurfaceStack.RenameGuide(Old,New);Layers(P.NoiseLayers);if(P.Suitability.LowerElevationGuide==Old)P.Suitability.LowerElevationGuide=New;if(P.Suitability.UpperElevationGuide==Old)P.Suitability.UpperElevationGuide=New;}
    if(Continents.LowerGuide==Old)Continents.LowerGuide=New;if(Continents.UpperGuide==Old)Continents.UpperGuide=New;
    if(Tectonics.LowerGuide==Old)Tectonics.LowerGuide=New;if(Tectonics.UpperGuide==Old)Tectonics.UpperGuide=New;
    for(auto& F:BaseStructure){if(F.Relief.Guide==Old)F.Relief.Guide=New;if(F.Relief.UpperGuide==Old)F.Relief.UpperGuide=New;}
}
bool UCLPlanetRecipeAsset::SetRadialGuideSet(UCLRadialGuideLibraryAsset* Library,FGuid Id,FString& Error)
{
    Error.Reset();if(!Library){RadialGuideLibrary=nullptr;RadialGuideSetId.Invalidate();
#if WITH_EDITOR
        BindGuideLibrary();
#endif
        return true;}
    if(!Library->FindSet(Id)){Error=TEXT("Radial guide set reference could not be resolved");return false;}
    if(!Library->FindSet(Id)->Validate(Error))return false;
    RadialGuideLibrary=Library;RadialGuideSetId=Id;Recipe.RadialGuides=*Library->FindSet(Id);
#if WITH_EDITOR
    BindGuideLibrary();
#endif
    return true;
}
bool UCLPlanetRecipeAsset::SynchronizeRadialGuides(FString& Error)
{
    Error.Reset();if(!RadialGuideLibrary)return true;
    const auto* Set=RadialGuideLibrary->FindSet(RadialGuideSetId);
    if(!Set){Error=TEXT("Referenced radial guide set is missing from its catalog");return false;}
    if(!Set->Validate(Error))return false;
    // A single label rename also carries named bindings on other referencing planets.
    if(Recipe.RadialGuides.Id==Set->Id && Recipe.RadialGuides.Guides.Num()==Set->Guides.Num())
    {
        TArray<FName> Old,New;
        for(const auto& G:Recipe.RadialGuides.Guides)if(!Set->Guides.ContainsByPredicate([&](const auto& V){return V.Name==G.Name;}))Old.Add(G.Name);
        for(const auto& G:Set->Guides)if(!Recipe.RadialGuides.Guides.ContainsByPredicate([&](const auto& V){return V.Name==G.Name;}))New.Add(G.Name);
        if(Old.Num()==1&&New.Num()==1)Recipe.RenameGuideReferences(Old[0],New[0]);
    }
    Recipe.RadialGuides=*Set;return true;
}
#if WITH_EDITOR
void UCLPlanetRecipeAsset::BindGuideLibrary()
{
    if(BoundGuideLibrary.Get()==RadialGuideLibrary)return;
    if(BoundGuideLibrary.IsValid())BoundGuideLibrary->OnGuidesChanged.Remove(GuideLibraryChanged);
    BoundGuideLibrary=RadialGuideLibrary;
    if(RadialGuideLibrary)GuideLibraryChanged=RadialGuideLibrary->OnGuidesChanged.AddUObject(this,&UCLPlanetRecipeAsset::GuideSetChanged);
}
void UCLPlanetRecipeAsset::GuideSetChanged()
{
    const auto Before=Recipe.RadialGuides;FString Error;
    const bool Valid=SynchronizeRadialGuides(Error);
    if(!Valid || !FCLRadialGuideSet::StaticStruct()->CompareScriptStruct(&Before,&Recipe.RadialGuides,0))
    {if(!GetOutermost()->HasAnyFlags(RF_Transient))MarkPackageDirty();OnRecipeChanged.Broadcast();}
}
#endif

bool UCLPlanetRecipeAsset::SetLandformSource(FGuid Id,UCLTerrainTypeAsset* Source,FString& Error)
{
    if(!Source){Error=TEXT("Landform source is missing");return false;}
    auto Candidate=Recipe;if(!Source->TerrainType.ApplyToRegion(Candidate,Id,Error))return false;
    Modify();Recipe=MoveTemp(Candidate);LandformSources.Add(Id,Source);
#if WITH_EDITOR
    PostEditChange();MarkPackageDirty();
#endif
    return true;
}
bool UCLPlanetRecipeAsset::RefreshLandformSource(FGuid Id,FString& Error)
{
    auto* Source=LandformSources.Find(Id);if(!Source || !*Source){Error=TEXT("This imported snapshot has no authoring source link");return false;}
    return SetLandformSource(Id,*Source,Error);
}

void FCLPlanetRecipe::SeparateGeologyLayers()
{
    if(SchemaVersion>=16)return;
    if(SchemaVersion==15){SchemaVersion=16;return;}
    // A deliberate geology edit adopts separated semantics in one transaction.
    // Pinned schema 14 continues to evaluate through its original combined field.
    if(Tectonics.bEnabled)
    {
        Continents.bEnabled=true;Continents.LowerGuide=Tectonics.LowerGuide;Continents.UpperGuide=Tectonics.UpperGuide;
        if(Tectonics.AlgorithmVersion==3)
        {
            Continents.bOverrideExtent=true;Continents.Extent=Tectonics.ContinentalExtent;
            Continents.Strength=1;Continents.Orientation=Tectonics.FoundationOrientation;
            Continents.bCoastalMargin=Tectonics.Setting==ECLContinentalSetting::Subduction;
        }
    }
    SchemaVersion=16;
}

void FCLPlanetRecipe::UseGuidedGeology()
{
    if(SchemaVersion>=17)return;
    SeparateGeologyLayers();Continents.ApplyProgression(Continents.Strength);Tectonics.ApplyProgression(Tectonics.Activity);SchemaVersion=17;
}

#include "CLStampDistribution.h"
#include "Model/CLSphericalPatches.h"
#include "Stamps/CLStampLibrary.h"
#include "PCG/CLSphereStampPoints.h"
#include "PCGContext.h"
#include "PCGElement.h"
#include "PCGPin.h"
#include "Data/PCGBasePointData.h"
#include "Elements/PCGTransformPoints.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Helpers/PCGHelpers.h"
#include "Heightmap/VoxelHeightmap.h"
#include "Heightmap/VoxelHeightmap_Height.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
TUniquePtr<FPCGContext> RunPCG(UPCGSettings* Settings,const FPCGDataCollection& Input,FString& Error)
{
    auto Data=Input;auto& Tagged=Data.TaggedData.Emplace_GetRef();Tagged.Data=Settings;Tagged.Pin=TEXT("Settings");
    const auto Element=Settings->GetElement();
    TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Data,nullptr,nullptr)));
    Context->InitializeSettings();Context->AsyncState.NumAvailableTasks=1;
    Context->AsyncState.EndTime=FPlatformTime::Seconds()+2;
    for(int32 Phase=0;Phase<16;++Phase)if(Element->Execute(Context.Get()))return Context;
    Error=TEXT("Bounded PCG point pipeline did not complete synchronously");return nullptr;
}
EVoxelHeightBlendMode NativeBlend(ECLStampBlend Blend)
{
    switch(Blend){case ECLStampBlend::Min:return EVoxelHeightBlendMode::Min;case ECLStampBlend::Override:return EVoxelHeightBlendMode::Override;default:return EVoxelHeightBlendMode::Max;}
}
}

TSharedPtr<const FCLCompiledStampDistribution> FCLStampDistributionCompiler::Compile(const FCLStampDistributionSettings& S,
    int32 Seed,const FCLCompiledPlanetRecipe& Input,UCLStampLibraryAsset* Library,FString& Error,double MinimumRelief,double MaximumRelief)
{
    Error.Reset();
    auto Fail=[&](const FString& E)->TSharedPtr<const FCLCompiledStampDistribution>{Error=E;return nullptr;};
    if(!IsInGameThread())return Fail(TEXT("Stamp/PCG compilation must resolve client assets on the game thread"));
    if(!S.Validate(Error))return nullptr;
    if(!Library)Library=UCLStampLibraryAsset::LoadConfigured();
    if(!Library)return Fail(TEXT("Configured Landform stamp library is missing"));
    if(!Library->Library.Validate(Error))return nullptr;
    const auto* Collection=Library->Library.Collections.FindByPredicate([&](const auto& C){return C.Id==S.CollectionId&&C.Revision==S.CollectionRevision;});
    if(!Collection)return Fail(TEXT("Missing stamp collection revision: ")+S.CollectionId.ToString()+TEXT(" / ")+LexToString(S.CollectionRevision));
    if(Collection->BakedRevision!=Collection->Revision || Collection->DefinitionDigest!=UCLStampLibraryAsset::CollectionDigest(*Collection))
        return Fail(TEXT("Stamp collection is unbaked or changed without a new revision: ")+S.CollectionId.ToString());
    auto Entries=Collection->Entries;
    Entries.Sort([](const auto& A,const auto& B){return A.Id.LexicalLess(B.Id);});
    const bool bLocal=Input.Coordinates==ECLTerrainCoordinates::LocalPatch;
    const bool bSurface=Input.Coordinates==ECLTerrainCoordinates::SphericalSurface;
    const double ReferenceRadius=FMath::Max(.01,Input.RadiusMeters+(bSurface?Input.BaseHeightMeters:Input.SampleHeightMeters(FVector::ZAxisVector)));
    const double CountValue=bLocal?0:FMath::RoundToDouble(4*UE_PI*FMath::Square(ReferenceRadius)*S.DensityPerSquareKilometer/1.e6);
    if(!FMath::IsFinite(CountValue)||CountValue>256)
        return Fail(FString::Printf(TEXT("Stamp layer needs %.0f placements (limit 256) at a %.0f m surface radius. Reduce density to %.3f /km2 or lower the preview radius."),
            CountValue,ReferenceRadius,256*1.e6/(4*UE_PI*FMath::Square(ReferenceRadius))));
    int32 Count=int32(CountValue);
    const double Low=S.SizeMode==ECLStampSizeMode::Absolute?S.MinimumSizeMeters:S.MinimumSizeRadiusFraction*Input.RadiusMeters;
    const double High=S.SizeMode==ECLStampSizeMode::Absolute?S.MaximumSizeMeters:S.MaximumSizeRadiusFraction*Input.RadiusMeters;
    if(Low<.1||(!bLocal&&High>2*ReferenceRadius)||(bLocal&&High>4000))return Fail(TEXT("Stamp size must be at least 0.1m and at most the reference surface diameter"));
    auto Source=TStrongObjectPtr<UCLSphereStampPointsSettings>(NewObject<UCLSphereStampPointsSettings>());
    Source->Count=Count;Source->RadiusMeters=ReferenceRadius;Source->Seed=Seed;Source->Weights.Reset();
    Source->bLocal=bLocal;
    if(bLocal)
    {
        const double Halo=High/FMath::Sqrt(2.); // Rotated rectangular footprint, including zero moat.
        Source->RegionMin=Input.EvaluationRegion.Min-FVector2D(Halo);
        Source->RegionMax=Input.EvaluationRegion.Max+FVector2D(Halo);
        Source->Density=S.DensityPerSquareKilometer;
        const FVector2D Tiles=(Source->RegionMax-Source->RegionMin)/256+FVector2D(2);
        if(Tiles.X*Tiles.Y>1024 || Tiles.X*Tiles.Y*FMath::CeilToDouble(S.DensityPerSquareKilometer*256*256/1.e6)>256)
            return Fail(TEXT("Local stamp window and its contributing halo exceed the 256-placement budget; reduce density or footprint size"));
    }
    for(const auto& E:Entries)Source->Weights.Add(E.Weight);
    auto Points=RunPCG(Source.Get(),{},Error);if(!Points)return nullptr;
    auto Size=TStrongObjectPtr<UPCGTransformPointsSettings>(NewObject<UPCGTransformPointsSettings>());
    Size->Seed=PCGHelpers::ComputeSeed(Seed,101);Size->ScaleMin=FVector(Low);Size->ScaleMax=FVector(High);
    Size->RotationMin=FRotator(0,0,0);Size->RotationMax=FRotator(0,360,0);Size->bUniformScale=true;
    auto SizeInput=Points->OutputData;for(auto& D:SizeInput.TaggedData)D.Pin=PCGPinConstants::DefaultInputLabel;
    auto Sized=RunPCG(Size.Get(),SizeInput,Error);if(!Sized)return nullptr;
    if(bLocal)
    {
        const auto SourceOutputs=Points->OutputData.GetAllSpatialInputs();
        if(SourceOutputs.Num()!=1)return Fail(TEXT("Local PCG source exceeded its bounded tile/placement budget"));
        const auto* SourceData=Cast<UPCGBasePointData>(SourceOutputs[0].Data);
        if(!SourceData)return Fail(TEXT("Local PCG source returned invalid data"));
        Count=SourceData->GetNumPoints();
    }
    // Guide relief is an absolute metric delta; undo footprint scale before the second transform.
    if(S.ReliefMode==ECLStampReliefMode::GuideInterval)
    {
        const auto Out=Sized->OutputData.GetAllSpatialInputs();
        if(Out.Num()!=1)return Fail(TEXT("Missing sized PCG output"));
        auto* Mutable=const_cast<UPCGBasePointData*>(Cast<UPCGBasePointData>(Out[0].Data));
        if(!Mutable)return Fail(TEXT("Invalid sized PCG data"));
        auto Range=Mutable->GetTransformValueRange();
        for(int32 I=0;I<Count;++I){FTransform T=Range[I];FVector Scale=T.GetScale3D();Scale.Z=1;T.SetScale3D(Scale);Range[I]=T;}
    }
    auto Relief=TStrongObjectPtr<UPCGTransformPointsSettings>(NewObject<UPCGTransformPointsSettings>());
    Relief->Seed=PCGHelpers::ComputeSeed(Seed,202);Relief->bUniformScale=false;
    Relief->ScaleMin=FVector(1,1,S.ReliefMode==ECLStampReliefMode::GuideInterval?MinimumRelief:S.MinimumReliefRatio);
    Relief->ScaleMax=FVector(1,1,S.ReliefMode==ECLStampReliefMode::GuideInterval?MaximumRelief:S.MaximumReliefRatio);
    auto ReliefInput=Sized->OutputData;for(auto& D:ReliefInput.TaggedData)D.Pin=PCGPinConstants::DefaultInputLabel;
    auto Final=RunPCG(Relief.Get(),ReliefInput,Error);if(!Final)return nullptr;
    const auto Outputs=Final->OutputData.GetAllSpatialInputs();
    if(Outputs.Num()!=1)return Fail(TEXT("PCG stamp pipeline returned an unexpected output count"));
    const auto* Data=Cast<UPCGBasePointData>(Outputs[0].Data);
    if(!Data||Data->GetNumPoints()!=Count)return Fail(TEXT("PCG stamp pipeline returned an unexpected point count"));
    const auto* Choices=Data->ConstMetadata()->GetConstTypedAttribute<int32>(TEXT("CLStampIndex"));
    if(!Choices)return Fail(TEXT("PCG stamp-selection metadata is missing"));
    const auto Transforms=Data->GetConstTransformValueRange();const auto Keys=Data->GetConstMetadataEntryValueRange();
    auto Result=MakeShared<FCLCompiledStampDistribution>();
    Result->bLocal=bLocal;Result->Surfaces=FVoxelSurfaceTypeTable::Get();Result->Assets.Emplace(Library);
    FCLPlanetStamp Stamp;Stamp.Radius=Input.RadiusMeters*100;Stamp.bUseRecipe=false;Stamp.bPlanarSources=bLocal;
    double ByteBudget=0;
    TArray<TStrongObjectPtr<UVoxelHeightmap>> Assets;
    for(const auto& E:Entries)
    {
        auto* H=E.Heightmap.LoadSynchronous();
        if(!H||!H->Height)return Fail(TEXT("Missing native heightmap for stamp: ")+E.Id.ToString());
        const auto D=H->Height->GetData();if(!D)return Fail(TEXT("Native heightmap has no built data: ")+E.Id.ToString());
        ByteBudget+=D->RawData.Num();if(ByteBudget>256*1024*1024)return Fail(TEXT("Stamp collection exceeds the 256 MiB native source-data budget"));
        if(E.SourceDigest.Len()!=32||UCLStampLibraryAsset::SourceDigest(H)!=E.SourceDigest)
            return Fail(TEXT("Stamp source digest changed or was not baked; publish a versioned collection source: ")+E.Id.ToString());
        if(D->SizeX<4||D->SizeY<4||!FMath::IsFinite(H->ScaleXY)||H->ScaleXY<=0||!FMath::IsFinite(H->Height->ScaleZ)||H->Height->ScaleZ<=0)
            return Fail(TEXT("Invalid native stamp source scale or resolution"));
        Assets.Emplace(H);Result->Assets.Emplace(H);
    }
    const double Smoothness=S.BlendSmoothnessRadiusFraction*Input.RadiusMeters;
    for(int32 I=0;I<Count;++I)
    {
        const int32 Choice=Choices->GetValueFromItemKey(Keys[I]);if(!Entries.IsValidIndex(Choice))return Fail(TEXT("Invalid PCG stamp index"));
        const auto& E=Entries[Choice];auto* H=Assets[Choice].Get();const auto D=H->Height->GetData();
        const FTransform T=Transforms[I];
        FCLResolvedStampPlacement P;P.bLocalCoordinates=bLocal;P.SourceId=E.Id;P.Anchor=T.GetLocation().GetSafeNormal();P.TangentX=T.GetUnitAxis(EAxis::X);
        P.WidthMeters=T.GetScale3D().X;P.ReliefMeters=T.GetScale3D().Z;
        P.PositionMeters=FVector2D(T.GetLocation())/100;
        P.ChartRadiusMeters=bLocal?0:bSurface?ReferenceRadius:Input.RadiusMeters+Input.SampleHeightMeters(P.Anchor);
        if(!bLocal&&P.WidthMeters>2*P.ChartRadiusMeters)return Fail(TEXT("A selected stamp is wider than its local surface diameter"));
        P.Blend=S.Blend==ECLStampBlend::Collection?E.Blend:S.Blend;
        P.Id=FGuid(0x434C5331,uint32(Seed),uint32(I),FCrc::StrCrc32(*(S.CollectionId.ToString()+LexToString(S.CollectionRevision)+E.Id.ToString())));
        if(bLocal)
        {
            const auto* X=Data->ConstMetadata()->GetConstTypedAttribute<int32>(TEXT("CLTileX"));
            const auto* Y=Data->ConstMetadata()->GetConstTypedAttribute<int32>(TEXT("CLTileY"));
            const auto* IAttr=Data->ConstMetadata()->GetConstTypedAttribute<int32>(TEXT("CLOrdinal"));
            if(!X||!Y||!IAttr)return Fail(TEXT("Local PCG identity metadata missing"));
            P.Id=FGuid(uint32(Seed)^FCrc::StrCrc32(*(S.CollectionId.ToString()+LexToString(S.CollectionRevision)+E.Id.ToString())),uint32(X->GetValueFromItemKey(Keys[I])),uint32(Y->GetValueFromItemKey(Keys[I])),uint32(IAttr->GetValueFromItemKey(Keys[I]))+1);
            P.Anchor=Input.TerrainDirection(P.PositionMeters);
        }
        FCLSphericalHeightmapPlacement Placement;Placement.Id=P.Id;Placement.Anchor=P.Anchor;Placement.TangentX=P.TangentX;
        Placement.bPlanar=bLocal;Placement.Center=P.PositionMeters*100;
        Placement.ChartRadius=P.ChartRadiusMeters*100;
        const double Width=(FMath::Max(D->SizeX,D->SizeY)-3.0001)*H->ScaleXY;
        Placement.Scale=FVector(P.WidthMeters*100/Width,P.WidthMeters*100/Width,P.ReliefMeters/E.ReferenceReliefMeters);
        Placement.EdgeFalloff=P.WidthMeters*100*S.EdgeFalloffFraction;
        Placement.Source.Heightmap=H;Placement.Source.BlendMode=NativeBlend(P.Blend);
        Placement.Source.Smoothness=Smoothness*100;Placement.Source.Behavior=EVoxelStampBehavior::AffectShape;
        if(bSurface)
        {
            // Include the full native safe rectangle, not an approximate half-width.
            const FVector2D SafeMin=(FVector2D(1)-FVector2D(D->SizeX,D->SizeY)*.5)*H->ScaleXY;
            const FVector2D SafeMax=(FVector2D(D->SizeX-2.0001,D->SizeY-2.0001)-FVector2D(D->SizeX,D->SizeY)*.5)*H->ScaleXY;
            const FVector2D Corner(FMath::Max(FMath::Abs(SafeMin.X),FMath::Abs(SafeMax.X))*Placement.Scale.X,
                FMath::Max(FMath::Abs(SafeMin.Y),FMath::Abs(SafeMax.Y))*Placement.Scale.Y);
            if(!FCLSphericalPatches::MayAffect(Input.SurfacePatch,P.Anchor,Corner.Size()/Placement.ChartRadius))continue;
        }
        Stamp.Placements.Add(Placement);Result->Placements.Add(P);
        const double Unit=D->bIsUINT16?1./65535:1.;
        double A=(D->DataMin*Unit*D->InternalScaleZ+D->InternalOffsetZ)*H->Height->ScaleZ+H->Height->OffsetZ;
        double B=(D->DataMax*Unit*D->InternalScaleZ+D->InternalOffsetZ)*H->Height->ScaleZ+H->Height->OffsetZ;
        const double Padding=H->Height->bUseBicubic?.5*(B-A):0;
        A=(A-Padding)*Placement.Scale.Z/100;B=(B+Padding)*Placement.Scale.Z/100;
        switch(P.Blend)
        {
        case ECLStampBlend::Max:Result->MaximumDeltaMeters=FMath::Max(Result->MaximumDeltaMeters,B)+Smoothness*FMath::Max(1.,Placement.Scale.Z);break;
        case ECLStampBlend::Min:Result->MinimumDeltaMeters=FMath::Min(Result->MinimumDeltaMeters,A)-Smoothness*FMath::Max(1.,Placement.Scale.Z);break;
        default:Result->MinimumDeltaMeters=FMath::Min(Result->MinimumDeltaMeters,A);Result->MaximumDeltaMeters=FMath::Max(Result->MaximumDeltaMeters,B);break;
        }
    }
    const auto Ref=FVoxelStampRef::New(Stamp);
    Result->Runtime=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,Ref,nullptr));
    if(!Result->Runtime)return Fail(TEXT("Native VPP stamp-distribution sampler failed to initialize"));
    return Result;
}
void FCLCompiledStampDistribution::Apply(TConstArrayView<FVector> Directions,TArrayView<double> Heights)const
{
    if(Placements.IsEmpty())return;
    FVoxelDoubleVectorBuffer Buffer;Buffer.Allocate(Directions.Num());
    FVoxelFloatBuffer Delta;Delta.Allocate(Directions.Num());
    for(int32 I=0;I<Directions.Num();++I){Buffer.Set(I,bLocal?Directions[I]*100:Directions[I]);Delta.Set(I,0);}
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.StampDistribution"));
    const FVoxelQuery Query(0,FVoxelLayers::Empty(),*Surfaces,Dependencies);
    Runtime->ApplySources(Query,Buffer,Delta);
    for(int32 I=0;I<Heights.Num();++I)Heights[I]+=Delta[I]/100.;
}

void FCLCompiledStampDistribution::Coverage(const FVector& Direction,int32& Count,double& MinimumWeight)const
{
    for(const auto& Source:Runtime->Sources)
    {
        FVector2D Chart;const double W=Source.Weight(0,bLocal?Direction*100:Direction,Chart);
        if(W>1.e-6){++Count;MinimumWeight=FMath::Min(MinimumWeight,W);}
    }
}

TSharedPtr<const FCLCompiledStampDistribution> FCLStampDistributionCompiler::CompileFixed(const FCLPlanetFeature& F,const FCLCompiledPlanetRecipe& Input,UCLStampLibraryAsset* Library,double Relief,FString& Error)
{
    auto Fail=[&](const FString& E)->TSharedPtr<const FCLCompiledStampDistribution>{Error=E;return nullptr;};
    if(!IsInGameThread())return Fail(TEXT("Editorial heightmap compilation must run on the game thread"));
    if(!Library)Library=UCLStampLibraryAsset::LoadConfigured();
    if(!Library || !Library->Library.Validate(Error))return Fail(TEXT("Invalid configured stamp library"));
    const auto* C=Library->Library.Collections.FindByPredicate([&](const auto& V){return V.Id==F.CollectionId && V.Revision==F.CollectionRevision;});
    if(!C || C->BakedRevision!=C->Revision || C->DefinitionDigest!=UCLStampLibraryAsset::CollectionDigest(*C))return Fail(TEXT("Missing or changed editorial stamp collection revision"));
    const auto* E=C->Entries.FindByPredicate([&](const auto& V){return V.Id==F.SourceId;});
    if(!E)return Fail(TEXT("Editorial heightmap source is missing"));
    auto* H=E->Heightmap.LoadSynchronous();
    if(!H || !H->Height || E->SourceDigest.Len()!=32 || UCLStampLibraryAsset::SourceDigest(H)!=E->SourceDigest)return Fail(TEXT("Editorial heightmap is missing, changed or unbaked"));
    const auto D=H->Height->GetData();
    if(!D || D->SizeX<4 || D->SizeY<4 || D->RawData.Num()>256*1024*1024 || H->ScaleXY<=0 || H->Height->ScaleZ<=0 || H->Height->OffsetZ!=0 || H->Height->bUseBicubic || D->DataMin<0)
        return Fail(TEXT("Editorial heightmap needs bounded nonnegative linear data, positive scales and zero vertical offset"));
    const double Unit=D->bIsUINT16?1./65535:1.;
    const double Peak=(D->DataMax*Unit*D->InternalScaleZ+D->InternalOffsetZ)*H->Height->ScaleZ/100;
    if(!FMath::IsFinite(Peak) || Peak<=0)return Fail(TEXT("Editorial heightmap has no positive depth/relief"));
    auto Result=MakeShared<FCLCompiledStampDistribution>();Result->Surfaces=FVoxelSurfaceTypeTable::Get();Result->Assets.Emplace(Library);Result->Assets.Emplace(H);
    FCLPlanetStamp Stamp;Stamp.Radius=Input.RadiusMeters*100;Stamp.bUseRecipe=false;
    const FQuat Frame=FCLCompiledPlanetFeature::Frame(F.LocationDegrees,F.HeadingDegrees);
    FCLSphericalHeightmapPlacement P;P.Id=F.Id;P.Anchor=Frame.GetAxisZ();P.TangentX=Frame.GetAxisX();
    P.ChartRadius=(Input.RadiusMeters+Input.BaseHeightMeters)*100;
    const FVector2D Extent=F.HalfExtentDegrees*(UE_DOUBLE_PI/180)*P.ChartRadius;
    // Native safe rectangle excludes one sample on either side; source itself has a zero moat.
    P.Scale=FVector(2*Extent.X/((D->SizeX-3.0001)*H->ScaleXY),2*Extent.Y/((D->SizeY-3.0001)*H->ScaleXY),FMath::Max(1.e-12,FMath::Abs(Relief))/Peak);
    P.EdgeFalloff=1;/* 1cm safety fade lies entirely inside the source zero moat. */P.Source.Heightmap=H;P.Source.BlendMode=EVoxelHeightBlendMode::Override;P.Source.Smoothness=0;P.Source.Behavior=EVoxelStampBehavior::AffectShape;
    Stamp.Placements.Add(P);
    FCLResolvedStampPlacement Diagnostic;Diagnostic.Id=F.Id;Diagnostic.SourceId=E->Id;Diagnostic.Anchor=P.Anchor;Diagnostic.TangentX=P.TangentX;Result->Placements.Add(Diagnostic);
    Result->Runtime=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(Stamp),nullptr));
    if(!Result->Runtime)return Fail(TEXT("Native editorial heightmap sampler failed"));
    Result->MaximumDeltaMeters=FMath::Abs(Relief);return Result;
}

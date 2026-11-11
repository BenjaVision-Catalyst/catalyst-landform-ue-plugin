#include "CLSpatialCaveFamily.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLCaves.h"
#include "Model/CLRegionGeneration.h"
#include "Model/CLProvinceDefinition.h"
#include "Model/CLWatershed.h"
#include "Model/CLCompileProgress.h"
#include "CLNativeNoiseKernel.h"
#include "VoxelAABBTree.h"
#include "Math/RotationMatrix.h"
#if CL_SPATIAL_SIMD
#include "CLSpatialMorph.generated.h"
#endif

namespace
{
double Blend(double A,double B,double K){const double H=FMath::Clamp(.5+.5*(B-A)/K,0.,1.);return FMath::Lerp(B,A,H)-K*H*(1-H);}
double Hill(double X,double Y,double CX,double CY,double RX,double RY){return FMath::Exp(-FMath::Square((X-CX)/RX)-FMath::Square((Y-CY)/RY));}
}
FVector FCLSpatialCaveFamily::BodyPoint(FVector P,int32 I)const
{
    const auto& N=Networks[I];FVector Q=N.Origin+N.X*P.X+N.Y*P.Y+N.Up*P.Z;
    for(int32 K=0;K<7;++K)Q+=N.Up*((N.Datum+P.Z-(Rock?Rock->RestoredRadiusMeters(Q):Q.Length()))/FMath::Max(.35,FVector::DotProduct(Q.GetSafeNormal(),N.Up)));
    return Q;
}
FVector FCLSpatialCaveFamily::BeddingPoint(FVector P,int32 I,bool Compiled)const
{
    const auto& N=Networks[I];const auto D=P-N.Origin;const double Along=FVector::DotProduct(P,N.Up);
    const double Radius=Compiled&&Along>1.e-6?P.Length()-N.Bedding.Sample(FVector2D(FVector::DotProduct(P,N.X),FVector::DotProduct(P,N.Y))*(N.Datum/Along)):(Rock?Rock->RestoredRadiusMeters(P):P.Length());
    return {FVector::DotProduct(D,N.X),FVector::DotProduct(D,N.Y),Radius-N.Datum};
}
bool FCLSpatialCaveFamily::TrySurfaces(const FVector& P,FCLSurfaceWeights& W)const
{
    if(!Rock)return false;
    for(const auto& N:Networks)
    {
        const double Along=FVector::DotProduct(P,N.Up);if(Along<=1.e-6)continue;
        const FVector2D UV=FVector2D(FVector::DotProduct(P,N.X),FVector::DotProduct(P,N.Y))*(N.Datum/Along);
        double Deformation,Basement;
        if(N.Bedding.TrySample(UV,Deformation)&&N.Basement.TrySample(UV,Basement)&&Rock->TryStableSurfaces(P,Deformation,Basement,.05,W))
        {PreparedSurfaces.fetch_add(1,std::memory_order_relaxed);return true;}
    }
    ExactSurfaces.fetch_add(1,std::memory_order_relaxed);return false;
}
double FCLSpatialCaveFamily::RollingFloor(const FNetwork& N,double X,double Y)const
{
    // Common to every opening and every level, including each ramp endpoint.
    X/=N.UnitScale;Y/=N.UnitScale;
    return N.UnitScale*(.40*FMath::Sin(X*.025+N.Phase)+.35*FMath::Cos(Y*.033-N.Phase)+.12*FMath::Sin((X+Y)*.09)
        +1.2*Hill(X,Y,48,-32,38,22)-.35*FMath::Exp(-FMath::Square((Y-3*FMath::Sin(X*.025))/6)));
}
double FCLSpatialCaveFamily::FloorHeight(double X,double Y,int32 Level,int32 I)const
{return -Networks[I].Spacing*Level+RollingFloor(Networks[I],X,Y);}
double FCLSpatialCaveFamily::CellFloor(const FNetwork& N,const FCell& C,double X,double Y)const
{
    const auto D=C.B-C.A;const double T=D.SquaredLength()>0?FMath::Clamp(FVector2D::DotProduct(FVector2D(X,Y)-C.A,D)/D.SquaredLength(),0.,1.):0.;
    const double Ramp=FMath::Clamp((T-.20)/.60,0.,1.);
    return FMath::Lerp(C.FloorA,C.FloorB,Ramp*Ramp*(3-2*Ramp))+RollingFloor(N,X,Y);
}
FBox FCLSpatialCaveFamily::Bounds(FGuid Id)const
{if(!Id.IsValid())return AllBounds;FBox B(ForceInit);for(const auto& N:Networks)if(!Id.IsValid()||N.Id==Id)B+=N.BodyBounds;return B;}
void FCLSpatialCaveFamily::CellSupportBounds(TArray<FBox>& Out)const
{Out.Reset();for(const auto& N:Networks)for(const auto& C:N.Cells)Out.Add(C.BodyBounds);}
void FCLSpatialCaveFamily::Stats(int32& Levels,int32& Cells,int32& Ramps,int32& Nodes,int32& Exact,uint64& Bytes,double& Error)const
{
    Levels=Cells=Ramps=Nodes=Exact=0;Bytes=0;Error=0;
    for(const auto& N:Networks){Levels=FMath::Max(Levels,N.Levels);Cells+=N.Cells.Num();Nodes+=N.Bedding.NumNodes();Exact+=N.Bedding.NumExactLeaves();Bytes+=N.Bedding.GetAllocatedSize()+N.Basement.GetAllocatedSize()+N.PlanarDistanceLowerBounds.GetAllocatedSize()+N.BodyBoxes.GetAllocatedSize()+N.SupportGrid.GetAllocatedSize()+N.SupportSize.GetAllocatedSize()+N.SupportMasks.GetAllocatedSize();Error=FMath::Max(Error,N.Bedding.MaximumValidationError());for(const auto& C:N.Cells)Ramps+=C.FloorA!=C.FloorB;}
}
void FCLSpatialCaveFamily::SampleCell(const FNetwork& N,const FCell& C,TConstArrayView<FVector> Q,TConstArrayView<float> Broad,TConstArrayView<float> Bed,TConstArrayView<float> Detail,TArray<double>& Values,bool Compiled)const
{
    const int32 Num=Q.Num();Values.SetNumUninitialized(Num);TArray<double> X,Y,Z,Floor;X.Reserve(Num);Y.Reserve(Num);Z.Reserve(Num);Floor.Reserve(Num);
    for(const auto& P:Q){X.Add(P.X);Y.Add(P.Y);Z.Add(P.Z);if(!Compiled)Floor.Add(CellFloor(N,C,P.X,P.Y));}
#if CL_SPATIAL_SIMD
    if(Compiled){ispc::CLFamilyCell(X.GetData(),Y.GetData(),Z.GetData(),Broad.GetData(),Bed.GetData(),Detail.GetData(),C.X.GetData(),C.Y.GetData(),C.X.Num(),N.Contacts.GetData(),N.Contacts.Num(),C.Center.X,C.Center.Y,C.Roof,C.Scale,N.Phase,N.UnitScale,C.Chamber?1:0,C.A.X,C.A.Y,C.B.X,C.B.Y,C.FloorA,C.FloorB,Values.GetData(),Num);return;}
#endif
    if(Floor.IsEmpty())for(const auto& P:Q)Floor.Add(CellFloor(N,C,P.X,P.Y));
    for(int32 I=0;I<Num;++I)
    {
        const auto P=Q[I];const double B=Broad[I],D=Bed[I],S=C.Scale;
        const FVector2D W(P.X+S*(2.8*B+.65*FMath::Sin(P.Y*.15+P.Z*.20)),P.Y+S*(4*B+1.8*D));
        const double Distance=FMath::Sqrt(C.Polygon.DistanceSquared(W));const double Plan=C.Polygon.Contains(W)?Distance:-Distance;
        const double Roof=Floor[I]+C.Roof+S*(2.0*B+D)+C.Roof*.12*Hill(P.X,P.Y,C.Center.X-8*S,C.Center.Y+3*S,18*S,14*S)
            -C.Roof*.16*Hill(P.X,P.Y,C.Center.X+7*S,C.Center.Y-4*S,13*S,11*S);
        int32 Low=0,High=N.Contacts.Num();while(Low<High){const int32 M=(Low+High)/2;if(N.Contacts[M]<P.Z)Low=M+1;else High=M;}
        double CD=MAX_dbl;if(Low<N.Contacts.Num())CD=FMath::Abs(N.Contacts[Low]-P.Z);if(Low>0)CD=FMath::Min(CD,FMath::Abs(N.Contacts[Low-1]-P.Z));
        const double Relief=S*((.25+.30*FMath::Clamp(B*2+D,0.,1.))*FMath::Exp(-FMath::Square(CD/.40))+.35*D);
        const double Under=S*1.4*FMath::Exp(-FMath::Square((P.Z-Floor[I]-5*S)/(2.7*S)));
        double V=Blend(Plan+Relief+Under,Roof-P.Z,3*S);V=Blend(V,P.Z-Floor[I],.8*S);
        if(C.Chamber)
        {
            const double PX=(P.X-C.Center.X-5*S)/(3.5*S),PY=(P.Y-C.Center.Y+4*S)/(4.8*S);
            const double Side=(FMath::Sqrt(PX*PX+PY*PY)-1)*3.5*S+S*(1.8*B+.65*D),Bottom=Floor[I]+C.Roof*.52+S*B-P.Z;
            V=Blend(V,-Blend(-Side,-Bottom,3*S),.8*S);
        }
        Values[I]=(V+.12*S*Detail[I]*FMath::SmoothStep(.3*S,2*S,P.Z-Floor[I]))*.6;
    }
}
void FCLSpatialCaveFamily::Sample(TConstArrayView<FVector> P,TArray<double>& Air,bool Compiled,const TSet<FGuid>* Selection)const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(CL_CaveFamilyEvaluate);
    Air.Init(-1.e6,P.Num());if(!Index||P.IsEmpty())return;
    if(!Compiled)
    {
        // Independent scalar oracle: no cell index or cell support clipping.
        // The finite front-side domain excludes the antipodal coordinate alias.
        for(int32 NI=0;NI<Networks.Num();++NI)
        {
            const auto& N=Networks[NI];if(Selection&&!Selection->IsEmpty()&&!Selection->Contains(N.Id))continue;
            TArray<int32> Active;TArray<FVector> Q,NP;
            for(int32 I=0;I<P.Num();++I)if(FVector::DotProduct(P[I],N.Up)>0&&N.BodyBounds.ExpandBy(30).IsInsideOrOn(P[I])){Active.Add(I);Q.Add(BeddingPoint(P[I],NI,false));}
            for(const auto& V:Q)NP.Add(V/FVector(16,13,9)+FVector(71,-39,113));for(const auto& V:Q)NP.Add(V/FVector(4.2,4.2,2.1)+FVector(-31,51,19));for(const auto& V:Q)NP.Add(V/.65+FVector(13,91,-43));
            TArray<float> Samples;Noise->SampleBatch(NP,Seed,Samples);const int32 Num=Q.Num();
            if(!Num)continue;
            for(const auto& C:N.Cells){TArray<double> V;SampleCell(N,C,Q,MakeArrayView(Samples.GetData(),Num),MakeArrayView(Samples.GetData()+Num,Num),MakeArrayView(Samples.GetData()+2*Num,Num),V,false);for(int32 I=0;I<Num;++I)Air[Active[I]]=FMath::Max(Air[Active[I]],V[I]);}
        }
        return;
    }
#if CL_SPATIAL_SIMD
    TArray<double> BodyX,BodyY,BodyZ;BodyX.Reserve(P.Num());BodyY.Reserve(P.Num());BodyZ.Reserve(P.Num());
    for(const auto& Q:P){BodyX.Add(Q.X);BodyY.Add(Q.Y);BodyZ.Add(Q.Z);}
    for(int32 NI=0;NI<Networks.Num();++NI)
    {
        const auto& N=Networks[NI];if(Selection&&!Selection->IsEmpty()&&!Selection->Contains(N.Id))continue;
        TArray<double> X,Y,Z;X.SetNumUninitialized(P.Num());Y.SetNumUninitialized(P.Num());Z.SetNumUninitialized(P.Num());TArray<int32> Enabled;Enabled.SetNumUninitialized(P.Num());
        const double Frame[]={N.Origin.X,N.Origin.Y,N.Origin.Z,N.X.X,N.X.Y,N.X.Z,N.Y.X,N.Y.Y,N.Y.Z,N.Up.X,N.Up.Y,N.Up.Z,N.Datum,
            N.PlanarBounds.Min.X,N.PlanarBounds.Min.Y,N.PlanarBounds.Max.X,N.PlanarBounds.Max.Y,N.PlanarStep.X,N.PlanarStep.Y,N.PlanarSupportPadding};
        const int32 PlanarSize[]={N.PlanarSize.X,N.PlanarSize.Y};double Bounds[6];int32 Counts[2];
        {TRACE_CPUPROFILER_EVENT_SCOPE(CL_CavePacketPrepare);
        ispc::CLFamilyPrepare(BodyX.GetData(),BodyY.GetData(),BodyZ.GetData(),N.BodyBoxes.GetData(),N.SupportGrid.GetData(),N.SupportSize.GetData(),N.SupportMasks.GetData(),
            Frame,PlanarSize,N.PlanarDistanceLowerBounds.GetData(),N.Bedding.PackedNodes().GetData(),N.Bedding.PackedLinks().GetData(),N.Bedding.PackedLookup().GetData(),
            X.GetData(),Y.GetData(),Z.GetData(),Enabled.GetData(),Air.GetData(),Bounds,Counts,P.Num());}
        if(!Counts[0])continue;
        FBox LocalBounds(FVector(Bounds[0],Bounds[1],Bounds[2]),FVector(Bounds[3],Bounds[4],Bounds[5]));
        // Native SIMD handles all validated leaves. Uncertain leaves retain
        // the exact geological evaluator, independently of query packet size.
        if(Counts[1])for(int32 I=0;I<P.Num();++I)if(Enabled[I]==2){const auto Q=BeddingPoint(P[I],NI,false);X[I]=Q.X;Y[I]=Q.Y;Z[I]=Q.Z;Enabled[I]=1;LocalBounds+=Q;}
        TArray<int32> Candidates;N.Index->Intersects(FVoxelFastBox(FVoxelBox(LocalBounds)),[&](int32 CI){Candidates.Add(CI);return false;});
        {TRACE_CPUPROFILER_EVENT_SCOPE(CL_CaveUnionKernel);
        ispc::CLFamilyUnion(X.GetData(),Y.GetData(),Z.GetData(),Enabled.GetData(),Seed,N.VertexX.GetData(),N.VertexY.GetData(),N.Edges.GetData(),N.EdgeMasks.GetData(),N.EdgeTiles.GetData(),N.EdgeGrids.GetData(),N.Starts.GetData(),N.Counts.GetData(),N.Params.GetData(),N.Cells.Num(),Candidates.GetData(),Candidates.Num(),N.Contacts.GetData(),N.Contacts.Num(),N.Phase,Air.GetData(),P.Num());}
    }
#else
    // One network packet: no per-cell point maps or repeated noise/coordinate
    // buffers. The SIMD union evaluates only cells containing each lane.
    FBox Packet(ForceInit);for(const auto& Q:P)Packet+=Q;
    TArray<double> BodyX,BodyY,BodyZ;BodyX.Reserve(P.Num());BodyY.Reserve(P.Num());BodyZ.Reserve(P.Num());
    for(const auto& Q:P){BodyX.Add(Q.X);BodyY.Add(Q.Y);BodyZ.Add(Q.Z);}
    for(int32 NI=0;NI<Networks.Num();++NI)
    {
        const auto& N=Networks[NI];if(Selection&&!Selection->IsEmpty()&&!Selection->Contains(N.Id))continue;
        TArray<double> Support;Support.SetNumUninitialized(P.Num());
#if CL_SPATIAL_SIMD
        {TRACE_CPUPROFILER_EVENT_SCOPE(CL_CaveSupportBounds);
        ispc::CLCellSupportDistances(BodyX.GetData(),BodyY.GetData(),BodyZ.GetData(),N.BodyBoxes.GetData(),N.SupportGrid.GetData(),N.SupportSize.GetData(),N.SupportMasks.GetData(),N.Cells.Num(),Support.GetData(),P.Num());}
#else
        for(int32 I=0;I<P.Num();++I){double D=MAX_dbl;for(const auto& C:N.Cells)D=FMath::Min(D,C.BodyBounds.ComputeSquaredDistanceToPoint(P[I]));Support[I]=FMath::Sqrt(D);}
#endif
        for(int32 I=0;I<P.Num();++I)Air[I]=FMath::Max(Air[I],-FMath::Max(.001,Support[I]));
        if(!N.BodyBounds.Intersect(Packet))continue;
        TArray<int32> Active;TArray<FVector> Local,NoisePoints;TArray<double> X,Y,Z;
        Active.Reserve(P.Num());Local.Reserve(P.Num());X.Reserve(P.Num());Y.Reserve(P.Num());Z.Reserve(P.Num());
        for(int32 I=0;I<P.Num();++I)if(Support[I]<=.001&&N.BodyBounds.IsInsideOrOn(P[I]))
        {
            const auto& V=P[I];const auto D=V-N.Origin;const double Along=FVector::DotProduct(V,N.Up);
            // XY projection is 1-Lipschitz in body space. A tile's minimum
            // distance to every unwarped footprint, minus the proven maximum
            // horizontal warp/relief, is a distance bound to any cave surface.
            // This classifies unsupported rock before bedding, noise or cells.
            const FVector2D XY(FVector::DotProduct(D,N.X),FVector::DotProduct(D,N.Y));
            if(N.PlanarBounds.IsInside(XY))
            {
                const auto Tile=(XY-N.PlanarBounds.Min)/N.PlanarStep;
                const int32 TileX=FMath::Clamp(FMath::FloorToInt(Tile.X),0,N.PlanarSize.X-1),TileY=FMath::Clamp(FMath::FloorToInt(Tile.Y),0,N.PlanarSize.Y-1);
                const double Bound=N.PlanarDistanceLowerBounds[TileX+TileY*N.PlanarSize.X]-N.PlanarSupportPadding;
                if(Bound>2){Air[I]=-.6*Bound;continue;}
            }
            double Restored=0,Offset=0;
            if(Along>1.e-6)
            {
                const auto UV=FVector2D(FVector::DotProduct(V,N.X),FVector::DotProduct(V,N.Y))*(N.Datum/Along);
                if(N.Bedding.TrySample(UV,Offset))Restored=V.Length()-Offset;
                else if(!N.Bedding.Contains(UV))
                {
                    // The chart encloses the projections of every finite cell
                    // support. Outside it there can be no void. Do not invoke
                    // full tectonic geology for unused space in the larger
                    // aggregate network box; return a world-distance bound.
                    double Nearest=MAX_dbl;
                    Index->Traverse([&](const FVoxelFastBox& B){return FBox(FVector(B.GetMin()),FVector(B.GetMax())).ComputeSquaredDistanceToPoint(V)<=Nearest;},
                        [&](int32 F){const auto Feature=Features[F];if(Feature.X==NI)Nearest=FMath::Min(Nearest,Networks[Feature.X].Cells[Feature.Y].BodyBounds.ComputeSquaredDistanceToPoint(V));});
                    Air[I]=-FMath::Sqrt(Nearest)-1;continue;
                }
                else Restored=(Rock?Rock->RestoredRadiusMeters(V):V.Length()); // Uncertain in-domain leaves retain exact evaluation.
            }
            else Restored=(Rock?Rock->RestoredRadiusMeters(V):V.Length());
            const FVector Q(FVector::DotProduct(D,N.X),FVector::DotProduct(D,N.Y),Restored-N.Datum);
            Active.Add(I);Local.Add(Q);X.Add(Q.X);Y.Add(Q.Y);Z.Add(Q.Z);
        }
        if(Active.IsEmpty())continue;
        FBox LocalBounds(ForceInit);for(const auto& Q:Local)LocalBounds+=Q;TArray<int32> Candidates;N.Index->Intersects(FVoxelFastBox(FVoxelBox(LocalBounds)),[&](int32 CI){Candidates.Add(CI);return false;});
        TArray<float> Samples;
        auto SampleNoise=[&]
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(CL_CaveNoise);
            NoisePoints.Reserve(3*Local.Num());
            for(const auto& Q:Local)NoisePoints.Add(Q/FVector(16,13,9)+FVector(71,-39,113));for(const auto& Q:Local)NoisePoints.Add(Q/FVector(4.2,4.2,2.1)+FVector(-31,51,19));for(const auto& Q:Local)NoisePoints.Add(Q/.65+FVector(13,91,-43));
            Noise->SampleBatch(NoisePoints,Seed,Samples);
        };
        TArray<double> Values;Values.SetNumUninitialized(Active.Num());
#if CL_SPATIAL_SIMD
        {TRACE_CPUPROFILER_EVENT_SCOPE(CL_CaveUnionKernel);
        ispc::CLFamilyUnion(X.GetData(),Y.GetData(),Z.GetData(),Seed,N.VertexX.GetData(),N.VertexY.GetData(),N.Edges.GetData(),N.EdgeMasks.GetData(),N.EdgeTiles.GetData(),N.EdgeGrids.GetData(),N.Starts.GetData(),N.Counts.GetData(),N.Params.GetData(),N.Cells.Num(),Candidates.GetData(),Candidates.Num(),N.Contacts.GetData(),N.Contacts.Num(),N.Phase,Values.GetData(),Active.Num());
        }
#else
        if(Samples.IsEmpty())SampleNoise();Values.Init(-1.e6,Active.Num());TBitArray<> Hit(false,Active.Num());TArray<double> Nearest;Nearest.Init(MAX_dbl,Active.Num());
        for(const auto& C:N.Cells)
        {
            TArray<double> V;SampleCell(N,C,Local,MakeArrayView(Samples.GetData(),Active.Num()),MakeArrayView(Samples.GetData()+Active.Num(),Active.Num()),MakeArrayView(Samples.GetData()+2*Active.Num(),Active.Num()),V,false);
            for(int32 I=0;I<Local.Num();++I){const double D=C.Bounds.ComputeSquaredDistanceToPoint(Local[I]);Nearest[I]=FMath::Min(Nearest[I],D);if(D==0){Hit[I]=true;Values[I]=FMath::Max(Values[I],V[I]);}}
        }
        for(int32 I=0;I<Local.Num();++I)if(!Hit[I])Values[I]=-.5*(FMath::Sqrt(Nearest[I])+2);
#endif
        for(int32 I=0;I<Active.Num();++I)Air[Active[I]]=Values[I];
    }
#endif
}
TSharedPtr<const FCLSpatialCaveFamily> FCLSpatialCaveFamily::Build(const FCLCompiledPlanetRecipe& Base,const FCLCaveSettings& S,int32 Seed,FCLCompiledCaves& Out,FString& Error,bool PreferGeology)
{
    // The existing spatial family supplies every enabled minimum. Geological
    // capabilities select preferred sites/bedding, never a replacement shape.
    const bool Preferred=PreferGeology&&Base.Rock&&Base.Rock->HasKarstHosts()&&Base.Watershed&&Base.GeneratedRegions&&!S.bSurfaceEntrances&&S.RadiusMeters>=1.2&&S.LengthMeters>=40;
    const double Reference=Base.RadiusMeters+(Base.MinimumHeightMeters+Base.MaximumHeightMeters)*.5,Area=4*UE_DOUBLE_PI*Reference*Reference/1.e6;
    const double UnitScale=Preferred?1:FMath::Min(1.,Reference/500.);
    auto Settings=S;
    if(!Preferred)
    {
        Settings.RadiusMeters=FMath::Min(FMath::Max(2.5,S.RadiusMeters)*UnitScale,Reference*.006);
        Settings.LengthMeters=FMath::Min(FMath::Max(40.,S.LengthMeters)*UnitScale,Reference*.4);
        Settings.DepthMeters=FMath::Min(S.DepthMeters*UnitScale,Reference*.08);
        Settings.MinimumCoverMeters=FMath::Min(S.MinimumCoverMeters*UnitScale,Reference*.01);
    }
    auto Family=MakeShared<FCLSpatialCaveFamily>();Family->Rock=Preferred?Base.Rock:nullptr;Family->Noise=FCLNativeNoiseKernel::Create(Error);Family->Seed=Seed^0x53504139;if(!Family->Noise)return nullptr;
    Out.Candidates=FMath::Clamp(FMath::CeilToInt(FMath::Min(Area*16,16384.)),16,16384);
    struct FCandidate{FVector D;int32 Id;uint32 Rank;};TArray<FCandidate> Eligible;
    FRandomStream Placement(Seed);const FQuat Rotation(FRotator(Placement.FRandRange(-90,90),Placement.FRandRange(-180,180),Placement.FRandRange(-180,180)));
    for(int32 I=0;I<Out.Candidates;++I)
    {
        const double Z=1-2*(I+.5)/Out.Candidates,A=I*2.399963229728653;const FVector D=Rotation.RotateVector(FVector(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z));
        if(Preferred){const auto& G=*Base.GeneratedRegions;const auto& Province=G.Palette[G.Cells[G.Owner(D)].PaletteIndex];
        if(Province.Suitability.Province.Type!=ECLProvinceType::CarbonatePlatform||Province.Suitability.Province.Match(FCLProvinceGeology::Sample(Base,D))!=ECLProvinceMatch::Eligible)continue;
        const auto& Water=Base.Watershed->GetCells();const auto* Nearest=&Water[0];double Dot=-2;for(const auto& C:Water){const double V=FVector::DotProduct(C.Direction,D);if(V>Dot){Dot=V;Nearest=&C;}}if(Nearest->bOcean||Nearest->bLake)continue;}
        Eligible.Add({D,I,HashCombineFast(uint32(Seed),uint32(I+1))});
    }
    Out.EligibleCandidates=Eligible.Num();Out.RequestedNetworks=FMath::Max(1,FMath::CeilToInt(FMath::Min(64.,S.NetworksPerSquareKilometer*Area*Eligible.Num()/Out.Candidates)));
    Out.RequestedNetworks=FMath::Min(64,Out.RequestedNetworks);
    Eligible.Sort([](const auto& A,const auto& B){return A.Rank==B.Rank?A.Id<B.Id:A.Rank<B.Rank;});
    for(const auto& Site:Eligible)
    {
        if(!FCLCompileProgress::Check(Error))return nullptr;
        const auto Column=Family->Rock?Family->Rock->SampleColumn(Site.D):FCLRockColumn();if(Preferred&&!Column.bValid)continue;
        FNetwork N;N.Id=FGuid(0x53504139,uint32(Seed),uint32(S.Seed),Site.Id+1);N.Up=Site.D;Site.D.FindBestAxisVectors(N.X,N.Y);
        FRandomStream Random(int32(Site.Rank));const FQuat Turn(N.Up,Random.FRandRange(0,2*UE_DOUBLE_PI));N.X=Turn.RotateVector(N.X);N.Y=Turn.RotateVector(N.Y);N.Phase=Random.FRandRange(0,6.28);
        const double R=Settings.RadiusMeters,Extent=Settings.LengthMeters*.38;N.UnitScale=UnitScale;N.Spacing=R*6.6+8*UnitScale;
        N.Levels=S.Branching<.3?1:S.Branching<.78?2:3;
        if(Extent*1.8<N.Spacing*1.5/.24)N.Levels=1;
        const int32 Count=4+2*FMath::FloorToInt(S.Branching*2);TArray<TArray<FVector2D>> Centers;
        const double AngleOffset=Random.FRandRange(-.2,.2);
        for(int32 Level=0;Level<N.Levels;++Level)
        {
            TArray<FVector2D> Row;
            for(int32 I=0;I<Count;++I){const double A=AngleOffset+I*2*UE_DOUBLE_PI/Count+Random.FRandRange(-.10,.10),Radius=Extent*Random.FRandRange(.90,1.05);Row.Add({FMath::Cos(A)*Radius,FMath::Sin(A)*Radius});}
            Centers.Add(MoveTemp(Row));
        }
        // Work in the canonical restored bedding coordinate, selecting actual
        // bed contacts only after all spatial footprints fit under real terrain.
        const double RoofBound=R*6+8*UnitScale,Half=Extent+R*10+12*UnitScale;
        double Limit=MAX_dbl;
        for(double X:{-Half,0.,Half})for(double Y:{-Half,0.,Half})
        {
            const FVector D=(Site.D+(N.X*X+N.Y*Y)/Reference).GetSafeNormal();const auto C=Family->Rock?Family->Rock->SampleColumn(D):FCLRockColumn();
            if(Preferred&&!C.bValid){Limit=-1;continue;}
            Limit=FMath::Min(Limit,Base.RadiusMeters+Base.SampleHeightMeters(D)-C.DeformationMeters-FMath::Max(Settings.DepthMeters,RoofBound+Settings.MinimumCoverMeters+6*UnitScale));
        }
        if(Limit<N.Spacing*(N.Levels-1)+R*4||Half>Limit*.65)continue;
        if(Preferred)
        {
            int32 Bed=0;while(Bed<Base.Rock->GetOlderBedCount()&&Base.Rock->GetBed(Bed).UpperMeters<=Limit)++Bed;
            N.Datum=Bed?Base.Rock->GetBed(Bed-1).UpperMeters:Limit;
        }
        else N.Datum=Limit;
        N.Origin=N.Up*(N.Datum+Column.DeformationMeters);
        if(!Preferred){bool Crowded=false;for(const auto& Old:Out.NetworkInfo)Crowded|=FVector::Distance(Old.OriginDirection*Reference,N.Up*Reference)<Half*3;if(Crowded)continue;}
        const int32 NI=Family->Networks.Num();Family->Networks.Add(N);auto& Network=Family->Networks.Last();
        const int32 NodeOffset=Out.Nodes.Num(),PassageOffset=Out.Passages.Num(),ChamberOffset=Out.Chambers.Num();
        auto AddCell=[&](TArray<FVector2D> Polygon,double F0,double F1,FVector2D A,FVector2D B,double Roof,bool Chamber)
        {
            auto& C=Network.Cells.AddDefaulted_GetRef();C.A=A;C.B=B;C.FloorA=F0;C.FloorB=F1;C.Roof=Roof;C.Chamber=Chamber;C.Scale=R/5;C.Phase=Random.FRandRange(0,6.28);C.Center=FVector2D::ZeroVector;
            C.Bounds=FBox(ForceInit);for(const auto& V:Polygon){C.X.Add(V.X);C.Y.Add(V.Y);C.Center+=V;C.Bounds+=FVector(V.X,V.Y,FMath::Min(F0,F1)-4*UnitScale);C.Bounds+=FVector(V.X,V.Y,FMath::Max(F0,F1)+Roof*1.12+3.2*C.Scale+3*UnitScale);}
            C.Center/=Polygon.Num();C.Bounds=C.Bounds.ExpandBy(FVector(10*C.Scale+2*UnitScale,10*C.Scale+2*UnitScale,3*UnitScale));C.Polygon=UE::Geometry::FPolygon2d(Polygon);
        };
        for(int32 L=0;L<Network.Levels;++L)for(int32 I=0;I<Count;++I)
        {
            const auto Center=Centers[L][I];Out.Nodes.Add({Network.Id,Family->BodyPoint(FVector(Center.X,Center.Y,Family->FloorHeight(Center.X,Center.Y,L,NI)+2*UnitScale),NI),{R*5,R*3},true,false});
            TArray<FVector2D> Polygon;const double RX=R*Random.FRandRange(4.8,8.0),RY=R*Random.FRandRange(3.2,6.5),Spin=Random.FRandRange(0,6.28);const FVector2D X(FMath::Cos(Spin),FMath::Sin(Spin)),Y(-X.Y,X.X);
            for(int32 K=0;K<12;++K){const double A=K*2*UE_DOUBLE_PI/12,J=Random.FRandRange(.78,1.18);Polygon.Add(Center+(X*(FMath::Cos(A)*RX)+Y*(FMath::Sin(A)*RY))*J);}
            const double Roof=R*Random.FRandRange(3.2,4.6);AddCell(Polygon,-L*Network.Spacing,-L*Network.Spacing,Center,Center,Roof,true);
            FCLCaveChamber Room;Room.Network=Network.Id;Room.Node=NodeOffset+L*Count+I;Room.Center=Family->BodyPoint({Center.X,Center.Y,-L*Network.Spacing+Roof*.5},NI);Room.HalfExtents={RX,RY,Roof*.5};Room.Rotation=FRotationMatrix::MakeFromXZ(Network.X,Network.Up).ToQuat();Room.Form=ECLCaveForm::BeddedChamber;Room.GeometryProfile=9;Room.FloorRock=Base.Rock;Room.FloorRadius=Network.Datum-L*Network.Spacing;Out.Chambers.Add(Room);
        }
        auto Passage=[&](int32 L0,int32 I0,int32 L1,int32 I1,double RampBend=0.,bool Entrance=false,FVector2D Mouth=FVector2D::ZeroVector,double MouthFloor=0.)
        {
            const auto A=Centers[L0][I0],B=Entrance?Mouth:Centers[L1][I1],Delta=B-A;const double Length=Delta.Length();const FVector2D Side(-Delta.Y/Length,Delta.X/Length);
            const double Bend=RampBend!=0?RampBend:R*Random.FRandRange(-1.4,1.4),Width=R*Random.FRandRange(2.1,3.1);TArray<FVector2D> Left,Right,Route;
            for(int32 K=0;K<=6;++K){const double T=K/6.,Wave=FMath::Sin(T*UE_DOUBLE_PI);const auto P=A+Delta*T+Side*(Bend*Wave);const double W=Width*Random.FRandRange(.82,1.22);const auto Cap=Entrance&&K==6?Delta/Length*(Width*2):FVector2D::ZeroVector;Left.Add(P+Side*W+Cap);Right.Add(P-Side*W+Cap);Route.Add(P);}
            TArray<FVector2D> Polygon=Left;for(int32 K=Right.Num()-1;K>=0;--K)Polygon.Add(Right[K]);
            AddCell(Polygon,-L0*Network.Spacing,Entrance?MouthFloor:-L1*Network.Spacing,Entrance?A-Delta*.2:A,Entrance?B+Delta*.3:B,R*Random.FRandRange(1.8,2.6),false);
            const auto& C=Network.Cells.Last();FCLCavePassage P;P.Network=Network.Id;P.From=NodeOffset+L0*Count+I0;P.To=Entrance?Out.Nodes.Num():NodeOffset+L1*Count+I1;P.Entrance=Entrance;P.Form=P.EndForm=ECLCaveForm::BeddedPassage;P.GeometryProfile=9;P.FloorRock=Base.Rock;
            for(int32 K=1;K<Route.Num();++K){const int32 Steps=FMath::Max(1,FMath::CeilToInt(FVector2D::Distance(Route[K-1],Route[K])/2));for(int32 J=K==1?0:1;J<=Steps;++J){const auto Q=FMath::Lerp(Route[K-1],Route[K],double(J)/Steps);const double F=Family->CellFloor(Network,C,Q.X,Q.Y);P.Points.Add(Family->BodyPoint({Q.X,Q.Y,F+2*UnitScale},NI));P.Sections.Add({Width,C.Roof*.5});P.FloorRadii.Add(Network.Datum+F);}}
            if(Entrance){Out.Nodes.Add({Network.Id,P.Points.Last(),{Width,C.Roof*.5},false,true});++Out.Entrances;}
            Out.Passages.Add(MoveTemp(P));
        };
        // A closed irregular gallery on each level supplies a return route.
        for(int32 L=0;L<Network.Levels;++L)for(int32 I=0;I<Count;++I)Passage(L,I,L,(I+1)%Count);
        for(int32 L=0;L+1<Network.Levels;++L){Passage(L,0,L+1,Count/2,R*6);Passage(L,0,L+1,Count/2,-R*6);}
        FBox InteriorBounds(ForceInit);for(const auto& C:Network.Cells)InteriorBounds+=C.Bounds;
        if(S.bSurfaceEntrances)
        {
            const auto A=Centers[0][0];double Best=MAX_dbl,EndFloor=0;FVector2D Mouth;
            for(int32 K=0;K<16;++K)
            {
                const double Angle=K*2*UE_DOUBLE_PI/16;const FVector2D Along(FMath::Cos(Angle),FMath::Sin(Angle));
                if(FVector2D::DotProduct(Along,A)<A.Length()*.5)continue;
                const double Run=FMath::Min(Reference*.45,FMath::Max(Settings.LengthMeters*2,Settings.DepthMeters*12));
                const auto End=A+Along*Run;double Floor=0;
                for(int32 J=0;J<6;++J)
                {
                    const auto P=Family->BodyPoint({End.X,End.Y,Floor+Family->RollingFloor(Network,End.X,End.Y)},NI);const auto D=P.GetSafeNormal();
                    const auto Surface=D*(Base.RadiusMeters+Base.SampleHeightMeters(D));
                    Floor=(Family->Rock?Family->Rock->RestoredRadiusMeters(Surface):Surface.Length())-Network.Datum-Family->RollingFloor(Network,End.X,End.Y)-.1*UnitScale;
                }
                const double Grade=FMath::Abs(Floor)/Run;bool Foundation=Grade<.20;
                for(int32 J=0;J<=24&&Foundation;++J)
                {
                    const double T=J/24.,Ramp=FMath::Clamp((T-.1)/.9,0.,1.);const auto XY=FMath::Lerp(A,End,T);
                    const double Height=Floor*Ramp*Ramp*(3-2*Ramp)+Family->RollingFloor(Network,XY.X,XY.Y);
                    const auto P=Family->BodyPoint({XY.X,XY.Y,Height},NI);
                    Foundation=Base.RadiusMeters+Base.SampleHeightMeters(P.GetSafeNormal())-P.Length()>=.04*UnitScale;
                }
                if(Foundation&&Grade<Best){Best=Grade;Mouth=End;EndFloor=Floor;}
            }
            if(Best==MAX_dbl){Out.Nodes.SetNum(NodeOffset);Out.Passages.SetNum(PassageOffset);Out.Chambers.SetNum(ChamberOffset);Family->Networks.Pop();continue;}
            Passage(0,0,0,0,0.,true,Mouth,EndFloor);
        }
        // Validate the actual walking foundation at the route's own spacing,
        // including terrain dips missed by a broad roof/placement grid.
        bool FloorSupported=true;
        for(int32 I=PassageOffset;I<Out.Passages.Num()&&FloorSupported;++I)for(const auto& Q:Out.Passages[I].Points)
        {
            const auto Foot=Q-Q.GetSafeNormal()*(2.5*UnitScale);
            if(Base.RadiusMeters+Base.SampleHeightMeters(Foot.GetSafeNormal())-Foot.Length()<.04*UnitScale){FloorSupported=false;break;}
        }
        if(!FloorSupported){if(S.bSurfaceEntrances)--Out.Entrances;Out.Nodes.SetNum(NodeOffset);Out.Passages.SetNum(PassageOffset);Out.Chambers.SetNum(ChamberOffset);Family->Networks.Pop();continue;}
        Network.BodyBounds=FBox(ForceInit);FBox LocalBounds(ForceInit);
        FVoxelAABBTree::FElementArray Elements;Elements.Reserve(Network.Cells.Num());
        for(int32 I=0;I<Network.Cells.Num();++I){LocalBounds+=Network.Cells[I].Bounds;Elements.Add(FVoxelBox(Network.Cells[I].Bounds),I);}
        Network.Index=FVoxelAABBTree::Create(MoveTemp(Elements));
        // Compile a conservative 2-D regional bound, never voxel geometry.
        // Native Perlin is bounded by 2: each lattice gradient is the sum of
        // two offsets in [-1,1], followed by convex interpolation and a
        // normalization factor <1. Thus |warp| <= hypot(6.25,11.6)*scale;
        // relief + undercut + detail <= (1.25+1.4+.24)*scale.
        // 16.2 exceeds that sum. Smooth minima cannot increase the field.
        Network.PlanarSupportPadding=16.2*(R/5);
        Network.PlanarBounds=FBox2D(FVector2D(LocalBounds.Min.X,LocalBounds.Min.Y),FVector2D(LocalBounds.Max.X,LocalBounds.Max.Y));
        const auto Span=Network.PlanarBounds.Max-Network.PlanarBounds.Min;const double TileMeters=FMath::Max(1.,4*R/5);
        Network.PlanarSize={FMath::Clamp(FMath::CeilToInt(Span.X/TileMeters),1,512),FMath::Clamp(FMath::CeilToInt(Span.Y/TileMeters),1,512)};
        Network.PlanarStep=Span/FVector2D(Network.PlanarSize.X,Network.PlanarSize.Y);
        const double TileRadius=Network.PlanarStep.Length()*.5;
        Network.PlanarDistanceLowerBounds.SetNumUninitialized(Network.PlanarSize.X*Network.PlanarSize.Y);
        for(int32 Y=0;Y<Network.PlanarSize.Y;++Y)for(int32 X=0;X<Network.PlanarSize.X;++X)
        {
            const FVector2D Center=Network.PlanarBounds.Min+Network.PlanarStep*FVector2D(X+.5,Y+.5);double Nearest=MAX_dbl;
            for(const auto& C:Network.Cells){if(C.Polygon.Contains(Center)){Nearest=0;break;}Nearest=FMath::Min(Nearest,C.Polygon.DistanceSquared(Center));}
            Network.PlanarDistanceLowerBounds[X+Y*Network.PlanarSize.X]=FMath::Max(0.,FMath::Sqrt(Nearest)-TileRadius-1.e-6);
        }
        bool SupportMapped=true;
        for(auto& C:Network.Cells)
        {
            C.BodyBounds=FBox(ForceInit);const auto Mid=C.Bounds.GetCenter();
            for(double X:{C.Bounds.Min.X,Mid.X,C.Bounds.Max.X})for(double Y:{C.Bounds.Min.Y,Mid.Y,C.Bounds.Max.Y})for(double Z:{C.Bounds.Min.Z,Mid.Z,C.Bounds.Max.Z}){const FVector Q=Family->BodyPoint({X,Y,Z},NI);SupportMapped&=FVector::DotProduct(Q,Network.Up)>0&&FMath::Abs((Family->Rock?Family->Rock->RestoredRadiusMeters(Q):Q.Length())-Network.Datum-Z)<.002;C.BodyBounds+=Q;}
            C.BodyBounds=C.BodyBounds.ExpandBy(12*UnitScale);Network.BodyBounds+=C.BodyBounds;
        }
        // Immutable primitive-bound address grid. Triangle inequality retains
        // every box that can be nearest anywhere in each tile. This indexes
        // analytic support only; no density, mesh, collider or edit is cached.
        check(Network.Cells.Num()<=64);
        FBox SupportDomain(ForceInit);
        for(const auto& C:Network.Cells)
        {
            const auto B=C.BodyBounds.ExpandBy(.002);SupportDomain+=B;
            Network.BodyBoxes.Append({float(B.Min.X),float(B.Min.Y),float(B.Min.Z),float(B.Max.X),float(B.Max.Y),float(B.Max.Z)});
        }
        const auto SupportSpan=SupportDomain.GetSize();const double SupportStep=FMath::Max(16.,SupportSpan.GetMax()/64.);
        const FIntVector SupportSize(FMath::Max(1,FMath::CeilToInt(SupportSpan.X/SupportStep)),FMath::Max(1,FMath::CeilToInt(SupportSpan.Y/SupportStep)),FMath::Max(1,FMath::CeilToInt(SupportSpan.Z/SupportStep)));
        const FVector SupportCell=SupportSpan/FVector(SupportSize);const double SupportRadius=SupportCell.Length()*.5+.01;
        Network.SupportGrid.Append({float(SupportDomain.Min.X),float(SupportDomain.Min.Y),float(SupportDomain.Min.Z),float(SupportDomain.Max.X),float(SupportDomain.Max.Y),float(SupportDomain.Max.Z),float(SupportCell.X),float(SupportCell.Y),float(SupportCell.Z)});
        Network.SupportSize.Append({SupportSize.X,SupportSize.Y,SupportSize.Z});
        for(int32 Z=0;Z<SupportSize.Z;++Z)for(int32 Y=0;Y<SupportSize.Y;++Y)for(int32 X=0;X<SupportSize.X;++X)
        {
            const FVector Center=SupportDomain.Min+SupportCell*FVector(X+.5,Y+.5,Z+.5);double Nearest=MAX_dbl;TArray<double,TInlineAllocator<64>> Distances;
            for(const auto& C:Network.Cells){const double D=FMath::Sqrt(C.BodyBounds.ExpandBy(.002).ComputeSquaredDistanceToPoint(Center));Distances.Add(D);Nearest=FMath::Min(Nearest,D);}
            uint64 Mask=0;for(int32 I=0;I<Distances.Num();++I)if(Distances[I]<=Nearest+2*SupportRadius)Mask|=uint64(1)<<I;
            Network.SupportMasks.Add(Mask);
        }
        bool Fit=SupportMapped;for(const auto& Old:Out.NetworkInfo)if(Old.InspectionBounds.Intersect(Network.BodyBounds))Fit=false;
        // Verify true upper support, not the centre of a chamber or a route.
        for(double X=InteriorBounds.Min.X;X<=InteriorBounds.Max.X;X+=FMath::Max(12*UnitScale,R*4))for(double Y=InteriorBounds.Min.Y;Y<=InteriorBounds.Max.Y;Y+=FMath::Max(12*UnitScale,R*4))
        {const auto P=Family->BodyPoint({X,Y,RoofBound},NI);if(Base.RadiusMeters+Base.SampleHeightMeters(P.GetSafeNormal())-P.Length()<Settings.MinimumCoverMeters)Fit=false;}
        if(!Fit){if(S.bSurfaceEntrances)--Out.Entrances;Out.Nodes.SetNum(NodeOffset);Out.Passages.SetNum(PassageOffset);Out.Chambers.SetNum(ChamberOffset);Family->Networks.Pop();continue;}
        for(int32 I=0;Family->Rock&&I<Base.Rock->GetOlderBedCount();++I){const double H=Base.Rock->GetBed(I).UpperMeters-Network.Datum;if(H>=LocalBounds.Min.Z-4&&H<=LocalBounds.Max.Z+4)Network.Contacts.Add(H);}
        FBox2D Chart(ForceInit);for(const auto& Cell:Network.Cells)for(int32 K=0;K<8;++K){const FVector P(K&1?Cell.BodyBounds.Max.X:Cell.BodyBounds.Min.X,K&2?Cell.BodyBounds.Max.Y:Cell.BodyBounds.Min.Y,K&4?Cell.BodyBounds.Max.Z:Cell.BodyBounds.Min.Z);const double Along=FVector::DotProduct(P,Network.Up);if(Along<=UnitScale){Error=TEXT("Spatial family crosses its bedding chart horizon; reduce dimensions");return nullptr;}Chart+=FVector2D(FVector::DotProduct(P,Network.X),FVector::DotProduct(P,Network.Y))*(Network.Datum/Along);}
        const auto Rock=Family->Rock;const auto X=Network.X,Y=Network.Y,Up=Network.Up;const double Datum=Network.Datum;
        Network.Bedding.SetMaximumDepth(12);
        Network.Bedding.Build(Chart.ExpandBy(1),[Rock,X,Y,Up,Datum](FVector2D P){const auto Q=(Up+X*(P.X/Datum)+Y*(P.Y/Datum)).GetSafeNormal()*Datum;return Datum-(Rock?Rock->RestoredRadiusMeters(Q):Q.Length());});
        Network.Bedding.BuildLookup();Network.Bedding.BuildPacked();
        Network.Basement.SetMaximumDepth(6);
        Network.Basement.Build(Chart.ExpandBy(1),[Rock,X,Y,Up,Datum](FVector2D P){return Rock?Rock->SampleColumn((Up+X*(P.X/Datum)+Y*(P.Y/Datum)).GetSafeNormal()).RestoredBasementRadiusMeters:0.;},{},.05);
        Network.Basement.BuildLookup();
        for(const auto& C:Network.Cells)
        {
            Network.Starts.Add(Network.VertexX.Num());Network.Counts.Add(C.X.Num());Network.VertexX.Append(C.X);Network.VertexY.Append(C.Y);
            for(int32 J=0;J<C.X.Num();++J)
            {
                const int32 K=(J+1)%C.X.Num();const float X0=C.X[J],Y0=C.Y[J],X1=C.X[K],Y1=C.Y[K],DX=X1-X0,DY=Y1-Y0;
                Network.Edges.Append({X0,Y0,DX,DY,1.f/(DX*DX+DY*DY),DY!=0?DX/DY:0.f,Y1,0.f});
            }
            // Exact closest-edge candidates in a finite 2-D address grid.
            // If d(C,e)>d(C,nearest)+2r, edge e cannot be nearest anywhere
            // in this tile. Every segment crossing C->P is also retained.
            check(C.X.Num()<=16);
            const double EdgeTileMeters=FMath::Max(2.,8*C.Scale);
            const FBox2D EdgeBounds(FVector2D(C.Bounds.Min.X,C.Bounds.Min.Y)-FVector2D(14*C.Scale+1),FVector2D(C.Bounds.Max.X,C.Bounds.Max.Y)+FVector2D(14*C.Scale+1));
            const auto EdgeSpan=EdgeBounds.Max-EdgeBounds.Min;
            const int32 Width=FMath::Clamp(FMath::CeilToInt(EdgeSpan.X/EdgeTileMeters),1,128),Height=FMath::Clamp(FMath::CeilToInt(EdgeSpan.Y/EdgeTileMeters),1,128);
            const float MinX=EdgeBounds.Min.X,MinY=EdgeBounds.Min.Y,StepX=EdgeSpan.X/Width,StepY=EdgeSpan.Y/Height;
            Network.EdgeTiles.Append({Network.EdgeMasks.Num(),Width,Height});Network.EdgeGrids.Append({MinX,MinY,StepX,StepY,1.f/StepX,1.f/StepY});
            const double RoundingGuard=FMath::Max(.001,EdgeBounds.Max.GetAbs().GetMax()*1.e-6);
            const double EdgeRadius=FVector2D(StepX,StepY).Length()*.5+RoundingGuard;
            for(int32 TY=0;TY<Height;++TY)for(int32 TX=0;TX<Width;++TX)
            {
                const FVector2D Center(double(MinX)+(TX+.5)*StepX,double(MinY)+(TY+.5)*StepY);TArray<double,TInlineAllocator<16>> Distances;double Nearest=MAX_dbl;
                for(int32 J=0;J<C.X.Num();++J)
                {
                    const int32 K=(J+1)%C.X.Num();const FVector2D A(float(C.X[J]),float(C.Y[J])),B(float(C.X[K]),float(C.Y[K]));const auto D=B-A;
                    const double T=FMath::Clamp(FVector2D::DotProduct(Center-A,D)/D.SquaredLength(),0.,1.);const double Distance=(Center-A-D*T).Length();Distances.Add(Distance);Nearest=FMath::Min(Nearest,Distance);
                }
                const bool Fallback=Nearest<4*RoundingGuard;uint32 Mask=0;
                for(int32 J=0;J<C.X.Num();++J)if(Fallback||Distances[J]<=Nearest+2*EdgeRadius)Mask|=uint32(1)<<J;
                const bool Inside=C.Polygon.Contains(Center);
                if(Inside)Mask|=0x40000000u;if(Fallback)Mask|=0x80000000u;Network.EdgeMasks.Add(Mask);

            }
            Network.Params.Append({C.A.X,C.A.Y,C.B.X,C.B.Y,C.FloorA,C.FloorB,C.Center.X,C.Center.Y,C.Roof,C.Scale,Network.UnitScale,C.Chamber?1.:0.,C.Bounds.Min.X,C.Bounds.Min.Y,C.Bounds.Min.Z,C.Bounds.Max.X,C.Bounds.Max.Y,C.Bounds.Max.Z});
        }
        FCLCompiledCaves::FNetwork Info;Info.Id=Network.Id;Info.Family=Preferred?FCLCompiledCaves::EFamily::CarbonateKarst:FCLCompiledCaves::EFamily::General;Info.OriginDirection=Network.Up;Info.InceptionRadiusMeters=Network.Datum;Info.InspectionBounds=Network.BodyBounds;if(Base.GeneratedRegions){const auto& G=*Base.GeneratedRegions;Info.Landform=G.Palette[G.Cells[G.Owner(Network.Up)].PaletteIndex].Id;}Out.NetworkInfo.Add(Info);Family->AllBounds+=Network.BodyBounds;++Out.Networks;
        if(Out.Networks>=Out.RequestedNetworks)break;
    }
    if(Family->Networks.IsEmpty())
    {
        if(Preferred&&FCLCompileProgress::Check(Error)){Out=FCLCompiledCaves();Error.Reset();return Build(Base,S,Seed,Out,Error,false);}
        Error=TEXT("No complete spatial cave fits the physical terrain");return nullptr;
    }
    FVoxelAABBTree::FElementArray Elements;int32 Total=0;for(const auto& N:Family->Networks)Total+=N.Cells.Num();Elements.Reserve(Total);
    for(int32 I=0;I<Family->Networks.Num();++I)for(int32 J=0;J<Family->Networks[I].Cells.Num();++J){const int32 F=Family->Features.Add({I,J});Elements.Add(FVoxelBox(Family->Networks[I].Cells[J].BodyBounds),F);}Family->Index=FVoxelAABBTree::Create(MoveTemp(Elements));
    int32 Levels,Cells,Ramps,Nodes,Exact;uint64 Bytes;double E;Family->Stats(Levels,Cells,Ramps,Nodes,Exact,Bytes,E);
    Out.Readiness=FString::Printf(TEXT("%d/%d complete spatial cave networks, up to %d interconnected levels, %d galleries and %d gradual ramps; %d/%d eligible sites"),Out.Networks,Out.RequestedNetworks,Levels,Out.Passages.Num(),Ramps,Out.EligibleCandidates,Out.Candidates);
    return Family;
}

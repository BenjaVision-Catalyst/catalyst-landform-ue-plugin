#include "Model/CLSpatialCave.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "CLSpatialCaveFamily.h"
#include "Async/ParallelFor.h"
#include "Model/CLSphericalPatches.h"
#include "Model/CLDirectionalScalarField.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLProvinceDefinition.h"
#include "Model/CLRegionGeneration.h"
#include "Model/CLWatershed.h"
#include "CLNativeNoiseKernel.h"
#include "Polygon2.h"
#include "Math/RotationMatrix.h"
#include "VoxelAABBTree.h"
#include <atomic>
#if CL_SPATIAL_SIMD
#include "CLSpatialMorph.generated.h"
#endif
#if PLATFORM_MAC && PLATFORM_CPU_ARM_FAMILY && !CL_SPATIAL_SIMD
#error Mac arm64 Landform requires the spatial SIMD build rules
#endif

class FCLSpatialCaveData
{
public:
    struct FRegion {UE::Geometry::FPolygon2d Polygon;FBox2D Bounds;};
    TSharedPtr<const FCLSpatialCaveFamily> Family;
    TArray<FRegion> Regions;
    TArray<double> VertexX,VertexY;TArray<int32> PolygonStarts,PolygonCounts;
    // Empty for the frozen study. Generated regions carry spatial roof pockets,
    // retained masses and level/ramp datums through the same morphology kernel.
    // SIMD ABI: four network scalars (floor phase, bank X/Y, level spacing),
    // then 28 scalars per joint: floor projection [0..6], roof/pockets [7..17],
    // retained mass [18..23], and conservative XY bounds [24..27]. FloorGroups
    // join every joint of a level or ramp before applying its rolling floor.
    TArray<double> Morphology;TArray<int32> FloorGroups,FloorSources;
    TArray<FCLCompiledCaves::FNode> GeneratedNodes;
    TArray<FCLCavePassage> GeneratedPassages;
    TArray<FCLCaveChamber> GeneratedChambers;
    int32 Levels=0,Ramps=0;
    double PlacementMin=0,PlacementMax=MAX_dbl,SurfaceClearance=0;
    bool SurfaceOpening=false;
    TSharedPtr<const FCLCompiledRock> Rock,ContactRock;
    TSharedPtr<const FCLNativeNoiseKernel> Noise;
    FVector Origin,X,Y,Up;
    double Datum=0,Scale=1,ContactDatum=0,MinimumHalfWidth=2;
    int32 Seed=0;
    bool bFinalSurfaceCompiled=false;
    TArray<double> Contacts;
    FBox Bounds;
    TArray<FBox> InspectionSupports;
    TSharedPtr<const FVoxelAABBTree> SupportIndex;
    FCLDirectionalScalarField Bedding,Surface[6];
    mutable std::atomic<uint64> SurfaceInterpolated{0},SurfaceExact{0};
    mutable std::atomic<uint64> EvaluationCycles[4]{};

};
namespace
{
double SmoothMin(double A,double B,double K)
{
    const double H=FMath::Clamp(.5+.5*(B-A)/K,0.,1.);
    return FMath::Lerp(B,A,H)-K*H*(1-H);
}
double Hill(double X,double Y,double CX,double CY,double RX,double RY)
{
    return FMath::Exp(-FMath::Square((X-CX)/RX)-FMath::Square((Y-CY)/RY));
}
double RoomWeight(double X)
{
    return FMath::SmoothStep(-24.,18.,X);
}
// Shared sediment/bedrock relief across every footprint and level of a network.
// The level datum is added separately; adjoining regions never acquire their
// own clipped floor or independently phased noise.
double FamilyRelief(const TArray<double>& M,double X,double Y)
{
    const double Phase=M[0];
    return -.004*X+1.0*FMath::Sin(X*.035+Y*.021+Phase)
        +.60*FMath::Cos(Y*.07-X*.018+Phase*.7)+.10*FMath::Sin(X*.21+Y*.19)
        +3.0*Hill(X,Y,M[1],M[2],35,22)
        +2.1*Hill(X,Y,M[1]-65,M[2]-31,30,17)
        +2.7*Hill(X,Y,M[1]+75,M[2]+36,32,20)
        -.8*FMath::Exp(-FMath::Square((Y-8*FMath::Sin(X*.012+Phase))/8));
}
double RegionDatum(const double* M,double X,double Y)
{
    double T=FMath::Clamp(((X-M[2])*M[4]+(Y-M[3])*M[5])/M[6],0.,1.);
    if(M[1]!=0)T+=.6*FMath::Sin(2*UE_DOUBLE_PI*T)/(2*UE_DOUBLE_PI);
    return M[0]+M[1]*FMath::SmoothStep(0.,1.,T);
}
}
FVector FCLSpatialCave::BeddingPoint(const FVector& P,bool bCompiled)const
{
    if(Data->Family)return Data->Family->BeddingPoint(P,0,bCompiled);
    const auto D=P-Data->Origin;
    const double Along=FVector::DotProduct(P,Data->Up);
    const double Restored=bCompiled&&Data->Rock&&Data->Bedding.NumNodes()>0&&Along>1.e-6?P.Length()-Data->Bedding.Sample(FVector2D(FVector::DotProduct(P,Data->X),FVector::DotProduct(P,Data->Y))*(Data->Datum/Along)):Data->Rock?Data->Rock->RestoredRadiusMeters(P):P.Length();
    return FVector(FVector::DotProduct(D,Data->X),FVector::DotProduct(D,Data->Y),Restored-Data->Datum)/Data->Scale;
}
FVector FCLSpatialCave::BodyPoint(const FVector& P)const
{
    if(Data->Family)return Data->Family->BodyPoint(P);
    // Invert the same canonical restored coordinate, retaining planetary curvature.
    FVector Q=Data->Origin+(Data->X*P.X+Data->Y*P.Y+Data->Up*P.Z)*Data->Scale;
    for(int32 I=0;I<5;++I)
    {
        const double Error=Data->Datum+P.Z*Data->Scale-(Data->Rock?Data->Rock->RestoredRadiusMeters(Q):Q.Length());
        Q+=Data->Up*(Error/FMath::Max(.5,FVector::DotProduct(Q.GetSafeNormal(),Data->Up)));
    }
    return Q;
}
double FCLSpatialCave::FloorHeight(double X,double Y)const
{
    if(Data->Family)return Data->Family->FloorHeight(X,Y);
    if(!Data->Morphology.IsEmpty())return FamilyRelief(Data->Morphology,X,Y);
    // One sediment/bedrock surface over the whole connected space. No per-room
    // clipping, route clamp, or independently joined floor patches.
    const double Bank=2.1*Hill(X,Y,-53,-9,24,6)+3.3*Hill(X,Y,35,24,25,13)+2.7*Hill(X,Y,55,-21,18,11);
    const double Channel=-.9*FMath::Exp(-FMath::Square((Y-1.8*FMath::Sin(X*.035))/4.8));
    const double Rolling=.55*FMath::Sin(X*.052+Y*.029)+.32*FMath::Cos(Y*.12-X*.018)+.10*FMath::Sin(X*.31+Y*.23);
    return -.012*X-1.4*RoomWeight(X)+Bank+Channel+Rolling;
}
void FCLSpatialCave::GetBeddingCompilationStats(int32& Nodes,int32& ExactLeaves,uint64& Bytes,double& Error)const
{if(Data->Family){int32 L,C,R;Data->Family->Stats(L,C,R,Nodes,ExactLeaves,Bytes,Error);return;}Nodes=Data->Bedding.NumNodes();ExactLeaves=Data->Bedding.NumExactLeaves();Bytes=Data->Bedding.GetAllocatedSize();Error=Data->Bedding.MaximumValidationError();}
void FCLSpatialCave::GetSurfaceCompilationStats(int32& Nodes,int32& ExactLeaves,uint64& Bytes,uint64& Interpolated,uint64& Exact)const
{Nodes=ExactLeaves=0;Bytes=0;for(const auto& S:Data->Surface){Nodes+=S.NumNodes();ExactLeaves+=S.NumExactLeaves();Bytes+=S.GetAllocatedSize();}Interpolated=Data->SurfaceInterpolated.load();Exact=Data->SurfaceExact.load();}
void FCLSpatialCave::SampleSurfaceHeights(const FCLCompiledPlanetRecipe& P,TConstArrayView<FVector> Points,TArray<double>& Heights)const
{
    Heights.SetNumUninitialized(Points.Num());TArray<FVector> Needed;TArray<int32> Indices;
    for(int32 I=0;I<Points.Num();++I)
    {
        const FVector& Q=Points[I];const int32 Face=FCLSphericalPatches::Owner(Q);FVector2D UV;
        if(Data->Surface[Face].NumNodes()>0&&FCLSphericalPatches::Coordinates(Face,Q,UV)&&Data->Surface[Face].TrySample(UV,Heights[I]))continue;
        Needed.Add(Q);Indices.Add(I);
    }
    if(!Needed.IsEmpty())
    {
        TArray<double> Values;(P.WatershedSurface&&!Data->bFinalSurfaceCompiled?P.WatershedSurface.Get():&P)->SampleHeightsMeters(Needed,Values);
        for(int32 I=0;I<Indices.Num();++I)Heights[Indices[I]]=Values[I];
    }
    if(P.WatershedSurface&&!Data->bFinalSurfaceCompiled)P.Watershed->MapHeights(Points,Heights);
    Data->SurfaceInterpolated.fetch_add(Points.Num()-Needed.Num(),std::memory_order_relaxed);Data->SurfaceExact.fetch_add(Needed.Num(),std::memory_order_relaxed);
}
void FCLSpatialCave::GetEvaluationSeconds(TArray<double>& Seconds)const
{
    Seconds.Reset();for(const auto& Cycles:Data->EvaluationCycles)Seconds.Add(FPlatformTime::ToSeconds64(Cycles.load()));
}
void FCLSpatialCave::GetCellSupportBounds(TArray<FBox>& Out)const
{Out.Reset();if(Data->Family)Data->Family->CellSupportBounds(Out);else Out.Add(Data->Bounds);}
void FCLSpatialCave::GetInspectionSupportBounds(TArray<FBox>& Out,FGuid Id)const
{if(Data->Family)Data->Family->InspectionSupportBounds(Out,Id);else if(Data->InspectionSupports.IsEmpty())Out.Add(Data->Bounds);else Out.Append(Data->InspectionSupports);}
double FCLSpatialCave::ExteriorDistance(const FVector& P)const
{
    if(!Data->SupportIndex)return FMath::Sqrt(Data->Bounds.ComputeSquaredDistanceToPoint(P));
    double Best=MAX_dbl;
    Data->SupportIndex->Traverse([&](const FVoxelFastBox& B){return B.GetBox().SquaredDistanceToPoint(P)<Best;},[&](int32 I){Best=FMath::Min(Best,Data->InspectionSupports[I].ComputeSquaredDistanceToPoint(P));});
    return FMath::Sqrt(Best);
}
FBox FCLSpatialCave::Bounds(FGuid Network)const{return Data->Family?Data->Family->Bounds(Network):Data->Bounds;}
double FCLSpatialCave::MinimumHalfWidth(FGuid Network)const{return Data->Family?Data->Family->MinimumHalfWidth(Network):Data->MinimumHalfWidth;}
bool FCLSpatialCave::IsProceduralFamily()const{return Data->Family.IsValid();}
bool FCLSpatialCave::TryPreparedSurfaces(const FVector& P,FCLSurfaceWeights& W)const{return Data->Family&&Data->Family->TrySurfaces(P,W);}
void FCLSpatialCave::GetPreparedGeologyStats(uint64& Prepared,uint64& Exact)const{Prepared=Exact=0;if(Data->Family)Data->Family->GeologyStats(Prepared,Exact);}
void FCLSpatialCave::GetFamilyStats(int32& Levels,int32& Cells,int32& Ramps)const
{Levels=Data->Levels;Cells=Data->Regions.Num();Ramps=Data->Ramps;if(Data->Family){int32 N,E;uint64 B;double Error;Data->Family->Stats(Levels,Cells,Ramps,N,E,B,Error);}}
void FCLSpatialCave::SampleSelected(TConstArrayView<FVector> P,TArray<double>& Air,const TSet<FGuid>& Selection)const
{if(Data->Family)Data->Family->Sample(P,Air,true,&Selection);else Sample(P,Air);}
void FCLSpatialCave::Sample(TConstArrayView<FVector> P,TArray<double>& Air,bool bCompiledInputs)const
{
    if(Data->Family){const uint64 Start=FPlatformTime::Cycles64();Data->Family->Sample(P,Air,bCompiledInputs);Data->EvaluationCycles[2].fetch_add(FPlatformTime::Cycles64()-Start,std::memory_order_relaxed);return;}
    Air.SetNumUninitialized(P.Num());TArray<int32> Active;TArray<FVector> Local,NoisePoints;TArray<double> ContactZ;
    for(int32 I=0;I<P.Num();++I)
    {
        // The compiler verifies a broad, finite 3-D support envelope. Outside it
        // the cavity is closed, and a cheap distance lower bound rejects rock.
        const double Outside=FMath::Sqrt(Data->Bounds.ComputeSquaredDistanceToPoint(P[I]));
        if(Outside>0){Air[I]=-Outside-1;continue;}
        // The joint envelopes include wall warp, undercuts, roof pockets and
        // a further twelve scaled metres of padding. Empty space between them
        // needs a finite conservative distance, not exact geological inversion
        // for every sample in the much larger whole-network box.
        if(Data->SupportIndex&&!Data->Rock)
        {
            const double JointDistance=ExteriorDistance(P[I]);
            if(JointDistance>0){Air[I]=-(JointDistance+8*Data->Scale)*.6;continue;}
        }
        Active.Add(I);
    }
    uint64 Stage=FPlatformTime::Cycles64();
    for(int32 I:Active)Local.Add(BeddingPoint(P[I],bCompiledInputs));
    if(!Data->Morphology.IsEmpty())for(int32 K=0;K<Active.Num();++K)
    {
        const int32 I=Active[K];
        if(Data->ContactRock&&Data->Rock==Data->ContactRock)
        {
            ContactZ.Add(Local[K].Z+(Data->Datum-Data->ContactDatum)/Data->Scale);continue;
        }
        const auto& Q=P[I];double Restored=Q.Length();
        if(Data->ContactRock)
        {
            const double Along=FVector::DotProduct(Q,Data->Up);
            Restored=bCompiledInputs&&Data->Bedding.NumNodes()>0&&Along>1.e-6?Q.Length()-Data->Bedding.Sample(FVector2D(FVector::DotProduct(Q,Data->X),FVector::DotProduct(Q,Data->Y))*(Data->Datum/Along)):Data->ContactRock->RestoredRadiusMeters(Q);
        }
        ContactZ.Add((Restored-Data->ContactDatum)/Data->Scale);
    }
    Data->EvaluationCycles[0].fetch_add(FPlatformTime::Cycles64()-Stage,std::memory_order_relaxed);Stage=FPlatformTime::Cycles64();
    NoisePoints.Reserve(Active.Num()*3);
    // Fixed spatial wavelengths. No nearest-key frame or per-section phase.
    for(const auto& Q:Local)NoisePoints.Add(Q/FVector(16,13,9)+FVector(71,-39,113));
    for(const auto& Q:Local)NoisePoints.Add(Q/FVector(4.2,4.2,2.1)+FVector(-31,51,19));
    for(const auto& Q:Local)NoisePoints.Add(Q/.65+FVector(13,91,-43));
    TArray<float> Noise;Data->Noise->SampleBatch(NoisePoints,Data->Seed,Noise);
    Data->EvaluationCycles[1].fetch_add(FPlatformTime::Cycles64()-Stage,std::memory_order_relaxed);Stage=FPlatformTime::Cycles64();
    const int32 N=Local.Num();
#if CL_SPATIAL_SIMD
    if(bCompiledInputs&&N>0)
    {
        TArray<double> X,Y,Z,Values;X.SetNumUninitialized(N);Y.SetNumUninitialized(N);Z.SetNumUninitialized(N);Values.SetNumUninitialized(N);
        for(int32 I=0;I<N;++I){X[I]=Local[I].X;Y[I]=Local[I].Y;Z[I]=Local[I].Z;}
        ispc::CLSpatialMorph(X.GetData(),Y.GetData(),Z.GetData(),Noise.GetData(),Data->VertexX.GetData(),Data->VertexY.GetData(),Data->PolygonStarts.GetData(),Data->PolygonCounts.GetData(),Data->Regions.Num(),Data->Contacts.GetData(),Data->Contacts.Num(),Data->Morphology.GetData(),ContactZ.GetData(),Data->FloorGroups.GetData(),Data->FloorSources.GetData(),Data->FloorSources.Num(),Values.GetData(),N);
        for(int32 I=0;I<N;++I)Air[Active[I]]=Values[I]*Data->Scale;
    }
    else
#endif
    for(int32 I=0;I<N;++I)
    {
        const auto Q=Local[I];const double Broad=Noise[I],Bed=Noise[N+I],Detail=Noise[2*N+I];
        // Spatial dissolution around independent joint footprints. The same
        // footprint can widen, undercut or retain rock at different bed heights.
        const double WX=Q.X+2.8*Broad+.65*FMath::Sin(Q.Y*.15+Q.Z*.20);
        const double WY=Q.Y+4.0*Broad+1.8*Bed;
        const FVector2D XY(WX,WY);double Plan=-MAX_dbl;
        if(!Data->Morphology.IsEmpty())
        {
            double ContactDistance=MAX_dbl;
            int32 Low=0,High=Data->Contacts.Num();
            while(Low<High){const int32 Mid=(Low+High)/2;if(Data->Contacts[Mid]<ContactZ[I])Low=Mid+1;else High=Mid;}
            if(Low<Data->Contacts.Num())ContactDistance=FMath::Abs(Data->Contacts[Low]-ContactZ[I]);
            if(Low>0)ContactDistance=FMath::Min(ContactDistance,FMath::Abs(Data->Contacts[Low-1]-ContactZ[I]));
            const double Relief=(.25+.30*FMath::Clamp(Broad*2+Bed,0.,1.))*FMath::Exp(-FMath::Square(ContactDistance/.40))+.35*Bed;
            const double Rolling=FamilyRelief(Data->Morphology,Q.X,Q.Y);
            double AirField=-MAX_dbl,Bodies[9],Datums[9];
            for(int32 G=0;G<Data->FloorSources.Num();++G){Bodies[G]=-MAX_dbl;Datums[G]=RegionDatum(Data->Morphology.GetData()+4+28*Data->FloorSources[G],Q.X,Q.Y);}
            for(int32 R=0;R<Data->Regions.Num();++R)
            {
                const auto& Region=Data->Regions[R];const auto* M=Data->Morphology.GetData()+4+28*R;
                const int32 G=Data->FloorGroups[R];const double Datum=Datums[G],Height=Q.Z-Datum;
                const double BX=FMath::Max3(M[24]-WX,0.,WX-M[26]),BY=FMath::Max3(M[25]-WY,0.,WY-M[27]);
                const double BoxPlan=BX>0||BY>0?-FMath::Sqrt(BX*BX+BY*BY):FMath::Min(FMath::Min(WX-M[24],M[26]-WX),FMath::Min(WY-M[25],M[27]-WY));
                const double Upper=FMath::Min(BoxPlan+Relief+1.6,Datum+M[7]+M[14]+2.3*Broad+Bed-Q.Z);
                // The exact scalar reference deliberately bypasses pruning.
                if(bCompiledInputs&&Upper<=Bodies[G]-4)continue;
                const double Distance=FMath::Sqrt(Region.Polygon.DistanceSquared(XY));
                const double Signed=Region.Polygon.Contains(XY)?Distance:-Distance;
                const double Roof=Datum+M[7]+M[14]*Hill(Q.X,Q.Y,M[8]+M[12],M[9]+M[13],M[10]*.65,M[11]*.65)
                    -M[17]*Hill(Q.X,Q.Y,M[8]+M[15],M[9]+M[16],M[10]*.4,M[11]*.4)+2.3*Broad+Bed;
                const double Undercut=1.6*FMath::Exp(-FMath::Square((Height-5.8)/2.7));
                double Void=SmoothMin(Signed+Relief+Undercut,Roof-Q.Z,5.5);
                if(M[18]>0)
                {
                    const double Flare=1+FMath::Square((Height-16)/19.);
                    const double PX=(Q.X-M[19]-1.4*FMath::Sin(Height*.14))/(M[21]*Flare);
                    const double PY=(Q.Y-M[20]+1.6*FMath::Cos(Height*.11))/(M[22]*Flare);
                    const double Side=(FMath::Sqrt(PX*PX+PY*PY)-1)*M[21]+1.8*Broad+.65*Bed;
                    const double Bottom=M[23]+1.6*Broad+.7*Bed-Height;
                    Void=SmoothMin(Void,-SmoothMin(-Side,-Bottom,5.),1.3);
                }
                Bodies[G]=-SmoothMin(-Bodies[G],-Void,4.);
            }
            for(int32 G=0;G<Data->FloorSources.Num();++G)
            {
                const double Floor=Datums[G]+Rolling;
                const double Void=SmoothMin(Bodies[G],Q.Z-Floor,1.2);
                AirField=FMath::Max(AirField,Void+.14*Detail*FMath::SmoothStep(.3,2.,Q.Z-Floor));
            }
            Air[Active[I]]=AirField*.6*Data->Scale;continue;
        }
        for(const auto& R:Data->Regions)
        {
            const double Distance=FMath::Sqrt(R.Polygon.DistanceSquared(XY));
            const double Signed=R.Polygon.Contains(XY)?Distance:-Distance;
            Plan=-SmoothMin(-Plan,-Signed,4.);
        }
        // Longitudinal roof shape is a spatial field: separate high dissolution
        // pockets and a retained low shoulder, rather than one scaled vault.
        const double Room=RoomWeight(Q.X);
        const double Roof=10.5+Room*15.5+8*Hill(Q.X,Q.Y,34,-10,24,20)+5*Hill(Q.X,Q.Y,8,20,21,16)
            -4.5*Hill(Q.X,Q.Y,48,13,15,12)+2.3*Broad+1.0*Bed;
        // Real canonical bed contacts supply varying wall recesses, not periodic
        // synthetic grooves. The contact distances are compiled once below.
        int32 Low=0,High=Data->Contacts.Num();
        while(Low<High){const int32 Mid=(Low+High)/2;if(Data->Contacts[Mid]<Q.Z)Low=Mid+1;else High=Mid;}
        double ContactDistance=MAX_dbl;
        if(Low<Data->Contacts.Num())ContactDistance=FMath::Abs(Data->Contacts[Low]-Q.Z);
        if(Low>0)ContactDistance=FMath::Min(ContactDistance,FMath::Abs(Data->Contacts[Low-1]-Q.Z));
        const double BedRelief=(.25+.30*FMath::Clamp(Broad*2+Bed,0.,1.))*FMath::Exp(-FMath::Square(ContactDistance/.40))+.35*Bed;
        const double Undercut=1.6*FMath::Exp(-FMath::Square((Q.Z-5.8)/2.7));
        double Void=SmoothMin(Plan+BedRelief+Undercut,Roof-Q.Z,5.5);
        const double Floor=FloorHeight(Q.X,Q.Y);Void=SmoothMin(Void,Q.Z-Floor,1.2);
        // A surviving roof pendant has an uneven, undercut base. It is a
        // three-dimensional retained mass, not a floor-to-ceiling cylinder.
        const double Flare=1+FMath::Square((Q.Z-16)/19.);
        const double PX=(Q.X-39-1.4*FMath::Sin(Q.Z*.14))/(5.0*Flare);
        const double PY=(Q.Y-12+1.6*FMath::Cos(Q.Z*.11))/(7.0*Flare);
        const double MassSide=(FMath::Sqrt(PX*PX+PY*PY)-1)*5.0+1.8*Broad+.65*Bed;
        const double MassBase=8.5+1.6*Broad+.7*Bed-Q.Z;
        const double OutsideMass=-SmoothMin(-MassSide,-MassBase,5.);
        Void=SmoothMin(Void,OutsideMass,1.3);
        // Detail is subordinate to anatomy and fades completely at the floor.
        const double WallDetail=.14*Detail*FMath::SmoothStep(.3,2.,Q.Z-Floor);
        Air[Active[I]]=(Void+WallDetail)*.6*Data->Scale;
    }
    Data->EvaluationCycles[2].fetch_add(FPlatformTime::Cycles64()-Stage,std::memory_order_relaxed);

}
void FCLSpatialCave::SamplePlanet(const FCLCompiledPlanetRecipe& P,TConstArrayView<FVector> Positions,TArray<double>& Fields,int32* HeightPoints)const
{
    Sample(Positions,Fields);const uint64 Stage=FPlatformTime::Cycles64();
    if(Data->Family)
    {
        TArray<FBox> Supports;Data->Family->CellSupportBounds(Supports);
        for(int32 I=0;I<Positions.Num();++I)
            if(!Supports.ContainsByPredicate([&](const FBox& B){return B.IsInsideOrOn(Positions[I]);}))Fields[I]=-1.e30;
    }
    TRACE_CPUPROFILER_EVENT_SCOPE(CL_PlanetFieldAfterCaves);
    // A smaller natural radius gives an upper bound on the solid/air field.
    // If the cavity already exceeds it, expensive terrain cannot affect max.
    const bool Bounded=P.SurfaceRange.IsValid()||(P.WatershedSurface&&P.WatershedSurface->SurfaceRange.IsValid());
    TArray<double> H,Natural;H.Init(P.MinimumHeightMeters,Positions.Num());P.SampleRadialFields(Positions,H,Natural);
    TArray<FVector> Needed;TArray<int32> Indices;
    for(int32 I=0;I<Positions.Num();++I)if(!Bounded||Natural[I]>Fields[I]){Needed.Add(Positions[I]);Indices.Add(I);}
    if(HeightPoints)*HeightPoints=Needed.Num();
    if(!Needed.IsEmpty())
    {
        TArray<double> Height,Actual;SampleSurfaceHeights(P,Needed,Height);P.SampleRadialFields(Needed,Height,Actual);
        for(int32 I=0;I<Indices.Num();++I)Fields[Indices[I]]=FMath::Max(Fields[Indices[I]],Actual[I]);
    }
    if(P.Volumes)P.Volumes->ComposeBatch(Positions,Fields);
    Data->EvaluationCycles[3].fetch_add(FPlatformTime::Cycles64()-Stage,std::memory_order_relaxed);
}
TSharedPtr<const FCLCompiledCaves> FCLSpatialCave::BuildPrototype(const FCLCompiledPlanetRecipe& Base,int32 Seed,FString& Error)
{
    if(!Base.Rock||!Base.Rock->HasKarstHosts()||!Base.Watershed||!Base.GeneratedRegions)
    {Error=TEXT("Spatial cave prototype requires canonical carbonate Rock, Watershed and eligible Landforms");return nullptr;}
    auto Data=MakeShared<FCLSpatialCaveData>();Data->Rock=Base.Rock;Data->Noise=FCLNativeNoiseKernel::Create(Error);if(!Data->Noise)return nullptr;Data->Seed=Seed^0x53504138;
    // This one-composition study deliberately does not replace planet-wide
    // network placement. Select a real eligible terrestrial carbonate site.
    bool Found=false;const auto& G=*Base.GeneratedRegions;
    for(const auto& C:Base.Watershed->GetCells())
    {
        if(C.bOcean||C.bLake)continue;const auto D=C.Direction;const auto& Province=G.Palette[G.Cells[G.Owner(D)].PaletteIndex];
        if(Province.Suitability.Province.Type!=ECLProvinceType::CarbonatePlatform||Province.Suitability.Province.Match(FCLProvinceGeology::Sample(Base,D))!=ECLProvinceMatch::Eligible)continue;
        const auto Column=Base.Rock->SampleColumn(D);if(!Column.bValid)continue;
        const double Limit=Base.RadiusMeters+Base.SampleHeightMeters(D)-Column.DeformationMeters-90;
        int32 Low=0,High=Base.Rock->GetOlderBedCount();while(Low<High){const int32 Mid=(Low+High)/2;if(Base.Rock->GetBed(Mid).UpperMeters<=Limit)Low=Mid+1;else High=Mid;}
        if(!Low)continue;Data->Datum=Base.Rock->GetBed(Low-1).UpperMeters;Data->Up=D;
        D.FindBestAxisVectors(Data->X,Data->Y);Data->Origin=D*(Data->Datum+Column.DeformationMeters);Found=true;break;
    }
    if(!Found){Error=TEXT("No eligible terrestrial carbonate bedding site for the spatial cave study");return nullptr;}
    return CompileComposition(Base,Data,Seed,Error,true);
}

TSharedPtr<const FCLCompiledCaves> FCLSpatialCave::BuildNetworkAt(const FCLCompiledPlanetRecipe& Base,const FCLCaveSettings& Settings,int32 Seed,const FVector& Direction,double Scale,FString& Error,bool FollowBedding,int32 RequestedLevels,double DepthFraction,bool SurfaceOpening)
{
    auto Data=MakeShared<FCLSpatialCaveData>();Data->Scale=Scale;Data->Up=Direction;
    if(!Settings.ResolvePlacement(Base,Data->PlacementMin,Data->PlacementMax,Data->SurfaceClearance,Error))return nullptr;
    Data->SurfaceOpening=SurfaceOpening;
    Direction.FindBestAxisVectors(Data->X,Data->Y);
    FRandomStream Random(Seed);const FQuat Turn(Direction,Random.FRandRange(0.,2*UE_DOUBLE_PI));
    Data->X=Turn.RotateVector(Data->X);Data->Y=Turn.RotateVector(Data->Y);
    Data->Seed=Seed^0x53504138;
    Data->Noise=FCLNativeNoiseKernel::Create(Error);if(!Data->Noise)return nullptr;
    const double DepthDraw=Random.FRand();
    Data->Levels=RequestedLevels>0?RequestedLevels:DepthDraw<.34?1:DepthDraw<.68?2:DepthDraw<.87?3:DepthDraw<.96?4:5;
    Data->Ramps=Data->Levels-1;
    Data->Morphology={Random.FRandRange(-PI,PI),Random.FRandRange(-90,90),Random.FRandRange(-90,90),0};
    const double Separation=Random.FRandRange(56,70);Data->Morphology[3]=Separation;
    const double Reach=Random.FRandRange(410,560)*FMath::Lerp(.9,1.1,FMath::Clamp(Settings.LengthMeters/500.,0.,1.));
    // Full-size Mammoth/Carlsbad proportions on ordinary planets. Small bodies
    // uniformly fit the same anatomy, including its interconnected levels.
    auto Cave=MakeShared<FCLSpatialCave>();Cave->Data=Data;
    auto AddRegion=[&](const TArray<FVector2D>& Points,double Floor,double Rise,FVector2D Start,FVector2D End,double Roof,FVector2D Center,double RX,double RY,bool Pendant,int32 Group=INDEX_NONE)
    {
        auto& R=Data->Regions.AddDefaulted_GetRef();R.Bounds=FBox2D(ForceInit);
        for(const auto& P:Points)R.Bounds+=P;R.Polygon=UE::Geometry::FPolygon2d(Points);
        const auto Delta=End-Start;const double Run=FMath::Max(1.,Delta.Length());const auto D=Delta/Run;
        const double Values[]={Floor,Rise,Start.X,Start.Y,D.X,D.Y,Run,Roof,Center.X,Center.Y,RX,RY,
            Random.FRandRange(-.3,.3)*RX,Random.FRandRange(-.3,.3)*RY,Roof<20?Random.FRandRange(1,3):Random.FRandRange(5,9),
            Random.FRandRange(-.55,.55)*RX,Random.FRandRange(-.55,.55)*RY,Roof<20?Random.FRandRange(1,2):Random.FRandRange(3,6),
            Pendant?1.:0.,Center.X+RX*.32,Center.Y+RY*.28,Random.FRandRange(4,7),Random.FRandRange(5,9),Random.FRandRange(8,12),R.Bounds.Min.X,R.Bounds.Min.Y,R.Bounds.Max.X,R.Bounds.Max.Y};
        Data->Morphology.Append(Values,UE_ARRAY_COUNT(Values));Data->FloorGroups.Add(Group==INDEX_NONE?FMath::RoundToInt(Floor/Separation):Group);
        return Data->Regions.Num()-1;
    };
    auto LocalFloor=[&](int32 Region,FVector2D P)
    {return RegionDatum(Data->Morphology.GetData()+4+28*Region,P.X,P.Y)+FamilyRelief(Data->Morphology,P.X,P.Y);};
    struct FJoint {FVector2D P;double Floor;int32 Node;};TArray<FJoint> Joints;
    auto AddJoint=[&](FVector2D P,double Floor,bool Room)
    {
        const double RX=Room?Random.FRandRange(37,52):Random.FRandRange(16,21),RY=Room?Random.FRandRange(31,44):Random.FRandRange(14,19);
        const double Heading=Random.FRandRange(-.6,.6);TArray<FVector2D> Polygon;
        const int32 Count=Room?Random.RandRange(9,13):8;
        // Independent joint faces, unequal recesses and shoulders. The ordered
        // radial faces are a compiled spatial footprint, never an oval cutter.
        for(int32 I=0;I<Count;++I)
        {
            const double A=2*UE_DOUBLE_PI*I/Count+Random.FRandRange(-.10,.10),W=Random.FRandRange(.82,1.17);
            const FVector2D V(FMath::Cos(A)*RX*W,FMath::Sin(A)*RY*W);
            Polygon.Add(P+FVector2D(V.X*FMath::Cos(Heading)-V.Y*FMath::Sin(Heading),V.X*FMath::Sin(Heading)+V.Y*FMath::Cos(Heading)));
        }
        const double Roof=Room?Random.FRandRange(25,29):12.;const bool Pendant=Room&&Random.FRand()<.8;
        const int32 Region=AddRegion(Polygon,Floor,0,P,P+FVector2D(1,0),Roof,P,RX,RY,Pendant);
        const int32 Node=Data->GeneratedNodes.Num();
        Data->GeneratedNodes.Add({FGuid(),FVector(P.X,P.Y,LocalFloor(Region,P)+2),{RX,RY},Room,false});
        if(Room)
        {
            FCLCaveChamber C;C.Node=Node;C.Center=FVector(P.X,P.Y,Floor+15);
            C.Rotation=FRotationMatrix::MakeFromXZ(Data->X,Data->Up).ToQuat();C.HalfExtents=FVector(RX,RY,21);
            C.Form=ECLCaveForm::BeddedChamber;C.GeometryProfile=8;C.FloorRock=Base.Rock;C.FloorRadius=Floor;
            Data->GeneratedChambers.Add(C);
            const int32 Recesses=Random.RandRange(0,2);
            for(int32 K=0;K<Recesses;++K)
            {
                const double A=Random.FRandRange(-PI,PI);const FVector2D Center=P+FVector2D(FMath::Cos(A)*RX*.65,FMath::Sin(A)*RY*.65);
                const double BX=RX*Random.FRandRange(.38,.58),BY=RY*Random.FRandRange(.4,.6);TArray<FVector2D> Bay;
                const int32 Faces=Random.RandRange(6,9);
                for(int32 J=0;J<Faces;++J)
                {const double T=J*2*UE_DOUBLE_PI/Faces,W=Random.FRandRange(.8,1.2);Bay.Add(Center+FVector2D(FMath::Cos(T)*BX*W,FMath::Sin(T)*BY*W));}
                AddRegion(Bay,Floor,0,Center,Center+FVector2D(1,0),Random.FRandRange(24,31),Center,BX,BY,false);
            }
        }
        return Joints.Add({P,Floor,Node});
    };
    bool LayoutValid=true;int32 RampGroup=Data->Levels;
    auto Connect=[&](int32 From,int32 To,bool Ramp)
    {
        const auto A=Joints[From],B=Joints[To];const auto Delta=B.P-A.P;const auto D=Delta.GetSafeNormal();const FVector2D Across(-D.Y,D.X);
        const double Run=Delta.Length(),Width=Random.FRandRange(13.5,19.5);
        const int32 Count=Ramp?9:Random.RandRange(5,8);TArray<FVector2D> Centers;
        const double Outward=FVector2D::DotProduct((A.P+B.P)*.5,Across);
        const double Side=Outward==0?(Random.FRand()<.5?-1.:1.):FMath::Sign(Outward);
        double Bend=Ramp?Side*Random.FRandRange(310,410):Random.FRandRange(-26,26);
        const double InitialBend=Bend,Meander=Ramp?FMath::Abs(Bend)*Random.FRandRange(.08,.16):8;
        const double Phase=Random.FRandRange(-PI,PI);double StartT=0,EndT=1;
        // Choose the floor aprons from actual neighbouring footprints. They
        // include native wall warp/undercut and a full walking-width margin.
        // Successive ramps must not excavate one another's walking foundation.
        bool Routed=!Ramp;
        for(int32 Attempt=0;Attempt<(Ramp?12:1);++Attempt)
        {
            Centers.Reset();
            for(int32 I=0;I<=Count;++I)
            {
                const double T=double(I)/Count;
                const double Offset=FMath::Sin(T*PI)*(Bend+Meander*FMath::Sin(T*(Ramp?3:2)*PI+Phase));
                Centers.Add(A.P+Delta*T+Across*Offset);
            }
            if(!Ramp)break;
            StartT=.08;EndT=.92;const double Buffer=Width*1.2+18;
            auto Near=[&](const FCLSpatialCaveData::FRegion& R,FVector2D P)
            {return R.Bounds.ExpandBy(Buffer).IsInside(P)&&(R.Polygon.Contains(P)||R.Polygon.DistanceSquared(P)<Buffer*Buffer);};
            auto Point=[&](double T)
            {const double K=T*Count;const int32 I=FMath::Min(Count-1,FMath::FloorToInt(K));return FMath::Lerp(Centers[I],Centers[I+1],K-I);};
            for(int32 K=0;K<=100;++K)
            {
                const double T=K/100.;const auto P=Point(T);
                for(int32 R=0;R<Data->Regions.Num();++R)
                {
                    const auto* M=Data->Morphology.GetData()+4+28*R;if(M[1]!=0||!Near(Data->Regions[R],P))continue;
                    if(M[0]==A.Floor&&T<.65)StartT=FMath::Max(StartT,T+.02);
                    if(M[0]==B.Floor&&T>.35)EndT=FMath::Min(EndT,T-.02);
                }
            }
            bool Clear=EndT-StartT>=.60;
            const auto Start=A.P+Delta*StartT,End=A.P+Delta*EndT;
            const double FloorProfile[]={A.Floor,B.Floor-A.Floor,Start.X,Start.Y,D.X,D.Y,(End-Start).Length()};
            if(Clear)for(int32 K=0;K<=100&&Clear;++K)
            {
                const auto P=Point(K/100.);const double Floor=RegionDatum(FloorProfile,P.X,P.Y);
                for(int32 R=0;R<Data->Regions.Num();++R)
                {
                    const auto* M=Data->Morphology.GetData()+4+28*R;if(M[1]==0||!Near(Data->Regions[R],P))continue;
                    const double Difference=FMath::Abs(Floor-RegionDatum(M,P.X,P.Y));
                    if(Difference>.001&&Difference<25){Clear=false;break;}
                }
            }
            if(Clear){Routed=true;break;}
            Bend=InitialBend*(Attempt<5?1.:-1.)*FMath::Pow(1.2,(Attempt+1)%6);
        }
        if(!Routed){LayoutValid=false;return;}
        // Compile independent joint faces with a physical cross-width at
        // every turn. A fixed global wall offset narrows steep bends into
        // slits; these overlapping faces retain broad passage anatomy.
        const int32 Group=Ramp?RampGroup++:FMath::RoundToInt(A.Floor/Separation);
        int32 Region=INDEX_NONE;const double Roof=Random.FRandRange(9,11);
        for(int32 I=0;I<Centers.Num()-1;++I)
        {
            const auto Along=(Centers[I+1]-Centers[I]).GetSafeNormal();const FVector2D Normal(-Along.Y,Along.X);
            const auto P=Centers[I]-Along*(Width*.35),Q=Centers[I+1]+Along*(Width*.35),Mid=(P+Q)*.5;
            const double L0=Width*Random.FRandRange(.9,1.15),L1=Width*Random.FRandRange(.9,1.15),LM=Width*Random.FRandRange(.85,1.2);
            const double R0=Width*Random.FRandRange(.9,1.15),R1=Width*Random.FRandRange(.9,1.15),RM=Width*Random.FRandRange(.85,1.2);
            const int32 Part=AddRegion({P+Normal*L0,Mid+Normal*LM,Q+Normal*L1,Q-Normal*R1,Mid-Normal*RM,P-Normal*R0},
                A.Floor,B.Floor-A.Floor,A.P+Delta*StartT,A.P+Delta*EndT,Roof,Mid,FMath::Max(10.,(Q-P).Length()*.6),Width*1.7,false,Group);
            if(Region==INDEX_NONE)Region=Part;
        }
        FCLCavePassage Path;Path.From=A.Node;Path.To=B.Node;Path.Form=Path.EndForm=ECLCaveForm::BeddedPassage;Path.GeometryProfile=8;Path.FloorRock=Base.Rock;
        // Dense traversal metadata follows the footprint's construction points;
        // voxel workers evaluate only the immutable joint polygons and fields.
        for(int32 I=0;I<Centers.Num()-1;++I)
        {
            const int32 Steps=FMath::CeilToInt(FVector2D::Distance(Centers[I],Centers[I+1])/4.);
            for(int32 K=0;K<Steps;++K)
            {
                const auto P=FMath::Lerp(Centers[I],Centers[I+1],double(K)/Steps);const double Floor=LocalFloor(Region,P);
                Path.Points.Add(FVector(P.X,P.Y,Floor+2));Path.Sections.Add({Width,6});Path.FloorRadii.Add(Floor);
            }
        }
        Path.Points.Add(Data->GeneratedNodes[B.Node].Position);Path.Sections.Add({Width,6});Path.FloorRadii.Add(LocalFloor(Region,B.P));
        Data->GeneratedPassages.Add(MoveTemp(Path));
    };
    TArray<TArray<int32>> Levels;
    for(int32 Level=0;Level<Data->Levels;++Level)
    {
        TArray<int32> Nodes;
        const int32 BaseCount=Random.RandRange(2,4);const double BranchDraw=Random.FRandRange(0,2);
        const int32 Count=BaseCount+FMath::RoundToInt(Settings.Branching*BranchDraw);
        const double Drift=Random.FRandRange(-25,25);
        const double Heading=Random.FRandRange(-PI,PI),C=FMath::Cos(Heading),S=FMath::Sin(Heading);
        for(int32 I=0;I<Count;++I)
        {
            const double T=double(I)/(Count-1);
            const double X=-Reach*.5+Reach*T+Random.FRandRange(-12,12),Y=Drift+Random.FRandRange(-60,60);
            Nodes.Add(AddJoint({X*C-Y*S,X*S+Y*C},Level*Separation,I>0));
            if(I)Connect(Nodes[Random.RandRange(FMath::Max(0,I-2),I-1)],Nodes[I],false);
        }
        Levels.Add(MoveTemp(Nodes));
    }
    for(int32 Level=1;Level<Levels.Num();++Level)
    {
        int32 From=Levels[Level-1][0],To=Levels[Level][0];double Longest=0;
        for(const int32 A:Levels[Level-1])for(const int32 B:Levels[Level])
        {const double Length=(Joints[A].P-Joints[B].P).SquaredLength();if(Length>Longest){Longest=Length;From=A;To=B;}}
        Connect(From,To,true);
    }
    // A rejected layout is an ordinary placement proposal, not an eligibility
    // failure and never a partially published network or substitute generator.
    if(!LayoutValid){Error.Reset();return nullptr;}
    FBox2D Footprint(ForceInit);for(const auto& R:Data->Regions)Footprint+=R.Bounds;
    const double Extent=FMath::Max(Footprint.Min.Length(),Footprint.Max.Length())+30;
    double RoofEnvelope=0;
    const auto& Gate=Joints[Levels.Last().Last()];
    // Fit the unchanged anatomy inside the requested radial band. Closed
    // systems use their own local terrain envelope, never the planet-wide
    // lowest surface. The seeded fraction distributes their remaining depth.
    double Ceiling=Data->PlacementMax;
    for(int32 Fit=0;Fit<4;++Fit)
    {
        if(Base.RadiusMeters+Base.MinimumHeightMeters>250)for(int32 I=0;I<Data->Regions.Num();++I)
        {auto& Roof=Data->Morphology[4+28*I+7];if(Roof<20)Roof=FMath::Min(FMath::Max(Roof,6./Scale),.60*Separation);}
        RoofEnvelope=0;for(int32 I=0;I<Data->Regions.Num();++I)
        {const auto* M=Data->Morphology.GetData()+4+28*I;RoofEnvelope=FMath::Max(RoofEnvelope,M[0]+M[1]+M[7]+M[14]+8);}
        Scale=FMath::Min(Scale,FMath::Max(1.e-12,(Ceiling-Data->PlacementMin)/(RoofEnvelope+20)));
        Data->Scale=Scale;
        const double Bottom=Data->PlacementMin+20*Scale,Top=Ceiling-RoofEnvelope*Scale;
        Data->Datum=FMath::Lerp(Bottom,FMath::Max(Bottom,Top),1-FMath::Clamp(DepthFraction,0.,1.));Data->Origin=Direction*Data->Datum;
        TArray<FVector> Directions;TArray<double> Heights;
        if(SurfaceOpening)
        {
            // Expose the upper part of an existing broad chamber. On slopes
            // this also makes side mouths; flat terrain can have a skylight.
            // No new passage, tail, capsule or floor cut is constructed.
            const double Z=Gate.Floor+18;
            const auto P=Cave->BodyPoint(FVector(Gate.P.X,Gate.P.Y,Z));Directions.Add(P.GetSafeNormal());Base.SampleHeightsMeters(Directions,Heights);
            Data->Datum+=Base.RadiusMeters+Heights[0]-P.Length();Data->Origin=Direction*Data->Datum;
            if(Data->Datum-20*Scale<Data->PlacementMin||Data->Datum+((Data->Levels-1)*Separation+8)*Scale>Data->PlacementMax){Error.Reset();return nullptr;}
        }
        else if(Data->Datum+RoofEnvelope*Scale>Base.RadiusMeters+Base.MinimumHeightMeters-Data->SurfaceClearance)
        {
            for(const auto& R:Data->Regions)
            {
                const auto C=R.Bounds.GetCenter();Directions.Add(Cave->BodyPoint(FVector(C.X,C.Y,RoofEnvelope)).GetSafeNormal());
                for(const auto& P:R.Polygon.GetVertices())Directions.Add(Cave->BodyPoint(FVector(P.X,P.Y,RoofEnvelope)).GetSafeNormal());
            }
            Base.SampleHeightsMeters(Directions,Heights);double LocalCeiling=Data->PlacementMax;
            for(double H:Heights)LocalCeiling=FMath::Min(LocalCeiling,Base.RadiusMeters+H-Data->SurfaceClearance);
            if(LocalCeiling<=Data->PlacementMin){Error.Reset();return nullptr;}
            Ceiling=LocalCeiling;
        }
        Scale=FMath::Min(Scale,Data->Datum*.78/Extent);
    }
    Data->Scale=Scale;
    if(Base.Rock&&FollowBedding)
    {
        const auto Column=Base.Rock->SampleColumn(Direction);
        if(Column.bValid){Data->Rock=Base.Rock;Data->Datum-=Column.DeformationMeters;}
    }
    Data->ContactRock=Base.Rock;Data->ContactDatum=Data->Rock?Data->Datum:Base.Rock?Base.Rock->RestoredRadiusMeters(Data->Origin):Data->Datum;
    const double EyeOffset=(Base.RadiusMeters+Base.MinimumHeightMeters>250?2./Scale:2.)-2.;
    for(auto& N:Data->GeneratedNodes){N.Position.Z+=EyeOffset;N.Position=Cave->BodyPoint(N.Position);N.Section*=Scale;}
    for(auto& P:Data->GeneratedPassages)
    {
        for(auto& V:P.Points){V.Z+=EyeOffset;V=Cave->BodyPoint(V);}for(auto& V:P.Sections)V*=Scale;for(auto& V:P.FloorRadii)V=Data->Datum+V*Scale;
        P.FloorRock=Data->Rock;
    }
    for(auto& C:Data->GeneratedChambers){C.Center=Cave->BodyPoint(C.Center);C.HalfExtents*=Scale;C.FloorRadius=Data->Datum+C.FloorRadius*Scale;C.FloorRock=Data->Rock;}
    Data->MinimumHalfWidth=4.2*Scale;
    // Reject an unwalkable restored bed before preparing its directional chart.
    // This is the same exact body-space route checked after compilation below.
    if(Data->Rock)for(const auto& Path:Data->GeneratedPassages)for(int32 I=1;I<Path.Points.Num();++I)
    {
        const auto A=Path.Points[I-1],B=Path.Points[I];
        const double Run=FVector::VectorPlaneProject(B-A,(A+B).GetSafeNormal()).Length();
        if(FMath::Abs(B.Length()-A.Length())>.45*Run)return BuildNetworkAt(Base,Settings,Seed,Direction,Scale,Error,false,RequestedLevels,DepthFraction,SurfaceOpening);
    }
    auto Compile=[&]{return CompileComposition(Base,Data,Seed,Error,false);};
    auto Result=Compile();
    bool Walkable=Result.IsValid();
    if(Result)for(const auto& Path:Result->Passages)for(int32 I=1;I<Path.Points.Num();++I)
    {
        const auto A=Path.Points[I-1],B=Path.Points[I];
        const double Run=FVector::VectorPlaneProject(B-A,(A+B).GetSafeNormal()).Length();
        Walkable&=FMath::Abs(B.Length()-A.Length())<=.45*Run;
    }
    if((!Result||!Walkable)&&Data->Rock)
    {
        // Retain the complete generated anatomy and canonical material queries
        // when a steep restored bed cannot support a walking floor.
        Error.Reset();Data->Rock.Reset();Data->Datum=Data->Origin.Length();
        return BuildNetworkAt(Base,Settings,Seed,Direction,Scale,Error,false,RequestedLevels,DepthFraction,SurfaceOpening);
    }
    if(!Result&&Scale>1.e-10){Error.Reset();return BuildNetworkAt(Base,Settings,Seed,Direction,Scale*.8,Error,false,RequestedLevels,DepthFraction,SurfaceOpening);}
    if(Result)
    {
        // Publish only a complete traversable proposal. This uses the actual
        // field, including retained rock and all intersecting levels, rather
        // than treating the construction centreline as proof of clearance.
        const double WalkScale=Base.RadiusMeters+Base.MinimumHeightMeters>250?1.:Scale;
        const double Head=Base.RadiusMeters+Base.MinimumHeightMeters>250?2.:2*Scale;
        TArray<FVector> Heads,Feet;
        for(const auto& Path:Result->Passages)for(int32 I=0;I<Path.Points.Num();++I)
        {
            const auto Q=Path.Points[I],Up=Q.GetSafeNormal();
            const auto Along=Path.Points[FMath::Min(I+1,Path.Points.Num()-1)]-Path.Points[FMath::Max(0,I-1)];
            const auto Side=FVector::CrossProduct(Up,Along).GetSafeNormal();
            for(double Offset:{-.5,0.,.5}){Heads.Add(Q+Side*(Offset*WalkScale));Feet.Add(Q+Side*(Offset*WalkScale)-Up*(Head+.5*WalkScale));}
        }
        TArray<double> H,F;Cave->Sample(Heads,H);Cave->Sample(Feet,F);
        for(int32 I=0;I<H.Num();++I)if(H[I]<.35*WalkScale||F[I]>-.05*WalkScale){Error.Reset();return nullptr;}
        if(SurfaceOpening)
        {
            // Placement may expose roofs and side walls, never remove the
            // actual terrain supporting the existing walking graph.
            TArray<double> GroundHeights,GroundField;Base.SampleHeightsMeters(Feet,GroundHeights);Base.SampleRadialFields(Feet,GroundHeights,GroundField);
            if(GroundField.ContainsByPredicate([&](double V){return V>-.05*WalkScale;})){Error.Reset();return nullptr;}
            TArray<double> Heights;Base.SampleHeightsMeters(Heads,Heights);
            int32 Inside=INDEX_NONE;double Cover=4*WalkScale;
            for(int32 I=0;I<Heads.Num();++I){const double D=Base.RadiusMeters+Heights[I]-Heads[I].Length();if(D>Cover){Inside=I;Cover=D;}}
            if(Inside==INDEX_NONE){Error.Reset();return nullptr;}
            const double HalfWidth=FMath::Min(5.,11*Scale);bool Found=false;
            for(const FVector2D Offset:{FVector2D::ZeroVector,FVector2D(8,0),FVector2D(-8,0),FVector2D(0,8),FVector2D(0,-8)})
            {
                const auto Center=Cave->BodyPoint(FVector(Gate.P.X+Offset.X,Gate.P.Y+Offset.Y,Gate.Floor+18));
                TArray<FVector> P;for(double X:{-HalfWidth,0.,HalfWidth})for(double Y:{-HalfWidth,0.,HalfWidth})P.Add((Center+Data->X*X+Data->Y*Y).GetSafeNormal());
                Base.SampleHeightsMeters(P,Heights);
                for(int32 I=0;I<P.Num();++I)P[I]*=Base.RadiusMeters+Heights[I]-.5*WalkScale;
                TArray<double> Air;Cave->Sample(P,Air);
                if(Air.ContainsByPredicate([&](double V){return V<.2*WalkScale;}))continue;
                auto Mutable=MakeShared<FCLCompiledCaves>(*Result);
                Mutable->SurfaceOpenings.Add({Mutable->NetworkInfo[0].Id,P[4]+P[4].GetSafeNormal()*(.5*WalkScale),Heads[Inside],HalfWidth*2});
                Mutable->Nodes[Gate.Node].Portal=true;Mutable->Entrances=1;Result=Mutable;Found=true;break;
            }
            if(!Found){Error.Reset();return nullptr;}
        }
    }
    Data->InspectionSupports.Reset();Data->SupportIndex.Reset();
    if(Result)
    {
        FVoxelAABBTree::FElementArray Elements;
        for(int32 I=0;I<Data->Regions.Num();++I)
        {
            const auto Rect=Data->Regions[I].Bounds.ExpandBy(16);const auto* M=Data->Morphology.GetData()+4+28*I;
            double Low=MAX_dbl,High=-MAX_dbl;
            for(double X:{Rect.Min.X,Rect.Max.X})for(double Y:{Rect.Min.Y,Rect.Max.Y}){const double D=RegionDatum(M,X,Y);Low=FMath::Min(Low,D);High=FMath::Max(High,D);}
            FBox Support(ForceInit);
            for(double X:{Rect.Min.X,Rect.GetCenter().X,Rect.Max.X})for(double Y:{Rect.Min.Y,Rect.GetCenter().Y,Rect.Max.Y})for(double Z:{Low-20,High+M[7]+M[14]+8})Support+=Cave->BodyPoint(FVector(X,Y,Z));
            Support=Support.ExpandBy(12*Data->Scale);Data->InspectionSupports.Add(Support);Elements.Add(FVoxelBox(Support),I);
        }
        Data->SupportIndex=FVoxelAABBTree::Create(MoveTemp(Elements));
    }
    return Result;
}

TSharedPtr<const FCLCompiledCaves> FCLSpatialCave::CompileComposition(const FCLCompiledPlanetRecipe& Base,TSharedPtr<FCLSpatialCaveData> Data,int32 Seed,FString& Error,bool PrepareSurface)
{
    const bool Generated=!Data->Morphology.IsEmpty();
    Data->FloorSources.Init(INDEX_NONE,Generated?Data->Levels+Data->Ramps:0);
    if(Generated)for(int32 I=0;I<Data->Regions.Num();++I)if(Data->FloorSources[Data->FloorGroups[I]]==INDEX_NONE)Data->FloorSources[Data->FloorGroups[I]]=I;
    Data->Bedding=FCLDirectionalScalarField();Data->Contacts.Reset();if(!Generated)Data->Regions.Reset();Data->VertexX.Reset();Data->VertexY.Reset();Data->PolygonStarts.Reset();Data->PolygonCounts.Reset();
    const auto ContactRock=Generated?Data->ContactRock:Data->Rock;
    const double ContactDatum=Generated?Data->ContactDatum:Data->Datum;
    if(ContactRock)for(int32 I=0;I<ContactRock->GetOlderBedCount();++I)
    {const double H=(ContactRock->GetBed(I).UpperMeters-ContactDatum)/Data->Scale;if(Generated||(H>=-25&&H<=60))Data->Contacts.Add(H);}
    auto Add=[&](TArray<FVector2D> Points)
    {auto& R=Data->Regions.AddDefaulted_GetRef();R.Bounds=FBox2D(ForceInit);for(const auto& P:Points)R.Bounds+=P;R.Polygon=UE::Geometry::FPolygon2d(Points);};
    if(!Generated){
    Add({{-112,-7},{-101,-12},{-79,-9},{-63,-14},{-43,-9},{-25,-15},{-4,-12},{13,-15},{14,5},{-10,11},{-29,7},{-44,12},{-66,6},{-89,11},{-109,5}});
    Add({{-18,-14},{0,-30},{25,-40},{54,-31},{71,-12},{67,14},{49,32},{19,39},{-4,25},{-15,8}});
    Add({{-78,4},{-64,3},{-56,14},{-60,23},{-71,25},{-80,15}});
    }
    for(const auto& Region:Data->Regions)
    {
        Data->PolygonStarts.Add(Data->VertexX.Num());Data->PolygonCounts.Add(Region.Polygon.GetVertices().Num());
        for(const auto& V:Region.Polygon.GetVertices()){Data->VertexX.Add(V.X);Data->VertexY.Add(V.Y);}
    }
#if CL_SPATIAL_SIMD
    UE_LOG(LogTemp,Display,TEXT("LANDFORM_SPATIAL_MORPHOLOGY backend=ISPC_neon_i32x4"));
#endif
    auto Cave=MakeShared<FCLSpatialCave>();Cave->Data=Data;
    // Enclose the finite field including warp, undercuts, floor and roof. All
    // boundaries are checked against the actual terrain below; no surface hole.
    Data->Bounds=FBox(ForceInit);
    FBox2D LocalBounds(FVector2D(-135,-62),FVector2D(95,62));double Top=52;
    if(Generated)
    {
        LocalBounds=FBox2D(ForceInit);for(const auto& R:Data->Regions)LocalBounds+=R.Bounds;
        LocalBounds=LocalBounds.ExpandBy(16);Top=0;
        for(int32 I=0;I<Data->Regions.Num();++I){const auto* M=Data->Morphology.GetData()+4+28*I;Top=FMath::Max(Top,M[0]+M[1]+M[7]+M[14]+8);}
    }
    for(double X:{LocalBounds.Min.X,LocalBounds.GetCenter().X,LocalBounds.Max.X})for(double Y:{LocalBounds.Min.Y,LocalBounds.GetCenter().Y,LocalBounds.Max.Y})for(double Z:{-20.,0.,Top})Data->Bounds+=Cave->BodyPoint(FVector(X,Y,Z));
    Data->Bounds=Data->Bounds.ExpandBy(12.*Data->Scale);
    FBox2D Chart(ForceInit);
    for(int32 Corner=0;Corner<8;++Corner)
    {
        const FVector Q(Corner&1?Data->Bounds.Max.X:Data->Bounds.Min.X,Corner&2?Data->Bounds.Max.Y:Data->Bounds.Min.Y,Corner&4?Data->Bounds.Max.Z:Data->Bounds.Min.Z);
        const double Along=FVector::DotProduct(Q,Data->Up);
        if(Along<=1.e-6){Error=TEXT("Spatial bedding chart crosses its directional horizon");return nullptr;}
        Chart+=FVector2D(FVector::DotProduct(Q,Data->X),FVector::DotProduct(Q,Data->Y))*(Data->Datum/Along);
    }
    const auto Rock=ContactRock;const FVector XAxis=Data->X,YAxis=Data->Y,Up=Data->Up;const double Datum=Data->Datum;
    // Keep the frozen fixture's preparation unchanged. Placement batches have
    // a bounded chart budget; uncertain leaves use the exact same rock query,
    // so this limits preparation without relaxing geometry tolerances.
    if(!PrepareSurface)Data->Bedding.SetMaximumDepth(4);
    if(Rock)Data->Bedding.Build(Chart.ExpandBy(Data->Scale),[Rock,XAxis,YAxis,Up,Datum](const FVector2D& P)
    {
        const FVector Q=(Up+XAxis*(P.X/Datum)+YAxis*(P.Y/Datum)).GetSafeNormal()*Datum;
        return Q.Length()-Rock->RestoredRadiusMeters(Q);
    });
    const auto Surface=Base.WatershedSurface?Base.WatershedSurface:MakeShared<const FCLCompiledPlanetRecipe>(Base);
    if(PrepareSurface)ParallelFor(6,[&](int32 Face)
    {
        auto Direction=[Face](const FVector2D& P){return FCLSphericalPatches::Direction(Face,P);};
        Data->Surface[Face].Build(FBox2D(FVector2D(-1.000001),FVector2D(1.000001)),[Surface,Direction](const FVector2D& P){return Surface->SampleHeightMeters(Direction(P));},
            [Surface,Direction](TConstArrayView<FVector2D> Points,TArray<double>& Heights)
            {TArray<FVector> Directions;Directions.Reserve(Points.Num());for(const auto& P:Points)Directions.Add(Direction(P));Surface->SampleHeightsMeters(Directions,Heights);},.00025);
    });
    if(Generated)
    {
        TArray<FVector> Points,Feet;
        auto AddPoint=[&](FVector2D P){Points.Add(Cave->BodyPoint(FVector(P.X,P.Y,Top)));Feet.Add(Cave->BodyPoint(FVector(P.X,P.Y,-20)));};
        for(const auto& R:Data->Regions)
        {
            AddPoint(R.Bounds.GetCenter());for(const auto& P:R.Polygon.GetVertices())AddPoint(P);
        }
        const bool CoveredByBound=!Data->SurfaceOpening&&!Data->Rock&&Data->Datum+Top*Data->Scale<=Base.RadiusMeters+Base.MinimumHeightMeters-Data->SurfaceClearance;
        TArray<double> Heights;if(!Data->SurfaceOpening&&!CoveredByBound)Base.SampleHeightsMeters(Points,Heights);
        for(int32 I=0;I<Points.Num();++I)
        {
            if(Feet[I].Length()<Data->PlacementMin-1.e-4||(!Data->SurfaceOpening&&(Points[I].Length()>Data->PlacementMax+1.e-4||(!CoveredByBound&&Base.RadiusMeters+Heights[I]-Points[I].Length()<Data->SurfaceClearance-1.e-4))))
            {Error=TEXT("Spatial network does not fit its local radial placement envelope");return nullptr;}
        }
    }
    else for(double X=LocalBounds.Min.X+10;X<=LocalBounds.Max.X-10;X+=20)for(double Y=LocalBounds.Min.Y+10;Y<=LocalBounds.Max.Y-10;Y+=20)
    {
        const auto Point=Cave->BodyPoint(FVector(X,Y,Generated?Top:45));const double Roof=Base.RadiusMeters+(PrepareSurface?Base.SampleHeightMeters(Point.GetSafeNormal()):Base.MinimumHeightMeters);
        if(Roof-Point.Length()<8*Data->Scale){Error=TEXT("Spatial study does not fit beneath this terrain; no partial cave published");return nullptr;}
    }
    auto Out=MakeShared<FCLCompiledCaves>();Out->Spatial=Cave;const FGuid Id(0x53504138,Seed,1,1);
    Out->Networks=Out->RequestedNetworks=1;Out->EligibleCandidates=1;Out->Candidates=1;
    FCLCompiledCaves::FNetwork Info;Info.Id=Id;Info.Family=FCLCompiledCaves::EFamily::CarbonateKarst;Info.InceptionRadiusMeters=Data->Datum;Info.OriginDirection=Data->Up;Info.InspectionBounds=Data->Bounds;Out->NetworkInfo.Add(Info);
    if(Generated)
    {
        Out->NetworkInfo[0].Levels=Data->Levels;Out->NetworkInfo[0].AnatomyScale=Data->Scale;
        Out->Nodes=Data->GeneratedNodes;Out->Passages=Data->GeneratedPassages;Out->Chambers=Data->GeneratedChambers;
        for(auto& N:Out->Nodes)N.Network=Id;for(auto& P:Out->Passages)P.Network=Id;for(auto& C:Out->Chambers)C.Network=Id;
        Out->Cutters=FCLCompiledCaveVolume::CompileSpatial(Cave,Id);Out->ResolveInspectionDomains();return Out;
    }
    const auto RoomPoint=Cave->BodyPoint(FVector(14,-5,Cave->FloorHeight(14,-5)+2));Out->Nodes.Add({Id,RoomPoint,{30,20},true,false});
    Out->Nodes.Add({Id,Cave->BodyPoint(FVector(-101,0,Cave->FloorHeight(-101,0)+2)),{8*Data->Scale,6*Data->Scale},false,false});
    // These points are traversal/inspection metadata only. They never generate
    // the field and changing them cannot change a wall, roof or floor.
    FCLCavePassage Route;Route.Network=Id;Route.From=0;Route.To=1;Route.Form=Route.EndForm=ECLCaveForm::BeddedPassage;Route.GeometryProfile=8;Route.FloorRock=Base.Rock;
    for(double X=14;X>=-101;X-=5)
    {
        const double Y=-3+2*FMath::Sin(X*.045);const auto P=Cave->BodyPoint(FVector(X,Y,Cave->FloorHeight(X,Y)+2));Route.Points.Add(P);Route.Sections.Add({9*Data->Scale,7*Data->Scale});Route.FloorRadii.Add(Data->Datum+Data->Scale*Cave->FloorHeight(X,Y));
    }
    Out->Passages.Add(Route);FCLCaveChamber Room;Room.Network=Id;Room.Node=0;Room.Center=Cave->BodyPoint(FVector(24,0,15));Room.Rotation=FRotationMatrix::MakeFromXZ(Data->X,Data->Up).ToQuat();Room.HalfExtents=FVector(48,40,20)*Data->Scale;Room.Form=ECLCaveForm::BeddedChamber;Room.GeometryProfile=8;Room.FloorRock=Base.Rock;Room.FloorRadius=Data->Datum;Out->Chambers.Add(Room);
    Out->Cutters=FCLCompiledCaveVolume::CompileSpatial(Cave,Id);Out->ResolveInspectionDomains();Out->Readiness=TEXT("Spatial prototype: one bedding-controlled passage/chamber composition; no swept geometry");return Out;
}

TSharedPtr<const FCLCompiledCaves> FCLSpatialCave::BuildFamily(const FCLCompiledPlanetRecipe& Base,const FCLCaveSettings& Settings,int32 Seed,FString& Error)
{
    auto Out=MakeShared<FCLCompiledCaves>();auto D=MakeShared<FCLSpatialCaveData>();
    D->Family=FCLSpatialCaveFamily::Build(Base,Settings,Seed,*Out,Error);if(!D->Family)return nullptr;
    D->Bounds=D->Family->Bounds();auto Cave=MakeShared<FCLSpatialCave>();Cave->Data=D;
    // Compile the final directional terrain input, including watershed mapping.
    // Reapplying the mapped surface per 3-D density point defeats preparation.
    if(Out->NetworkInfo[0].Family!=FCLCompiledCaves::EFamily::General)
    {
    D->bFinalSurfaceCompiled=true;
    const auto Surface=MakeShared<const FCLCompiledPlanetRecipe>(Base);
    ParallelFor(6,[&](int32 Face)
    {
        auto Direction=[Face](const FVector2D& P){return FCLSphericalPatches::Direction(Face,P);};
        D->Surface[Face].Build(FBox2D(FVector2D(-1.000001),FVector2D(1.000001)),[Surface,Direction](const FVector2D& P){return Surface->SampleHeightMeters(Direction(P));},
            [Surface,Direction](TConstArrayView<FVector2D> Points,TArray<double>& Heights){TArray<FVector> Directions;for(const auto& P:Points)Directions.Add(Direction(P));Surface->SampleHeightsMeters(Directions,Heights);},.00020);
    });
    }
    Out->Spatial=Cave;Out->Cutters=FCLCompiledCaveVolume::CompileSpatial(Cave,FGuid());Out->ResolveInspectionDomains();return Out;
}

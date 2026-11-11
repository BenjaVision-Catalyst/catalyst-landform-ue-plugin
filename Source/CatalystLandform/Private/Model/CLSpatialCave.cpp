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
    TSharedPtr<const FCLCompiledRock> Rock;
    TSharedPtr<const FCLNativeNoiseKernel> Noise;
    FVector Origin,X,Y,Up;
    double Datum=0;
    int32 Seed=0;
    bool bFinalSurfaceCompiled=false;
    TArray<double> Contacts;
    FBox Bounds;
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
}
FVector FCLSpatialCave::BeddingPoint(const FVector& P,bool bCompiled)const
{
    if(Data->Family)return Data->Family->BeddingPoint(P,0,bCompiled);
    const auto D=P-Data->Origin;
    const double Along=FVector::DotProduct(P,Data->Up);
    const double Restored=bCompiled&&Along>1.e-6?P.Length()-Data->Bedding.Sample(FVector2D(FVector::DotProduct(P,Data->X),FVector::DotProduct(P,Data->Y))*(Data->Datum/Along)):Data->Rock->RestoredRadiusMeters(P);
    return FVector(FVector::DotProduct(D,Data->X),FVector::DotProduct(D,Data->Y),Restored-Data->Datum);
}
FVector FCLSpatialCave::BodyPoint(const FVector& P)const
{
    if(Data->Family)return Data->Family->BodyPoint(P);
    // Invert the same canonical restored coordinate, retaining planetary curvature.
    FVector Q=Data->Origin+Data->X*P.X+Data->Y*P.Y+Data->Up*P.Z;
    for(int32 I=0;I<5;++I)
    {
        const double Error=Data->Datum+P.Z-Data->Rock->RestoredRadiusMeters(Q);
        Q+=Data->Up*(Error/FMath::Max(.5,FVector::DotProduct(Q.GetSafeNormal(),Data->Up)));
    }
    return Q;
}
double FCLSpatialCave::FloorHeight(double X,double Y)const
{
    if(Data->Family)return Data->Family->FloorHeight(X,Y);
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
FBox FCLSpatialCave::Bounds(FGuid Network)const{return Data->Family?Data->Family->Bounds(Network):Data->Bounds;}
bool FCLSpatialCave::IsProceduralFamily()const{return Data->Family.IsValid();}
bool FCLSpatialCave::TryPreparedSurfaces(const FVector& P,FCLSurfaceWeights& W)const{return Data->Family&&Data->Family->TrySurfaces(P,W);}
void FCLSpatialCave::GetPreparedGeologyStats(uint64& Prepared,uint64& Exact)const{Prepared=Exact=0;if(Data->Family)Data->Family->GeologyStats(Prepared,Exact);}
void FCLSpatialCave::GetFamilyStats(int32& Levels,int32& Cells,int32& Ramps)const
{Levels=Cells=Ramps=0;if(Data->Family){int32 N,E;uint64 B;double Error;Data->Family->Stats(Levels,Cells,Ramps,N,E,B,Error);}}
void FCLSpatialCave::SampleSelected(TConstArrayView<FVector> P,TArray<double>& Air,const TSet<FGuid>& Selection)const
{if(Data->Family)Data->Family->Sample(P,Air,true,&Selection);else Sample(P,Air);}
void FCLSpatialCave::Sample(TConstArrayView<FVector> P,TArray<double>& Air,bool bCompiledInputs)const
{
    if(Data->Family){const uint64 Start=FPlatformTime::Cycles64();Data->Family->Sample(P,Air,bCompiledInputs);Data->EvaluationCycles[2].fetch_add(FPlatformTime::Cycles64()-Start,std::memory_order_relaxed);return;}
    Air.SetNumUninitialized(P.Num());TArray<int32> Active;TArray<FVector> Local,NoisePoints;
    for(int32 I=0;I<P.Num();++I)
    {
        // The compiler verifies a broad, finite 3-D support envelope. Outside it
        // the cavity is closed, and a cheap distance lower bound rejects rock.
        const double Outside=FMath::Sqrt(Data->Bounds.ComputeSquaredDistanceToPoint(P[I]));
        if(Outside>0){Air[I]=-Outside-1;continue;}
        Active.Add(I);
    }
    uint64 Stage=FPlatformTime::Cycles64();
    for(int32 I:Active)Local.Add(BeddingPoint(P[I],bCompiledInputs));
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
        ispc::CLSpatialMorph(X.GetData(),Y.GetData(),Z.GetData(),Noise.GetData(),Data->VertexX.GetData(),Data->VertexY.GetData(),Data->PolygonStarts.GetData(),Data->PolygonCounts.GetData(),Data->Regions.Num(),Data->Contacts.GetData(),Data->Contacts.Num(),Values.GetData(),N);
        for(int32 I=0;I<N;++I)Air[Active[I]]=Values[I];
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
        Air[Active[I]]=(Void+WallDetail)*.6;
    }
    Data->EvaluationCycles[2].fetch_add(FPlatformTime::Cycles64()-Stage,std::memory_order_relaxed);

}
void FCLSpatialCave::SamplePlanet(const FCLCompiledPlanetRecipe& P,TConstArrayView<FVector> Positions,TArray<double>& Fields,int32* HeightPoints)const
{
    Sample(Positions,Fields);const uint64 Stage=FPlatformTime::Cycles64();
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
    for(int32 I=0;I<Base.Rock->GetOlderBedCount();++I)
    {const double H=Base.Rock->GetBed(I).UpperMeters-Data->Datum;if(H>=-25&&H<=60)Data->Contacts.Add(H);}
    auto Add=[&](TArray<FVector2D> Points)
    {auto& R=Data->Regions.AddDefaulted_GetRef();R.Bounds=FBox2D(ForceInit);for(const auto& P:Points)R.Bounds+=P;R.Polygon=UE::Geometry::FPolygon2d(Points);};
    Add({{-112,-7},{-101,-12},{-79,-9},{-63,-14},{-43,-9},{-25,-15},{-4,-12},{13,-15},{14,5},{-10,11},{-29,7},{-44,12},{-66,6},{-89,11},{-109,5}});
    Add({{-18,-14},{0,-30},{25,-40},{54,-31},{71,-12},{67,14},{49,32},{19,39},{-4,25},{-15,8}});
    Add({{-78,4},{-64,3},{-56,14},{-60,23},{-71,25},{-80,15}});
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
    for(double X:{-135.,0.,95.})for(double Y:{-62.,0.,62.})for(double Z:{-20.,0.,52.})Data->Bounds+=Cave->BodyPoint(FVector(X,Y,Z));
    Data->Bounds=Data->Bounds.ExpandBy(12.);
    FBox2D Chart(ForceInit);
    for(int32 Corner=0;Corner<8;++Corner)
    {
        const FVector Q(Corner&1?Data->Bounds.Max.X:Data->Bounds.Min.X,Corner&2?Data->Bounds.Max.Y:Data->Bounds.Min.Y,Corner&4?Data->Bounds.Max.Z:Data->Bounds.Min.Z);
        const double Along=FVector::DotProduct(Q,Data->Up);
        if(Along<=1.e-6){Error=TEXT("Spatial bedding chart crosses its directional horizon");return nullptr;}
        Chart+=FVector2D(FVector::DotProduct(Q,Data->X),FVector::DotProduct(Q,Data->Y))*(Data->Datum/Along);
    }
    const auto Rock=Data->Rock;const FVector XAxis=Data->X,YAxis=Data->Y,Up=Data->Up;const double Datum=Data->Datum;
    Data->Bedding.Build(Chart.ExpandBy(1.),[Rock,XAxis,YAxis,Up,Datum](const FVector2D& P)
    {
        const FVector Q=(Up+XAxis*(P.X/Datum)+YAxis*(P.Y/Datum)).GetSafeNormal()*Datum;
        return Q.Length()-Rock->RestoredRadiusMeters(Q);
    });
    const auto Surface=Base.WatershedSurface?Base.WatershedSurface:MakeShared<const FCLCompiledPlanetRecipe>(Base);
    ParallelFor(6,[&](int32 Face)
    {
        auto Direction=[Face](const FVector2D& P){return FCLSphericalPatches::Direction(Face,P);};
        Data->Surface[Face].Build(FBox2D(FVector2D(-1.000001),FVector2D(1.000001)),[Surface,Direction](const FVector2D& P){return Surface->SampleHeightMeters(Direction(P));},
            [Surface,Direction](TConstArrayView<FVector2D> Points,TArray<double>& Heights)
            {TArray<FVector> Directions;Directions.Reserve(Points.Num());for(const auto& P:Points)Directions.Add(Direction(P));Surface->SampleHeightsMeters(Directions,Heights);},.00025);
    });
    for(double X=-125;X<=85;X+=10)for(double Y=-52;Y<=52;Y+=10)
    {
        const auto Point=Cave->BodyPoint(FVector(X,Y,45));const double Roof=Base.RadiusMeters+Base.SampleHeightMeters(Point.GetSafeNormal());
        if(Roof-Point.Length()<8){Error=TEXT("Spatial study does not fit beneath this terrain; no partial cave published");return nullptr;}
    }
    auto Out=MakeShared<FCLCompiledCaves>();Out->Spatial=Cave;const FGuid Id(0x53504138,Seed,1,1);
    Out->Networks=Out->RequestedNetworks=1;Out->EligibleCandidates=1;Out->Candidates=1;
    FCLCompiledCaves::FNetwork Info;Info.Id=Id;Info.Family=FCLCompiledCaves::EFamily::CarbonateKarst;Info.InceptionRadiusMeters=Data->Datum;Info.OriginDirection=Data->Up;Info.InspectionBounds=Data->Bounds;Out->NetworkInfo.Add(Info);
    const auto RoomPoint=Cave->BodyPoint(FVector(14,-5,2));Out->Nodes.Add({Id,RoomPoint,{30,20},true,false});
    Out->Nodes.Add({Id,Cave->BodyPoint(FVector(-101,0,2)),{8,6},false,false});
    // These points are traversal/inspection metadata only. They never generate
    // the field and changing them cannot change a wall, roof or floor.
    FCLCavePassage Route;Route.Network=Id;Route.From=0;Route.To=1;Route.Form=Route.EndForm=ECLCaveForm::BeddedPassage;Route.GeometryProfile=8;Route.FloorRock=Base.Rock;
    for(double X=14;X>=-101;X-=5)
    {
        const double Y=-3+2*FMath::Sin(X*.045);const auto P=Cave->BodyPoint(FVector(X,Y,2));Route.Points.Add(P);Route.Sections.Add({9,7});Route.FloorRadii.Add(Data->Datum+Cave->FloorHeight(X,Y));
    }
    Out->Passages.Add(Route);FCLCaveChamber Room;Room.Network=Id;Room.Node=0;Room.Center=Cave->BodyPoint(FVector(24,0,15));Room.Rotation=FRotationMatrix::MakeFromXZ(Data->X,Data->Up).ToQuat();Room.HalfExtents=FVector(48,40,20);Room.Form=ECLCaveForm::BeddedChamber;Room.GeometryProfile=8;Room.FloorRock=Base.Rock;Room.FloorRadius=Data->Datum;Out->Chambers.Add(Room);
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

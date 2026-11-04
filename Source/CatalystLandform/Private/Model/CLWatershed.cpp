#include "Model/CLWatershed.h"
#include "Model/CLPlanetRecipe.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
using namespace UE::Geometry;

void FCLWatershedSettings::ApplyProgression(double P)
{
    Character=FMath::Clamp(P,0.,1.);const double T=Character*Character*(3-2*Character);
    IncisionMeters=FMath::Lerp(.75,9.,T);ValleyWidthMeters=FMath::Lerp(35.,100.,T);
    ChannelAreaSquareMeters=FMath::Lerp(12000.,1500.,T);
}
bool FCLWatershedSettings::Validate(FString& E) const
{
    auto In=[](double V,double A,double B){return FMath::IsFinite(V)&&V>=A&&V<=B;};
    if(AlgorithmVersion!=1||!In(Character,0,1)||AnalysisSubdivisions<4||AnalysisSubdivisions>7||MaximumCells<1026||MaximumCells>65538||
       !In(IncisionMeters,.1,100)||!In(ValleyWidthMeters,1,1000)||!In(ChannelAreaSquareMeters,100,1.e7)||!In(MinimumChannelSlope,0,.01)||!In(ReferenceRainMetersPerYear,0,10)||!In(RunoffFraction,0,1)||ClosedBasinDirections.Num()>64)
    {E=TEXT("Watershed requires version 1, bounded physical controls and a 1026–65538-cell work budget");return false;}
    int32 Count=6;for(int32 L=0;L<AnalysisSubdivisions;++L)Count=(Count-2)*4+2;
    if(Count>MaximumCells){E=TEXT("Watershed analysis exceeds the accepted cell budget; no coarsening was applied");return false;}
    for(const auto& D:ClosedBasinDirections)if(D.ContainsNaN()||!FMath::IsFinite(D.SizeSquared())||!FMath::IsNearlyEqual(D.SizeSquared(),1.,1.e-6))
    {E=TEXT("Closed-basin directions must be finite body-space unit vectors");return false;}
    return true;
}
struct FCLCompiledWatershed::FSpatial
{
    FDynamicMesh3 Mesh;
    FDynamicMeshAABBTree3 Tree;
    FSpatial():Tree(&Mesh,false){}
};
namespace
{
uint64 EdgeKey(int32 A,int32 B){return uint64(FMath::Min(A,B))<<32|uint32(FMath::Max(A,B));}
struct FQueue {double Value=0;int32 Id=0;};
struct FQueueLess {bool operator()(const FQueue& A,const FQueue& B)const{return A.Value==B.Value?A.Id<B.Id:A.Value<B.Value;}};
double Arc(const FVector& A,const FVector& B){return FMath::Acos(FMath::Clamp(FVector::DotProduct(A,B),-1.,1.));}
}
TSharedPtr<const FCLCompiledWatershed> FCLCompiledWatershed::Compile(const FCLWatershedSettings& S,const FCLCompiledPlanetRecipe& Base,FString& Error)
{
    Error.Reset();if(!S.Validate(Error))return nullptr;
    const auto* Sea=Base.Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("OceanHeight");});
    if(!Sea){Error=TEXT("Watersheds require an explicit OceanHeight guide");return nullptr;}
    auto O=MakeShared<FCLCompiledWatershed>();O->Settings=S;O->ReferenceRadiusMeters=Base.RadiusMeters+Base.BaseHeightMeters;
    if(O->ReferenceRadiusMeters<=0){Error=TEXT("Watershed reference surface has no positive radius");return nullptr;}
    // Adapt the retained research's canonical octasphere/routing construction.
    // The analytic research terrain is replaced by the actual composed native source.
    for(const FVector& D:{FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0),FVector(0,0,1),FVector(0,0,-1)}){auto& C=O->Cells.Emplace_GetRef();C.Direction=D;}
    int32 Levels=S.AnalysisSubdivisions;
    while(Levels<7&&O->ReferenceRadiusMeters*2.5/FMath::Pow(2.,Levels)>S.ValleyWidthMeters)++Levels;
    int32 Required=6;for(int32 L=0;L<Levels;++L)Required=(Required-2)*4+2;
    if(Required>S.MaximumCells){Error=TEXT("Physical watershed width requires more cells than the accepted analysis budget");return nullptr;}
    O->Triangles={FIntVector(4,0,2),FIntVector(4,2,1),FIntVector(4,1,3),FIntVector(4,3,0),FIntVector(5,2,0),FIntVector(5,1,2),FIntVector(5,3,1),FIntVector(5,0,3)};
    for(int32 L=0;L<Levels;++L)
    {
        TSet<uint64> Edges;for(const auto& F:O->Triangles)for(int32 K=0;K<3;++K)Edges.Add(EdgeKey(F[K],F[(K+1)%3]));
        auto Keys=Edges.Array();Keys.Sort();TMap<uint64,int32> Mid;
        for(uint64 K:Keys){const FVector D=(O->Cells[int32(K>>32)].Direction+O->Cells[int32(uint32(K))].Direction).GetSafeNormal();auto& C=O->Cells.Emplace_GetRef();C.Direction=D;Mid.Add(K,O->Cells.Num()-1);}
        TArray<FIntVector> Next;Next.Reserve(O->Triangles.Num()*4);
        for(const auto& F:O->Triangles){const int32 A=Mid[EdgeKey(F.X,F.Y)],B=Mid[EdgeKey(F.Y,F.Z)],C=Mid[EdgeKey(F.Z,F.X)];Next.Append({FIntVector(F.X,A,C),FIntVector(A,F.Y,B),FIntVector(C,B,F.Z),FIntVector(A,B,C)});}O->Triangles=MoveTemp(Next);
    }
    TArray<FVector> Directions;for(const auto& C:O->Cells)Directions.Add(C.Direction);
    TArray<double> Heights;Base.SampleHeightsMeters(Directions,Heights);
    int32 Lowest=0;
    for(int32 I=0;I<O->Cells.Num();++I)
    {
        auto& C=O->Cells[I];if(!FMath::IsFinite(Heights[I])){Error=TEXT("Watershed source contains non-finite terrain");return nullptr;}
        C.HeightMeters=Heights[I];C.SpillMeters=DBL_MAX;C.bOcean=C.HeightMeters<=Sea->HeightMeters;
        C.bTerminal=C.bOcean;O->OceanCells+=C.bOcean;
        if(C.HeightMeters<O->Cells[Lowest].HeightMeters)Lowest=I;
    }
    if(!O->OceanCells){O->Cells[Lowest].bTerminal=true;O->ClosedTerminals=1;}
    TSet<int32> Closed;
    for(const auto& D:S.ClosedBasinDirections)
    {
        int32 Best=0;double Dot=-2;
        for(int32 I=0;I<O->Cells.Num();++I){const double V=FVector::DotProduct(D,O->Cells[I].Direction);if(V>Dot){Dot=V;Best=I;}}
        if(Closed.Contains(Best)){Error=TEXT("Closed-basin directions alias the same analysis cell");return nullptr;}
        if(!O->Cells[Best].bTerminal){O->Cells[Best].bTerminal=true;++O->ClosedTerminals;}Closed.Add(Best);
    }
    O->Spatial=MakeShared<FSpatial>();for(const auto& C:O->Cells)O->Spatial->Mesh.AppendVertex(C.Direction);
    for(const auto& F:O->Triangles)
    {
        const auto A=O->Cells[F.X].Direction,B=O->Cells[F.Y].Direction,C=O->Cells[F.Z].Direction;
        const double Area=2*FMath::Atan2(FMath::Abs(FVector::DotProduct(A,FVector::CrossProduct(B,C))),1+FVector::DotProduct(A,B)+FVector::DotProduct(B,C)+FVector::DotProduct(C,A))*FMath::Square(O->ReferenceRadiusMeters);
        for(int32 K=0;K<3;++K){auto& Cell=O->Cells[F[K]];Cell.AreaSquareMeters+=Area/3;Cell.Neighbours.AddUnique(F[(K+1)%3]);Cell.Neighbours.AddUnique(F[(K+2)%3]);}
        if(O->Spatial->Mesh.AppendTriangle(F.X,F.Y,F.Z)<0){Error=TEXT("Native UE watershed analysis mesh rejected a face");return nullptr;}
    }
    O->Spatial->Tree.Build();
    for(auto& C:O->Cells){C.Neighbours.Sort();O->TotalAreaSquareMeters+=C.AreaSquareMeters;for(int32 J:C.Neighbours)O->MaximumEdgeMeters=FMath::Max(O->MaximumEdgeMeters,O->ReferenceRadiusMeters*Arc(C.Direction,O->Cells[J].Direction));}
    if(S.ValleyWidthMeters<O->MaximumEdgeMeters){Error=TEXT("Watershed valley width is below analysis resolution; increase resolution or width");return nullptr;}
    if(!O->Route(Sea->HeightMeters,Closed,Error))return nullptr;
    TArray<FQueue> Queue;
    for(auto& C:O->Cells){C.bShore=C.bOcean||C.bLake;for(int32 J:C.Neighbours)C.bShore|=O->Cells[J].bLake;}
    for(int32 I:O->FloodOrder)
    {
        auto& C=O->Cells[I];const double Erodibility=Base.Rock?Base.Rock->Sample(C.Direction*(Base.RadiusMeters+C.HeightMeters-.1)).Erodibility:.5;
        const double Depth=S.IncisionMeters*FMath::Lerp(.45,1.,FMath::Clamp(Erodibility,0.,1.));
        C.BedMeters=FMath::Max(Base.MinimumHeightMeters,C.HeightMeters-Depth);
        if(C.bShore||C.bTerminal)C.BedMeters=C.HeightMeters;
        if(C.Receiver<0||C.bShore||O->Cells[C.Receiver].bShore||C.AccumulatedAreaSquareMeters<S.ChannelAreaSquareMeters)continue;
        const auto& D=O->Cells[C.Receiver];const double Grade=S.MinimumChannelSlope*O->ReferenceRadiusMeters*Arc(C.Direction,D.Direction);
        if(C.HeightMeters<D.BedMeters+Grade)continue; // unresolved flats remain routing relations, never fabricated slopes
        C.BedMeters=FMath::Max(C.BedMeters,D.BedMeters+Grade);
        O->Channels.Add({I,C.Receiver});
    }
    // Influence distances on the same immutable analysis graph. This is not VPP terrain LOD.
    double LargestArea=1;for(const auto& C:O->Cells)if(!C.bOcean)LargestArea=FMath::Max(LargestArea,C.AccumulatedAreaSquareMeters);
    TArray<double> Width;for(const auto& C:O->Cells)Width.Add(FMath::Clamp(S.ValleyWidthMeters*(.2+.8*FMath::Sqrt(C.AccumulatedAreaSquareMeters/LargestArea)),O->MaximumEdgeMeters,S.ValleyWidthMeters));
    TArray<double> Distance;Distance.Init(DBL_MAX,O->Cells.Num());Queue.Reset();
    for(const auto& C:O->Channels)for(int32 I:{C.From,C.To})if(Distance[I]>0){Distance[I]=0;Queue.HeapPush({0,I},FQueueLess());}
    while(!Queue.IsEmpty())
    {
        FQueue Q;Queue.HeapPop(Q,FQueueLess(),EAllowShrinking::No);if(Q.Value!=Distance[Q.Id])continue;
        for(int32 J:O->Cells[Q.Id].Neighbours){const double V=Q.Value+O->ReferenceRadiusMeters*Arc(O->Cells[Q.Id].Direction,O->Cells[J].Direction)/((Width[Q.Id]+Width[J])*.5);if(V<1&&V<Distance[J]){Distance[J]=V;Queue.HeapPush({V,J},FQueueLess());}}
    }
    for(int32 I=0;I<O->Cells.Num();++I){const double T=FMath::Clamp(1-Distance[I],0.,1.);O->Cells[I].Influence=T*T*(3-2*T);}
    // Record actual native-source conditioning at fixed quarter-edge probes.
    Directions.Reset();for(const auto& C:O->Channels)for(int32 K=0;K<=4;++K)Directions.Add(FMath::Lerp(O->Cells[C.From].Direction,O->Cells[C.To].Direction,K/4.).GetSafeNormal());
    if(!Directions.IsEmpty())
    {
        Base.SampleHeightsMeters(Directions,Heights);
        for(int32 I=0;I<Directions.Num();++I){const double Delta=O->MapHeight(Directions[I],Heights[I])-Heights[I];O->MaximumMeasuredCutMeters=FMath::Max(O->MaximumMeasuredCutMeters,-Delta);O->MaximumMeasuredFillMeters=FMath::Max(O->MaximumMeasuredFillMeters,Delta);}
    }
    // Drainage is analysed again on the accepted conditioned field. Never present
    // pre-incision catchments as the hydrology of a changed surface.
    O->ShapeCells=O->Cells;O->ConditionedReaches=O->Channels.Num();
    for(auto& C:O->Cells){C.HeightMeters=O->MapHeight(C.Direction,C.HeightMeters);C.BedMeters=C.HeightMeters;}
    if(!O->Route(Sea->HeightMeters,Closed,Error))return nullptr;
    for(int32 I=0;I<O->Cells.Num();++I){const auto& C=O->Cells[I];if(C.Receiver>=0&&!C.bLake&&!O->Cells[C.Receiver].bLake&&C.AccumulatedAreaSquareMeters>=S.ChannelAreaSquareMeters)O->DrainageLinks.Add({I,C.Receiver});}
    O->Channels.RemoveAll([&](const auto& C){return O->Cells[C.From].Receiver!=C.To||O->Cells[C.From].bLake||O->Cells[C.To].bLake;});
    return O;
}
bool FCLCompiledWatershed::Route(double Sea,const TSet<int32>& Closed,FString& Error)
{
    OceanCells=LakeCells=Catchments=ClosedTerminals=0;InputDischarge=TerminalDischarge=OceanExport=ClosedInput=MaximumLakeDepthMeters=0;FloodOrder.Reset();
    int32 Lowest=0;
    for(int32 I=0;I<Cells.Num();++I)
    {
        auto& C=Cells[I];C.bOcean=false;C.bTerminal=false;C.Receiver=C.Rank=C.Outlet=INDEX_NONE;C.SpillMeters=DBL_MAX;
        if(C.HeightMeters<Cells[Lowest].HeightMeters)Lowest=I;
    }
    // The largest connected sub-sea component is the reference ocean. Isolated
    // sub-sea depressions remain inland basins; a height threshold alone is insufficient.
    TBitArray<> Classified(false,Cells.Num());TArray<int32> Ocean;double OceanArea=0;
    for(int32 I=0;I<Cells.Num();++I)if(!Classified[I]&&Cells[I].HeightMeters<=Sea)
    {
        TArray<int32> Component{I};Classified[I]=true;double Area=0;
        for(int32 K=0;K<Component.Num();++K){const auto& C=Cells[Component[K]];Area+=C.AreaSquareMeters;for(int32 J:C.Neighbours)if(!Classified[J]&&Cells[J].HeightMeters<=Sea){Classified[J]=true;Component.Add(J);}}
        if(Area>OceanArea){OceanArea=Area;Ocean=MoveTemp(Component);}
    }
    for(int32 I:Ocean){Cells[I].bOcean=Cells[I].bTerminal=true;}OceanCells=Ocean.Num();
    for(int32 I:Closed)if(Cells[I].bOcean){Error=TEXT("A declared closed terminal belongs to the connected reference ocean");return false;}
    if(!OceanCells){Cells[Lowest].bTerminal=true;++ClosedTerminals;}
    for(int32 I:Closed)if(!Cells[I].bTerminal){Cells[I].bTerminal=true;++ClosedTerminals;}
    TArray<FQueue> Queue;TBitArray<> Seen(false,Cells.Num());
    for(int32 I=0;I<Cells.Num();++I)if(Cells[I].bTerminal){auto& C=Cells[I];C.SpillMeters=C.HeightMeters;Seen[I]=true;Queue.HeapPush({C.SpillMeters,I},FQueueLess());}
    while(!Queue.IsEmpty())
    {
        FQueue Q;Queue.HeapPop(Q,FQueueLess(),EAllowShrinking::No);auto& C=Cells[Q.Id];C.Rank=FloodOrder.Add(Q.Id);
        for(int32 J:C.Neighbours)if(!Seen[J]){Seen[J]=true;auto& V=Cells[J];V.Receiver=Q.Id;V.SpillMeters=FMath::Max(Q.Value,V.HeightMeters);Queue.HeapPush({V.SpillMeters,J},FQueueLess());}
    }
    if(FloodOrder.Num()!=Cells.Num()){Error=TEXT("Watershed routing did not visit its complete sphere");return false;}
    TSet<int32> Basins;
    for(int32 I:FloodOrder)
    {
        auto& C=Cells[I];
        if(C.bTerminal)C.Outlet=I;
        else
        {
            double Best=0;int32 Receiver=C.Receiver;
            for(int32 J:C.Neighbours)if(Cells[J].Rank<C.Rank){const double Drop=C.SpillMeters-Cells[J].SpillMeters;if(Drop<=0)continue;const double Grade=Drop/Arc(C.Direction,Cells[J].Direction);if(Grade>Best){Best=Grade;Receiver=J;}}
            C.Receiver=Receiver;C.Outlet=Cells[Receiver].Outlet;
        }
        C.bLake=!C.bOcean&&C.SpillMeters-C.HeightMeters>.002;LakeCells+=C.bLake;MaximumLakeDepthMeters=FMath::Max(MaximumLakeDepthMeters,C.SpillMeters-C.HeightMeters);
        C.AccumulatedAreaSquareMeters=C.bOcean?0:C.AreaSquareMeters;C.DischargeCubicMetersPerSecond=C.AccumulatedAreaSquareMeters*Settings.ReferenceRainMetersPerYear/(365.25*86400)*Settings.RunoffFraction;
        InputDischarge+=C.DischargeCubicMetersPerSecond;if(!C.bOcean)Basins.Add(C.Outlet);
    }
    Catchments=Basins.Num();
    for(int32 K=FloodOrder.Num()-1;K>=0;--K){auto& C=Cells[FloodOrder[K]];if(C.Receiver>=0){auto& D=Cells[C.Receiver];D.AccumulatedAreaSquareMeters+=C.AccumulatedAreaSquareMeters;D.DischargeCubicMetersPerSecond+=C.DischargeCubicMetersPerSecond;}else {TerminalDischarge+=C.DischargeCubicMetersPerSecond;if(C.bOcean)OceanExport+=C.DischargeCubicMetersPerSecond;else ClosedInput+=C.DischargeCubicMetersPerSecond;}}
    return true;
}
double FCLCompiledWatershed::MapHeight(const FVector& Direction,double Height) const
{
    if((ShapeCells.IsEmpty()?Channels.IsEmpty():ConditionedReaches==0)||Direction.ContainsNaN()||Direction.IsNearlyZero())return Height;
    double T;int32 Id;FVector3d B;
    if(!Spatial->Tree.FindNearestHitTriangle(FWatertightRay3d(FRay3d(FVector3d::Zero(),Direction.GetSafeNormal())),T,Id,B))return Height;
    const auto F=Spatial->Mesh.GetTriangle(Id);double W=0,Bed=0,Lake=-DBL_MAX;
    for(int32 K=0;K<3;++K){const auto& C=(ShapeCells.IsEmpty()?Cells:ShapeCells)[F[K]];W+=B[K]*C.Influence;Bed+=B[K]*C.BedMeters;if(C.bLake)Lake=FMath::Max(Lake,C.SpillMeters);}
    // Preserve submerged basin floors; the shoreline transition is continuous.
    if(Lake>-DBL_MAX)W*=FMath::Clamp((Height-Lake)/1.,0.,1.);
    return FMath::Lerp(Height,Bed,FMath::Clamp(W,0.,1.));
}

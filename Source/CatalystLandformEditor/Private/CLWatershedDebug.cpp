#include "CLWatershedDebug.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMeshEditor.h"
#include "Model/CLWatershed.h"

namespace
{
uint64 Edge(int32 A,int32 B){return uint64(FMath::Min(A,B))<<32|uint32(FMath::Max(A,B));}
struct FClip {FVector D;double Depth;int32 Vertex;};
struct FCrossing {int32 Vertex,Body;FVector Wet,Dry;};
}
TSharedPtr<const FCLWatershedDebugGeometry> FCLWatershedDebugGeometry::Build(const FCLCompiledPlanetRecipe& Plan,FCLCompileProgress* Control)
{
    if(!Plan.Watershed)return nullptr;
    const double Start=FPlatformTime::Seconds();auto Out=MakeShared<FCLWatershedDebugGeometry>();Out->Source=Plan.Watershed;
    const auto& W=*Plan.Watershed;const auto& Cells=W.GetCells();const auto& Faces=W.GetTriangles();
    const auto* Sea=Plan.Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("OceanHeight");});if(!Sea)return nullptr;
    auto Cancelled=[&]{return Control&&Control->Cancelled.load();};
    auto Progress=[&](uint32 Done,uint32 Total){if(Control)Control->Work=(uint64(Total)<<32)|Done;};
    TArray<int32> Body;Body.Init(INDEX_NONE,Cells.Num());
    Out->Bodies.Add({Sea->HeightMeters,true,W.OceanCells,0});
    for(int32 I=0;I<Cells.Num();++I)if(Cells[I].bOcean)Body[I]=0;
    for(int32 I=0;I<Cells.Num();++I)if(Cells[I].bLake&&Body[I]<0)
    {
        const int32 Id=Out->Bodies.Num();const double Level=Cells[I].SpillMeters;TArray<int32> Queue{I};Body[I]=Id;
        for(int32 K=0;K<Queue.Num();++K)for(int32 J:Cells[Queue[K]].Neighbours)
            if(Body[J]<0&&Cells[J].bLake&&FMath::Abs(Cells[J].SpillMeters-Level)<1.e-6){Body[J]=Id;Queue.Add(J);}
        Out->Bodies.Add({Level,false,Queue.Num(),0});
    }
    Out->Water.EnableAttributes();Out->Water.Attributes()->SetNumNormalLayers(1);
    struct FMaps {TMap<int32,int32> Vertices;TMap<uint64,int32> Edges;};
    TArray<FIntVector> ShoreIndices;TArray<FMaps> Maps;Maps.SetNum(Out->Bodies.Num());TArray<FCrossing> Crossings;
    auto Vertex=[&](FVector D,int32 Id){const int32 V=Out->Water.AppendVertex(D*(Plan.RadiusMeters+Out->Bodies[Id].HeightMeters));Out->Water.Attributes()->PrimaryNormals()->AppendElement(FVector3f(D));return V;};
    for(int32 F=0;F<Faces.Num();++F)
    {
        if((F&255)==0){if(Cancelled())return nullptr;Progress(F,Faces.Num());}
        const auto Face=Faces[F];TArray<int32,TInlineAllocator<3>> Candidates;
        for(int32 K=0;K<3;++K)if(Body[Face[K]]>=0)Candidates.AddUnique(Body[Face[K]]);
        for(int32 Id:Candidates)
        {
            const double Level=Out->Bodies[Id].HeightMeters;auto& Map=Maps[Id];TArray<FClip,TInlineAllocator<4>> Polygon;TArray<int32,TInlineAllocator<2>> Shore;
            FClip C[3];
            for(int32 K=0;K<3;++K)
            {
                const int32 I=Face[K];double Depth=Level-Cells[I].HeightMeters;if(FMath::Abs(Depth)<1.e-7)Depth=0;
                if(Body[I]>=0&&Body[I]!=Id)Depth=-FMath::Max(.002,FMath::Abs(Depth));
                int32 V=INDEX_NONE;if(Depth>0){auto* Found=Map.Vertices.Find(I);if(Found)V=*Found;else {V=Vertex(Cells[I].Direction,Id);Map.Vertices.Add(I,V);}}
                C[K]={Cells[I].Direction,Depth,V};
            }
            for(int32 K=0;K<3;++K)
            {
                const auto& A=C[K];const auto& B=C[(K+1)%3];if(A.Depth>0)Polygon.Add(A);
                if((A.Depth>0)==(B.Depth>0))continue;
                const uint64 Key=Edge(Face[K],Face[(K+1)%3]);int32 V;FVector D;
                if(auto* Found=Map.Edges.Find(Key)){V=*Found;D=Out->Water.GetVertex(V).GetSafeNormal();}
                else
                {
                    const double T=A.Depth/(A.Depth-B.Depth);D=FMath::Lerp(A.D,B.D,T).GetSafeNormal();V=Vertex(D,Id);Map.Edges.Add(Key,V);
                    // A boundary between distinct wet classifications is not a physical level crossing.
                    if((Level-Cells[Face[K]].HeightMeters)*(Level-Cells[Face[(K+1)%3]].HeightMeters)<=0)
                        Crossings.Add({V,Id,A.Depth>0?A.D:B.D,A.Depth>0?B.D:A.D});
                }
                Polygon.Add({D,0,V});Shore.Add(V);
            }
            for(int32 K=1;K+1<Polygon.Num();++K)
            {
                int32 A=Polygon[0].Vertex,B=Polygon[K].Vertex,CV=Polygon[K+1].Vertex;
                const FVector PA=Out->Water.GetVertex(A),PB=Out->Water.GetVertex(B),PC=Out->Water.GetVertex(CV);
                if(FVector::CrossProduct(PB-PA,PC-PA).SizeSquared()<1.e-16)continue;
                if(FVector::DotProduct(FVector::CrossProduct(PB-PA,PC-PA),PA)<0)Swap(B,CV);
                const int32 T=Out->Water.AppendTriangle(A,B,CV);
                if(T>=0){Out->Water.Attributes()->PrimaryNormals()->SetTriangle(T,UE::Geometry::FIndex3i(A,B,CV));++Out->Bodies[Id].Triangles;}
            }
            // Keep boundary vertex references until native shoreline refinement has completed.
            if(Shore.Num()==2)ShoreIndices.Add(FIntVector(Shore[0],Shore[1],Id));
        }
    }
    // Refine each unique shoreline edge against the final native terrain, not a proxy sphere.
    for(int32 Round=0;!Crossings.IsEmpty()&&Round<8;++Round)
    {
        if(Cancelled())return nullptr;Progress(Round,8);TArray<FVector> D;D.Reserve(Crossings.Num());
        for(const auto& C:Crossings)D.Add((C.Wet+C.Dry).GetSafeNormal());
        TArray<double> H;Plan.SampleHeightsMeters(D,H);
        for(int32 I=0;I<Crossings.Num();++I){auto& C=Crossings[I];if(H[I]<Out->Bodies[C.Body].HeightMeters)C.Wet=D[I];else C.Dry=D[I];}
    }
    for(const auto& C:Crossings){const FVector D=(C.Wet+C.Dry).GetSafeNormal();Out->Water.SetVertex(C.Vertex,D*(Plan.RadiusMeters+Out->Bodies[C.Body].HeightMeters));Out->Water.Attributes()->PrimaryNormals()->SetElement(C.Vertex,FVector3f(D));}
    for(const auto& S:ShoreIndices)Out->Shores.Add({Out->Water.GetVertex(S.X),Out->Water.GetVertex(S.Y),S.Z});

    TSet<uint64> Channels;for(const auto& E:W.GetChannels())Channels.Add(Edge(E.From,E.To));
    TArray<int32> Next,Incoming;Next.Init(INDEX_NONE,Cells.Num());Incoming.Init(0,Cells.Num());
    for(int32 I=0;I<Cells.Num();++I)if(!Cells[I].bOcean&&!Cells[I].bLake&&Cells[I].Receiver>=0&&Cells[I].AccumulatedAreaSquareMeters>=Plan.SourceRecipe->Watershed.ChannelAreaSquareMeters)
    {Next[I]=Cells[I].Receiver;++Incoming[Next[I]];++Out->RoutingEdges;}
    TBitArray<> Seen(false,Cells.Num());
    auto Trace=[&](int32 First)
    {
        FPath P;P.Channel=Channels.Contains(Edge(First,Next[First]));P.Cells.Add(First);int32 I=First;
        while(Next[I]>=0&&!Seen[I]){Seen[I]=true;const int32 J=Next[I];P.Cells.Add(J);if(Next[J]<0||Incoming[J]!=1||Channels.Contains(Edge(J,Next[J]))!=P.Channel)break;I=J;}
        Out->Paths.Add(MoveTemp(P));
    };
    for(int32 I=0;I<Cells.Num();++I)if(Next[I]>=0&&!Seen[I]&&Incoming[I]!=1)Trace(I);
    for(int32 I=0;I<Cells.Num();++I)if(Next[I]>=0&&!Seen[I])Trace(I);
    TArray<FVector> D;TArray<int32> Starts;
    for(const auto& P:Out->Paths)
    {
        Starts.Add(D.Num());D.Add(Cells[P.Cells[0]].Direction);
        for(int32 K=1;K<P.Cells.Num();++K)
        {
            const int32 A=P.Cells[K-1],B=P.Cells[K];FVector End=Cells[B].Direction;
            if(Body[B]>=0)if(const auto* V=Maps[Body[B]].Edges.Find(Edge(A,B)))End=Out->Water.GetVertex(*V).GetSafeNormal();
            for(int32 S=1;S<=4;++S)D.Add(FMath::Lerp(Cells[A].Direction,End,S/4.).GetSafeNormal());
        }
    }
    TArray<double> H;H.SetNumUninitialized(D.Num());
    for(int32 I=0;I<D.Num();I+=512)
    {
        if(Cancelled())return nullptr;Progress(I,D.Num());const int32 N=FMath::Min(512,D.Num()-I);TArray<double> Heights;
        Plan.SampleHeightsMeters(MakeArrayView(D.GetData()+I,N),Heights);for(int32 K=0;K<N;++K)H[I+K]=Heights[K];
    }
    Starts.Add(D.Num());
    for(int32 I=0;I<Out->Paths.Num();++I)for(int32 K=Starts[I];K<Starts[I+1];++K)
    {
        const int32 Key=Out->Paths[I].Curve.AddPoint(float(K-Starts[I]),D[K]*(Plan.RadiusMeters+H[K]+.05));
        // Native linear spline interpolation preserves the draped route; unconstrained cubic curves can cross ridges.
        Out->Paths[I].Curve.Points[Key].InterpMode=CIM_Linear;
    }
    // Clipped wet regions can touch only at a saddle vertex. Use UE's native
    // fan split to retain their positions while making every water patch manifold.
    UE::Geometry::FDynamicMeshEditor Editor(&Out->Water);UE::Geometry::FDynamicMeshEditResult Repair;Editor.SplitBowties(Repair);
    Progress(1,1);Out->Seconds=FPlatformTime::Seconds()-Start;return Out;
}

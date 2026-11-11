#include "CLSpatialCaveFamily.h"
#include "Model/CLSpatialCave.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLCompileProgress.h"

FVector FCLSpatialCaveFamily::BodyPoint(FVector P,int32 I)const{return Networks[I].Cave->BodyPoint(P);}
FVector FCLSpatialCaveFamily::BeddingPoint(FVector P,int32 I,bool Compiled)const{return Networks[I].Cave->BeddingPoint(P,Compiled);}
double FCLSpatialCaveFamily::FloorHeight(double X,double Y,int32,int32 I)const{return Networks[I].Cave->FloorHeight(X,Y);}
bool FCLSpatialCaveFamily::TrySurfaces(const FVector&,FCLSurfaceWeights&)const{return false;}
FBox FCLSpatialCaveFamily::Bounds(FGuid Id)const
{if(!Id.IsValid())return AllBounds;for(const auto& N:Networks)if(N.Id==Id)return N.Bounds;return FBox(ForceInit);}
void FCLSpatialCaveFamily::CellSupportBounds(TArray<FBox>& Out)const{for(const auto& N:Networks)Out.Add(N.Bounds);}
void FCLSpatialCaveFamily::Stats(int32& Levels,int32& Cells,int32& Ramps,int32& Nodes,int32& Exact,uint64& Bytes,double& Error)const
{
    Levels=Networks.IsEmpty()?0:1;Cells=Networks.Num()*3;Ramps=Nodes=Exact=0;Bytes=0;Error=0;
    for(const auto& N:Networks){int32 A,B;uint64 C;double D;N.Cave->GetBeddingCompilationStats(A,B,C,D);Nodes+=A;Exact+=B;Bytes+=C;Error=FMath::Max(Error,D);}
}
void FCLSpatialCaveFamily::Sample(TConstArrayView<FVector> Points,TArray<double>& Air,bool Compiled,const TSet<FGuid>* Selection)const
{
    Air.Init(-1.e30,Points.Num());
    // Cull each approved composition before evaluating noise/morphology. A
    // selected native domain never evaluates another composition's shape.
    for(const auto& N:Networks)
    {
        if(Selection&&!Selection->IsEmpty()&&!Selection->Contains(N.Id))continue;
        TArray<FVector> Active;TArray<int32> Indices;
        for(int32 I=0;I<Points.Num();++I)
        {
            const double Distance=FMath::Sqrt(N.Bounds.ComputeSquaredDistanceToPoint(Points[I]));
            if(Distance>0)Air[I]=FMath::Max(Air[I],-Distance-1);
            else {Active.Add(Points[I]);Indices.Add(I);}
        }
        if(Active.IsEmpty())continue;
        TArray<double> Values;N.Cave->Sample(Active,Values,Compiled);
        for(int32 I=0;I<Indices.Num();++I)Air[Indices[I]]=FMath::Max(Air[Indices[I]],Values[I]);
    }
}
TSharedPtr<const FCLSpatialCaveFamily> FCLSpatialCaveFamily::Build(const FCLCompiledPlanetRecipe& Base,const FCLCaveSettings& S,int32 Seed,FCLCompiledCaves& Out,FString& Error)
{
    auto Family=MakeShared<FCLSpatialCaveFamily>();
    const double MinimumRadius=Base.RadiusMeters+Base.MinimumHeightMeters;
    const double Reference=Base.RadiusMeters+(Base.MinimumHeightMeters+Base.MaximumHeightMeters)*.5;
    const double Scale=FMath::Min(1.,MinimumRadius/500.);
    const double Area=4*UE_DOUBLE_PI*Reference*Reference/1.e6;
    Out.RequestedNetworks=FMath::Max(1,FMath::CeilToInt(FMath::Min(64.,S.NetworksPerSquareKilometer*Area)));
    Out.Candidates=512;Out.EligibleCandidates=Out.Candidates;
    FRandomStream Placement(Seed);const FQuat Rotation(FRotator(Placement.FRandRange(-90,90),Placement.FRandRange(-180,180),Placement.FRandRange(-180,180)));
    for(int32 I=0;I<Out.Candidates&&Out.Networks<Out.RequestedNetworks;++I)
    {
        if(!FCLCompileProgress::Check(Error))return nullptr;
        const double Z=1-2*(I+.5)/Out.Candidates,A=I*2.399963229728653;
        const FVector D=Rotation.RotateVector(FVector(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z));
        bool Crowded=false;for(const auto& N:Out.NetworkInfo)Crowded|=FVector::Distance(N.OriginDirection*Reference,D*Reference)<330*Scale;
        if(Crowded)continue;
        const int32 CompositionSeed=int32(HashCombineFast(uint32(Seed),uint32(I+1)));
        const auto C=FCLSpatialCave::BuildApprovedAt(Base,CompositionSeed,D,Scale,Error);if(!C)return nullptr;
        auto& N=Family->Networks.AddDefaulted_GetRef();N.Id=FGuid(0x53504138,uint32(Seed),uint32(S.Seed),I+1);N.Cave=C->Spatial;N.Bounds=C->Spatial->Bounds();Family->AllBounds+=N.Bounds;
        auto Info=C->NetworkInfo[0];Info.Id=N.Id;Info.Family=FCLCompiledCaves::EFamily::General;Out.NetworkInfo.Add(Info);
        const int32 NodeOffset=Out.Nodes.Num();for(auto Node:C->Nodes){Node.Network=N.Id;Out.Nodes.Add(Node);}
        for(auto Passage:C->Passages){Passage.Network=N.Id;Passage.From+=NodeOffset;Passage.To+=NodeOffset;for(int32 K=1;K<Passage.Points.Num();++K)Out.Segments.Add({N.Id,Passage.Points[K-1],Passage.Points[K],Passage.Sections[K].X,FVector::OneVector,false});Out.Passages.Add(MoveTemp(Passage));}
        for(auto Room:C->Chambers){Room.Network=N.Id;Room.Node+=NodeOffset;Out.Chambers.Add(MoveTemp(Room));}
        ++Out.Networks;
    }
    Out.Readiness=FString::Printf(TEXT("%d approved passage/chamber compositions; original profile-8 anatomy, no ring layouts or appended entrance tunnels"),Out.Networks);
    return Family;
}

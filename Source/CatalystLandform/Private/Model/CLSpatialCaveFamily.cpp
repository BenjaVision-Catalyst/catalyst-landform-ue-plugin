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
double FCLSpatialCaveFamily::MinimumHalfWidth(FGuid Id)const
{double Width=MAX_dbl;for(const auto& N:Networks)if(!Id.IsValid()||N.Id==Id)Width=FMath::Min(Width,N.Cave->MinimumHalfWidth());return Width;}
void FCLSpatialCaveFamily::CellSupportBounds(TArray<FBox>& Out)const{for(const auto& N:Networks)Out.Add(N.Bounds);}
void FCLSpatialCaveFamily::InspectionSupportBounds(TArray<FBox>& Out,FGuid Id)const
{for(const auto& N:Networks)if(!Id.IsValid()||N.Id==Id)N.Cave->GetInspectionSupportBounds(Out);}
void FCLSpatialCaveFamily::Stats(int32& Levels,int32& Cells,int32& Ramps,int32& Nodes,int32& Exact,uint64& Bytes,double& Error)const
{
    Levels=Cells=Ramps=Nodes=Exact=0;Bytes=0;Error=0;
    for(const auto& N:Networks){Levels=FMath::Max(Levels,N.Levels);Cells+=N.Cells;Ramps+=N.Ramps;int32 A,B;uint64 C;double D;N.Cave->GetBeddingCompilationStats(A,B,C,D);Nodes+=A;Exact+=B;Bytes+=C;Error=FMath::Max(Error,D);}
}
void FCLSpatialCaveFamily::Sample(TConstArrayView<FVector> Points,TArray<double>& Air,bool Compiled,const TSet<FGuid>* Selection)const
{
    Air.Init(-1.e30,Points.Num());
    // Cull each spatial network before evaluating noise/morphology.
    for(const auto& N:Networks)
    {
        if(Selection&&!Selection->IsEmpty()&&!Selection->Contains(N.Id))continue;
        TArray<FVector> Active;TArray<int32> Indices;
        for(int32 I=0;I<Points.Num();++I)
        {
            const double Distance=FMath::Sqrt(N.Bounds.ComputeSquaredDistanceToPoint(Points[I]));
            // Native cave inspection needs a finite exterior distance for
            // coarse-cell interpolation. Complete-planet evaluation separately
            // excludes these certified empty supports from cave composition.
            if(Distance>0)Air[I]=FMath::Max(Air[I],-N.Cave->ExteriorDistance(Points[I])-2*N.Cave->MinimumHalfWidth());
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
    const double Scale=FMath::Min(1.,MinimumRadius/900.);
    double Low,High,Cover;if(!S.ResolvePlacement(Base,Low,High,Cover,Error))return nullptr;
    const double Area=4*UE_DOUBLE_PI*Reference*Reference/1.e6;
    Out.RequestedNetworks=FMath::Max(1,FMath::CeilToInt(FMath::Min(64.,S.NetworksPerSquareKilometer*Area)));
    Out.Candidates=512;Out.EligibleCandidates=Out.Candidates;
    FRandomStream Placement(Seed);const double Pitch=Placement.FRandRange(-90,90),Yaw=Placement.FRandRange(-180,180),Roll=Placement.FRandRange(-180,180);
    const FQuat Rotation(FRotator(Pitch,Yaw,Roll));
    for(int32 I=0;I<Out.Candidates&&Out.Networks<Out.RequestedNetworks;++I)
    {
        if(!FCLCompileProgress::Check(Error))return nullptr;
        const double Z=1-2*(I+.5)/Out.Candidates,A=I*2.399963229728653;
        const FVector D=Rotation.RotateVector(FVector(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z));
        // Odd multiplication gives each bounded candidate a distinct stream,
        // including the low seed bits consumed by FRandomStream's floats.
        const int32 CompositionSeed=int32(uint32(Seed)^((uint32(I)+1)*0x9e3779b9u));
        FRandomStream SizeRandom(CompositionSeed^0x53495a45);const double SizeDraw=SizeRandom.FRand();
        const double Size=SizeDraw<.18?FMath::Lerp(.28,.55,SizeDraw/.18):SizeDraw<.88?FMath::Lerp(.65,1.35,(SizeDraw-.18)/.7):FMath::Lerp(1.5,2.8,(SizeDraw-.88)/.12);
        const double FittedScale=Scale*Size;
        const double DepthFraction=((uint32(I)*61u+uint32(Seed)%101u)%101u)/100.;
        const FVector ProposedCentre=D*FMath::Lerp(Low,High,1-DepthFraction);
        bool Crowded=false;for(const auto& N:Family->Networks)Crowded|=FVector::Distance(N.Bounds.GetCenter(),ProposedCentre)<N.Bounds.GetExtent().Length()+420*FittedScale;
        if(Crowded)continue;
        // A seeded permutation stratifies depth across candidates. In every
        // complete 100-candidate cycle the full family, including its ordinary
        // single-level members, is present before placement/physical fitting.
        const int32 DepthCode=(uint32(I)*37u+uint32(Seed)%100u)%100u;
        const int32 Levels=DepthCode<34?1:DepthCode<68?2:DepthCode<87?3:DepthCode<96?4:5;
        const bool Opening=S.bSurfaceEntrances&&(I%4==0||Out.Entrances==0);
        auto C=FCLSpatialCave::BuildNetworkAt(Base,S,CompositionSeed,D,FittedScale,Error,true,Levels,DepthFraction,Opening);
        if(!C&&Opening&&Error.IsEmpty())C=FCLSpatialCave::BuildNetworkAt(Base,S,CompositionSeed,D,FittedScale,Error,true,Levels,DepthFraction,false);
        if(!C){if(!FCLCompileProgress::Check(Error)||!Error.IsEmpty())return nullptr;continue;}
        const FBox CandidateBounds=C->Spatial->Bounds();bool Conflict=false;
        auto CheckFeet=[&](const TArray<FCLCavePassage>& Paths,FGuid Id,const FCLSpatialCave& Other,const FBox& Bounds)
        {
            TArray<FVector> Points;TArray<double> Limits;
            for(const auto& Path:Paths)if(!Id.IsValid()||Path.Network==Id)
            {
                const double ShapeScale=Path.Sections[0].Y/6,WalkScale=MinimumRadius>250?1.:ShapeScale,Head=MinimumRadius>250?2.:2*ShapeScale;
                for(int32 K=0;K<Path.Points.Num();++K)
                {
                    const auto P=Path.Points[K],Up=P.GetSafeNormal();const auto Along=Path.Points[FMath::Min(K+1,Path.Points.Num()-1)]-Path.Points[FMath::Max(0,K-1)];
                    const auto Across=FVector::CrossProduct(Up,Along).GetSafeNormal();
                    for(double Offset:{-.5,0.,.5}){const auto Foot=P+Across*(Offset*WalkScale)-Up*(Head+.5*WalkScale);if(Bounds.IsInsideOrOn(Foot)){Points.Add(Foot);Limits.Add(-.05*WalkScale);}}
                }
            }
            if(Points.IsEmpty())return true;TArray<double> Values;Other.Sample(Points,Values);
            for(int32 K=0;K<Values.Num();++K)if(Values[K]>Limits[K])return false;return true;
        };
        for(const auto& Existing:Family->Networks)if(Existing.Bounds.Intersect(CandidateBounds))
        {
            if(!CheckFeet(C->Passages,FGuid(),*Existing.Cave,Existing.Bounds)||!CheckFeet(Out.Passages,Existing.Id,*C->Spatial,CandidateBounds)){Conflict=true;break;}
        }
        if(Conflict)continue;
        auto& N=Family->Networks.AddDefaulted_GetRef();N.Id=FGuid(0x53504138,uint32(Seed),uint32(S.Seed),I+1);N.Cave=C->Spatial;N.Bounds=C->Spatial->Bounds();Family->AllBounds+=N.Bounds;
        N.Cave->GetFamilyStats(N.Levels,N.Cells,N.Ramps);
        auto Info=C->NetworkInfo[0];Info.Id=N.Id;Info.Family=FCLCompiledCaves::EFamily::General;Out.NetworkInfo.Add(Info);
        const int32 NodeOffset=Out.Nodes.Num();for(auto Node:C->Nodes){Node.Network=N.Id;Out.Nodes.Add(Node);}
        for(auto Passage:C->Passages){Passage.Network=N.Id;Passage.From+=NodeOffset;Passage.To+=NodeOffset;for(int32 K=1;K<Passage.Points.Num();++K)Out.Segments.Add({N.Id,Passage.Points[K-1],Passage.Points[K],Passage.Sections[K].X,FVector::OneVector,false});Out.Passages.Add(MoveTemp(Passage));}
        for(auto Room:C->Chambers){Room.Network=N.Id;Room.Node+=NodeOffset;Out.Chambers.Add(MoveTemp(Room));}
        for(auto Entry:C->SurfaceOpenings){Entry.Network=N.Id;Out.SurfaceOpenings.Add(Entry);++Out.Entrances;}
        ++Out.Networks;
    }
    if(Family->Networks.IsEmpty()){Error=TEXT("Enabled spatial generation produced no valid family member");return nullptr;}
    Out.Readiness=FString::Printf(TEXT("%d seeded spatial networks; connected joint passages, asymmetric chambers and seed-varied floor levels"),Out.Networks);
    return Family;
}

#include "Model/CLCaves.h"
#include "Model/CLSpatialCave.h"

TSharedPtr<const FCLCompiledCaveVolume> FCLCompiledCaveVolume::CompileSpatial(TSharedPtr<const FCLSpatialCave> Cave,FGuid Network)
{
    check(Cave);auto Out=MakeShared<FCLCompiledCaveVolume>();Out->Spatial=MoveTemp(Cave);Out->SpatialNetwork=Network;return Out;
}
TSharedPtr<const FCLCompiledCaveVolume> FCLCompiledCaveVolume::Select(FGuid Network)const
{
    auto Out=MakeShared<FCLCompiledCaveVolume>();Out->Spatial=Spatial;Out->SpatialNetwork=SpatialNetwork;if(Network.IsValid())Out->SelectedNetworks.Add(Network);return Out;
}
TSharedPtr<const FCLCompiledCaveVolume> FCLCompiledCaveVolume::Select(TConstArrayView<FGuid> Networks)const
{
    auto Out=MakeShared<FCLCompiledCaveVolume>();Out->Spatial=Spatial;Out->SpatialNetwork=SpatialNetwork;for(const auto Id:Networks)Out->SelectedNetworks.Add(Id);return Out;
}
FBox FCLCompiledCaveVolume::Bounds(FGuid Network)const
{
    if(Spatial->IsProceduralFamily())
    {
        if(Network.IsValid())return SelectedNetworks.IsEmpty()||SelectedNetworks.Contains(Network)?Spatial->Bounds(Network):FBox(ForceInit);
        if(SelectedNetworks.IsEmpty())return Spatial->Bounds();FBox B(ForceInit);for(const auto Id:SelectedNetworks)B+=Spatial->Bounds(Id);return B;
    }
    return (!Network.IsValid()||Network==SpatialNetwork)&&(SelectedNetworks.IsEmpty()||SelectedNetworks.Contains(SpatialNetwork))?Spatial->Bounds():FBox(ForceInit);
}
double FCLCompiledCaveVolume::MinimumHalfWidth(FGuid Network)const
{
    if(Network.IsValid())return Spatial->MinimumHalfWidth(Network);
    if(SelectedNetworks.IsEmpty())return Spatial->MinimumHalfWidth();
    double Minimum=MAX_dbl;for(const auto Id:SelectedNetworks)Minimum=FMath::Min(Minimum,Spatial->MinimumHalfWidth(Id));return Minimum;
}
double FCLCompiledCaveVolume::Compose(const FVector3d& P,double Field,FCLSurfaceWeights*)const
{ComposeBatch(MakeArrayView(&P,1),MakeArrayView(&Field,1));return Field;}
void FCLCompiledCaveVolume::ComposeBatch(TConstArrayView<FVector3d> Positions,TArrayView<double> Fields)const
{
    check(Positions.Num()==Fields.Num());
    if(!Spatial->IsProceduralFamily()&&!SelectedNetworks.IsEmpty()&&!SelectedNetworks.Contains(SpatialNetwork))
    {for(double& Field:Fields)if(!FMath::IsFinite(Field))Field=-1.e6;return;}
    TArray<double> Air;Spatial->SampleSelected(Positions,Air,SelectedNetworks);
    for(int32 I=0;I<Fields.Num();++I)Fields[I]=FMath::IsFinite(Fields[I])?FMath::Max(Fields[I],Air[I]):Air[I];
}
FVector FCLCompiledCaveVolume::GeologicalPosition(const FVector& Position)const
{return Position;}

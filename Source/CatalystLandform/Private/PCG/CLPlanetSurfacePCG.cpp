#include "PCG/CLPlanetSurfacePCG.h"
#include "CLPlanet.h"
#include "CLPlanetStamp.h"
#include "Model/CLPlanetRecipe.h"
#include "PCGComponent.h"
#include "PCGContext.h"
#include "Data/PCGPointData.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogCLSurfacePCG,Log,All);
namespace
{
UCLPlanetSurfaceRegion* Region(FPCGContext* C)
{
    const auto* Component=Cast<UPCGComponent>(C->ExecutionSource.GetObject());
    return Component&&Component->GetOwner()?Component->GetOwner()->FindComponentByClass<UCLPlanetSurfaceRegion>():nullptr;
}
struct FCLSurfaceContext : FPCGContext
{
    bool Initialized=false;
    int32 Grid=0,X0=0,X1=0,Y0=0,Y1=0,X=0,Y=0;
    int32 Face=0,SampleSeed=0;
    double Reference=0;
    FTransform Pose;
    TSharedPtr<const FCLCompiledPlanetRecipe> Plan;
};
class FCLSurfaceElement : public IPCGElementWithCustomContext<FCLSurfaceContext>
{
public:
    virtual bool CanExecuteOnlyOnMainThread(FPCGContext*) const override {return true;}
    virtual bool IsCacheable(const UPCGSettings*) const override {return false;}
protected:
    virtual bool ExecuteInternal(FPCGContext* Base) const override
    {
        auto* C=static_cast<FCLSurfaceContext*>(Base);
        const auto* S=C->GetInputSettings<UCLPlanetSurfacePCGSettings>();
        if(!C->Initialized)
        {
            C->Initialized=true;auto* R=Region(C);
            if(!S||!R||!IsValid(R->Planet)||R->Face<0||R->Face>=6||R->TilesPerFace<1||R->TilesPerFace>64||
                R->TileX<0||R->TileX>=R->TilesPerFace||R->TileY<0||R->TileY>=R->TilesPerFace||
                !FMath::IsFinite(S->PointsPerSquaredMeter)||S->PointsPerSquaredMeter<=0)
            {UE_LOG(LogCLSurfacePCG,Error,TEXT("Planet surface requires a valid configured region and positive density."));return true;}
            const auto Runtime=R->Planet->GetSamplingRuntime();C->Plan=Runtime?Runtime->RecipePlan:nullptr;
            if(!C->Plan){UE_LOG(LogCLSurfacePCG,Error,TEXT("Planet surface has no accepted terrain plan."));return true;}
            C->Reference=C->Plan->RadiusMeters+C->Plan->BaseHeightMeters;C->Pose=R->Planet->GetActorTransform();
            if(C->Reference<=0||!C->Pose.GetScale3D().Equals(FVector::OneVector,1.e-6))return true;
            const double Cells=FMath::CeilToDouble(2*C->Reference*FMath::Sqrt(S->PointsPerSquaredMeter));
            if(Cells<1||Cells>200000){UE_LOG(LogCLSurfacePCG,Error,TEXT("Planet surface density exceeds lattice budget."));return true;}
            C->Grid=int32(Cells);C->Face=R->Face;C->SampleSeed=S->Seed;
            C->X0=int64(R->TileX)*C->Grid/R->TilesPerFace;C->X1=int64(R->TileX+1)*C->Grid/R->TilesPerFace;
            C->Y0=int64(R->TileY)*C->Grid/R->TilesPerFace;C->Y1=int64(R->TileY+1)*C->Grid/R->TilesPerFace;
            if(int64(C->X1-C->X0)*(C->Y1-C->Y0)>2000000){UE_LOG(LogCLSurfacePCG,Error,TEXT("Planet surface region exceeds two million candidates; increase tile count."));return true;}
            C->X=C->X0;C->Y=C->Y0;
        }
        if(!C->Plan||C->X0>=C->X1||C->Y0>=C->Y1)return true;
        TArray<FVector> Directions;TArray<int32> Seeds;Directions.Reserve(1536);Seeds.Reserve(512);
        // Yield between bounded native sampling batches, keeping the editor responsive.
        for(int32 Count=0;C->X<C->X1&&Count<512;++Count)
        {
            const int32 X=C->X,Y=C->Y;
            if(++C->Y>=C->Y1){C->Y=C->Y0;++C->X;}
            FRandomStream Random(HashCombineFast(uint32(C->SampleSeed),HashCombineFast(uint32(C->Face),HashCombineFast(uint32(X),uint32(Y)))));
            const double U=-1+2*(X+Random.FRand())/C->Grid,V=-1+2*(Y+Random.FRand())/C->Grid;
            const double J=1/FMath::Pow(1+U*U+V*V,1.5);
            if(Random.FRand()>J)continue;
            const FVector D=UCLPlanetSurfacePCGSettings::FaceDirection(C->Face,U,V);
            const FVector T=FVector::CrossProduct(D,FMath::Abs(D.Z)<.9?FVector::ZAxisVector:FVector::XAxisVector).GetSafeNormal();
            const FVector B=FVector::CrossProduct(D,T);
            Directions.Add(D);Directions.Add((D+T*.001).GetSafeNormal());Directions.Add((D+B*.001).GetSafeNormal());Seeds.Add(Random.GetCurrentSeed());
        }
        TArray<double> H;C->Plan->SampleHeightsMeters(Directions,H);
        UPCGPointData* Data=nullptr;
        for(int32 I=0;I<Seeds.Num();++I)
        {
            if(!Data||Data->GetPoints().Num()>=65536)
            {
                // Reuse the last incomplete data set. Output remains owned by the PCG context across yields.
                if(!C->OutputData.TaggedData.IsEmpty())Data=const_cast<UPCGPointData*>(Cast<UPCGPointData>(C->OutputData.TaggedData.Last().Data));
                if(!Data||Data->GetPoints().Num()>=65536)
                {Data=NewObject<UPCGPointData>();auto& O=C->OutputData.TaggedData.AddDefaulted_GetRef();O.Data=Data;O.Pin=PCGPinConstants::DefaultOutputLabel;}
            }
            const int32 K=I*3;const FVector P=Directions[K]*(C->Plan->RadiusMeters+H[K]);
            const FVector A=Directions[K+1]*(C->Plan->RadiusMeters+H[K+1]),B=Directions[K+2]*(C->Plan->RadiusMeters+H[K+2]);
            const FVector N=C->Pose.TransformVectorNoScale(FVector::CrossProduct(A-P,B-P).GetSafeNormal());
            auto& Point=Data->GetMutablePoints().AddDefaulted_GetRef();
            Point.Transform=FTransform(FRotationMatrix::MakeFromZ(N).ToQuat(),C->Pose.TransformPosition(P*100));
            Point.Seed=Seeds[I];Point.Density=1;Point.SetLocalBounds(FBox(FVector(-50),FVector(50)));
        }
        return C->X>=C->X1;
    }
};
class FCLRadialElement : public IPCGElement
{
public:
    virtual bool CanExecuteOnlyOnMainThread(FPCGContext*) const override {return true;}
    virtual bool IsCacheable(const UPCGSettings*) const override {return false;}
protected:
    virtual bool ExecuteInternal(FPCGContext* C) const override
    {
        auto* R=Region(C);if(!R||!IsValid(R->Planet))return true;
        const FVector Center=R->Planet->GetActorLocation();
        for(const auto& Input:C->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel))
        {
            const auto* In=Cast<UPCGPointData>(Input.Data);if(!In)continue;
            auto* Out=NewObject<UPCGPointData>();Out->InitializeFromData(In);Out->GetMutablePoints()=In->GetPoints();
            for(auto& P:Out->GetMutablePoints())P.Transform.SetRotation(FRotationMatrix::MakeFromZ((P.Transform.GetLocation()-Center).GetSafeNormal()).ToQuat());
            auto& O=C->OutputData.TaggedData.Add_GetRef(Input);O.Data=Out;O.Pin=PCGPinConstants::DefaultOutputLabel;
        }
        return true;
    }
};
}
FVector UCLPlanetSurfacePCGSettings::FaceDirection(int32 Face,double U,double V)
{
    FVector D;
    switch(Face){case 0:D=FVector(1,U,V);break;case 1:D=FVector(-1,U,V);break;case 2:D=FVector(U,1,V);break;case 3:D=FVector(U,-1,V);break;case 4:D=FVector(U,V,1);break;default:D=FVector(U,V,-1);break;}
    return D.GetSafeNormal();
}
UCLPlanetSurfaceRegion* UCLPlanetSurfacePCGSettings::ConfigureRegion(AActor* Owner,ACLPlanet* Planet,int32 Face,int32 X,int32 Y,int32 N)
{
    if(!IsValid(Owner)||!IsValid(Planet)||Owner->GetWorld()!=Planet->GetWorld()||Face<0||Face>=6||N<1||N>64||X<0||X>=N||Y<0||Y>=N)return nullptr;
    auto* R=Owner->FindComponentByClass<UCLPlanetSurfaceRegion>();
    if(!R){R=NewObject<UCLPlanetSurfaceRegion>(Owner);Owner->AddInstanceComponent(R);R->RegisterComponent();}
    R->Planet=Planet;R->Face=Face;R->TileX=X;R->TileY=Y;R->TilesPerFace=N;return R;
}
TArray<FPCGPinProperties> UCLPlanetSurfacePCGSettings::OutputPinProperties() const{return {FPCGPinProperties(PCGPinConstants::DefaultOutputLabel,EPCGDataType::Point)};}
FPCGElementPtr UCLPlanetSurfacePCGSettings::CreateElement() const{return MakeShared<FCLSurfaceElement>();}
TArray<FPCGPinProperties> UCLPlanetRadialOrientationPCGSettings::InputPinProperties() const{return {FPCGPinProperties(PCGPinConstants::DefaultInputLabel,EPCGDataType::Point)};}
TArray<FPCGPinProperties> UCLPlanetRadialOrientationPCGSettings::OutputPinProperties() const{return {FPCGPinProperties(PCGPinConstants::DefaultOutputLabel,EPCGDataType::Point)};}
FPCGElementPtr UCLPlanetRadialOrientationPCGSettings::CreateElement() const{return MakeShared<FCLRadialElement>();}

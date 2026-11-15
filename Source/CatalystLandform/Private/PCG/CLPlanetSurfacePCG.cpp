#include "PCG/CLPlanetSurfacePCG.h"
#include "CLPlanet.h"
#include "CLPlanetStamp.h"
#include "Model/CLPlanetRecipe.h"
#include "PCGComponent.h"
#include "PCGContext.h"
#include "Data/PCGPointData.h"
#include "GameFramework/Actor.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

DEFINE_LOG_CATEGORY_STATIC(LogCLSurfacePCG,Log,All);
namespace
{
UCLPlanetSurfaceRegion* Region(FPCGContext* C)
{
    const auto* Component=Cast<UPCGComponent>(C->ExecutionSource.GetObject());
    const auto* Original=Component?Component->GetOriginalComponent():nullptr;
    return Original&&Original->GetOwner()?Original->GetOwner()->FindComponentByClass<UCLPlanetSurfaceRegion>():nullptr;
}
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
UCLPlanetSurfaceRegion* UCLPlanetPCGLibrary::BindSource(AActor* Owner,ACLPlanet* Planet)
{
    if(!IsValid(Owner)||!IsValid(Planet)||Owner->GetWorld()!=Planet->GetWorld())return nullptr;
    auto* R=Owner->FindComponentByClass<UCLPlanetSurfaceRegion>();
    if(!R){R=NewObject<UCLPlanetSurfaceRegion>(Owner);Owner->AddInstanceComponent(R);R->RegisterComponent();}
    R->Planet=Planet;return R;
}
TArray<FPCGPinProperties> UCLPlanetRadialOrientationPCGSettings::InputPinProperties() const{return {FPCGPinProperties(PCGPinConstants::DefaultInputLabel,EPCGDataType::Point)};}
TArray<FPCGPinProperties> UCLPlanetRadialOrientationPCGSettings::OutputPinProperties() const{return {FPCGPinProperties(PCGPinConstants::DefaultOutputLabel,EPCGDataType::Point)};}
FPCGElementPtr UCLPlanetRadialOrientationPCGSettings::CreateElement() const{return MakeShared<FCLRadialElement>();}

#include "CLDeliveryGraphTools.h"
#include "CLDeliveryPrototype.h"
#include "Graphs/VoxelVolumeGraph.h"
#include "Graphs/VoxelOutputNode_OutputVolume.h"
#include "VoxelTerminalGraph.h"
#include "VoxelTerminalGraphRuntime.h"
#include "VoxelParameter.h"
#include "VoxelNodeDefinition.h"
#include "VoxelLinearColorMetadata.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

UVoxelVolumeGraph* CLCreateDeliveryGraph(FString& Error,bool SaveAsset)
{
    Error.Reset();const FString PackageName=TEXT("/CatalystLandform/Prototype/VG_Delivery");
    if(SaveAsset&&FPackageName::DoesPackageExist(PackageName)){Error=TEXT("Refusing to overwrite an existing graph package");return nullptr;}
    auto* Package=SaveAsset?CreatePackage(*PackageName):GetTransientPackage();
    auto* Graph=NewObject<UVoxelVolumeGraph>(Package,SaveAsset?FName(TEXT("VG_Delivery")):NAME_None,SaveAsset?RF_Public|RF_Standalone:RF_Transient);
    FVoxelParameter Parameter;Parameter.Name=TEXT("Delivery");Parameter.Type=FVoxelPinType::Make<FCLDeliveryBinding>();Graph->AddParameter(CLDeliveryPrototype::ParameterGuid(),Parameter);
    if(!Graph->HasMainTerminalGraph())Graph->AddTerminalGraph(GVoxelMainTerminalGraphGuid);
    auto& Terminal=Graph->GetMainTerminalGraph();auto* Ed=&Terminal.GetEdGraph();
    // VPP editor node classes are reflected but not DLL-exported in 2.0p8.
    // This is native editor authoring, with checked schema/property discovery.
    UClass* StructClass=LoadClass<UEdGraphNode>(nullptr,TEXT("/Script/VoxelGraphEditor.VoxelGraphNode_Struct"));
    UClass* ParameterClass=LoadClass<UEdGraphNode>(nullptr,TEXT("/Script/VoxelGraphEditor.VoxelGraphNode_Parameter"));
    if(!StructClass||!ParameterClass){Error=TEXT("Native VPP editor nodes unavailable");return nullptr;}
    auto AddStruct=[&](const FVoxelNode& Value,int32 X)
    {
        auto* Node=NewObject<UEdGraphNode>(Ed,StructClass,NAME_None,RF_Transactional);
        auto* Property=FindFProperty<FStructProperty>(StructClass,TEXT("Struct"));check(Property);
        *Property->ContainerPtrToValuePtr<FVoxelInstancedStruct>(Node)=FVoxelInstancedStruct::Make(Value);
        Node->NodePosX=X;Node->CreateNewGuid();Ed->AddNode(Node,false,false);Node->PostPlacedNewNode();Node->AllocateDefaultPins();return Node;
    };
    auto* Param=NewObject<UEdGraphNode>(Ed,ParameterClass,NAME_None,RF_Transactional);
    auto* GuidProperty=FindFProperty<FStructProperty>(ParameterClass,TEXT("Guid"));check(GuidProperty);
    *GuidProperty->ContainerPtrToValuePtr<FGuid>(Param)=CLDeliveryPrototype::ParameterGuid();
    Param->CreateNewGuid();Ed->AddNode(Param,false,false);Param->PostPlacedNewNode();Param->AllocateDefaultPins();
    FCLNode_DeliveryDistance Distance;auto* DistanceNode=AddStruct(Distance,300);
    FCLNode_DeliverySurfaceType Surface;auto* SurfaceNode=AddStruct(Surface,300);
    FCLNode_DeliveryRockProperties Rock;auto* RockNode=AddStruct(Rock,300);
    FCLNode_DeliveryBounds Bounds;auto* BoundsNode=AddStruct(Bounds,300);
    SurfaceNode->NodePosY=200;RockNode->NodePosY=400;BoundsNode->NodePosY=600;
    FVoxelOutputNode_OutputVolume Output;FVoxelOutputNode_MetadataBase::FDefinition Definition(Output);Definition.AddInputPin();
    Output.PromotePin(Output.FindPinChecked(TEXT("MetadataValue_Entry")),FVoxelPinType::Make<FVoxelLinearColorBuffer>());
    auto* Sink=AddStruct(Output,650);
    auto Link=[&](UEdGraphNode* From,const TCHAR* A,UEdGraphNode* To,const TCHAR* B)
    {
        auto* Out=From->FindPin(FName(A),EGPD_Output);auto* In=To->FindPin(FName(B),EGPD_Input);
        if(!Out||!In||!Ed->GetSchema()->TryCreateConnection(Out,In)){Error=FString::Printf(TEXT("Native graph connection failed: %s -> %s"),A,B);return false;}return true;
    };
    for(auto* Node:{DistanceNode,SurfaceNode,RockNode,BoundsNode})if(!Link(Param,TEXT("Value"),Node,TEXT("Binding")))return nullptr;
    if(!Link(DistanceNode,TEXT("Distance"),Sink,TEXT("Distance"))||!Link(BoundsNode,TEXT("Bounds"),Sink,TEXT("Bounds"))||!Link(SurfaceNode,TEXT("SurfaceType"),Sink,TEXT("SurfaceType"))||!Link(RockNode,TEXT("RockProperties"),Sink,TEXT("MetadataValue_Entry")))return nullptr;
    auto* Metadata=LoadObject<UVoxelLinearColorMetadata>(nullptr,FCLCompiledRock::PropertiesAssetPath());
    if(!Metadata){Error=TEXT("Rock metadata missing");return nullptr;}
    auto* MetadataPin=Sink->FindPin(TEXT("MetadataName_Entry"),EGPD_Input);
    FVoxelPinValue MetadataDefault(FVoxelPinType(MetadataPin->PinType).GetPinDefaultValueType());MetadataDefault.GetObject()=Metadata;MetadataDefault.ApplyToPinDefaultValue(*MetadataPin);
    Ed->NotifyGraphChanged();Terminal.GetRuntime().EnsureIsCompiled(true);
    if(SaveAsset)
    {
        FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
        if(!UPackage::SavePackage(Package,Graph,*FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension()),Args)){Error=TEXT("Native graph package save failed");return nullptr;}
    }
    return Graph;
}

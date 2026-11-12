#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/StaticMesh.h"
#include "StaticMesh/VoxelStaticMesh.h"
#include "StaticMesh/VoxelStaticMeshData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FCLRiftStampCandidateTest,"Catalyst.Landform.Rift.Stamps.NativeMeshCandidates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FCLRiftStampCandidateTest::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{
    FString Input,Output;if(FParse::Value(FCommandLine::Get(),TEXT("CLRiftStampMeshes="),Input)&&FParse::Value(FCommandLine::Get(),TEXT("CLRiftStampReport="),Output)){Names.Add(TEXT("Local Megascans"));Commands.Add(TEXT("local"));}
}
bool FCLRiftStampCandidateTest::RunTest(const FString&)
{
    FString Input,Output,Json;
    if(!FParse::Value(FCommandLine::Get(),TEXT("CLRiftStampMeshes="),Input)||!FParse::Value(FCommandLine::Get(),TEXT("CLRiftStampReport="),Output))
    {AddError(TEXT("Native stamp inspection requires explicit local mesh manifest and report paths"));return false;}
    TArray<TSharedPtr<FJsonValue>> Items;if(!FFileHelper::LoadFileToString(Json,*Input)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Items)){AddError(TEXT("Invalid local mesh manifest"));return false;}
    if(Items.Num()<1||Items.Num()>8){AddError(TEXT("Inspect one to eight explicit local candidates"));return false;}
    TArray<TSharedPtr<FJsonValue>> Rows;
    for(const auto& Item:Items)
    {
        const auto Obj=Item->AsObject();const FString Path=Obj->GetStringField(TEXT("package"));auto* Mesh=LoadObject<UStaticMesh>(nullptr,*Path);
        if(!TestNotNull(TEXT("Local candidate mesh loads"),Mesh))return false;
        const FVector Size=Mesh->GetBoundingBox().GetSize();const int32 Voxel=FMath::Min3(Size.X,Size.Y,Size.Z)<200?5:20;const double Cells=(Size.X/Voxel+8)*(Size.Y/Voxel+8)*(Size.Z/Voxel+8);
        if(!TestTrue(TEXT("Native bake remains bounded to ten million cells"),Cells<1.e7))return false;
        const FString PackageName=TEXT("/Game/LandformDesigner/Provinces/RiftStampCandidates/VM_")+Mesh->GetName();
        auto* Stamp=LoadObject<UVoxelStaticMesh>(nullptr,*PackageName,nullptr,LOAD_NoWarn|LOAD_Quiet);
        if(!Stamp)
        {
            Stamp=NewObject<UVoxelStaticMesh>(CreatePackage(*PackageName),*FPackageName::GetLongPackageAssetName(PackageName),RF_Public|RF_Standalone);
            Stamp->Mesh=Mesh;Stamp->VoxelSize=Voxel;Stamp->VoxelizerSettings.bWatertight=false;
            Stamp->VoxelizerSettings.SweepDirection=Size.X<=Size.Y&&Size.X<=Size.Z?EVoxelAxis::X:Size.Y<=Size.Z?EVoxelAxis::Y:EVoxelAxis::Z;
        }
        if(!TestTrue(TEXT("An existing candidate retains its original mesh"),Stamp->Mesh.ToSoftObjectPath()==FSoftObjectPath(Mesh)))return false;
        if(Stamp->VoxelSize!=Voxel){Stamp->VoxelSize=Voxel;Stamp->OnReimport();}
        const auto Data=Stamp->GetData();if(!TestTrue(TEXT("Stock VPP voxelizes the real mesh"),Data&&Data->DistanceField.Num()>0))return false;
        int32 Inside=0,Outside=0;for(float V:Data->DistanceField){if(!FMath::IsFinite(V)){AddError(TEXT("Nonfinite native stamp distance"));return false;}Inside+=V<0;Outside+=V>0;}
        if(!TestTrue(TEXT("Native stamp has both solid and exterior samples"),Inside>0&&Outside>0))return false;
        FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
        const FString File=FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension());IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
        if(!TestTrue(TEXT("Private native stamp saves"),UPackage::SavePackage(Stamp->GetOutermost(),Stamp,*File,Args)))return false;
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("source"),Path);Row->SetStringField(TEXT("sourceSHA256"),Obj->GetStringField(TEXT("sha256")));Row->SetStringField(TEXT("nativeStamp"),PackageName);Row->SetStringField(TEXT("sizeMeters"),(Size*.01).ToString());Row->SetNumberField(TEXT("voxelCentimeters"),Stamp->VoxelSize);Row->SetNumberField(TEXT("fieldSamples"),Data->DistanceField.Num());Row->SetNumberField(TEXT("solidSamples"),Inside);Row->SetNumberField(TEXT("exteriorSamples"),Outside);Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    FJsonSerializer::Serialize(Rows,TJsonWriterFactory<>::Create(&Json));TestTrue(TEXT("Native candidate report saves"),FFileHelper::SaveStringToFile(Json,*Output));AddInfo(FString::Printf(TEXT("RIFT_NATIVE_STAMPS candidates=%d stockVoxelizer=1 privateHostOnly=1"),Rows.Num()));return !HasAnyErrors();
}
#endif

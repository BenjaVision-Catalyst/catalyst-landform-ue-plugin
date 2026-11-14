#include "CLInspectionRecoveryLibrary.h"
#include "AssetCompilingManager.h"
#include "Editor.h"
#include "Misc/PackageName.h"
#include "UObject/Linker.h"
#include "UObject/LinkerLoad.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

bool UCLInspectionRecoveryLibrary::DetachRecoveryPackages(const TArray<FString>& PackageNames,FString& Error)
{
    Error.Reset();
    if(!IsInGameThread() || !GEditor || GEditor->PlayWorld || PackageNames.IsEmpty())
    {Error=TEXT("Recovery requires the editor game thread, no PIE, and explicit loaded packages");return false;}
    TArray<UObject*> Packages;
    for(const auto& Name:PackageNames)
    {
        if(!FPackageName::IsValidLongPackageName(Name) || (!Name.StartsWith(TEXT("/Game/")) && !Name.StartsWith(TEXT("/Engine/"))))
        {Error=TEXT("Recovery package must be an explicit /Game or /Engine package: ")+Name;return false;}
        auto* Package=FindPackage(nullptr,*Name);
        if(!Package){Error=TEXT("Recovery package is not loaded: ")+Name;return false;}
        Packages.AddUnique(Package);
    }
    FlushAsyncLoading();
    for(auto* Object:Packages) CastChecked<UPackage>(Object)->FullyLoad();
    FAssetCompilingManager::Get().FinishAllCompilation();
    FlushAsyncLoading();
    // Read thumbnails while checkpoint bytes still match the loaded headers.
    // Warming a SavePackage alone is insufficient: its linker remains live.
    for(auto* Object:Packages)
        if(auto* Linker=FLinkerLoad::FindExistingLinkerForPackage(Object->GetPackage())) Linker->SerializeThumbnails(true);
    ResetLoaders(Packages);
    for(auto* Object:Packages)
        if(FLinkerLoad::FindExistingLinkerForPackage(Object->GetPackage()))
        {Error=TEXT("Recovery package still has a live reader: ")+Object->GetName();return false;}
    return true;
}

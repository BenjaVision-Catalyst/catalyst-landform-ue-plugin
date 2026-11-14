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


#include "ObjectTools.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/SavePackage.h"
#include "UObject/ObjectRedirector.h"
bool UCLInspectionRecoveryLibrary::RenameAssetPreservingUnsavedReferences(UObject* Asset,const FString& NewName,const TArray<FString>& ReferencingPackages,FString& Error)
{
 Error.Reset();
 if(!GEditor||GEditor->PlayWorld||!IsValid(Asset)||!Asset->GetOutermost()->GetName().StartsWith(TEXT("/Game/"))||NewName.IsEmpty()||!FName::IsValidXName(NewName,INVALID_OBJECTNAME_CHARACTERS))
 {Error=TEXT("Rename requires an explicit game asset, valid name and no PIE");return false;}
 TArray<UPackage*> Packages;
 for(const auto& Name:ReferencingPackages)
 {
  if(!Name.StartsWith(TEXT("/Game/"))&&!Name.StartsWith(TEXT("/Engine/"))){Error=TEXT("Invalid explicit referencer: ")+Name;return false;}
  auto* P=FindPackage(nullptr,*Name);if(!P){Error=TEXT("Referencer is not loaded: ")+Name;return false;}Packages.AddUnique(P);
 }
 auto* OldPackage=Asset->GetOutermost();const FSoftObjectPath OldPath(Asset);
 ObjectTools::FPackageGroupName Target;Target.ObjectName=NewName;Target.PackageName=FPackageName::GetLongPackagePath(OldPackage->GetName())/NewName;
 if(FindObject<UObject>(nullptr,*(Target.PackageName+TEXT(".")+NewName))||FPackageName::DoesPackageExist(Target.PackageName))
 {Error=TEXT("Rename destination already exists");return false;}
 TSet<UPackage*> Refused;FText NativeError;
 if(!ObjectTools::RenameSingleObject(Asset,Target,Refused,NativeError,nullptr,true))
 {Error=NativeError.ToString();return false;}
 TMap<FSoftObjectPath,FSoftObjectPath> Redirects;Redirects.Add(OldPath,FSoftObjectPath(Asset));
 FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().RenameReferencingSoftObjectPaths(Packages,Redirects);
 FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
 for(auto* Package:{Asset->GetOutermost(),OldPackage})
 {
  const FString Filename=FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());
  if(!UPackage::SavePackage(Package,Package==Asset->GetOutermost()?Asset:nullptr,*Filename,Args))
  {Error=TEXT("Rename applied in memory but definition/redirector save failed: ")+Filename;return false;}
 }
 for(auto* Package:Packages)if(Package!=Asset->GetOutermost()&&Package!=OldPackage)Package->MarkPackageDirty();
 return true;
}

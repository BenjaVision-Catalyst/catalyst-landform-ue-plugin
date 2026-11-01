#include "CLRadialGuideEditing.h"
#include "ScopedTransaction.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
bool FCLRadialGuideEditing::CanRename(const FCLRadialGuideSet& RadialGuides, int32 Index, const FString& Name, FString& Error)
{
    const FString Trimmed = Name.TrimStartAndEnd();
    const FName NewName(*Trimmed);
    if (!RadialGuides.Guides.IsValidIndex(Index) || Trimmed.IsEmpty() || Trimmed.Len() > 128 || NewName.IsNone())
    { Error = TEXT("Use a nonempty Radial Guide name of at most 128 characters"); return false; }
    for (int32 I = 0; I < RadialGuides.Guides.Num(); ++I) if (I != Index && RadialGuides.Guides[I].Name == NewName)
    { Error = TEXT("Another Radial Guide already has this name"); return false; }
    Error.Reset(); return true;
}
bool FCLRadialGuideEditing::Rename(FCLPlanetRecipe& Recipe, int32 Index, const FString& Name, FString& Error)
{
    if (!CanRename(Recipe.RadialGuides, Index, Name, Error)) return false;
    const FName Old = Recipe.RadialGuides.Guides[Index].Name, New(*Name.TrimStartAndEnd());
    Recipe.RadialGuides.Guides[Index].Name = New;
    Recipe.RenameGuideReferences(Old,New);
    return true;
}
int32 FCLRadialGuideEditing::Add(FCLRadialGuideSet& RadialGuides, double Height)
{
    if (RadialGuides.Guides.Num() >= 128 || !FMath::IsFinite(Height) || Height < 0 || Height > 1) return INDEX_NONE;
    int32 Suffix = 1; FName Name;
    do { Name = FName(*FString::Printf(TEXT("RadialGuide%d"), Suffix++)); }
    while (RadialGuides.Guides.ContainsByPredicate([&](const FCLRadialGuide& S) { return S.Name == Name; }));
    FCLRadialGuide S; S.Name = Name; S.NormalizedHeight = Height;
    return RadialGuides.Guides.Add(S);
}
bool FCLRadialGuideEditing::Remove(FCLRadialGuideSet& RadialGuides, int32 Index)
{
    if (!RadialGuides.Guides.IsValidIndex(Index)) return false;
    RadialGuides.Guides.RemoveAt(Index); return true;
}

FCLRadialGuideSession::FCLRadialGuideSession() = default;
FCLRadialGuideSession::~FCLRadialGuideSession() = default;
void FCLRadialGuideSession::Open(UCLPlanetRecipeAsset* InAsset)
{
    bReadOnly = false;
    Asset = InAsset; Asset->SetFlags(RF_Transactional);
    Library.Reset(Asset->RadialGuideLibrary);
    if(!Library)Library.Reset(LoadObject<UCLRadialGuideLibraryAsset>(nullptr,TEXT("/Game/LandformDesigner/DA_RadialGuideLibrary.DA_RadialGuideLibrary")));
    if(!Library)
    {
        Library.Reset(NewObject<UCLRadialGuideLibraryAsset>(CreatePackage(TEXT("/Game/LandformDesigner/DA_RadialGuideLibrary")),TEXT("DA_RadialGuideLibrary"),RF_Public|RF_Standalone|RF_Transactional));
        if(const auto* Old=LoadObject<UCLRadialGuideLibraryAsset>(nullptr,TEXT("/Game/LandformDesigner/DA_StrataLibrary.DA_StrataLibrary")))Library->Library=Old->Library;
        FAssetRegistryModule::AssetCreated(Library.Get());Library->MarkPackageDirty();
    }
    Library->SetFlags(RF_Transactional);
    if(Library->GetPathName()==TEXT("/Game/LandformDesigner/DA_RadialGuideLibrary.DA_RadialGuideLibrary") && FCLRadialGuidePresets::Seed(Library->Library))Library->MarkPackageDirty();
    // Legacy snapshots remain detached until an explicit authoring change/selection.
    FString Error;Asset->SynchronizeRadialGuides(Error);
}

bool FCLRadialGuideSession::OpenPreview(UCLPlanetRecipeAsset* TransientPreview, const FCLRadialGuideSet* InitialRadialGuides)
{
    // Preview ownership must never be installed on a persistent planet asset.
    if (!TransientPreview || !TransientPreview->GetOutermost()->HasAnyFlags(RF_Transient)) return false;
    Asset = TransientPreview; bReadOnly = true;
    Library.Reset(NewObject<UCLRadialGuideLibraryAsset>(GetTransientPackage(), NAME_None, RF_Transient));
    auto* Catalog=LoadObject<UCLRadialGuideLibraryAsset>(nullptr,TEXT("/Game/LandformDesigner/DA_RadialGuideLibrary.DA_RadialGuideLibrary"));
    if(!Catalog)Catalog=LoadObject<UCLRadialGuideLibraryAsset>(nullptr,TEXT("/Game/LandformDesigner/DA_StrataLibrary.DA_StrataLibrary"));
    if (Catalog)
        Library->Library = Catalog->Library;
    else Library->Library.Sets = FCLRadialGuidePresets::Defaults();
    UsePreviewRadialGuides(InitialRadialGuides ? *InitialRadialGuides : Asset->Recipe.RadialGuides);
    return true;
}
void FCLRadialGuideSession::UsePreviewRadialGuides(const FCLRadialGuideSet& RadialGuides)
{
    if (!bReadOnly || !Asset || !Asset->GetOutermost()->HasAnyFlags(RF_Transient)) return;
    const FCLRadialGuideSet Snapshot = RadialGuides;
    Asset->Recipe.RadialGuides = Snapshot; Selected = INDEX_NONE;
    if (auto* Entry = Library->Library.Sets.FindByPredicate([&](const FCLRadialGuideSet& S) { return S.Id == Snapshot.Id; })) *Entry = Snapshot;
    else Library->Library.Sets.Add(Snapshot);
    // Notify the scratch renderer without a transaction, dirty flag, version
    // update or write to the shared catalog or terrain definition.
    if (OnPreviewChanged) OnPreviewChanged();
    Asset->OnRecipeChanged.Broadcast();
}

void FCLRadialGuideSession::Begin(const FText& Text)
{
    if (bReadOnly || Gesture) return;
    Gesture = MakeUnique<FScopedTransaction>(Text); BeforeRecipe = Asset->Recipe; BeforeLibrary = Library->Library; Asset->Modify(); Library->Modify();
    BeforeGuideLibrary=Asset->RadialGuideLibrary;BeforeGuideSetId=Asset->RadialGuideSetId;
}

void FCLRadialGuideSession::End(bool bCancel)
{
    if (!Gesture) return;
    const bool bNotify = bPendingChange && !bCancel;
    if (bCancel)
    {
        Library->Library=BeforeLibrary;FString Error;Asset->SetRadialGuideSet(BeforeGuideLibrary,BeforeGuideSetId,Error);Asset->Recipe=BeforeRecipe;
        if (OnPreviewChanged) OnPreviewChanged();
        Gesture->Cancel();
    }
    bPendingChange = false; Gesture.Reset();
    if (bNotify) {if(Asset->RadialGuideLibrary)Library->PostEditChange();Asset->PostEditChange();}
}

void FCLRadialGuideSession::Changed()
{
    if (bReadOnly) return;
    if(!Asset->RadialGuideLibrary)
    {
        // Do not overwrite an edited catalog entry when linking an older snapshot.
        const auto* Existing=Library->FindSet(Asset->Recipe.RadialGuides.Id);
        const auto& Before=Gesture?BeforeRecipe.RadialGuides:Asset->Recipe.RadialGuides;
        if(!Existing || !FCLRadialGuideSet::StaticStruct()->CompareScriptStruct(Existing,&Before,0))
        {
            Asset->Recipe.RadialGuides.Id=FGuid::NewGuid();
            const FString BaseName=Asset->Recipe.Name+TEXT(" guides");Asset->Recipe.RadialGuides.Name=BaseName;int32 Suffix=2;
            while(Library->Library.Sets.ContainsByPredicate([&](const auto& S){return S.Name.Equals(Asset->Recipe.RadialGuides.Name,ESearchCase::IgnoreCase);}))
                Asset->Recipe.RadialGuides.Name=FString::Printf(TEXT("%s %d"),*BaseName,Suffix++);
        }
    }
    const auto Value=Asset->Recipe.RadialGuides;
    if(auto* S=Library->Library.Sets.FindByPredicate([&](const auto& V){return V.Id==Value.Id;}))*S=Value;
    else Library->Library.Sets.Add(Value);
    FString Error;Asset->SetRadialGuideSet(Library.Get(),Value.Id,Error);
    Library->MarkPackageDirty();Asset->MarkPackageDirty();bPendingChange=true;
    if(OnPreviewChanged)OnPreviewChanged();
    if(!Gesture){bPendingChange=false;Library->PostEditChange();Asset->PostEditChange();}
}

void FCLRadialGuideSession::Height(int32 Index, double Height)
{
    if (bReadOnly || !Asset->Recipe.RadialGuides.Guides.IsValidIndex(Index)) return;
    double& Current = Asset->Recipe.RadialGuides.Guides[Index].NormalizedHeight;
    if (!FMath::IsFinite(Height) || Height < 0 || Height > 1 || FMath::IsNearlyEqual(Current, Height, 1.e-10)) return;
    Current = Height; Changed();
}

int32 FCLRadialGuideSession::Add(double Height)
{
    if (bReadOnly || !FMath::IsFinite(Height) || Height < 0 || Height > 1 || Asset->Recipe.RadialGuides.Guides.Num() >= 128) return INDEX_NONE;
    Begin(NSLOCTEXT("CL", "AddRadialGuide", "Add Radial Guide"));
    Selected = FCLRadialGuideEditing::Add(Asset->Recipe.RadialGuides, Height);
    Changed(); End(); return Selected;
}

void FCLRadialGuideSession::Remove(int32 Index)
{
    if (bReadOnly || !Asset->Recipe.RadialGuides.Guides.IsValidIndex(Index)) return;
    Begin(NSLOCTEXT("CL", "RemoveRadialGuide", "Remove Radial Guide"));
    FCLRadialGuideEditing::Remove(Asset->Recipe.RadialGuides, Index); Selected = INDEX_NONE; Changed(); End();
}

void FCLRadialGuideSession::Apply(FGuid Id)
{
    const auto* S = Library->Library.Sets.FindByPredicate([&](const FCLRadialGuideSet& V) { return V.Id == Id; });
    if (!S) return;
    if (bReadOnly) { UsePreviewRadialGuides(*S); return; }
    Begin(NSLOCTEXT("CL", "ApplyRadial Guides", "Apply Radial Guides to planet"));
    FString Error;Asset->SetRadialGuideSet(Library.Get(),Id,Error);Selected=INDEX_NONE;Changed();End();
}

void FCLRadialGuideSession::NewSet()
{
    if (bReadOnly) return;
    Begin(NSLOCTEXT("CL", "NewRadial Guides", "Create Radial Guides"));
    FCLRadialGuideSet S = Asset->Recipe.RadialGuides; S.Id = FGuid::NewGuid();
    int32 N = 1;
    do { S.Name = FString::Printf(TEXT("New Radial Guides %d"), N++); }
    while (Library->Library.Sets.ContainsByPredicate([&](const FCLRadialGuideSet& V) { return V.Name.Equals(S.Name, ESearchCase::IgnoreCase); }));
    Library->Library.Sets.Add(S);FString Error;Asset->SetRadialGuideSet(Library.Get(),S.Id,Error);Selected=INDEX_NONE;Changed();End();
}

void FCLRadialGuideSession::RemoveSet()
{
    if (bReadOnly) return;
    Begin(NSLOCTEXT("CL", "RemoveRadial Guides", "Remove Radial Guides"));
    const FGuid Id = Asset->Recipe.RadialGuides.Id;
    Library->Library.Sets.RemoveAll([&](const FCLRadialGuideSet& S) { return S.Id == Id; });
    FString Error;Asset->SetRadialGuideSet(nullptr,FGuid(),Error);
    if (!Library->Library.Sets.IsEmpty()) {Asset->SetRadialGuideSet(Library.Get(),Library->Library.Sets[0].Id,Error);}
    else { Asset->Recipe.RadialGuides = FCLRadialGuideSet(); Asset->Recipe.RadialGuides.Id = FGuid::NewGuid(); }
    Selected = INDEX_NONE; Changed(); End();
}

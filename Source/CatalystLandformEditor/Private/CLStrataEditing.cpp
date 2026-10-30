#include "CLStrataEditing.h"
#include "ScopedTransaction.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
bool FCLStrataEditing::CanRename(const FCLStrata& Strata, int32 Index, const FString& Name, FString& Error)
{
    const FString Trimmed = Name.TrimStartAndEnd();
    const FName NewName(*Trimmed);
    if (!Strata.Stratum.IsValidIndex(Index) || Trimmed.IsEmpty() || Trimmed.Len() > 128 || NewName.IsNone())
    { Error = TEXT("Use a nonempty Stratum name of at most 128 characters"); return false; }
    for (int32 I = 0; I < Strata.Stratum.Num(); ++I) if (I != Index && Strata.Stratum[I].Name == NewName)
    { Error = TEXT("Another Stratum already has this name"); return false; }
    Error.Reset(); return true;
}
bool FCLStrataEditing::Rename(FCLPlanetRecipe& Recipe, int32 Index, const FString& Name, FString& Error)
{
    if (!CanRename(Recipe.Strata, Index, Name, Error)) return false;
    const FName Old = Recipe.Strata.Stratum[Index].Name, New(*Name.TrimStartAndEnd());
    Recipe.Strata.Stratum[Index].Name = New;
    for (FCLNoiseLayer& L : Recipe.NoiseLayers)
    {
        if (L.Minimum.Guide == Old) L.Minimum.Guide = New;
        if (L.Maximum.Guide == Old) L.Maximum.Guide = New;
    }
    return true;
}
int32 FCLStrataEditing::Add(FCLStrata& Strata, double Height)
{
    if (Strata.Stratum.Num() >= 128 || !FMath::IsFinite(Height) || Height < 0 || Height > 1) return INDEX_NONE;
    int32 Suffix = 1; FName Name;
    do { Name = FName(*FString::Printf(TEXT("Stratum%d"), Suffix++)); }
    while (Strata.Stratum.ContainsByPredicate([&](const FCLStratum& S) { return S.Name == Name; }));
    FCLStratum S; S.Name = Name; S.NormalizedHeight = Height;
    return Strata.Stratum.Add(S);
}
bool FCLStrataEditing::Remove(FCLStrata& Strata, int32 Index)
{
    if (!Strata.Stratum.IsValidIndex(Index)) return false;
    Strata.Stratum.RemoveAt(Index); return true;
}

FCLStrataSession::FCLStrataSession() = default;
FCLStrataSession::~FCLStrataSession() = default;
void FCLStrataSession::Open(UCLPlanetRecipeAsset* InAsset)
{
    Asset = InAsset; Asset->SetFlags(RF_Transactional);
    const TCHAR* Path = TEXT("/Game/LandformDesigner/DA_StrataLibrary.DA_StrataLibrary");
    Library.Reset(LoadObject<UCLStrataLibraryAsset>(nullptr, Path));
    if (!Library)
    {
        Library.Reset(NewObject<UCLStrataLibraryAsset>(CreatePackage(TEXT("/Game/LandformDesigner/DA_StrataLibrary")),
            TEXT("DA_StrataLibrary"), RF_Public | RF_Standalone | RF_Transactional));
        FAssetRegistryModule::AssetCreated(Library.Get());
        Library->MarkPackageDirty();
    }
    Library->SetFlags(RF_Transactional);
    if (FCLStrataPresets::Seed(Library->Library)) Library->MarkPackageDirty();
    if (!Asset->Recipe.Strata.Id.IsValid()) Asset->Recipe.Strata.Id = FGuid::NewGuid();
    if (!Library->Library.Sets.ContainsByPredicate([&](const FCLStrata& S) { return S.Id == Asset->Recipe.Strata.Id; }))
    { Library->Modify(); Library->Library.Sets.Add(Asset->Recipe.Strata); Library->MarkPackageDirty(); }
}

void FCLStrataSession::Begin(const FText& Text)
{
    if (Gesture) return;
    Gesture = MakeUnique<FScopedTransaction>(Text); BeforeRecipe = Asset->Recipe; BeforeLibrary = Library->Library; Asset->Modify(); Library->Modify();
}

void FCLStrataSession::End(bool bCancel)
{
    if (!Gesture) return;
    const bool bNotify = bPendingChange && !bCancel;
    if (bCancel)
    {
        Asset->Recipe = BeforeRecipe; Library->Library = BeforeLibrary;
        if (OnPreviewChanged) OnPreviewChanged();
        Gesture->Cancel();
    }
    bPendingChange = false; Gesture.Reset();
    if (bNotify) Asset->PostEditChange();
}

void FCLStrataSession::Changed()
{
    if (FCLStrata* S = Library->Library.Sets.FindByPredicate([&](const FCLStrata& V) { return V.Id == Asset->Recipe.Strata.Id; }))
        *S = Asset->Recipe.Strata;
    Library->MarkPackageDirty(); Asset->MarkPackageDirty(); bPendingChange = true;
    if (OnPreviewChanged) OnPreviewChanged();
    if (!Gesture) { bPendingChange = false; Asset->PostEditChange(); }
}

void FCLStrataSession::Height(int32 Index, double Height)
{
    if (!Asset->Recipe.Strata.Stratum.IsValidIndex(Index)) return;
    double& Current = Asset->Recipe.Strata.Stratum[Index].NormalizedHeight;
    if (!FMath::IsFinite(Height) || Height < 0 || Height > 1 || FMath::IsNearlyEqual(Current, Height, 1.e-10)) return;
    Current = Height; Changed();
}

void FCLStrataSession::Remove(int32 Index)
{
    if (!Asset->Recipe.Strata.Stratum.IsValidIndex(Index)) return;
    Begin(NSLOCTEXT("CL", "RemoveStratum", "Remove Stratum"));
    FCLStrataEditing::Remove(Asset->Recipe.Strata, Index); Selected = INDEX_NONE; Changed(); End();
}

void FCLStrataSession::Apply(FGuid Id)
{
    const auto* S = Library->Library.Sets.FindByPredicate([&](const FCLStrata& V) { return V.Id == Id; });
    if (!S) return;
    Begin(NSLOCTEXT("CL", "ApplyStrata", "Apply Strata to planet"));
    Asset->Recipe.Strata = *S; Selected = INDEX_NONE; Changed(); End();
}

void FCLStrataSession::NewSet()
{
    Begin(NSLOCTEXT("CL", "NewStrata", "Create Strata"));
    FCLStrata S = Asset->Recipe.Strata; S.Id = FGuid::NewGuid();
    int32 N = 1;
    do { S.Name = FString::Printf(TEXT("New Strata %d"), N++); }
    while (Library->Library.Sets.ContainsByPredicate([&](const FCLStrata& V) { return V.Name.Equals(S.Name, ESearchCase::IgnoreCase); }));
    Library->Library.Sets.Add(S); Asset->Recipe.Strata = S; Selected = INDEX_NONE; Changed(); End();
}

void FCLStrataSession::RemoveSet()
{
    Begin(NSLOCTEXT("CL", "RemoveStrata", "Remove Strata"));
    const FGuid Id = Asset->Recipe.Strata.Id;
    Library->Library.Sets.RemoveAll([&](const FCLStrata& S) { return S.Id == Id; });
    if (!Library->Library.Sets.IsEmpty()) Asset->Recipe.Strata = Library->Library.Sets[0];
    else { Asset->Recipe.Strata = FCLStrata(); Asset->Recipe.Strata.Id = FGuid::NewGuid(); }
    Selected = INDEX_NONE; Changed(); End();
}

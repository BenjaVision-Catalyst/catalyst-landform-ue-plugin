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
    auto RenameLayers = [&](TArray<FCLNoiseLayer>& Layers)
    {
        for (FCLNoiseLayer& L : Layers)
        for (FCLHeightReference* Ref : {&L.Minimum, &L.Maximum, &L.Terrace.StepHeight, &L.Terrace.Origin,
            &L.Distribution.MinimumRelief, &L.Distribution.MaximumRelief})
        {
            if (Ref->Guide == Old) Ref->Guide = New;
            if (Ref->UpperGuide == Old) Ref->UpperGuide = New;
        }
    };
    RenameLayers(Recipe.NoiseLayers);
    for(auto& Region:Recipe.TerrainRegions)RenameLayers(Region.NoiseLayers);
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
    const TCHAR* Path = TEXT("/Game/LandformDesigner/DA_StrataLibrary.DA_StrataLibrary");
    Library.Reset(LoadObject<UCLRadialGuideLibraryAsset>(nullptr, Path));
    if (!Library)
    {
        Library.Reset(NewObject<UCLRadialGuideLibraryAsset>(CreatePackage(TEXT("/Game/LandformDesigner/DA_StrataLibrary")),
            TEXT("DA_StrataLibrary"), RF_Public | RF_Standalone | RF_Transactional));
        FAssetRegistryModule::AssetCreated(Library.Get());
        Library->MarkPackageDirty();
    }
    Library->SetFlags(RF_Transactional);
    if (FCLRadialGuidePresets::Seed(Library->Library)) Library->MarkPackageDirty();
    if (!Asset->Recipe.RadialGuides.Id.IsValid()) Asset->Recipe.RadialGuides.Id = FGuid::NewGuid();
    if (!Library->Library.Sets.ContainsByPredicate([&](const FCLRadialGuideSet& S) { return S.Id == Asset->Recipe.RadialGuides.Id; }))
    { Library->Modify(); Library->Library.Sets.Add(Asset->Recipe.RadialGuides); Library->MarkPackageDirty(); }
}

bool FCLRadialGuideSession::OpenPreview(UCLPlanetRecipeAsset* TransientPreview, const FCLRadialGuideSet* InitialRadialGuides)
{
    // Preview ownership must never be installed on a persistent planet asset.
    if (!TransientPreview || !TransientPreview->GetOutermost()->HasAnyFlags(RF_Transient)) return false;
    Asset = TransientPreview; bReadOnly = true;
    Library.Reset(NewObject<UCLRadialGuideLibraryAsset>(GetTransientPackage(), NAME_None, RF_Transient));
    if (const auto* Catalog = LoadObject<UCLRadialGuideLibraryAsset>(nullptr, TEXT("/Game/LandformDesigner/DA_StrataLibrary.DA_StrataLibrary")))
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
}

void FCLRadialGuideSession::End(bool bCancel)
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

void FCLRadialGuideSession::Changed()
{
    if (bReadOnly) return;
    if (FCLRadialGuideSet* S = Library->Library.Sets.FindByPredicate([&](const FCLRadialGuideSet& V) { return V.Id == Asset->Recipe.RadialGuides.Id; }))
        *S = Asset->Recipe.RadialGuides;
    Library->MarkPackageDirty(); Asset->MarkPackageDirty(); bPendingChange = true;
    if (OnPreviewChanged) OnPreviewChanged();
    if (!Gesture) { bPendingChange = false; Asset->PostEditChange(); }
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
    Asset->Recipe.RadialGuides = *S; Selected = INDEX_NONE; Changed(); End();
}

void FCLRadialGuideSession::NewSet()
{
    if (bReadOnly) return;
    Begin(NSLOCTEXT("CL", "NewRadial Guides", "Create Radial Guides"));
    FCLRadialGuideSet S = Asset->Recipe.RadialGuides; S.Id = FGuid::NewGuid();
    int32 N = 1;
    do { S.Name = FString::Printf(TEXT("New Radial Guides %d"), N++); }
    while (Library->Library.Sets.ContainsByPredicate([&](const FCLRadialGuideSet& V) { return V.Name.Equals(S.Name, ESearchCase::IgnoreCase); }));
    Library->Library.Sets.Add(S); Asset->Recipe.RadialGuides = S; Selected = INDEX_NONE; Changed(); End();
}

void FCLRadialGuideSession::RemoveSet()
{
    if (bReadOnly) return;
    Begin(NSLOCTEXT("CL", "RemoveRadial Guides", "Remove Radial Guides"));
    const FGuid Id = Asset->Recipe.RadialGuides.Id;
    Library->Library.Sets.RemoveAll([&](const FCLRadialGuideSet& S) { return S.Id == Id; });
    if (!Library->Library.Sets.IsEmpty()) Asset->Recipe.RadialGuides = Library->Library.Sets[0];
    else { Asset->Recipe.RadialGuides = FCLRadialGuideSet(); Asset->Recipe.RadialGuides.Id = FGuid::NewGuid(); }
    Selected = INDEX_NONE; Changed(); End();
}

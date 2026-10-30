#include "CLStrataEditing.h"
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

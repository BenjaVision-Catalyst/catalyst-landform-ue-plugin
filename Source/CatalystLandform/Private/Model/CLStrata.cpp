#include "Model/CLStrata.h"

bool FCLStrata::Validate(FString& Error) const
{
    Error.Reset();
    if (Name.TrimStartAndEnd().IsEmpty() || Stratum.Num() > 128)
    { Error = TEXT("Strata requires a name and at most 128 Stratum"); return false; }
    TSet<FName> Names;
    for (const FCLStratum& S : Stratum)
    {
        if (S.Name.IsNone() || Names.Contains(S.Name))
        { Error = TEXT("Stratum names must be nonempty and unique"); return false; }
        if (!FMath::IsFinite(S.NormalizedHeight) || S.NormalizedHeight < 0 || S.NormalizedHeight > 1 ||
            !FMath::IsFinite(S.Color.R) || !FMath::IsFinite(S.Color.G) || !FMath::IsFinite(S.Color.B) || !FMath::IsFinite(S.Color.A))
        { Error = TEXT("Invalid Stratum: ") + S.Name.ToString(); return false; }
        Names.Add(S.Name);
    }
    return true;
}
FString UCLStrataLibraryAsset::GetSummaryText() const
{ return FString::Printf(TEXT("%d Strata styles"), Library.Sets.Num()); }
bool UCLStrataLibraryAsset::ValidatePayload(FString& Error) const
{
    TSet<FGuid> Ids; TSet<FString> Names;
    for (const FCLStrata& S : Library.Sets)
    {
        if (!S.Id.IsValid() || Ids.Contains(S.Id) || Names.Contains(S.Name.ToLower()))
        { Error = TEXT("Strata IDs and names must be unique"); return false; }
        if (!S.Validate(Error)) return false;
        Ids.Add(S.Id); Names.Add(S.Name.ToLower());
    }
    return true;
}

TArray<FCLStrata> FCLStrataPresets::Defaults()
{
    TArray<FCLStrata> Sets;
    auto Add = [&](const TCHAR* Name, uint32 Id, double Low, double High, TOptional<double> Ocean, TOptional<double> Atmosphere)
    {
        FCLStrata S; S.Name = Name; S.Id = FGuid(0x434C0001, 0, 0, Id);
        auto Guide = [&](const TCHAR* Label, double Height, FLinearColor Color)
        { FCLStratum G; G.Name = Label; G.NormalizedHeight = Height; G.Color = Color; S.Stratum.Add(G); };
        if (Ocean) Guide(TEXT("OceanHeight"), *Ocean, FLinearColor(.1, .55, 1));
        Guide(TEXT("SurfaceMin"), Low, FLinearColor(.8, .65, .3));
        Guide(TEXT("SurfaceMax"), High, FLinearColor(.4, .9, .45));
        if (Atmosphere) Guide(TEXT("AtmosphereMax"), *Atmosphere, FLinearColor(.5, .7, 1));
        Sets.Add(MoveTemp(S));
    };
    Add(TEXT("Temperate"), 1, .30, .60, .40, .90);
    Add(TEXT("Oceanic"), 3, .35, .60, .55, .94);
    Add(TEXT("High relief"), 4, .30, .80, .45, .95);
    Add(TEXT("Airless rocky"), 5, .65, .82, {}, {});
    Add(TEXT("Deep atmosphere"), 6, .20, .40, .30, .95);
    return Sets;
}
bool FCLStrataPresets::Seed(FCLStrataLibrary& Library)
{
    if (Library.PresetRevision >= 1) return false;
    for (FCLStrata Preset : Defaults())
    {
        if (Library.Sets.ContainsByPredicate([&](const FCLStrata& S) { return S.Id == Preset.Id; })) continue;
        const FString BaseName = Preset.Name; int32 Suffix = 1;
        while (Library.Sets.ContainsByPredicate([&](const FCLStrata& S) { return S.Name.Equals(Preset.Name, ESearchCase::IgnoreCase); }))
            Preset.Name = FString::Printf(TEXT("%s (built-in %d)"), *BaseName, Suffix++);
        Library.Sets.Add(MoveTemp(Preset));
    }
    Library.PresetRevision = 1;
    return true;
}

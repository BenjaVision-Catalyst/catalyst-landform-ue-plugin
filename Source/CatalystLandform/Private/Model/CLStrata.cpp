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

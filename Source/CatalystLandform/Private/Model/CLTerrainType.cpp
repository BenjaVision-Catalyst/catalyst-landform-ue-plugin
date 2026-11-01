#include "Model/CLTerrainType.h"
#include "CLModelJson.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "Validation/CFValidationTypes.h"

#if WITH_EDITOR
void UCLTerrainTypeAsset::PostEditChangeProperty(FPropertyChangedEvent& Event)
{ Super::PostEditChangeProperty(Event); if (!(Event.ChangeType & EPropertyChangeType::Interactive)) OnTerrainChanged.Broadcast(); }
void UCLTerrainTypeAsset::PostEditUndo()
{ Super::PostEditUndo(); OnTerrainChanged.Broadcast(); }
#endif

bool FCLTerrainType::Validate(FString& Error) const
{
    if ((SchemaVersion < 1 || SchemaVersion > 6) || (AlgorithmVersion != 1 && AlgorithmVersion != 2) ||
        (SchemaVersion == 1 && AlgorithmVersion != 1) || !Id.IsValid() || Name.TrimStartAndEnd().IsEmpty())
    { Error = TEXT("Terrain type requires schema 1..6, a supported algorithm, a valid ID and a nonempty name"); return false; }
    if (SchemaVersion < 3 && NoiseLayers.ContainsByPredicate([](const auto& L) { return L.Operation != ECLTerrainOperation::Noise; }))
    { Error = TEXT("Terracing requires terrain type schema 3"); return false; }
    if(SchemaVersion<4 && NoiseLayers.ContainsByPredicate([](const auto& L){return L.Operation==ECLTerrainOperation::StampDistribution;}))
    {Error=TEXT("Stamp distributions require terrain schema 4");return false;}
    if (uint8(Coordinates) > uint8(ECLTerrainCoordinates::SphericalSurface) || (Coordinates != ECLTerrainCoordinates::Sphere && SchemaVersion < 5))
    { Error = TEXT("Local terrain coordinates require terrain schema 5"); return false; }
    if (SchemaVersion < 5 && NoiseLayers.ContainsByPredicate([](const auto& L){return L.Operation==ECLTerrainOperation::StampDistribution && L.Distribution.ReliefMode==ECLStampReliefMode::GuideInterval;}))
    { Error = TEXT("Guide-based stamp relief requires terrain schema 5"); return false; }
    if(Coordinates==ECLTerrainCoordinates::SphericalSurface && SchemaVersion<6){Error=TEXT("Spherical surface terrain requires schema 6");return false;}
    return FCLPlanetCompiler::ValidateNoiseLayers(NoiseLayers, Error);
}
bool FCLTerrainType::ApplyTo(FCLPlanetRecipe& Recipe, FString& Error, const FBox2D* LocalRegion) const
{
    if (!Validate(Error)) return false;
    FCLPlanetRecipe Candidate = Recipe;
    Candidate.SchemaVersion = Coordinates==ECLTerrainCoordinates::SphericalSurface?8:Candidate.TerrainRegions.IsEmpty()?6:7; Candidate.Coordinates = Coordinates; Candidate.AlgorithmVersion = AlgorithmVersion; Candidate.TerrainTypeId = Id; Candidate.TerrainTypeName = Name;
    Candidate.NoiseLayers = NoiseLayers;
    const auto* First = NoiseLayers.FindByPredicate([](const auto& Layer) { return Layer.bEnabled; });
    if (Candidate.BaseSurfaceNormalized == 1 && (Coordinates != ECLTerrainCoordinates::Sphere || !First || First->Operation != ECLTerrainOperation::Noise || First->Blend != ECLNoiseBlend::Override))
    {
        // Stamps and additive/shaping stacks need a neutral input with headroom.
        // Resolve it into the recipe so muting an absolute base layer does not
        // move the remaining noise to the sky. Explicit non-default bases survive.
        const auto* Lower = Candidate.Strata.Stratum.FindByPredicate([](const auto& Guide) { return Guide.Name == TEXT("SurfaceMin"); });
        const auto* Upper = Candidate.Strata.Stratum.FindByPredicate([](const auto& Guide) { return Guide.Name == TEXT("SurfaceMax"); });
        if (Lower && Upper) Candidate.BaseSurfaceNormalized = (Lower->NormalizedHeight + Upper->NormalizedHeight) * .5;
    }
    if (!FCLPlanetCompiler::Compile(Candidate, Error, nullptr, LocalRegion)) return false;
    Recipe = MoveTemp(Candidate); return true;
}
bool UCLTerrainTypeAsset::ApplyToRecipe(UCLPlanetRecipeAsset* Asset, FString& Error) const
{
    if (!Asset) { Error = TEXT("A contextual planet recipe is required"); return false; }
    FCLPlanetRecipe Candidate = Asset->Recipe;
    if (!TerrainType.ApplyTo(Candidate, Error)) return false;
    Asset->Modify(); Asset->Recipe = MoveTemp(Candidate);
#if WITH_EDITOR
    Asset->PostEditChange(); Asset->MarkPackageDirty();
#endif
    return true;
}
bool FCLTerrainType::ApplyToRegion(FCLPlanetRecipe& Recipe, const FGuid& RegionId, FString& Error) const
{
    if(!Validate(Error))return false;
    if(Coordinates==ECLTerrainCoordinates::Sphere || Recipe.Coordinates!=Coordinates)
    {Error=TEXT("Region assembly requires a local terrain definition and local planet patch");return false;}
    auto Candidate=Recipe;auto* Region=Candidate.TerrainRegions.FindByPredicate([&](const auto& R){return R.Id==RegionId;});
    if(!Region){Error=TEXT("Terrain region was not found");return false;}
    Region->bSphericalMask=Coordinates==ECLTerrainCoordinates::SphericalSurface;
    Region->TerrainTypeId=Id;Region->TerrainTypeName=Name;Region->AlgorithmVersion=AlgorithmVersion;Region->NoiseLayers=NoiseLayers;
    Candidate.SchemaVersion=Coordinates==ECLTerrainCoordinates::SphericalSurface?8:7;
    if(!FCLPlanetCompiler::Compile(Candidate,Error))return false;
    Recipe=MoveTemp(Candidate);return true;
}
bool UCLTerrainTypeAsset::ApplyToRegion(UCLPlanetRecipeAsset* Asset, FGuid RegionId, FString& Error) const
{
    if(!Asset){Error=TEXT("A planet recipe is required");return false;}
    auto Candidate=Asset->Recipe;if(!TerrainType.ApplyToRegion(Candidate,RegionId,Error))return false;
    Asset->Modify();Asset->Recipe=MoveTemp(Candidate);
#if WITH_EDITOR
    Asset->PostEditChange();Asset->MarkPackageDirty();
#endif
    return true;
}
void UCLTerrainTypeAsset::PostLoad()
{
    Super::PostLoad();
    if (TerrainType.SchemaVersion >= 1 && TerrainType.SchemaVersion < 4) TerrainType.SchemaVersion = 4;
    Version.SchemaVersion = TerrainType.SchemaVersion;
}
void UCLTerrainTypeAsset::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
    Super::PostDuplicate(DuplicateMode);
    if (DuplicateMode == EDuplicateMode::Normal) TerrainType.Id = FGuid::NewGuid();
}
FString UCLTerrainTypeAsset::GetSummaryText() const
{ return FString::Printf(TEXT("%s: %d noise layers; planet guides supplied by context"), *TerrainType.Name, TerrainType.NoiseLayers.Num()); }
bool UCLTerrainTypeAsset::ValidatePayload(FString& Error) const { return TerrainType.Validate(Error); }
void UCLTerrainTypeAsset::CollectValidationMessages(TArray<FCFValidationMessage>& Messages) const
{
    Super::CollectValidationMessages(Messages); FString Error;
    if (!ValidatePayload(Error)) Messages.Add(FCFValidationMessage::Make(ECFValidationSeverity::Error, TEXT("CL.TerrainType.Invalid"),
        FText::FromString(Error), FText(), TEXT("/TerrainType")));
}
bool UCLTerrainTypeAsset::ImportTerrainTypeJson(const FString& Json, FString& Error)
{
    Error.Reset();
    if (Json.Len() > 256 * 1024) { Error = TEXT("Terrain type JSON exceeds 256 KiB"); return false; }
    TSharedPtr<FJsonObject> Object;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object)
    { Error = TEXT("Invalid terrain type JSON object"); return false; }
    if (!CLModelJson::CheckFields(*Object, *FCLTerrainType::StaticStruct(), Error, TEXT("TerrainType"))) return false;
    double Schema = 0;
    if (!Object->TryGetNumberField(TEXT("schemaVersion"), Schema) || (Schema != 1 && Schema != 2 && Schema != 3 && Schema != 4 && Schema != 5 && Schema != 6))
    { Error = TEXT("Terrain type requires schemaVersion 1..6"); return false; }
    FCLTerrainType Candidate; FText Reason;
    if (!FJsonObjectConverter::JsonObjectToUStruct(Object.ToSharedRef(), &Candidate, 0, 0, false, &Reason))
    { Error = Reason.ToString(); return false; }
    if (!Candidate.Validate(Error)) return false;
    Candidate.SchemaVersion = Candidate.Coordinates==ECLTerrainCoordinates::SphericalSurface?6:5;
    Modify(); TerrainType = MoveTemp(Candidate);
#if WITH_EDITOR
    PostEditChange(); MarkPackageDirty();
#endif
    return true;
}

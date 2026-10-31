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
    if ((SchemaVersion < 1 || SchemaVersion > 4) || (AlgorithmVersion != 1 && AlgorithmVersion != 2) ||
        (SchemaVersion == 1 && AlgorithmVersion != 1) || !Id.IsValid() || Name.TrimStartAndEnd().IsEmpty())
    { Error = TEXT("Terrain type requires schema 1/2/3/4, a supported algorithm, a valid ID and a nonempty name"); return false; }
    if (SchemaVersion < 3 && NoiseLayers.ContainsByPredicate([](const auto& L) { return L.Operation != ECLTerrainOperation::Noise; }))
    { Error = TEXT("Terracing requires terrain type schema 3"); return false; }
    if(SchemaVersion<4 && NoiseLayers.ContainsByPredicate([](const auto& L){return L.Operation==ECLTerrainOperation::StampDistribution;}))
    {Error=TEXT("Stamp distributions require terrain schema 4");return false;}
    return FCLPlanetCompiler::ValidateNoiseLayers(NoiseLayers, Error);
}
bool FCLTerrainType::ApplyTo(FCLPlanetRecipe& Recipe, FString& Error) const
{
    if (!Validate(Error)) return false;
    FCLPlanetRecipe Candidate = Recipe;
    Candidate.SchemaVersion = 5; Candidate.AlgorithmVersion = AlgorithmVersion; Candidate.TerrainTypeId = Id; Candidate.TerrainTypeName = Name;
    Candidate.NoiseLayers = NoiseLayers;
    if (!FCLPlanetCompiler::Compile(Candidate, Error)) return false;
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
    if (!Object->TryGetNumberField(TEXT("schemaVersion"), Schema) || (Schema != 1 && Schema != 2 && Schema != 3 && Schema != 4))
    { Error = TEXT("Terrain type requires schemaVersion 1, 2, 3 or 4"); return false; }
    FCLTerrainType Candidate; FText Reason;
    if (!FJsonObjectConverter::JsonObjectToUStruct(Object.ToSharedRef(), &Candidate, 0, 0, false, &Reason))
    { Error = Reason.ToString(); return false; }
    if (!Candidate.Validate(Error)) return false;
    Candidate.SchemaVersion = 4;
    Modify(); TerrainType = MoveTemp(Candidate);
#if WITH_EDITOR
    PostEditChange(); MarkPackageDirty();
#endif
    return true;
}

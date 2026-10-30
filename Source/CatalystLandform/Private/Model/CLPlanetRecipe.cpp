#include "Model/CLPlanetRecipe.h"
#include "CLModelJson.h"
#include "JsonObjectConverter.h"
#include "Validation/CFValidationTypes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "Serialization/CustomVersion.h"

namespace
{
const FGuid RecipeAssetVersion(0x434C5250, 0x82B741FA, 0xA3482664, 0xC96741B2);
FCustomVersionRegistration RegisterRecipeAssetVersion(RecipeAssetVersion, 1, TEXT("CLRecipeOuterSkyRadius"));
uint32 Mix(uint32 V)
{
    V ^= V >> 16; V *= 0x7feb352du; V ^= V >> 15; V *= 0x846ca68bu; return V ^ (V >> 16);
}

}

double FCLCompiledPlanetRecipe::SampleHeightMeters(const FVector& Direction) const
{
    if (Direction.ContainsNaN() || Direction.IsNearlyZero()) return 0;
    const FVector Position = Direction.GetSafeNormal() * RadiusMeters;
    double Height = BaseHeightMeters;
    for (const FNoise& Layer : Layers)
    {
        double Sum = 0, Weight = 1, Total = 0, Frequency = 1;
        for (int32 I = 0; I < Layer.Octaves; ++I)
        {
            const double N = FMath::PerlinNoise3D(Position * (Frequency / Layer.Wavelength) + Layer.Offset);
            Sum += (Layer.Shape == ECLNoiseShape::Ridged ? 1 - FMath::Abs(N) : .5 + .5 * N) * Weight;
            Total += Weight; Weight *= Layer.Persistence; Frequency *= 2;
        }
        const double Value = FMath::Lerp(Layer.Minimum, Layer.Maximum, FMath::Clamp(Sum / Total, 0.0, 1.0));
        switch (Layer.Blend)
        {
        case ECLNoiseBlend::Override: Height = Value; break;
        case ECLNoiseBlend::Add: Height += Value; break;
        case ECLNoiseBlend::Max: Height = FMath::Max(Height, Value); break;
        case ECLNoiseBlend::Min: Height = FMath::Min(Height, Value); break;
        }
    }
    return FMath::Min(Height, 0.0);
}

bool FCLPlanetCompiler::ValidateNoiseLayers(const TArray<FCLNoiseLayer>& Layers, FString& Error)
{
    Error.Reset();
    if (Layers.Num() > 32) { Error = TEXT("At most 32 noise layers are supported"); return false; }
    TSet<FName> Names;
    for (const FCLNoiseLayer& Layer : Layers)
    {
        if (Layer.Name.IsNone() || Names.Contains(Layer.Name)) { Error = TEXT("Noise layer names must be nonempty and unique"); return false; }
        Names.Add(Layer.Name); if (!Layer.bEnabled) continue;
        for (const FCLHeightReference* Ref : {&Layer.Minimum, &Layer.Maximum})
        {
            const bool bHeight = Ref->Mode == ECLHeightReferenceMode::Height;
            if (!FMath::IsFinite(Ref->OffsetMeters) || !FMath::IsFinite(Ref->Fraction) ||
                static_cast<uint8>(Ref->Mode) > static_cast<uint8>(ECLHeightReferenceMode::IntervalDelta) ||
                (bHeight && (!Ref->UpperGuide.IsNone() || Ref->Fraction != 0)) ||
                (!bHeight && (Ref->Guide.IsNone() || Ref->UpperGuide.IsNone() || Ref->Fraction > 1 ||
                    Ref->Fraction < (Ref->Mode == ECLHeightReferenceMode::IntervalHeight ? 0 : -1))))
            { Error = TEXT("Invalid height reference in layer: ") + Layer.Name.ToString(); return false; }
        }
        if (!FMath::IsFinite(Layer.Persistence) || Layer.Persistence < 0 || Layer.Persistence > 1 || Layer.Octaves < 1 || Layer.Octaves > 8 ||
            static_cast<uint8>(Layer.Shape) > static_cast<uint8>(ECLNoiseShape::Ridged) || static_cast<uint8>(Layer.Blend) > static_cast<uint8>(ECLNoiseBlend::Min) ||
            static_cast<uint8>(Layer.Scale) > static_cast<uint8>(ECLNoiseScale::PlanetRadius) ||
            (Layer.Scale == ECLNoiseScale::Meters && (!FMath::IsFinite(Layer.WavelengthMeters) || Layer.WavelengthMeters < .1)) ||
            (Layer.Scale == ECLNoiseScale::PlanetRadius && (!FMath::IsFinite(Layer.WavelengthRadiusFraction) || Layer.WavelengthRadiusFraction <= 0)))
        { Error = TEXT("Invalid noise settings: ") + Layer.Name.ToString(); return false; }
    }
    return true;
}

TSharedPtr<const FCLCompiledPlanetRecipe> FCLPlanetCompiler::Compile(const FCLPlanetRecipe& Recipe, FString& Error)
{
    Error.Reset();
    auto Fail = [&Error](const FString& Message) -> TSharedPtr<const FCLCompiledPlanetRecipe> { Error = Message; return nullptr; };
    if ((Recipe.SchemaVersion != 2 && Recipe.SchemaVersion != 3) || Recipe.AlgorithmVersion != 1) return Fail(TEXT("Unsupported recipe schema or algorithm version"));
    if (!FMath::IsFinite(Recipe.RadiusMeters) || Recipe.RadiusMeters < 10 || Recipe.RadiusMeters > 1.e7)
        return Fail(TEXT("RadiusMeters must be finite and between 10 and 10000000"));
    if (Recipe.Strata.Stratum.Num() > 128 || Recipe.NoiseLayers.Num() > 32) return Fail(TEXT("Recipe exceeds 128 guides or 32 noise layers"));
    if (!ValidateNoiseLayers(Recipe.NoiseLayers, Error)) return nullptr;
    auto Plan = MakeShared<FCLCompiledPlanetRecipe>();
    Plan->RadiusMeters = Recipe.RadiusMeters;
    if (!FMath::IsFinite(Recipe.BaseSurfaceNormalized) || Recipe.BaseSurfaceNormalized <= 0 || Recipe.BaseSurfaceNormalized > 1)
        return Fail(TEXT("BaseSurfaceNormalized must be in (0, 1]"));
    Plan->BaseHeightMeters = (Recipe.BaseSurfaceNormalized - 1) * Recipe.RadiusMeters;
    Plan->MinimumHeightMeters = Plan->MaximumHeightMeters = Plan->BaseHeightMeters;
    TMap<FName, double> Guides;
    if (!Recipe.Strata.Validate(Error)) return nullptr;
    for (const FCLStratum& Stratum : Recipe.Strata.Stratum)
    {
        FCLHeightGuide Guide; Guide.Name = Stratum.Name; Guide.Color = Stratum.Color;
        Guide.HeightMeters = (Stratum.NormalizedHeight - 1) * Recipe.RadiusMeters;
        if (Guide.Name.IsNone() || Guides.Contains(Guide.Name)) return Fail(TEXT("Height Guide names must be nonempty and unique"));
        if (!FMath::IsFinite(Guide.HeightMeters) || FMath::Abs(Guide.HeightMeters) > 1.e7 || Recipe.RadiusMeters + Guide.HeightMeters < 0 ||
            !FMath::IsFinite(Guide.Color.R) || !FMath::IsFinite(Guide.Color.G) ||
            !FMath::IsFinite(Guide.Color.B) || !FMath::IsFinite(Guide.Color.A)) return Fail(FString::Printf(TEXT("Invalid Height Guide: %s"), *Guide.Name.ToString()));
        Guides.Add(Guide.Name, Guide.HeightMeters);
        Plan->Guides.Add(Guide);
    }
    auto Resolve = [&](const FCLHeightReference& Ref, double& Value) -> bool
    {
        Value = Ref.OffsetMeters;
        const double* Lower = Ref.Guide.IsNone() ? nullptr : Guides.Find(Ref.Guide);
        if (!Ref.Guide.IsNone() && !Lower) { Error = TEXT("Missing required Height Guide: ") + Ref.Guide.ToString(); return false; }
        if (Ref.Mode == ECLHeightReferenceMode::Height) { if (Lower) Value += *Lower; }
        else
        {
            if (Recipe.SchemaVersion < 3) { Error = TEXT("Guide intervals require recipe schema 3"); return false; }
            const double* Upper = Guides.Find(Ref.UpperGuide);
            if (!Upper) { Error = TEXT("Missing required Height Guide: ") + Ref.UpperGuide.ToString(); return false; }
            if (*Lower > *Upper) { Error = TEXT("Guide interval is reversed: ") + Ref.Guide.ToString() + TEXT(" / ") + Ref.UpperGuide.ToString(); return false; }
            Value += Ref.Mode == ECLHeightReferenceMode::IntervalHeight ? FMath::Lerp(*Lower, *Upper, Ref.Fraction) : (*Upper - *Lower) * Ref.Fraction;
        }
        if (!FMath::IsFinite(Value)) { Error = TEXT("Height reference overflow"); return false; }
        return true;
    };
    for (const FCLNoiseLayer& Layer : Recipe.NoiseLayers)
    {
        if (!Layer.bEnabled) continue;
        FCLCompiledPlanetRecipe::FNoise Noise;
        if (!Resolve(Layer.Minimum, Noise.Minimum) || !Resolve(Layer.Maximum, Noise.Maximum)) return nullptr;
        if (Noise.Minimum > Noise.Maximum) return Fail(TEXT("Noise minimum must not exceed maximum: ") + Layer.Name.ToString());
        if (Layer.Scale == ECLNoiseScale::PlanetRadius && Recipe.SchemaVersion < 3)
            return Fail(TEXT("Planet-relative wavelengths require recipe schema 3"));
        const double Wavelength = Layer.Scale == ECLNoiseScale::PlanetRadius ? Layer.WavelengthRadiusFraction * Recipe.RadiusMeters : Layer.WavelengthMeters;
        if (!FMath::IsFinite(Wavelength) || Wavelength < .1 || Recipe.RadiusMeters / Wavelength * (1 << (Layer.Octaves - 1)) > 1.e6)
            return Fail(TEXT("Noise wavelength/octaves exceed supported precision or minimum wavelength"));
        Noise.Wavelength = Wavelength; Noise.Persistence = Layer.Persistence;
        Noise.Octaves = Layer.Octaves; Noise.Shape = Layer.Shape; Noise.Blend = Layer.Blend;
        uint32 Seed = Mix(uint32(Recipe.Seed) ^ Mix(uint32(Layer.Seed)));
        // Explicit seed, independent of array order or process-local FName indices.
        for (TCHAR C : Layer.Name.ToString().ToLower()) Seed = Mix(Seed ^ uint32(C));
        Noise.Offset = FVector(Mix(Seed) % 8192 + .37, Mix(Seed + 1) % 8192 + .59, Mix(Seed + 2) % 8192 + .83);
        double& Low = Plan->MinimumHeightMeters; double& High = Plan->MaximumHeightMeters;
        switch (Noise.Blend)
        {
        case ECLNoiseBlend::Override: Low = Noise.Minimum; High = Noise.Maximum; break;
        case ECLNoiseBlend::Add: Low += Noise.Minimum; High += Noise.Maximum; break;
        case ECLNoiseBlend::Max: Low = FMath::Max(Low, Noise.Minimum); High = FMath::Max(High, Noise.Maximum); break;
        case ECLNoiseBlend::Min: Low = FMath::Min(Low, Noise.Minimum); High = FMath::Min(High, Noise.Maximum); break;
        }
        if (!FMath::IsFinite(Low) || !FMath::IsFinite(High) || Low + Plan->RadiusMeters <= .01 || FMath::Max(FMath::Abs(Low), FMath::Abs(High)) > 1.e7)
            return Fail(TEXT("Noise stack exceeds supported radial bounds"));
        Plan->Layers.Add(Noise);
    }
    Plan->MinimumHeightMeters = FMath::Min(Plan->MinimumHeightMeters, 0.0);
    Plan->MaximumHeightMeters = FMath::Min(Plan->MaximumHeightMeters, 0.0);
    return Plan;
}

FCLPlanetRecipe FCLPlanetCompiler::RollingPlains()
{
    FCLPlanetRecipe Recipe;
    Recipe.Strata = FCLStrataPresets::Defaults()[0];
    FCLNoiseLayer Broad; Broad.Name = TEXT("Broad undulations"); Broad.Minimum.Guide = TEXT("SurfaceMin");
    Broad.Maximum.Guide = TEXT("SurfaceMax"); Broad.WavelengthMeters = 60; Recipe.NoiseLayers.Add(Broad);
    FCLNoiseLayer Detail; Detail.Name = TEXT("Ground detail"); Detail.Seed = 12; Detail.Blend = ECLNoiseBlend::Add;
    Detail.Minimum.OffsetMeters = -.2; Detail.Maximum.OffsetMeters = .2; Detail.WavelengthMeters = 8; Detail.Octaves = 2;
    Recipe.NoiseLayers.Add(Detail);
    return Recipe;
}

void FCLPlanetRecipe::MigrateLegacyGuides()
{
    if (HeightGuides_DEPRECATED.IsEmpty() && SchemaVersion != 1) return;
    const double OldRadius = ReferenceRadiusMeters_DEPRECATED;
    TMap<FName, double> Old; double Top = 0;
    for (const auto& G : HeightGuides_DEPRECATED) { Old.Add(G.Name, G.HeightMeters); Top = FMath::Max(Top, G.HeightMeters); }
    auto Resolve = [&](const FCLHeightReference& Ref) { return Ref.OffsetMeters + Old.FindRef(Ref.Guide); };
    double High = 0;
    for (const FCLNoiseLayer& L : NoiseLayers) if (L.bEnabled)
    {
        const double V = Resolve(L.Maximum);
        switch (L.Blend)
        {
        case ECLNoiseBlend::Override: High = V; break;
        case ECLNoiseBlend::Add: High += V; break;
        case ECLNoiseBlend::Min: High = FMath::Min(High, V); break;
        case ECLNoiseBlend::Max: High = FMath::Max(High, V); break;
        }
    }
    // Retain the old physical terrain while moving its radius to a sky boundary.
    RadiusMeters = OldRadius + FMath::Max(Top, High) + OldRadius * .05;
    const double Shift = RadiusMeters - OldRadius;
    BaseSurfaceNormalized = OldRadius / RadiusMeters;
    Strata.Name = TEXT("Lab plains"); Strata.Id = FGuid(0x434C0001, 0, 0, 2); Strata.Stratum.Reset();
    for (const FCLHeightGuide& G : HeightGuides_DEPRECATED)
    { FCLStratum S; S.Name = G.Name; S.NormalizedHeight = (OldRadius + G.HeightMeters) / RadiusMeters;
      S.Color = G.Color; Strata.Stratum.Add(S); }
    for (FCLNoiseLayer& L : NoiseLayers)
    {
        L.WavelengthMeters *= RadiusMeters / OldRadius;
        for (FCLHeightReference* Ref : {&L.Minimum, &L.Maximum})
        {
            if (L.Blend == ECLNoiseBlend::Add && !Ref->Guide.IsNone()) Ref->OffsetMeters += Shift;
            if (L.Blend != ECLNoiseBlend::Add && Ref->Guide.IsNone()) Ref->OffsetMeters -= Shift;
        }
    }
    HeightGuides_DEPRECATED.Reset(); SchemaVersion = 2;
}
void UCLPlanetRecipeAsset::Serialize(FArchive& Ar)
{
    if (Ar.IsLoading() && Ar.IsPersistent() && !Ar.IsTransacting() && Ar.CustomVer(RecipeAssetVersion) < 1 && !HasAnyFlags(RF_ClassDefaultObject))
    {
        // UE omits fields equal to native defaults. Restore the old defaults
        // before tagged properties load, so missing tags do not adopt new ones.
        Recipe = FCLPlanetCompiler::RollingPlains(); Recipe.SchemaVersion = 1;
        Recipe.Strata = FCLStrata(); Recipe.ReferenceRadiusMeters_DEPRECATED = 100;
        Recipe.HeightGuides_DEPRECATED.Reset();
        const FName Names[] = {TEXT("OceanHeight"), TEXT("SurfaceMin"), TEXT("SurfaceMax"), TEXT("AtmosphereMax")};
        const double Heights[] = {0, 1, 7, 20};
        const FLinearColor Colors[] = {FLinearColor(.1, .55, 1), FLinearColor(.8, .65, .3), FLinearColor(.4, .9, .45), FLinearColor(.5, .7, 1)};
        for (int32 I = 0; I < 4; ++I)
        { FCLHeightGuide G; G.Name = Names[I]; G.HeightMeters = Heights[I]; G.Color = Colors[I]; Recipe.HeightGuides_DEPRECATED.Add(G); }
    }
    Ar.UsingCustomVersion(RecipeAssetVersion);
    Super::Serialize(Ar);
}
void UCLPlanetRecipeAsset::PostLoad() { Super::PostLoad(); Recipe.MigrateLegacyGuides(); if (Recipe.SchemaVersion == 2) Recipe.SchemaVersion = 3; Version.SchemaVersion = Recipe.SchemaVersion; }
UCLPlanetRecipeAsset::UCLPlanetRecipeAsset() { Recipe = FCLPlanetCompiler::RollingPlains(); }
FString UCLPlanetRecipeAsset::GetSummaryText() const
{ return FString::Printf(TEXT("%s: %d guides, %d noise layers"), *Recipe.Name, Recipe.Strata.Stratum.Num(), Recipe.NoiseLayers.Num()); }
bool UCLPlanetRecipeAsset::ValidatePayload(FString& Error) const { return FCLPlanetCompiler::Compile(Recipe, Error).IsValid(); }
void UCLPlanetRecipeAsset::CollectValidationMessages(TArray<FCFValidationMessage>& Messages) const
{
    Super::CollectValidationMessages(Messages);
    FString Error;
    if (!ValidatePayload(Error)) Messages.Add(FCFValidationMessage::Make(ECFValidationSeverity::Error, TEXT("CL.Recipe.Invalid"),
        FText::FromString(Error), FText(), TEXT("/Recipe")));
}
bool UCLPlanetRecipeAsset::ImportRecipeJson(const FString& Json, FString& Error)
{
    Error.Reset();
    if (Json.Len() > 256 * 1024) { Error = TEXT("Recipe JSON exceeds 256 KiB"); return false; }
    TSharedPtr<FJsonObject> Object;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object)
    { Error = TEXT("Invalid recipe JSON object"); return false; }
    double Schema = 0, Algorithm = 0;
    if (!Object->TryGetNumberField(TEXT("schemaVersion"), Schema) || !Object->TryGetNumberField(TEXT("algorithmVersion"), Algorithm) || (Schema != 1 && Schema != 2 && Schema != 3) || Algorithm != 1)
    { Error = TEXT("Recipe requires schemaVersion 1, 2 or 3 and algorithmVersion 1"); return false; }
    if (Schema == 1)
    {
        // Explicit, lossless conversion of the old metre-based document.
        const TArray<TSharedPtr<FJsonValue>>* Old = nullptr;
        FCLPlanetRecipe Legacy; Legacy.SchemaVersion = 1; Legacy.Strata.Stratum.Reset();
        if (Object->TryGetArrayField(TEXT("heightGuides"), Old))
            for (const auto& V : *Old)
            {
                FCLHeightGuide G;
                if (V->Type != EJson::Object || !CLModelJson::CheckFields(*V->AsObject(), *FCLHeightGuide::StaticStruct(), Error, TEXT("heightGuides")) ||
                    !FJsonObjectConverter::JsonObjectToUStruct(V->AsObject().ToSharedRef(), &G)) return false;
                Legacy.HeightGuides_DEPRECATED.Add(G);
            }
        Object->TryGetNumberField(TEXT("referenceRadiusMeters"), Legacy.ReferenceRadiusMeters_DEPRECATED);
        // Convert the entire v1 payload so base, noise scales and absolute references migrate together.
        TSharedPtr<FJsonObject> OldObject = MakeShared<FJsonObject>(*Object);
        OldObject->RemoveField(TEXT("heightGuides")); OldObject->RemoveField(TEXT("referenceRadiusMeters"));
        if (!CLModelJson::CheckFields(*OldObject, *FCLPlanetRecipe::StaticStruct(), Error, TEXT("Recipe"))) return false;
        const auto SavedGuides = Legacy.HeightGuides_DEPRECATED;
        if (!FJsonObjectConverter::JsonObjectToUStruct(OldObject.ToSharedRef(), &Legacy)) return false;
        Legacy.HeightGuides_DEPRECATED = SavedGuides;
        Legacy.MigrateLegacyGuides();
        Object = FJsonObjectConverter::UStructToJsonObject(Legacy);
    }
    if (!CLModelJson::CheckFields(*Object, *FCLPlanetRecipe::StaticStruct(), Error, TEXT("Recipe"))) return false;
    // Fresh candidate prevents omitted fields inheriting previous document values.
    FCLPlanetRecipe Candidate;
    FText Reason;
    if (!FJsonObjectConverter::JsonObjectToUStruct(Object.ToSharedRef(), &Candidate, 0, 0, false, &Reason))
    { Error = Reason.ToString(); return false; }
    if (!FCLPlanetCompiler::Compile(Candidate, Error)) return false;
    if (Candidate.SchemaVersion == 2) Candidate.SchemaVersion = 3;
    Modify(); Recipe = MoveTemp(Candidate);
#if WITH_EDITOR
    PostEditChange(); MarkPackageDirty();
#endif
    return true;
}
#if WITH_EDITOR
void UCLPlanetRecipeAsset::PostEditChangeProperty(FPropertyChangedEvent& Event)
{ Super::PostEditChangeProperty(Event); OnRecipeChanged.Broadcast(); }
void UCLPlanetRecipeAsset::PostEditUndo()
{ Super::PostEditUndo(); OnRecipeChanged.Broadcast(); }
#endif

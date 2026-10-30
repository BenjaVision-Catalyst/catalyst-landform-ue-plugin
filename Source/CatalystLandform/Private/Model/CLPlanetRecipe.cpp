#include "Model/CLPlanetRecipe.h"
#include "JsonObjectConverter.h"
#include "Validation/CFValidationTypes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

namespace
{
uint32 Mix(uint32 V)
{
    V ^= V >> 16; V *= 0x7feb352du; V ^= V >> 15; V *= 0x846ca68bu; return V ^ (V >> 16);
}
bool CheckFields(const FJsonObject& Object, const UStruct& Struct, FString& Error, const FString& Path)
{
    for (const auto& Pair : Object.Values)
    {
        const FProperty* Found = nullptr;
        for (TFieldIterator<FProperty> It(&Struct); It; ++It)
            if (It->GetName().Equals(Pair.Key, ESearchCase::IgnoreCase)) { Found = *It; break; }
        const FString Location = Path + TEXT("/") + Pair.Key;
        if (!Found) { Error = TEXT("Unknown recipe field: ") + Location; return false; }
        auto CheckStruct = [&](const FProperty* Property, const TSharedPtr<FJsonValue>& Value) -> bool
        {
            const auto* S = CastField<FStructProperty>(Property);
            return !S || Value->Type != EJson::Object || CheckFields(*Value->AsObject(), *S->Struct, Error, Location);
        };
        if (const auto* A = CastField<FArrayProperty>(Found); A && Pair.Value->Type == EJson::Array)
        {
            for (const auto& Value : Pair.Value->AsArray()) if (!CheckStruct(A->Inner, Value)) return false;
        }
        else if (!CheckStruct(Found, Pair.Value)) return false;
    }
    return true;
}
}

double FCLCompiledPlanetRecipe::SampleHeightMeters(const FVector& Direction) const
{
    if (Direction.ContainsNaN() || Direction.IsNearlyZero()) return 0;
    const FVector Position = Direction.GetSafeNormal() * RadiusMeters;
    double Height = 0;
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
    return Height;
}

TSharedPtr<const FCLCompiledPlanetRecipe> FCLPlanetCompiler::Compile(const FCLPlanetRecipe& Recipe, FString& Error)
{
    Error.Reset();
    auto Fail = [&Error](const FString& Message) -> TSharedPtr<const FCLCompiledPlanetRecipe> { Error = Message; return nullptr; };
    if (Recipe.SchemaVersion != 1 || Recipe.AlgorithmVersion != 1) return Fail(TEXT("Unsupported recipe schema or algorithm version"));
    if (!FMath::IsFinite(Recipe.ReferenceRadiusMeters) || Recipe.ReferenceRadiusMeters < 10 || Recipe.ReferenceRadiusMeters > 1.e7)
        return Fail(TEXT("ReferenceRadiusMeters must be finite and between 10 and 10000000"));
    if (Recipe.HeightGuides.Num() > 128 || Recipe.NoiseLayers.Num() > 32) return Fail(TEXT("Recipe exceeds 128 guides or 32 noise layers"));
    auto Plan = MakeShared<FCLCompiledPlanetRecipe>();
    Plan->RadiusMeters = Recipe.ReferenceRadiusMeters;
    TMap<FName, double> Guides;
    for (const FCLHeightGuide& Guide : Recipe.HeightGuides)
    {
        if (Guide.Name.IsNone() || Guides.Contains(Guide.Name)) return Fail(TEXT("Height Guide names must be nonempty and unique"));
        if (!FMath::IsFinite(Guide.HeightMeters) || FMath::Abs(Guide.HeightMeters) > 1.e7 || Recipe.ReferenceRadiusMeters + Guide.HeightMeters <= 0 ||
            !FMath::IsFinite(Guide.Color.R) || !FMath::IsFinite(Guide.Color.G) ||
            !FMath::IsFinite(Guide.Color.B) || !FMath::IsFinite(Guide.Color.A)) return Fail(FString::Printf(TEXT("Invalid Height Guide: %s"), *Guide.Name.ToString()));
        Guides.Add(Guide.Name, Guide.HeightMeters);
        Plan->Guides.Add(Guide);
    }
    auto Resolve = [&](const FCLHeightReference& Ref, double& Value) -> bool
    {
        if (!FMath::IsFinite(Ref.OffsetMeters)) { Error = TEXT("Height reference offset must be finite"); return false; }
        Value = Ref.OffsetMeters;
        if (!Ref.Guide.IsNone())
        {
            const double* Guide = Guides.Find(Ref.Guide);
            if (!Guide) { Error = TEXT("Missing required Height Guide: ") + Ref.Guide.ToString(); return false; }
            Value += *Guide;
        }
        if (!FMath::IsFinite(Value)) { Error = TEXT("Height reference overflow"); return false; }
        return true;
    };
    TSet<FName> Names;
    for (const FCLNoiseLayer& Layer : Recipe.NoiseLayers)
    {
        if (Layer.Name.IsNone() || Names.Contains(Layer.Name)) return Fail(TEXT("Noise layer names must be nonempty and unique"));
        Names.Add(Layer.Name);
        if (!Layer.bEnabled) continue;
        FCLCompiledPlanetRecipe::FNoise Noise;
        if (!Resolve(Layer.Minimum, Noise.Minimum) || !Resolve(Layer.Maximum, Noise.Maximum)) return nullptr;
        if (Noise.Minimum > Noise.Maximum) return Fail(TEXT("Noise minimum must not exceed maximum: ") + Layer.Name.ToString());
        if (!FMath::IsFinite(Layer.WavelengthMeters) || Layer.WavelengthMeters < .1 ||
            Layer.Octaves < 1 || Layer.Octaves > 8 || !FMath::IsFinite(Layer.Persistence) || Layer.Persistence < 0 || Layer.Persistence > 1 ||
            static_cast<uint8>(Layer.Shape) > static_cast<uint8>(ECLNoiseShape::Ridged) ||
            static_cast<uint8>(Layer.Blend) > static_cast<uint8>(ECLNoiseBlend::Min)) return Fail(TEXT("Invalid noise settings: ") + Layer.Name.ToString());
        // Keep float noise coordinates below 2^20, including eight octaves.
        if (Recipe.ReferenceRadiusMeters / Layer.WavelengthMeters * (1 << (Layer.Octaves - 1)) > 1.e6)
            return Fail(TEXT("Noise wavelength/octaves exceed the supported coordinate precision"));
        Noise.Wavelength = Layer.WavelengthMeters; Noise.Persistence = Layer.Persistence;
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
    return Plan;
}

FCLPlanetRecipe FCLPlanetCompiler::RollingPlains()
{
    FCLPlanetRecipe Recipe;
    auto Guide = [&](FName Name, double Height, FLinearColor Color)
    { FCLHeightGuide G; G.Name = Name; G.HeightMeters = Height; G.Color = Color; Recipe.HeightGuides.Add(G); };
    Guide(TEXT("OceanHeight"), 0, FLinearColor(.1, .55, 1));
    Guide(TEXT("SurfaceMin"), 1, FLinearColor(.8, .65, .3));
    Guide(TEXT("SurfaceMax"), 7, FLinearColor(.4, .9, .45));
    Guide(TEXT("AtmosphereMax"), 20, FLinearColor(.5, .7, 1));
    FCLNoiseLayer Broad; Broad.Name = TEXT("Broad undulations"); Broad.Minimum.Guide = TEXT("SurfaceMin");
    Broad.Maximum.Guide = TEXT("SurfaceMax"); Broad.WavelengthMeters = 60; Recipe.NoiseLayers.Add(Broad);
    FCLNoiseLayer Detail; Detail.Name = TEXT("Ground detail"); Detail.Seed = 12; Detail.Blend = ECLNoiseBlend::Add;
    Detail.Minimum.OffsetMeters = -.2; Detail.Maximum.OffsetMeters = .2; Detail.WavelengthMeters = 8; Detail.Octaves = 2;
    Recipe.NoiseLayers.Add(Detail);
    return Recipe;
}

UCLPlanetRecipeAsset::UCLPlanetRecipeAsset() { Recipe = FCLPlanetCompiler::RollingPlains(); }
FString UCLPlanetRecipeAsset::GetSummaryText() const
{ return FString::Printf(TEXT("%s: %d guides, %d noise layers"), *Recipe.Name, Recipe.HeightGuides.Num(), Recipe.NoiseLayers.Num()); }
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
    if (!CheckFields(*Object, *FCLPlanetRecipe::StaticStruct(), Error, TEXT("Recipe"))) return false;
    double Schema = 0, Algorithm = 0;
    if (!Object->TryGetNumberField(TEXT("schemaVersion"), Schema) || !Object->TryGetNumberField(TEXT("algorithmVersion"), Algorithm) || Schema != 1 || Algorithm != 1)
    { Error = TEXT("Recipe requires supported schemaVersion and algorithmVersion (1)"); return false; }
    // Fresh candidate prevents omitted fields inheriting previous document values.
    FCLPlanetRecipe Candidate;
    FText Reason;
    if (!FJsonObjectConverter::JsonObjectToUStruct(Object.ToSharedRef(), &Candidate, 0, 0, false, &Reason))
    { Error = Reason.ToString(); return false; }
    if (!FCLPlanetCompiler::Compile(Candidate, Error)) return false;
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

#include "CLTerrainEditing.h"
#include "ScopedTransaction.h"
#include "Stamps/CLStampLibrary.h"

void FCLTerrainEditing::Open(UCLTerrainTypeAsset* InAsset, const FCLPlanetRecipe* InContext)
{
    Asset = InAsset; Asset->SetFlags(RF_Transactional);
    Context = InContext ? *InContext : FCLPlanetCompiler::RollingPlains();
    if (!InContext) { Context.RadiusMeters = 1000; Context.Strata = FCLStrataPresets::Defaults()[2]; }
    Selected = Asset->TerrainType.NoiseLayers.IsEmpty() ? INDEX_NONE : 0; Solo = INDEX_NONE; SoloName = NAME_None;
}
void FCLTerrainEditing::Reconcile()
{
    const int32 Count = Asset->TerrainType.NoiseLayers.Num();
    Selected = Count ? FMath::Clamp(Selected, 0, Count - 1) : INDEX_NONE;
    const int32 Index = SoloName.IsNone() ? INDEX_NONE : Asset->TerrainType.NoiseLayers.IndexOfByPredicate([&](const auto& L) { return L.Name == SoloName; });
    SetSolo(Index);
}
void FCLTerrainEditing::SetSolo(int32 Index)
{
    Solo = Asset->TerrainType.NoiseLayers.IsValidIndex(Index) && Asset->TerrainType.NoiseLayers[Index].bEnabled ? Index : INDEX_NONE;
    SoloName = Solo == INDEX_NONE ? NAME_None : Asset->TerrainType.NoiseLayers[Solo].Name;
}
FName FCLTerrainEditing::UniqueName(const FString& Base) const
{
    FName Name(*Base); int32 N = 2;
    while (Asset->TerrainType.NoiseLayers.ContainsByPredicate([&](const auto& L) { return L.Name == Name; }))
        Name = FName(*FString::Printf(TEXT("%s %d"), *Base, N++));
    return Name;
}
bool FCLTerrainEditing::Commit(TArray<FCLNoiseLayer> Layers, int32 NewSelection, int32 NewSolo, const FText& Description)
{
    if (!FCLPlanetCompiler::ValidateNoiseLayers(Layers, Error)) return false;
    const FScopedTransaction Tx(Description);
    Asset->Modify(); Asset->TerrainType.NoiseLayers = MoveTemp(Layers);
    Selected = NewSelection; SetSolo(NewSolo); Error.Reset();
    Asset->MarkPackageDirty(); Asset->PostEditChange(); return true;
}
bool FCLTerrainEditing::SetLayer(const FCLNoiseLayer& Layer)
{
    if (!Asset->TerrainType.NoiseLayers.IsValidIndex(Selected)) return false;
    if (FCLNoiseLayer::StaticStruct()->CompareScriptStruct(&Layer, &Asset->TerrainType.NoiseLayers[Selected], 0)) { Error.Reset(); return true; }
    if (Layer.bEnabled && Layer.Operation == ECLTerrainOperation::Noise)
    {
        // Resolve this layer's bounds in the current guides without compiling
        // unrelated stamps. Reversed intervals must not enter the saved source.
        auto Probe = Context; Probe.SchemaVersion = 6; Probe.Coordinates = Asset->TerrainType.Coordinates; Probe.AlgorithmVersion = Asset->TerrainType.AlgorithmVersion;
        Probe.NoiseLayers = {Layer}; Probe.NoiseLayers[0].Blend = ECLNoiseBlend::Override;
        if (!FCLPlanetCompiler::Compile(Probe, Error)) return false;
    }
    auto Layers = Asset->TerrainType.NoiseLayers; Layers[Selected] = Layer;
    return Commit(MoveTemp(Layers), Selected, Solo, NSLOCTEXT("CL", "EditNoise", "Edit terrain layer"));
}
bool FCLTerrainEditing::Add(ECLTerrainOperation Operation)
{
    if (Asset->TerrainType.NoiseLayers.Num() >= 32) return false;
    auto Layers = Asset->TerrainType.NoiseLayers;
    FCLNoiseLayer Layer; Layer.Operation = Operation;
    if (Operation == ECLTerrainOperation::StampDistribution)
        if (const auto* Library = UCLStampLibraryAsset::LoadConfigured())
            for (const auto& Collection : Library->Library.Collections)
                if (Collection.Id == Layer.Distribution.CollectionId && Collection.Revision > Layer.Distribution.CollectionRevision &&
                    Collection.BakedRevision == Collection.Revision && Collection.DefinitionDigest == UCLStampLibraryAsset::CollectionDigest(Collection))
                    Layer.Distribution.CollectionRevision = Collection.Revision;
    Layer.Name = UniqueName(Operation == ECLTerrainOperation::Noise ? TEXT("Noise layer") : Operation == ECLTerrainOperation::Terrace ? TEXT("Terracing") : TEXT("Stamp distribution"));
    Layer.Blend = Layers.IsEmpty() ? ECLNoiseBlend::Override : ECLNoiseBlend::Add;
    Layer.Scale = Asset->TerrainType.Coordinates==ECLTerrainCoordinates::LocalPatch ? ECLNoiseScale::Meters : ECLNoiseScale::PlanetRadius;
    if(Asset->TerrainType.Coordinates==ECLTerrainCoordinates::LocalPatch)
    {
        Layer.Distribution.SizeMode=ECLStampSizeMode::Absolute;
        Layer.Distribution.ReliefMode=ECLStampReliefMode::GuideInterval;
        Layer.Distribution.BlendSmoothnessRadiusFraction=0;
    } Layer.WavelengthRadiusFraction = .1;
    Layer.Minimum.Mode = Layer.Maximum.Mode = Layers.IsEmpty() ? ECLHeightReferenceMode::IntervalHeight : ECLHeightReferenceMode::IntervalDelta;
    Layer.Minimum.Guide = Layer.Maximum.Guide = TEXT("SurfaceMin");
    Layer.Minimum.UpperGuide = Layer.Maximum.UpperGuide = TEXT("SurfaceMax");
    Layer.Minimum.Fraction = Layers.IsEmpty() ? .25 : -.02; Layer.Maximum.Fraction = Layers.IsEmpty() ? .75 : .02;
    const int32 Index = Layers.Add(Layer);
    return Commit(MoveTemp(Layers), Index, Solo, NSLOCTEXT("CL", "AddNoise", "Add terrain layer"));
}
bool FCLTerrainEditing::Duplicate()
{
    if (!Asset->TerrainType.NoiseLayers.IsValidIndex(Selected) || Asset->TerrainType.NoiseLayers.Num() >= 32) return false;
    auto Layers = Asset->TerrainType.NoiseLayers; auto Copy = Layers[Selected];
    Copy.Name = UniqueName(Copy.Name.ToString() + TEXT(" copy"));
    const int32 Index = Selected + 1; Layers.Insert(Copy, Index);
    return Commit(MoveTemp(Layers), Index, Solo >= Index ? Solo + 1 : Solo, NSLOCTEXT("CL", "DuplicateNoise", "Duplicate noise layer"));
}
bool FCLTerrainEditing::Remove()
{
    if (!Asset->TerrainType.NoiseLayers.IsValidIndex(Selected)) return false;
    auto Layers = Asset->TerrainType.NoiseLayers; Layers.RemoveAt(Selected);
    const int32 NewSolo = Solo == Selected ? INDEX_NONE : Solo > Selected ? Solo - 1 : Solo;
    const int32 Index = Layers.IsEmpty() ? INDEX_NONE : FMath::Min(Selected, Layers.Num() - 1);
    return Commit(MoveTemp(Layers), Index, NewSolo, NSLOCTEXT("CL", "RemoveNoise", "Remove noise layer"));
}
bool FCLTerrainEditing::Move(int32 Delta)
{
    const int32 To = Selected + Delta;
    if (FMath::Abs(Delta) != 1 || !Asset->TerrainType.NoiseLayers.IsValidIndex(Selected) || !Asset->TerrainType.NoiseLayers.IsValidIndex(To)) return false;
    auto Layers = Asset->TerrainType.NoiseLayers; Layers.Swap(Selected, To);
    const int32 NewSolo = Solo == Selected ? To : Solo == To ? Selected : Solo;
    return Commit(MoveTemp(Layers), To, NewSolo, NSLOCTEXT("CL", "MoveNoise", "Reorder noise layer"));
}
bool FCLTerrainEditing::Enable(int32 Index, bool bEnabled)
{
    if (!Asset->TerrainType.NoiseLayers.IsValidIndex(Index)) return false;
    auto Layers = Asset->TerrainType.NoiseLayers;
    if (Layers[Index].bEnabled == bEnabled) return true;
    Layers[Index].bEnabled = bEnabled;
    return Commit(MoveTemp(Layers), Selected, Solo, NSLOCTEXT("CL", "EnableNoise", "Enable noise layer"));
}
TSharedPtr<const FCLCompiledPlanetRecipe> FCLTerrainEditing::CompilePreview(FString& OutError, double WidthMeters) const
{
    FCLPlanetRecipe Recipe = Context;
    auto Visible = Asset->TerrainType;
    if (Visible.NoiseLayers.IsValidIndex(Solo) && Visible.NoiseLayers[Solo].bEnabled)
    {
        if (Visible.NoiseLayers[Solo].Operation == ECLTerrainOperation::Terrace)
        {
            // Shaping needs its enabled upstream input, never downstream layers.
            Visible.NoiseLayers.SetNum(Solo + 1);
        }
        else
        {
            // Isolate before any compilation: muted/hidden stamps must not consume
            // PCG budgets or block an unrelated noise preview. Use the same neutral
            // guide base for independent noise and stamp views.
            auto Layer = Visible.NoiseLayers[Solo];
            if (Layer.Operation == ECLTerrainOperation::Noise)
            {
                // Solo displays source values, not a Max/Min comparison against
                // an absent input. Delta ranges sit above/below the neutral base.
                const bool bDelta = Layer.Minimum.Mode == ECLHeightReferenceMode::IntervalDelta &&
                    Layer.Maximum.Mode == ECLHeightReferenceMode::IntervalDelta;
                Layer.Blend = Layer.Blend == ECLNoiseBlend::Add || bDelta ? ECLNoiseBlend::Add : ECLNoiseBlend::Override;
            }
            Visible.NoiseLayers = {Layer};
            Recipe.BaseSurfaceNormalized = 1;
        }
    }
    const double Half=FMath::Clamp(WidthMeters,100.,1000.)*.5;
    const FBox2D Region(FVector2D(-Half),FVector2D(Half));
    if (!Visible.ApplyTo(Recipe, OutError, &Region)) return nullptr;
    return FCLPlanetCompiler::Compile(Recipe, OutError, nullptr, &Region);
}

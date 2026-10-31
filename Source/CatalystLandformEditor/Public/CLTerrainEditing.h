#pragma once
#include "CoreMinimal.h"
#include "Model/CLTerrainType.h"

/** Terrain authoring owns layers; planet context and solo are preview-only values. */
class CATALYSTLANDFORMEDITOR_API FCLTerrainEditing
{
public:
    UCLTerrainTypeAsset* Asset = nullptr; // held by the asset editor
    FCLPlanetRecipe Context;
    int32 Selected = INDEX_NONE;
    int32 Solo = INDEX_NONE;
    FString Error;
    void Open(UCLTerrainTypeAsset* InAsset, const FCLPlanetRecipe* InContext = nullptr);
    bool SetLayer(const FCLNoiseLayer& Layer);
    bool Add();
    bool Duplicate();
    bool Remove();
    bool Move(int32 Delta);
    bool Enable(int32 Index, bool bEnabled);
    void Reconcile();
    void SetSolo(int32 Index);
    TSharedPtr<const FCLCompiledPlanetRecipe> CompilePreview(FString& OutError) const;
    bool ApplyToPlanet(UCLPlanetRecipeAsset* Planet);
private:
    FName SoloName;
    bool Commit(TArray<FCLNoiseLayer> Layers, int32 NewSelection, int32 NewSolo, const FText& Description);
    FName UniqueName(const FString& Base) const;
};

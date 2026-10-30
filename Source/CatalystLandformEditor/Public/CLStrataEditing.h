#pragma once
#include "CoreMinimal.h"
#include "Model/CLPlanetRecipe.h"

/** Shared authoring operations; names remain valid references after a rename. */
struct CATALYSTLANDFORMEDITOR_API FCLStrataEditing
{
    static bool CanRename(const FCLStrata& Strata, int32 Index, const FString& Name, FString& Error);
    static bool Rename(FCLPlanetRecipe& Recipe, int32 Index, const FString& Name, FString& Error);
    static int32 Add(FCLStrata& Strata, double NormalizedHeight);
    static bool Remove(FCLStrata& Strata, int32 Index);
};

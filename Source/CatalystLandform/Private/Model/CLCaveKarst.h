#pragma once
#include "Model/CLCaves.h"
struct FCLCompiledPlanetRecipe;
/** Geological construction only. Uses retained watershed receivers and rock bedding. */
bool CLBuildKarst(const FCLCaveSettings&,const FCLCompiledPlanetRecipe&,FVector,FCLCompiledCaves::FNetwork&,FRandomStream&,TArray<FCLCompiledCaves::FNode>&,TArray<FCLCavePassage>&,TArray<FCLCaveChamber>&);

#pragma once
#include "CoreMinimal.h"
#include "Toolkits/IToolkit.h"
class UCLTerrainTypeAsset;
class UCLPlanetRecipeAsset;
class IToolkitHost;
/** Open the shared Designer with an explicit, optional planet context snapshot. */
CATALYSTLANDFORMEDITOR_API void CLOpenTerrainDesigner(UCLTerrainTypeAsset* Terrain, UCLPlanetRecipeAsset* Context = nullptr,
    EToolkitMode::Type Mode = EToolkitMode::Standalone, TSharedPtr<IToolkitHost> Host = nullptr);

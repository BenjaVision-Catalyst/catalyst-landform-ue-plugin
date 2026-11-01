#pragma once
#include "CoreMinimal.h"
#include "Toolkits/IToolkit.h"
class UCLTerrainTypeAsset;
struct FCLStrata;
class IToolkitHost;
/** Open the terrain Designer with optional preview guides. No planet identity or settings are retained. */
CATALYSTLANDFORMEDITOR_API void CLOpenTerrainDesigner(UCLTerrainTypeAsset* Terrain, const FCLStrata* PreviewStrata = nullptr,
    EToolkitMode::Type Mode = EToolkitMode::Standalone, TSharedPtr<IToolkitHost> Host = nullptr);

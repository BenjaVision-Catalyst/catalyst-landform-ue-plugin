#pragma once
#include "CoreMinimal.h"
#include "Toolkits/IToolkit.h"
class UCLTerrainTypeAsset;
struct FCLRadialGuideSet;
class IToolkitHost;
/** Open the terrain Designer with optional preview guides. No planet identity or settings are retained. */
CATALYSTLANDFORMEDITOR_API void CLOpenTerrainDesigner(UCLTerrainTypeAsset* Terrain, const FCLRadialGuideSet* PreviewRadialGuides = nullptr,
    EToolkitMode::Type Mode = EToolkitMode::Standalone, TSharedPtr<IToolkitHost> Host = nullptr);

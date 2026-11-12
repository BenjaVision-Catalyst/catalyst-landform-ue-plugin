#pragma once
#include "CoreMinimal.h"
#include "Toolkits/IToolkit.h"
class UCLTerrainTypeAsset;
struct FCLRadialGuideSet;
class IToolkitHost;
/** Legacy API alias: focus the planet editor Landforms tab. Does not create a terrain editor. */
CATALYSTLANDFORMEDITOR_API void CLOpenTerrainDesigner(UCLTerrainTypeAsset* Terrain, const FCLRadialGuideSet* PreviewRadialGuides = nullptr,
    EToolkitMode::Type Mode = EToolkitMode::Standalone, TSharedPtr<IToolkitHost> Host = nullptr);

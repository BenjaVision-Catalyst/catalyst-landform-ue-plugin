#pragma once
#include "CoreMinimal.h"
#include "Toolkits/IToolkit.h"
class UCLTerrainTypeAsset;
class IToolkitHost;

/** Focus the source's row in the Planet editor's Landforms tab. */
CATALYSTLANDFORMEDITOR_API void CLOpenPlanetLandform(UCLTerrainTypeAsset* Landform,
    EToolkitMode::Type Mode=EToolkitMode::Standalone,TSharedPtr<IToolkitHost> Host=nullptr);

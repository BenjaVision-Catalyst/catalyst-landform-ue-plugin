#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Presentation/CLVPPMaterialSelection.h"
#include "PCG/CLPlanetSurfacePCG.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatalystLandform, Log, All);

class FCLLandformModule final : public IModuleInterface
{
public:
    void ShutdownModule() override { CLStopPlanetSurfaceRefresh(); CLStopVPPMaterialSelection(); }
    virtual void StartupModule() override
    {
        CLStartVPPMaterialSelection();
        CLStartPlanetSurfaceRefresh();
        UE_LOG(LogCatalystLandform, Display, TEXT("Catalyst Landform runtime loaded"));
    }
};

IMPLEMENT_MODULE(FCLLandformModule, CatalystLandform);

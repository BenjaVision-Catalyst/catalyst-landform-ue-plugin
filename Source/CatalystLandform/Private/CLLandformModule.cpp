#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatalystLandform, Log, All);

class FCLLandformModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        UE_LOG(LogCatalystLandform, Display, TEXT("Catalyst Landform runtime loaded"));
    }
};

IMPLEMENT_MODULE(FCLLandformModule, CatalystLandform);

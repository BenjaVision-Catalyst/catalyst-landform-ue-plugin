#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

class FCLLandformRenderModule final : public IModuleInterface
{
public:
    void StartupModule() override
    {
        const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"));
        check(Plugin);
        AddShaderSourceDirectoryMapping(TEXT("/Plugin/CatalystLandform"), Plugin->GetBaseDir() / TEXT("Shaders"));
    }
};
IMPLEMENT_MODULE(FCLLandformRenderModule, CatalystLandformRender);

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"
#include "CLNaniteProjection.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/ConfigCacheIni.h"
#include "Containers/Ticker.h"
#include "CoreGlobals.h"

static bool GCLNaniteProjectionOverlay = false;
bool CLNaniteProjectionShaderOverlayEnabled() { return GCLNaniteProjectionOverlay; }

class FCLLandformRenderModule final : public IModuleInterface
{
public:
    void ShutdownModule() override
    {
#if WITH_EDITOR && PLATFORM_MAC
        const auto Version=FEngineVersion::Current();
        if(GIsEditor && IsEngineExitRequested() && Version.GetMajor()==5 && Version.GetMinor()==8 && Version.GetPatch()==4)
        {
            // This PostConfigInit module retires after editor/Slate modules.
            // Their shutdown can queue notifications after the engine's first
            // ticker reset. Release captures while ICU is still initialized.
            FTSTicker::GetCoreTicker().Reset();
            FTSTicker::GetCoreTicker().Reset();
            UE_LOG(LogTemp,Display,TEXT("LANDFORM_LATE_SLATE_SHUTDOWN: released deferred text before ICU teardown"));
        }
#endif
    }
    void StartupModule() override
    {
        const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"));
        check(Plugin);
        AddShaderSourceDirectoryMapping(TEXT("/Plugin/CatalystLandform"), Plugin->GetBaseDir() / TEXT("Shaders"));
        GCLNaniteProjectionOverlay = FParse::Param(FCommandLine::Get(), TEXT("CLNaniteProjectionShaders"));
        bool ProjectDefault=true;GConfig->GetBool(TEXT("CatalystLandform.Presentation"),TEXT("bEnableProjectionShaders"),ProjectDefault,GGameIni);
        GCLNaniteProjectionOverlay|=ProjectDefault;
        if (GCLNaniteProjectionOverlay) UE_LOG(LogTemp, Display, TEXT("CL_NANITE_PROJECTION_OVERLAY: enabled; cooked=%d"), FPlatformProperties::RequiresCookedData());
        if (GCLNaniteProjectionOverlay && !FPlatformProperties::RequiresCookedData())
        {
            const auto Version = FEngineVersion::Current();
            checkf(Version.GetMajor()==5 && Version.GetMinor()==8 && Version.GetPatch()==4, TEXT("Nanite projection overlay requires qualified UE 5.8.4"));
            const FString Directory = FPaths::ProjectSavedDir() / TEXT("NaniteProjection/EngineShaders/Nanite");
            checkf(IFileManager::Get().FileExists(*(Directory / TEXT("CLProjectionOverlayMarker.ush"))), TEXT("Run stage-nanite-projection.py before enabling projection shaders"));
            AddShaderSourceDirectoryMapping(TEXT("/Engine/Private/Nanite"), Directory);
        }

    }
};
IMPLEMENT_MODULE(FCLLandformRenderModule, CatalystLandformRender);

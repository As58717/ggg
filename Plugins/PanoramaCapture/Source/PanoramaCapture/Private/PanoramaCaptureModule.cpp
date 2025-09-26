#include "PanoramaCaptureModule.h"
#include "PanoramaCaptureLog.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"

IMPLEMENT_MODULE(FPanoramaCaptureModule, PanoramaCapture)

DEFINE_LOG_CATEGORY(LogPanoramaCapture);

void FPanoramaCaptureModule::StartupModule()
{
    if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PanoramaCapture")))
    {
        FString ShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
        AddShaderSourceDirectoryMapping(TEXT("/PanoramaCapture"), ShaderDir);
    }
}

void FPanoramaCaptureModule::ShutdownModule()
{
    ResetAllShaderSourceDirectoryMappings();
}

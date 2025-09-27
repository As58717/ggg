#pragma once

#include "Modules/ModuleInterface.h"
#include "ToolMenus.h"

class SDockTab;
struct FSpawnTabArgs;

class FPanoramaCaptureEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TSharedRef<SDockTab> SpawnPanoramaTab(const FSpawnTabArgs& Args);
    void RegisterMenus();

    FDelegateHandle MenuExtenderHandle;
};

#pragma once

#include "Modules/ModuleInterface.h"

class FPanoramaCaptureModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};

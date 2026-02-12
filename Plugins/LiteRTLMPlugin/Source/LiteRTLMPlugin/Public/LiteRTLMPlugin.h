// Copyright 2026 Sanjyot Dahale (LifeIsARepo). All rights reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FLiteRTLMPluginModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

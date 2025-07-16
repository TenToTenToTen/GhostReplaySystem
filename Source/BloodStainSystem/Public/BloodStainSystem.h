// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Stats/Stats.h"

DECLARE_LOG_CATEGORY_EXTERN(LogBloodStain, Log, All);
DECLARE_STATS_GROUP(TEXT("BloodStain"), STATGROUP_BloodStain, STATCAT_Advanced);
class FBloodStainSystemModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

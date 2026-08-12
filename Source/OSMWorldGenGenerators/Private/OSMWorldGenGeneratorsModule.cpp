// Copyright InviMind. All Rights Reserved.

#include "OSMWorldGenGenerators.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogOSMWorldGenGenerators);

void FOSMWorldGenGeneratorsModule::StartupModule()
{
    // Phase 1: skeleton — generators will be added in Phases 2-4.
}

void FOSMWorldGenGeneratorsModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FOSMWorldGenGeneratorsModule, OSMWorldGenGenerators)

// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

OSMWORLDGENGENERATORS_API DECLARE_LOG_CATEGORY_EXTERN(LogOSMWorldGenGenerators, Log, All);

/**
 * OSMWorldGenGenerators module — editor-only module containing:
 *   - Terrain generator (Landscape API)
 *   - Road generator (splines + PCG)
 *   - Building generator (Geometry Scripting extrusion)
 *   - Water/landuse generator
 *   - Asset replacement engine (Interchange)
 */
class FOSMWorldGenGeneratorsModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};

// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogOSMWorldGen, Log, All);

/**
 * OSMWorldGenCore module — runtime module containing:
 *   - OSM parsing (libosmium wrapper + fallback XML parser)
 *   - Internal feature data model
 *   - CRS / geodesy math (ENU, UTM projections)
 *   - Tag classification engine
 *   - Generator base classes
 *   - Metadata component (UOSMMetadataComponent)
 */
class FOSMWorldGenCoreModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    /** Get the module instance */
    static FOSMWorldGenCoreModule& Get()
    {
        return FModuleManager::GetModuleChecked<FOSMWorldGenCoreModule>("OSMWorldGenCore");
    }

    /** Check if the module is loaded */
    static bool IsAvailable()
    {
        return FModuleManager::Get().IsModuleLoaded("OSMWorldGenCore");
    }
};

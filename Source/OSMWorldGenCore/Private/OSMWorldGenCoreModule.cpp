// Copyright InviMind. All Rights Reserved.

#include "OSMWorldGenCore.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogOSMWorldGen);

void FOSMWorldGenCoreModule::StartupModule()
{
    UE_LOG(LogOSMWorldGen, Log, TEXT("OSMWorldGenCore module starting up."));

#if OSM_WITH_LIBOSMIUM
    UE_LOG(LogOSMWorldGen, Log, TEXT("  libosmium integration: ENABLED (PBF + XML parsing)"));
#else
    UE_LOG(LogOSMWorldGen, Warning, TEXT("  libosmium integration: DISABLED (XML-only parsing, no PBF support)"));
    UE_LOG(LogOSMWorldGen, Warning, TEXT("  To enable PBF support, download libosmium + protozero into ThirdParty/"));
#endif

#if OSM_WITH_GDAL
    UE_LOG(LogOSMWorldGen, Log, TEXT("  GDAL integration: ENABLED (advanced DEM/CRS support)"));
#else
    UE_LOG(LogOSMWorldGen, Log, TEXT("  GDAL integration: DISABLED (using built-in GeoTIFF reader)"));
#endif
}

void FOSMWorldGenCoreModule::ShutdownModule()
{
    UE_LOG(LogOSMWorldGen, Log, TEXT("OSMWorldGenCore module shutting down."));
}

IMPLEMENT_MODULE(FOSMWorldGenCoreModule, OSMWorldGenCore)

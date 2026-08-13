// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UOSMWorldGenSettings.generated.h"

/**
 * Plugin-wide settings for the region auto-fetch subsystem (plan_v2_workflow.md Stage 2).
 *
 * Stored in EditorPerProjectUserSettings (per-user, not source-controlled) since the
 * OpenTopography API key is tied to an individual account, not the project.
 */
UCLASS(config = EditorPerProjectUserSettings, defaultconfig, meta = (DisplayName = "OSM World Generator"))
class OSMWORLDGENEDITOR_API UOSMWorldGenSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UOSMWorldGenSettings();

    // config=EditorPerProjectUserSettings (below) controls where the *data* is stored
    // (per-user ini, not source-controlled) — it does NOT control where the settings
    // *page* appears. UDeveloperSettings::GetContainerName() defaults to "Editor" for
    // that config category, which puts the page under Editor Preferences instead of
    // Project Settings. Force "Project" here so it shows up where GetCategoryName()
    // ("Plugins") would lead you to look.
    virtual FName GetContainerName() const override { return TEXT("Project"); }
    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

    /**
     * Free API key from https://opentopography.org/ — required for DEM auto-fetch
     * (plan_v2_workflow.md §4.1's blocking prerequisite). Without it, DEM auto-fetch is
     * skipped and the region-import flow falls back to manual .tif upload / flat terrain.
     */
    UPROPERTY(EditAnywhere, config, Category = "Data Sources", meta = (DisplayName = "OpenTopography API Key"))
    FString OpenTopographyApiKey;

    /**
     * OpenTopography global DEM dataset identifier.
     *
     * SRTMGL1 (1 arc-second, ~30m) is the default: at city scale a 1.5 km region is only
     * ~11 px across in SRTMGL3 (~90m), which flattens the terrain into a featureless ramp.
     * SRTMGL1 gives ~3x the linear resolution for the same area. Both cover 60N-56S; switch
     * to SRTMGL3 only if a region returns no data.
     */
    UPROPERTY(EditAnywhere, config, Category = "Data Sources", meta = (DisplayName = "OpenTopography DEM Type"))
    FString OpenTopographyDemType = TEXT("SRTMGL1");

    /**
     * Package path of the most recently saved city graph.
     *
     * The in-memory session survives closing the Control Center but not restarting the editor.
     * Remembering the path lets the panel offer the last graph back instead of demanding a
     * re-fetch, which is the expensive half of an import.
     */
    UPROPERTY(config)
    FString LastSavedGraphPath;

    /** Overpass API endpoint used for .osm vector data fetch. No API key required. */
    UPROPERTY(EditAnywhere, config, Category = "Data Sources", meta = (DisplayName = "Overpass API URL"))
    FString OverpassApiUrl = TEXT("https://overpass-api.de/api/interpreter");

    /** HTTP request timeout in seconds for both Overpass and OpenTopography fetches. */
    UPROPERTY(EditAnywhere, config, Category = "Data Sources", meta = (ClampMin = "10", ClampMax = "300"))
    float RequestTimeoutSeconds = 60.0f;
};

// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Terrain/FOSMDEMSampler.h"
#include "FOSMTerrainSettings.generated.h"

/**
 * Terrain generator settings.
 * Passed to UOSMTerrainGenerator to configure the Landscape actor parameters.
 *
 * UE Landscape resolution formula:
 *   Total heightmap size = ((SectionSize - 1) * SectionsPerComponent * ComponentsX) + 1
 *   Valid SectionSize values: 7, 15, 31, 63, 127, 255
 *   Valid SectionsPerComponent values: 1, 2
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENGENERATORS_API FOSMTerrainSettings
{
    GENERATED_BODY()

    /**
     * Landscape section size in quads. Each section is SectionSize×SectionSize quads.
     * Valid: 7, 15, 31, 63, 127, 255.  Default 63 matches most SRTM workflows.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "7", ClampMax = "255"))
    int32 SectionSize = 63;

    /**
     * Number of landscape sections per component.
     * 1 or 2. Higher = larger components, fewer draw calls, but less granular LOD.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "1", ClampMax = "2"))
    int32 SectionsPerComponent = 1;

    /**
     * Number of landscape components along each axis (X and Y).
     * Total heightmap width = ((SectionSize - 1) * SectionsPerComponent * ComponentCount) + 1
     * Auto-computed from DEM extent when <= 0.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "1"))
    int32 ComponentCount = 8;

    /**
     * Optional DEM file path. Leave empty to generate a flat (sea-level) terrain.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (FilePathFilter = "tif,tiff,hgt"))
    FString DEMFilePath;

    /**
     * Vertical exaggeration scale applied to elevation values.
     * 1.0 = real world scale. 2.0 = double height.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0.01"))
    float VerticalScale = 1.0f;

    /**
     * Apply EGM96 geoid correction to convert SRTM orthometric heights
     * to WGS84 ellipsoidal heights. Required when using Cesium ECEF mode.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    bool bApplyGeoidCorrection = false;

    /**
     * Flat terrain elevation in meters (used only when no DEM is provided).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    float FlatElevationMeters = 0.0f;

    /**
     * Maximum expected elevation range in meters. Used for uint16 quantization
     * when DEM min/max are not available. Default covers most of Earth's range.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    float ElevationRangeMeters = 9000.0f;

    /** Compute the total heightmap side length from section and component counts */
    int32 ComputeHeightmapSize() const
    {
        return (SectionSize - 1) * SectionsPerComponent * ComponentCount + 1;
    }
};

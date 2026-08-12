// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Terrain/FOSMDEMSampler.h"
#include "Terrain/FOSMTerrainSettings.h"

/**
 * Result from a heightmap build operation.
 */
struct OSMWORLDGENGENERATORS_API FOSMHeightmapResult
{
    /** Row-major uint16 heightmap data, Size × Size elements */
    TArray<uint16> Data;

    /** Heightmap side length in pixels */
    int32 Size = 0;

    /** Elevation in meters that maps to uint16 value 0 */
    float MinElevationMeters = 0.0f;

    /** Elevation in meters that maps to uint16 value 65535 */
    float MaxElevationMeters = 0.0f;

    /**
     * The UE Landscape Z-scale derived from the elevation range.
     * LandscapeZScale = ElevationRangeMeters * 100.0 / 512.0
     * (100 = m→cm, 512 = UE's internal scale reference)
     */
    float LandscapeZScaleCm = 0.0f;

    /** True if all values were produced from actual DEM data (vs flat fill) */
    bool bFromDEM = false;

    bool IsValid() const { return Size > 0 && Data.Num() == Size * Size; }
};

/**
 * Resamples a loaded DEM to a Landscape-compatible uint16 heightmap buffer.
 *
 * Handles:
 *   - Arbitrary DEM-to-Landscape resolution resampling (bilinear)
 *   - Geoid correction via FOSMGeoidCorrection
 *   - Flat fallback when no DEM is loaded
 *   - uint16 quantization with correctly computed ZScale for Landscape
 */
class OSMWORLDGENGENERATORS_API FOSMHeightmapBuilder
{
public:
    /**
     * Build a heightmap from a DEM sampler and geographic bounds.
     *
     * @param Sampler          Loaded DEM sampler (may be unloaded for flat fallback)
     * @param Settings         Terrain configuration settings
     * @param MinLat           South edge of the import area
     * @param MaxLat           North edge of the import area
     * @param MinLon           West edge of the import area
     * @param MaxLon           East edge of the import area
     * @param OutResult        Populated heightmap result
     * @param OnProgress       Optional progress callback
     * @param bCancel          Optional cancel flag
     * @return                 True if build succeeded
     */
    static bool Build(
        const FOSMDEMSampler& Sampler,
        const FOSMTerrainSettings& Settings,
        double MinLat, double MaxLat,
        double MinLon, double MaxLon,
        FOSMHeightmapResult& OutResult,
        TFunction<void(float Percent, const FText& Status)> OnProgress = nullptr,
        FThreadSafeBool* bCancel = nullptr);

    /**
     * Auto-compute a good ComponentCount to cover the given area at approximately
     * native DEM resolution.
     *
     * @param AreaExtentKm     Area width/height in km (use the larger dimension)
     * @param Settings         Terrain settings (SectionSize, SectionsPerComponent)
     * @return                 Recommended ComponentCount (clamped to [1, 32])
     */
    static int32 AutoComputeComponentCount(double AreaExtentKm, const FOSMTerrainSettings& Settings);
};

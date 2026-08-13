// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fetch/FOSMRegionCache.h"

/**
 * Fetches a DEM (GeoTIFF) for a bounding box from the OpenTopography Global DEM API
 * (plan_v2_workflow.md §4.1) — requires a free API key (UOSMWorldGenSettings).
 *
 * This is the "blocking prerequisite" flagged in plan_v2_workflow.md: without a key
 * configured, FetchAsync fails immediately with a clear message rather than making a
 * request that OpenTopography will reject — callers should treat that failure as the
 * documented fallback trigger (manual .tif upload, or flat terrain if skipped entirely).
 */
class OSMWORLDGENEDITOR_API FOSMOpenTopographyClient
{
public:
    /** @param bSuccess false on missing key / network / HTTP failure; ErrorMessage is human-readable. */
    DECLARE_DELEGATE_TwoParams(FOnFetchComplete, bool /*bSuccess*/, const FString& /*ErrorMessage*/);

    /**
     * Margin in degrees added to the requested region when asking OpenTopography for a raster.
     *
     * OpenTopography returns whole source-grid cells rather than cropping to the requested
     * coordinates, so an unpadded request yields a raster that snaps a fraction of a cell
     * *inside* the region and leaves its edges uncovered. 0.002 deg (~220 m) exceeds one cell
     * for any global DEM (SRTMGL1 ~30 m, SRTMGL3 ~90 m).
     *
     * Public because the cache manifest records it: a change here must invalidate every
     * previously fetched DEM, and that can only happen if the value is comparable.
     */
    static constexpr double FetchPaddingDegrees = 0.002;

    /**
     * Fetch DEM coverage for Region and write it to OutputFilePath as GeoTIFF.
     * The request is padded by FetchPaddingDegrees; the region itself is unchanged.
     * Async — OnComplete fires on the game thread once the HTTP request resolves.
     */
    static void FetchAsync(
        const FOSMRegion& Region,
        const FString& OutputFilePath,
        FOnFetchComplete OnComplete);
};

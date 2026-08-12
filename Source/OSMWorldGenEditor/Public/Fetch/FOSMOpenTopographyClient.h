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
     * Fetch DEM coverage for Bbox and write it to OutputFilePath as GeoTIFF.
     * Async — OnComplete fires on the game thread once the HTTP request resolves.
     */
    static void FetchAsync(
        const FOSMRegionCache::FBoundingBox& Bbox,
        const FString& OutputFilePath,
        FOnFetchComplete OnComplete);
};

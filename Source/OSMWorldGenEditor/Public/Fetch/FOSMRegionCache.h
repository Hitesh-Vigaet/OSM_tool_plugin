// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Resolves and manages the on-disk cache of auto-fetched region files
 * (plan_v2_workflow.md §4.2). Keyed by bounding box so re-selecting the same region
 * reuses previously fetched files instead of re-querying Overpass / OpenTopography.
 *
 * Cache lives in Saved/OSMWorldGen/RegionCache/<bbox-hash>/ — local-only, per-machine,
 * consistent with how the engine already treats Saved/.
 */
class OSMWORLDGENEDITOR_API FOSMRegionCache
{
public:
    /** A geographic bounding box, in WGS84 degrees. */
    struct FBoundingBox
    {
        double MinLat = 0.0;
        double MaxLat = 0.0;
        double MinLon = 0.0;
        double MaxLon = 0.0;

        bool IsValid() const { return MaxLat > MinLat && MaxLon > MinLon; }
    };

    /** Deterministic cache key for a bounding box (same bbox always hashes the same). */
    static FString MakeBboxHash(const FBoundingBox& Bbox);

    /** Absolute path to the cache directory for this bbox (may not exist yet). */
    static FString GetCacheDir(const FBoundingBox& Bbox);

    /** Absolute path the fetched .osm file for this bbox should be read from / written to. */
    static FString GetOSMFilePath(const FBoundingBox& Bbox);

    /** Absolute path the fetched DEM (.tif) file for this bbox should be read from / written to. */
    static FString GetDEMFilePath(const FBoundingBox& Bbox);

    /** True if a cached .osm file already exists for this bbox. */
    static bool HasCachedOSM(const FBoundingBox& Bbox);

    /** True if a cached DEM file already exists for this bbox. */
    static bool HasCachedDEM(const FBoundingBox& Bbox);

    /** Deletes any cached files for this bbox, forcing the next fetch to hit the network. */
    static void InvalidateCache(const FBoundingBox& Bbox);
};

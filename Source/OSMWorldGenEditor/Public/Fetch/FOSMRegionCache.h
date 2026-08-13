// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Region/FOSMRegion.h"

/**
 * Records the exact terms a cached region was fetched under (plan_v3_pipeline.md Phase 1.2).
 *
 * Without this, the cache key was the bounding box alone — so changing the DEM dataset or the
 * fetch padding left every previously-fetched region silently serving files obtained under the
 * old terms. You would change a setting, re-run, and see the identical result, with nothing
 * anywhere indicating why.
 *
 * The manifest makes the terms explicit and comparable, so reuse becomes a decision the code
 * can justify rather than an assumption.
 */
struct OSMWORLDGENEDITOR_API FOSMCacheManifest
{
    /** Bounds the files were fetched for. */
    double MinLat = 0.0;
    double MaxLat = 0.0;
    double MinLon = 0.0;
    double MaxLon = 0.0;

    /** Endpoint that served the .osm (mirrors differ in completeness, so this is worth keeping). */
    FString OverpassUrl;

    /** OpenTopography dataset id, e.g. "SRTMGL1". */
    FString DEMType;

    /** Degrees of padding added to the DEM request beyond the region. */
    double DEMPaddingDegrees = 0.0;

    /** When the fetch completed (UTC). */
    FDateTime FetchedAtUtc = FDateTime::MinValue();

    /** Sizes as written, to detect a file truncated or replaced after the fetch. */
    int64 OSMFileSize = 0;
    int64 DEMFileSize = 0;

    /** Verdict recorded by the import gates the last time these files were validated. */
    FString LastValidationVerdict;

    /**
     * Manifest schema + fetch-semantics version.
     *
     * Bumped whenever something changes that alters what a fetch RETURNS, not just how the
     * manifest is written — version 2 is the node-bounded Overpass query. Old cache entries
     * then stop matching and are re-fetched, which is the whole reason this field exists.
     */
    static constexpr int32 CurrentVersion = 2;
    int32 Version = CurrentVersion;

    bool SaveToFile(const FString& FilePath) const;
    static bool LoadFromFile(const FString& FilePath, FOSMCacheManifest& OutManifest);

    /**
     * True if files described by this manifest may be reused for a fetch under the given terms.
     * Any difference in dataset or padding means the cached DEM is not what would be fetched now.
     */
    bool MatchesFetchTerms(const FString& InDEMType, double InDEMPaddingDegrees) const;
};

/**
 * Resolves and manages the on-disk cache of auto-fetched region files.
 *
 * Cache lives in Saved/OSMWorldGen/RegionCache/<region-hash>/ — local-only, per-machine,
 * consistent with how the engine already treats Saved/.
 */
class OSMWORLDGENEDITOR_API FOSMRegionCache
{
public:
    /** Deterministic cache key for a region (the same region always hashes the same). */
    static FString MakeRegionHash(const FOSMRegion& Region);

    /** Absolute path to the cache directory for this region (may not exist yet). */
    static FString GetCacheDir(const FOSMRegion& Region);

    /** Absolute path the fetched .osm file for this region should be read from / written to. */
    static FString GetOSMFilePath(const FOSMRegion& Region);

    /** Absolute path the fetched DEM (.tif) file for this region should be read from / written to. */
    static FString GetDEMFilePath(const FOSMRegion& Region);

    /** Absolute path to this region's cache manifest. */
    static FString GetManifestFilePath(const FOSMRegion& Region);

    /**
     * True if a cached .osm exists for this region AND was fetched under terms still in force.
     * A cached file with no manifest is treated as unusable: it predates provenance tracking,
     * so there is no way to know what it actually contains.
     */
    static bool HasValidCachedOSM(const FOSMRegion& Region, const FString& DEMType, double DEMPaddingDegrees);

    /** As HasValidCachedOSM, for the elevation raster. */
    static bool HasValidCachedDEM(const FOSMRegion& Region, const FString& DEMType, double DEMPaddingDegrees);

    /** Reads this region's manifest, if one exists. */
    static bool LoadManifest(const FOSMRegion& Region, FOSMCacheManifest& OutManifest);

    /** Writes the manifest for this region, creating the cache directory if needed. */
    static bool SaveManifest(const FOSMRegion& Region, const FOSMCacheManifest& Manifest);

    /** Deletes any cached files for this region, forcing the next fetch to hit the network. */
    static void InvalidateCache(const FOSMRegion& Region);
};

// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fetch/FOSMRegionCache.h"

/**
 * Fetches OSM vector data for a bounding box from the Overpass API
 * (plan_v2_workflow.md §4.1) — no API key required. The result is written as a standard
 * OSM XML file, structurally equivalent to a manual export, so it feeds the existing
 * FOSMXMLParser unchanged.
 */
class OSMWORLDGENEDITOR_API FOSMOverpassClient
{
public:
    /** @param bSuccess false on network/HTTP/parse failure; ErrorMessage is human-readable. */
    DECLARE_DELEGATE_TwoParams(FOnFetchComplete, bool /*bSuccess*/, const FString& /*ErrorMessage*/);

    /**
     * Fetch all nodes/ways/relations within Bbox and write them to OutputFilePath as OSM XML.
     * Async — OnComplete fires on the game thread once the HTTP request resolves.
     */
    static void FetchAsync(
        const FOSMRegionCache::FBoundingBox& Bbox,
        const FString& OutputFilePath,
        FOnFetchComplete OnComplete);

private:
    /** Build the Overpass QL query text for a bbox (fetch everything, resolve referenced nodes). */
    static FString BuildQuery(const FOSMRegionCache::FBoundingBox& Bbox, float TimeoutSeconds);
};

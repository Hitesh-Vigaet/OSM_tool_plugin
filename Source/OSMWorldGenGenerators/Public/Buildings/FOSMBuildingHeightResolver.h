// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FOSMFeature;

/**
 * Resolves the effective building height in meters from an OSM feature's tags.
 *
 * Priority chain (first hit wins):
 *   1. "height"           (raw numeric meters, e.g. "42.5")
 *   2. "building:height"  (same format)
 *   3. "building:levels" or "levels" × DefaultFloorHeight
 *   4. BuildingSubtype default (e.g. "house" → 6m, "skyscraper" → 200m)
 *   5. GlobalDefault
 */
class OSMWORLDGENGENERATORS_API FOSMBuildingHeightResolver
{
public:
    struct FResolutionResult
    {
        /** Resolved height in meters */
        float HeightMeters = 0.0f;

        /** Source that produced this height */
        enum class ESource : uint8
        {
            ExplicitTag,        // "height" or "building:height" tag
            LevelsTag,          // "levels" / "building:levels" × floor height
            SubtypeDefault,     // Known subtype default
            GlobalDefault       // Final fallback
        };
        ESource Source = ESource::GlobalDefault;
    };

    /**
     * Resolve the height for a building feature.
     *
     * @param Feature            The OSM building feature to examine
     * @param DefaultFloorHeight Floor-to-floor height (meters) used when only level count is known
     * @param GlobalDefault      Last-resort height (meters) when no tag gives a clue
     * @return                   Result containing the resolved height and which source was used
     */
    static FResolutionResult Resolve(
        const FOSMFeature& Feature,
        float DefaultFloorHeight = 3.0f,
        float GlobalDefault = 9.0f);

    /**
     * Attempt to parse a height tag value that may be:
     *  - A bare number ("12.5")
     *  - A number with units ("12.5 m", "42 ft")
     *
     * @param TagValue   Raw string from OSM tag
     * @param OutMeters  Parsed height in meters (only valid when function returns true)
     * @return           true if parsing succeeded
     */
    static bool TryParseHeightTag(const FString& TagValue, float& OutMeters);

private:
    /** Per-subtype sensible height defaults (meters) */
    static float GetSubtypeDefaultHeight(const FString& BuildingSubtype);
};

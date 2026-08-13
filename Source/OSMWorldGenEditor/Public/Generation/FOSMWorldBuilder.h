// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Region/FOSMRegion.h"

class UOSMCityGraph;

/**
 * Spawns the city into the level (plan_v3_pipeline.md Phase 5).
 *
 * The first code in the rebuilt pipeline that creates city geometry, and it runs only after the
 * graph has been validated, inspected and dry-run — the stages the original pipeline lacked,
 * which is why its failures were only ever visible as wreckage.
 *
 * Every actor is tagged and parented under one root, so a rebuild removes exactly what it made
 * and nothing else. Regeneration that accumulates actors is how a scene becomes inexplicable.
 */
class OSMWORLDGENEDITOR_API FOSMWorldBuilder
{
public:
    struct FOptions
    {
        bool bBuildings = true;
        bool bRoads = true;
        bool bAreas = true;      // vegetation, water, landuse, leisure

        /** Drape geometry onto the DEM surface rather than a flat plane. */
        bool bUseTerrain = true;

        /**
         * Flat colours instead of assigned materials.
         *
         * Shape and placement are what need checking first, and textures hide exactly the defects
         * being looked for — a building sitting in a road reads instantly in flat grey and not at
         * all through a facade material.
         */
        bool bGreyBox = true;
    };

    struct FResult
    {
        int32 Buildings = 0;
        int32 Roads = 0;
        int32 Areas = 0;
        int32 Skipped = 0;
        TArray<FString> Problems;
        double DurationSeconds = 0.0;

        FString ToString() const;
    };

    /** Build the city. Removes anything a previous run created first. */
    static FResult Build(const UOSMCityGraph& Graph, const FOSMRegion& Region, const FOptions& Options);

    /** Remove everything previous runs created, leaving the rest of the level untouched. */
    static int32 Clear();

    /** Tag identifying actors this builder owns. */
    static const FName GetGeneratedActorTag();
};

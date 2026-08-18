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
        /** The ground surface itself, sampled from the DEM. Everything else sits on it. */
        bool bTerrain = true;

        bool bBuildings = true;
        bool bRoads = true;
        bool bAreas = true;      // vegetation, water, landuse, leisure

        /** Drape geometry onto the DEM surface rather than a flat plane. */
        bool bUseTerrain = true;

        /**
         * Target terrain quad size in metres.
         *
         * 8 m by default: fine enough that roads follow the ground without visibly stepping,
         * coarse enough that a 1 km region stays one cheap mesh. The source is 30 m SRTM, so
         * going much below this interpolates rather than reveals.
         */
        double TerrainQuadMeters = 8.0;

        /**
         * How far the ground extends beyond the outermost geometry.
         *
         * Not decoration: at the very edge the surface stops interpolating and starts clamping, so
         * anything sitting exactly on the boundary is grounded against a flat approximation.
         */
        double TerrainMarginMeters = 20.0;

        /**
         * How far a building's base is sunk below the lowest ground beneath its footprint.
         *
         * Buildings are buried into their slope rather than balanced on it. Half a metre is enough
         * to absorb the difference between the terrain grid and a footprint corner that falls
         * between grid nodes, without a visible plinth on flat ground.
         */
        double FoundationSkirtMeters = 0.5;

        /**
         * Flat colours instead of assigned materials.
         *
         * Shape and placement are what need checking first, and textures hide exactly the defects
         * being looked for — a building sitting in a road reads instantly in flat grey and not at
         * all through a facade material.
         */
        bool bGreyBox = true;

        /**
         * When set, every emitted mesh is written here for offline inspection.
         *
         * Empty by default, so a normal build pays nothing. See FOSMGeometryDump for why looking
         * at the output matters more than counting it.
         */
        FString DebugDumpPath;
    };

    struct FResult
    {
        int32 Terrain = 0;
        int32 Buildings = 0;
        int32 Roads = 0;
        int32 Areas = 0;
        int32 Skipped = 0;
        TArray<FString> Problems;
        double DurationSeconds = 0.0;

        /** Where the geometry dump was written, when one was requested. */
        FString DumpPath;

        FString ToString() const;
    };

    /** Build the city. Removes anything a previous run created first. */
    static FResult Build(const UOSMCityGraph& Graph, const FOSMRegion& Region, const FOptions& Options);

    /** Remove everything previous runs created, leaving the rest of the level untouched. */
    static int32 Clear();

    /** Tag identifying actors this builder owns. */
    static const FName GetGeneratedActorTag();
};

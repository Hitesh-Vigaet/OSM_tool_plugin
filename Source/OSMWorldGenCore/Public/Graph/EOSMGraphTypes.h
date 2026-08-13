// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EOSMGraphTypes.generated.h"

/**
 * What a city-graph node represents (plan_v3_pipeline.md Phase 2.2).
 *
 * Deliberately finer-grained than EOSMFeatureType: classification answers "what tag was this",
 * the graph answers "what is this in a city". A `natural=water` polygon and a `waterway=river`
 * line are one classification family but two different things to reason about, and a Junction
 * has no classified feature behind it at all.
 */
UENUM(BlueprintType)
enum class EOSMNodeType : uint8
{
    Unknown         UMETA(DisplayName = "Unknown"),

    Building        UMETA(DisplayName = "Building"),
    RoadSegment     UMETA(DisplayName = "Road Segment"),
    /** Derived: a point where two or more road segments meet. Has no OSM feature of its own. */
    Junction        UMETA(DisplayName = "Junction"),
    WaterBody       UMETA(DisplayName = "Water Body"),
    Waterway        UMETA(DisplayName = "Waterway"),
    VegetationArea  UMETA(DisplayName = "Vegetation Area"),
    LanduseZone     UMETA(DisplayName = "Land Use Zone"),
    LeisureArea     UMETA(DisplayName = "Leisure Area"),
    Railway         UMETA(DisplayName = "Railway"),
    Barrier         UMETA(DisplayName = "Barrier"),
    PowerLine       UMETA(DisplayName = "Power Line"),
    Amenity         UMETA(DisplayName = "Amenity"),
    /** Derived from the DEM rather than from OSM. */
    TerrainTile     UMETA(DisplayName = "Terrain Tile"),
    /** Derived: a road-enclosed polygon and the buildings inside it. Opt-in. */
    Block           UMETA(DisplayName = "Block"),

    MAX             UMETA(Hidden)
};

/**
 * How two nodes relate.
 *
 * Split into two families that are never mixed, because they differ in trustworthiness:
 * topological edges are read from OSM's own structure and are exact; spatial edges are derived
 * from geometry within a tolerance and can be wrong. Code consuming an edge needs to know which
 * kind it is holding, so the distinction is in the type rather than in a comment.
 */
UENUM(BlueprintType)
enum class EOSMRelationshipType : uint8
{
    Unknown     UMETA(DisplayName = "Unknown"),

    // ---- Topological: exact, from OSM structure ----
    /** Two ways meet at a shared node. */
    SharesNode  UMETA(DisplayName = "Shares Node"),
    /** Road segment connects to road segment through a junction. */
    ConnectsTo  UMETA(DisplayName = "Connects To"),
    /** Way is a member of a multipolygon relation. */
    PartOf      UMETA(DisplayName = "Part Of"),
    /** Ring bounds an area (outer or inner). */
    Bounds      UMETA(DisplayName = "Bounds"),

    // ---- Spatial: derived, tolerance-based ----
    /** Zone or block contains a building. */
    Contains    UMETA(DisplayName = "Contains"),
    /** Building faces its nearest road segment. */
    FrontsOnto  UMETA(DisplayName = "Fronts Onto"),
    /** Areas share or nearly share a boundary. */
    AdjacentTo  UMETA(DisplayName = "Adjacent To"),
    /** Linear feature crosses another (bridge/tunnel over water). */
    Crosses     UMETA(DisplayName = "Crosses"),

    MAX         UMETA(Hidden)
};

/** How a group of nodes was formed (plan_v3_pipeline.md Phase 2.4). */
UENUM(BlueprintType)
enum class EOSMGroupKind : uint8
{
    /** All nodes of one type — "Buildings", "Roads". The default UI grouping. */
    Category    UMETA(DisplayName = "Category"),
    /** Buildings enclosed by the same road loop. */
    Block       UMETA(DisplayName = "Block"),
    /** Road segments sharing a name or ref, so a street is one entity not forty fragments. */
    Corridor    UMETA(DisplayName = "Corridor"),

    MAX         UMETA(Hidden)
};

/** True for relations derived from geometry rather than read from OSM structure. */
inline bool IsSpatialRelationship(EOSMRelationshipType Type)
{
    return Type == EOSMRelationshipType::Contains
        || Type == EOSMRelationshipType::FrontsOnto
        || Type == EOSMRelationshipType::AdjacentTo
        || Type == EOSMRelationshipType::Crosses;
}

OSMWORLDGENCORE_API FString OSMNodeTypeToString(EOSMNodeType Type);
OSMWORLDGENCORE_API FString OSMRelationshipTypeToString(EOSMRelationshipType Type);
OSMWORLDGENCORE_API FString OSMGroupKindToString(EOSMGroupKind Kind);

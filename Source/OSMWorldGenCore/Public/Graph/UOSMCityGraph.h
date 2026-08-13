// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Graph/EOSMGraphTypes.h"
#include "Graph/FOSMGeometryStore.h"
#include "Graph/FOSMGenerationConfig.h"
#include "UOSMCityGraph.generated.h"

/**
 * Measurements computed once at build time.
 *
 * Stored rather than recomputed so that inspecting the graph never re-derives geometry — the
 * numbers shown in the Control Center are exactly the numbers the builders saw.
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMNodeMetrics
{
    GENERATED_BODY()

    /** Enclosed area in m^2 (areas only). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    double AreaSqm = 0.0;

    /** Length in m (polylines) or perimeter (areas). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    double LengthMeters = 0.0;

    /** WGS84 centroid: X = latitude, Y = longitude. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    FVector2D CentroidLatLon = FVector2D::ZeroVector;

    /** Height in m, for buildings. 0 when unknown. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    double HeightMeters = 0.0;

    /** Width in m, for roads. 0 when unknown. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    double WidthMeters = 0.0;

    /** Elevation range in m. Populated for TerrainTile nodes; 0 elsewhere. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    double MinElevationMeters = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    double MaxElevationMeters = 0.0;
};

/**
 * One node in the city graph.
 *
 * Carries everything needed to reason about the entity without touching the source file: its
 * identity, its full original tag dictionary, its measurements, and a handle to its geometry.
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMGraphNode
{
    GENERATED_BODY()

    /** Stable index within the owning graph. Equals the node's position in Nodes. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    int32 Id = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    EOSMNodeType Type = EOSMNodeType::Unknown;

    /** Tag-derived subtype: "residential", "motorway", "river". */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    FString SubType;

    /** Human-readable name from the name=* tag, when present. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    FString Name;

    /** Originating OSM element id. 0 for derived nodes (junctions, blocks, terrain tiles). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    int64 OSMId = 0;

    /** Complete original tag dictionary, preserved verbatim. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    TMap<FString, FString> Tags;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    FOSMNodeMetrics Metrics;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    FOSMGeometryHandle Geometry;

    /**
     * Validation codes raised against this node, e.g. "node.geometry.degenerate".
     *
     * A flagged node is KEPT, never dropped. Silently discarding invalid nodes is how
     * "where did my water go?" happens; the Control Center shows the flag instead.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    TArray<FString> ValidationFlags;

    bool HasGeometry() const { return Geometry.IsValid(); }
    bool IsFlagged() const { return ValidationFlags.Num() > 0; }
};

/**
 * A directed relationship between two nodes.
 *
 * Spatial edges record the tolerance they were derived under, so a questionable result can be
 * judged rather than merely doubted.
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMGraphEdge
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    EOSMRelationshipType Type = EOSMRelationshipType::Unknown;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    int32 FromNode = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    int32 ToNode = INDEX_NONE;

    /** Measured value behind the edge: distance in m for FrontsOnto, 0 for exact relations. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    double Value = 0.0;

    /** Tolerance the derivation used, in m. 0 for topological edges, which need none. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    double ToleranceMeters = 0.0;

    bool IsSpatial() const { return IsSpatialRelationship(Type); }
};

/** A named set of nodes managed as one entity (plan_v3_pipeline.md Phase 2.4). */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMNodeGroup
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    FString Name;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    EOSMGroupKind Kind = EOSMGroupKind::Category;

    /** Node type this group collects, for Category groups. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    EOSMNodeType NodeType = EOSMNodeType::Unknown;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    TArray<int32> NodeIds;

    int32 Num() const { return NodeIds.Num(); }
};

/**
 * A persistent, inspectable description of one region's city (plan_v3_pipeline.md Phase 2).
 *
 * This is the layer the old pipeline never had. Previously the only way to discover that the
 * data had been misunderstood was to build the geometry and look at the wreckage; the graph
 * makes the interpretation reviewable while it is still cheap to fix.
 *
 * A UObject so it can be saved as a .uasset and a region reopened without re-fetching.
 *
 * Deliberately contains NO generation logic and spawns nothing.
 */
UCLASS(BlueprintType)
class OSMWORLDGENCORE_API UOSMCityGraph : public UObject
{
    GENERATED_BODY()

public:
    /** Region this graph describes, as plain bounds so the asset stays self-contained. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    double RegionMinLat = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    double RegionMaxLat = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    double RegionMinLon = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    double RegionMaxLon = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    TArray<FOSMGraphNode> Nodes;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    TArray<FOSMGraphEdge> Edges;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    TArray<FOSMNodeGroup> Groups;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    FOSMGeometryStore Geometry;

    /**
     * User configuration: asset rules, ratios, seeds, visibility (plan_v3_pipeline.md Phase 3.5).
     *
     * Saved with the graph so a region's setup survives closing the editor. Nothing reads this
     * to generate anything yet — Phase 5 will.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Graph")
    FOSMGenerationConfig Config;

    /** Source files this graph was built from, for provenance. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    FString SourceOSMFile;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    FString SourceDEMFile;

    // ---- Queries ----

    int32 NumNodes() const { return Nodes.Num(); }
    int32 NumEdges() const { return Edges.Num(); }

    const FOSMGraphNode* FindNode(int32 NodeId) const
    {
        return Nodes.IsValidIndex(NodeId) ? &Nodes[NodeId] : nullptr;
    }

    UFUNCTION(BlueprintCallable, Category = "OSM|Graph")
    int32 CountNodesOfType(EOSMNodeType Type) const;

    UFUNCTION(BlueprintCallable, Category = "OSM|Graph")
    int32 CountEdgesOfType(EOSMRelationshipType Type) const;

    /** All node ids of a given type, in ascending id order. */
    TArray<int32> GetNodesOfType(EOSMNodeType Type) const;

    /** Edges leaving a node. */
    TArray<int32> GetOutgoingEdges(int32 NodeId) const;

    /** Edges arriving at a node. */
    TArray<int32> GetIncomingEdges(int32 NodeId) const;

    const FOSMNodeGroup* FindGroup(const FString& GroupName) const;

    /** Every node carrying at least one validation flag. */
    TArray<int32> GetFlaggedNodes() const;

    /**
     * Content hash over nodes, edges, groups and geometry.
     *
     * The determinism check: the same input must always produce the same graph. Anything that
     * makes a build order-dependent — an unsorted map iteration, a float comparison that goes
     * either way — shows up here as a changed hash rather than as a rendering difference
     * noticed three phases later.
     */
    UFUNCTION(BlueprintCallable, Category = "OSM|Graph")
    FString ComputeContentHash() const;

    /** Multi-line summary for logs and the wizard. */
    FString ToSummaryString() const;
};

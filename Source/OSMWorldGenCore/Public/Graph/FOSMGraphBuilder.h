// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Graph/UOSMCityGraph.h"
#include "Region/FOSMRegion.h"
#include "Validation/FOSMValidation.h"

struct FOSMFeatureTable;

/** Knobs for a graph build. Defaults are the settings the plan specifies. */
struct OSMWORLDGENCORE_API FOSMGraphBuildOptions
{
    /**
     * Maximum distance from a building to the road it is considered to front onto.
     *
     * 50 m comfortably covers a setback from the street while staying below the distance at
     * which "nearest road" stops meaning anything. Buildings with no road inside the cutoff are
     * flagged rather than silently left unlinked.
     */
    double FrontsOntoCutoffMeters = 50.0;

    /**
     * Extract Block nodes by finding road-enclosed polygons.
     *
     * Opt-in: enclosure over the road graph is the expensive derivation here, and it is only
     * needed once per-block grouping is actually being used.
     */
    bool bExtractBlocks = false;

    /** Build Contains edges from zones to the buildings inside them. */
    bool bComputeContains = true;

    /** Build FrontsOnto edges from buildings to their nearest street. */
    bool bComputeFrontsOnto = true;

    /** Group road segments sharing a name into Corridor groups. */
    bool bBuildCorridors = true;
};

/**
 * Findings from a graph build (plan_v3_pipeline.md Phase 2.6).
 *
 * Reuses the Phase 1 validation vocabulary so the Control Center has one issue format to render
 * and one severity rule to honour, rather than two that drift apart.
 */
struct OSMWORLDGENCORE_API FOSMGraphReport
{
    FOSMValidationResult Validation;

    int32 FeaturesConsidered = 0;
    int32 NodesCreated = 0;
    int32 EdgesCreated = 0;
    int32 GroupsCreated = 0;

    /**
     * Features that produced no node, and why.
     *
     * The acceptance criterion "nothing vanishes" is enforced here: every considered feature
     * either becomes a node or appears in this list. A feature that does neither is a bug.
     */
    TArray<FString> RejectedFeatures;

    int32 FlaggedNodes = 0;
    double DurationSeconds = 0.0;

    bool IsAccepted() const { return Validation.IsAccepted(); }

    /** Every feature accounted for, either as a node or as a stated rejection. */
    bool IsComplete() const { return NodesCreated + RejectedFeatures.Num() >= FeaturesConsidered; }

    FString ToDisplayString() const;
};

/**
 * Turns a classified feature table into a city graph (plan_v3_pipeline.md Phase 2.5).
 *
 * Pipeline:
 *   NodeBuilder -> TopologyBuilder -> SpatialBuilder -> GroupBuilder -> GraphValidator
 *
 * Each stage is a pure function over its input, so each is testable without an editor. That is
 * what makes Phase 2 verifiable in a way the old actor-spawning pipeline never was.
 *
 * Builds representation only. Spawns nothing, renders nothing.
 */
class OSMWORLDGENCORE_API FOSMGraphBuilder
{
public:
    /**
     * @param Features  Classified, region-clipped features from FOSMImportValidator::Run.
     * @param Region    The region — copied into the graph, never re-derived from geometry.
     * @param Outer     UObject owner for the created graph.
     * @return          A populated graph, or nullptr only if Outer is null.
     */
    static UOSMCityGraph* Build(
        const FOSMFeatureTable& Features,
        const FOSMRegion& Region,
        const FOSMGraphBuildOptions& Options,
        UObject* Outer,
        FOSMGraphReport& OutReport);

    /** Node type a classified feature becomes. Unknown means "no node from this feature". */
    static EOSMNodeType MapFeatureToNodeType(const struct FOSMFeature& Feature);

    // ---- Stages, exposed for testing ----

    static void BuildNodes(const FOSMFeatureTable& Features, UOSMCityGraph& Graph, FOSMGraphReport& Report);
    static void BuildTopology(UOSMCityGraph& Graph, FOSMGraphReport& Report);
    static void BuildSpatial(UOSMCityGraph& Graph, const FOSMGraphBuildOptions& Options, FOSMGraphReport& Report);
    static void BuildGroups(UOSMCityGraph& Graph, const FOSMGraphBuildOptions& Options, FOSMGraphReport& Report);
    static void ValidateGraph(UOSMCityGraph& Graph, const FOSMRegion& Region, FOSMGraphReport& Report);
};

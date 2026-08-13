// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Graph/EOSMGraphTypes.h"
#include "Graph/FOSMGenerationConfig.h"

class UOSMCityGraph;

/** Why a node ended up with the asset it did (plan_v3_pipeline.md Phase 4.3). */
enum class EOSMSelectionOutcome : uint8
{
    /** A configured asset was chosen by weight. */
    Selected,
    /** A rule existed but every choice had an empty asset path. */
    FallbackNoAsset,
    /** No rule was configured for this node type at all. */
    FallbackNoRule,
    /** The node belongs to a corridor, and the corridor's choice was reused. */
    InheritedFromCorridor
};

/** One node's resolved asset choice. */
struct OSMWORLDGENCORE_API FOSMAssetSelection
{
    int32 NodeId = INDEX_NONE;
    EOSMNodeType NodeType = EOSMNodeType::Unknown;
    FString SubType;

    /** Index into the rule's Choices, or INDEX_NONE when nothing was selectable. */
    int32 ChoiceIndex = INDEX_NONE;

    FSoftObjectPath Asset;
    FString Label;

    EOSMSelectionOutcome Outcome = EOSMSelectionOutcome::FallbackNoRule;

    /** Group this selection was inherited from, for corridor-level choices. */
    FString CorridorName;

    bool IsFallback() const
    {
        return Outcome == EOSMSelectionOutcome::FallbackNoAsset
            || Outcome == EOSMSelectionOutcome::FallbackNoRule;
    }
};

/**
 * Resolves which asset each node should use (plan_v3_pipeline.md Phase 4.2 and 4.3).
 *
 * Two properties matter more than the choice itself:
 *
 *   **Determinism.** The same graph and the same seeds must always produce the same selection.
 *   The seed is derived per node from the rule seed and the node's stable id, so the result does
 *   not depend on iteration order, thread scheduling, or how many nodes were processed first.
 *   A global random stream would make a rebuild differ from the dry run that approved it.
 *
 *   **No silent skips.** A node with no usable asset gets a recorded fallback with a reason, not
 *   an absence. Silent omission is how "where did my water go?" happens, and the whole pipeline
 *   was rebuilt to stop answering questions like that with a shrug.
 *
 * Selection only. Nothing here spawns or loads anything.
 */
class OSMWORLDGENCORE_API FOSMAssetSelector
{
public:
    /**
     * Resolve a selection for every node in the graph.
     *
     * Corridor-scoped rules are resolved once per corridor and shared by its segments, so a
     * street cannot alternate surfaces every ten metres.
     */
    static TArray<FOSMAssetSelection> SelectForGraph(const UOSMCityGraph& Graph);

    /**
     * Deterministic per-node seed.
     *
     * Combines the rule seed with the node's stable id, so changing one rule's seed reshuffles
     * only that rule's nodes, and a node keeps its choice when unrelated nodes are added.
     */
    static int32 MakeNodeSeed(int32 RuleSeed, int32 NodeId);

    /**
     * Pick a choice index by weight for a given seed, or INDEX_NONE when nothing is selectable.
     *
     * Exposed for testing: the distribution is the part most worth checking, and it should be
     * checkable without constructing a graph.
     */
    static int32 PickWeightedChoice(const FOSMAssetRule& Rule, int32 Seed);

    static FString DescribeOutcome(EOSMSelectionOutcome Outcome);
};

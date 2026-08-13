// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Graph/EOSMGraphTypes.h"
#include "FOSMGenerationConfig.generated.h"

/**
 * One asset choice and how often it should be picked (plan_v3_pipeline.md Phase 3.5).
 *
 * The asset is a soft path so a graph can be saved, moved, and reopened without dragging every
 * referenced mesh into memory, and without breaking if an asset is missing — a missing asset
 * becomes a reported fallback rather than a load failure.
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMAssetChoice
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Generation")
    FSoftObjectPath Asset;

    /** Human label shown in the UI when the asset path is empty or unresolvable. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Generation")
    FString Label;

    /**
     * Relative weight, not a percentage.
     *
     * Stored as a weight so editing one entry does not silently rewrite the others: the UI
     * shows normalised percentages, but the stored numbers stay independent. Percentages that
     * must sum to 100 make every edit a multi-field edit, which is how ratios drift.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Generation", meta = (ClampMin = "0.0"))
    float Weight = 1.0f;
};

/**
 * Asset selection rules for one node type / subtype (plan_v3_pipeline.md Phase 3.5).
 *
 * Configuration only. Nothing in this phase spawns anything.
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMAssetRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Generation")
    EOSMNodeType NodeType = EOSMNodeType::Unknown;

    /** Empty means "every subtype of this node type". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Generation")
    FString SubType;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Generation")
    TArray<FOSMAssetChoice> Choices;

    /**
     * Seed for the deterministic selection this rule will drive.
     *
     * Stored per rule rather than globally so that changing the buildings seed cannot reshuffle
     * the roads — an edit should only affect what it names.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Generation")
    int32 Seed = 12345;

    /**
     * Apply one choice per corridor rather than per segment.
     *
     * A street is one entity; picking per segment lets a single road alternate mud and tarmac
     * every ten metres. Roads default to corridor-level selection for that reason; area
     * features have no corridor and ignore this.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Generation")
    bool bSelectPerCorridor = true;

    /** Sum of all weights. Zero when the rule has no usable choices. */
    float GetTotalWeight() const;

    /** Weight of one choice as a fraction of the total, in [0,1]. */
    float GetNormalisedRatio(int32 ChoiceIndex) const;
};

/**
 * Everything the user has configured about how this graph should eventually be generated.
 *
 * Lives on the graph asset so a region's setup survives closing the editor. Deliberately holds
 * no generation logic — Phase 5 will read this; nothing writes geometry from it yet.
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMGenerationConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Generation")
    TArray<FOSMAssetRule> Rules;

    /** Node types the user has hidden in the Control Center. Persisted with the graph. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Generation")
    TSet<EOSMNodeType> HiddenNodeTypes;

    /** Find the rule for a node type and subtype, preferring an exact subtype match. */
    const FOSMAssetRule* FindRule(EOSMNodeType NodeType, const FString& SubType) const;

    /** Find or create a rule, so the UI can edit one without pre-seeding every combination. */
    FOSMAssetRule& FindOrAddRule(EOSMNodeType NodeType, const FString& SubType);

    bool IsNodeTypeVisible(EOSMNodeType NodeType) const { return !HiddenNodeTypes.Contains(NodeType); }

    void SetNodeTypeVisible(EOSMNodeType NodeType, bool bVisible)
    {
        if (bVisible) HiddenNodeTypes.Remove(NodeType);
        else          HiddenNodeTypes.Add(NodeType);
    }
};

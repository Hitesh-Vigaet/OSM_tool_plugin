// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Generation/FOSMAssetSelector.h"
#include "Graph/EOSMGraphTypes.h"

class UOSMCityGraph;

/** What one node type would produce, and how (plan_v3_pipeline.md Phase 4.4). */
struct OSMWORLDGENCORE_API FOSMDryRunCategory
{
    EOSMNodeType NodeType = EOSMNodeType::Unknown;

    int32 NodeCount = 0;
    int32 SelectedCount = 0;
    int32 FallbackCount = 0;

    /** PCG data type this category would become — see docs/pcg-reference.md section 3. */
    FString PCGDataType;

    /** Chosen asset label -> how many nodes would use it. */
    TMap<FString, int32> AssetCounts;

    /** Nodes carrying validation flags, which will still be generated but are suspect. */
    int32 FlaggedCount = 0;
};

/**
 * What generation WOULD do, without doing any of it (plan_v3_pipeline.md Phase 4.4).
 *
 * The last checkpoint before geometry exists. Every number here is answerable before anything is
 * built, so a wrong ratio or an unassigned category costs a re-read of a table rather than
 * another wrecked scene.
 */
struct OSMWORLDGENCORE_API FOSMDryRunReport
{
    TArray<FOSMDryRunCategory> Categories;

    int32 TotalNodes = 0;
    int32 TotalSelected = 0;
    int32 TotalFallbacks = 0;

    /** Corridors whose segments share one choice, and how many segments each covers. */
    TMap<FString, int32> CorridorSizes;

    bool bHasTerrain = false;
    double RegionAreaSqKm = 0.0;

    /** True when nothing would be generated at all, which is worth saying out loud. */
    bool IsEmpty() const { return TotalSelected == 0; }

    FString ToDisplayString() const;
};

/** Builds the dry run. Reads the graph and its config; writes nothing, spawns nothing. */
class OSMWORLDGENCORE_API FOSMDryRun
{
public:
    static FOSMDryRunReport Run(const UOSMCityGraph& Graph);

    /** PCG data type a node type maps to, per docs/pcg-reference.md section 3. */
    static FString GetPCGDataTypeFor(EOSMNodeType NodeType);
};

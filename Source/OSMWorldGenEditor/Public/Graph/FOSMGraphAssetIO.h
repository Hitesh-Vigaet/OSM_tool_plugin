// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UOSMCityGraph;
struct FOSMRegion;

/**
 * Saves and loads city graphs as .uasset packages (plan_v3_pipeline.md Phase 3.1).
 *
 * Deferred out of Phase 2 deliberately: UOSMCityGraph was serialisable from the start, but
 * nothing needed to reopen a graph until the Control Center existed, and a save path with no
 * consumer is a save path nobody notices is broken.
 */
class OSMWORLDGENEDITOR_API FOSMGraphAssetIO
{
public:
    /** Content-relative folder every generated graph lands in. */
    static const TCHAR* GetGraphPackageRoot();

    /**
     * Asset name for a region, derived from its centre so the same region always maps to the
     * same asset and re-importing updates it rather than accumulating near-duplicates.
     */
    static FString MakeAssetName(const FOSMRegion& Region);

    /** Full package path, e.g. /Game/OSMWorldGen/Graphs/CityGraph_13p0840_77p6000. */
    static FString MakePackagePath(const FOSMRegion& Region);

    /**
     * Save a graph, creating or replacing the package.
     *
     * @param SourceGraph  Graph to persist. Its outer need not be the package.
     * @param OutError     Populated with the reason on failure.
     * @return             The saved graph (owned by the package), or nullptr on failure.
     */
    static UOSMCityGraph* SaveGraph(UOSMCityGraph* SourceGraph, const FOSMRegion& Region, FString& OutError);

    /** Load a previously saved graph for a region, or nullptr if none exists. */
    static UOSMCityGraph* LoadGraph(const FOSMRegion& Region);
};

// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Graph/FOSMGraphBuilder.h"
#include "Region/FOSMRegion.h"
#include "UObject/StrongObjectPtr.h"

class UOSMCityGraph;

/**
 * Holds the imported city graph for the lifetime of the editor session.
 *
 * The Control Center used to own the graph outright, which made closing the panel destroy the
 * import: the widget held the only reference, so dismissing a window — easily done by accident —
 * silently threw away work that took a network fetch and a full validation pass to produce.
 *
 * Ownership therefore lives here instead. The panel becomes a view onto the session rather than
 * the thing that keeps it alive, so it can be closed and reopened freely.
 *
 * This survives closing the panel, not closing the editor. For that, save the graph asset —
 * FOSMGraphAssetIO writes a .uasset that reloads without re-fetching.
 */
class OSMWORLDGENEDITOR_API FOSMGraphSession
{
public:
    static FOSMGraphSession& Get();

    /** Record the result of an import. Replaces whatever was held before. */
    void Set(UOSMCityGraph* InGraph, const FOSMRegion& InRegion, const FOSMGraphReport& InReport);

    bool HasGraph() const { return Graph.IsValid(); }

    UOSMCityGraph* GetGraph() const { return Graph.Get(); }
    const FOSMRegion& GetRegion() const { return Region; }
    const FOSMGraphReport& GetReport() const { return Report; }

    /** Forget the current graph. The panel shows its empty state afterwards. */
    void Clear();

private:
    /**
     * Strong, so the graph is rooted independently of any widget.
     *
     * This is the whole point: the graph is transient (built into the transient package, not yet
     * saved as an asset), so without a strong reference held somewhere outside the UI, garbage
     * collection would take it the moment the panel let go.
     */
    TStrongObjectPtr<UOSMCityGraph> Graph;

    FOSMRegion Region;
    FOSMGraphReport Report;
};

// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Graph/EOSMGraphTypes.h"
#include "Graph/FOSMGraphBuilder.h"
#include "Elevation/FOSMDEMSampler.h"
#include "Region/FOSMRegion.h"
#include "UObject/StrongObjectPtr.h"

class ITableRow;
class STableViewBase;
class SScrollBox;
class UOSMCityGraph;
template <typename ItemType> class STreeView;

/**
 * One row in the node explorer tree (plan_v3_pipeline.md Phase 3.2).
 *
 * The tree is category -> subtype -> instance. Rows are shared pointers because STreeView
 * identifies items by pointer, so rebuilding the tree must not recreate rows the user has
 * expanded or selected.
 */
struct FOSMExplorerItem
{
    enum class EKind : uint8 { Category, SubType, Instance };

    EKind Kind = EKind::Category;
    EOSMNodeType NodeType = EOSMNodeType::Unknown;
    FString SubType;

    /** Valid only for Instance rows. */
    int32 NodeId = INDEX_NONE;

    /** Node count under this row, including children. */
    int32 Count = 0;

    /** Number of flagged nodes under this row, so problems are visible without expanding. */
    int32 FlaggedCount = 0;

    TArray<TSharedPtr<FOSMExplorerItem>> Children;

    FString GetDisplayText() const;
};

/**
 * The Control Center (plan_v3_pipeline.md Phase 3).
 *
 * Where the city is understood and configured before anything is built. It exists because the
 * old pipeline had no stage between "file on disk" and "actors in the world" — defects were only
 * ever discoverable as broken geometry, after the most expensive step had already run.
 *
 * Everything here is inspection and configuration. Nothing spawns actors, and the viewport
 * overlay is debug drawing only, so what you see is the graph itself rather than a rendering of
 * something derived from it.
 */
class OSMWORLDGENEDITOR_API SOSMControlCenter : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SOSMControlCenter) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    virtual ~SOSMControlCenter() override;

    /**
     * Open the Control Center on a graph. Called by the wizard after a successful import, and
     * safe to call repeatedly — an already-open panel is reused and repointed.
     */
    static void OpenWithGraph(UOSMCityGraph* Graph, const FOSMRegion& Region, const FOSMGraphReport& Report);

    /** Register the tab spawner. Called once at module startup. */
    static void RegisterTabSpawner();

    static const FName TabId;

private:
    TStrongObjectPtr<UOSMCityGraph> Graph;
    FOSMRegion Region;
    FOSMGraphReport Report;

    /** Explorer tree state. */
    TArray<TSharedPtr<FOSMExplorerItem>> RootItems;
    TSharedPtr<STreeView<TSharedPtr<FOSMExplorerItem>>> ExplorerTree;
    FString SearchText;

    /** Currently inspected node, or INDEX_NONE. */
    int32 SelectedNodeId = INDEX_NONE;

    /** Draw relationship connector lines in the viewport overlay. */
    bool bShowRelationships = false;

    /**
     * Lift overlay geometry onto the DEM surface instead of drawing it flat.
     *
     * Loaded from the graph's source DEM when one is available. This is what makes the overlay
     * answer "does my elevation data line up with my city?" — a question that otherwise waits
     * until generation exists, which is exactly the kind of late discovery this pipeline was
     * rebuilt to avoid.
     */
    bool bDrapeOnTerrain = true;
    FOSMDEMSampler DEMSampler;
    bool bHasDEM = false;

    /** Lowest elevation in the DEM, so the drape sits near Z=0 rather than 900 m up. */
    double DEMBaseElevationMeters = 0.0;

    /** Elevation in cm above the base for a coordinate, or 0 when no DEM is loaded. */
    double GetDrapeHeightCm(const FVector2D& LatLon) const;

    FReply OnToggleDrape();

    /** Redraw handle so the overlay can be cleared without clearing everyone else's lines. */
    bool bOverlayActive = false;

    void SetGraph(UOSMCityGraph* InGraph, const FOSMRegion& InRegion, const FOSMGraphReport& InReport);

    // ---- Panels ----
    TSharedRef<SWidget> BuildHeader();
    TSharedRef<SWidget> BuildExplorerPanel();
    TSharedRef<SWidget> BuildInspectorPanel();
    TSharedRef<SWidget> BuildIssuesPanel();
    TSharedRef<SWidget> BuildAssetsPanel();

    // ---- Explorer ----
    void RebuildExplorer();
    TSharedRef<ITableRow> OnGenerateExplorerRow(TSharedPtr<FOSMExplorerItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
    void OnGetExplorerChildren(TSharedPtr<FOSMExplorerItem> Item, TArray<TSharedPtr<FOSMExplorerItem>>& OutChildren);
    void OnExplorerSelectionChanged(TSharedPtr<FOSMExplorerItem> Item, ESelectInfo::Type SelectInfo);

    bool IsNodeTypeVisible(EOSMNodeType NodeType) const;
    void ToggleNodeTypeVisibility(EOSMNodeType NodeType);

    // ---- Inspector ----
    void SelectNode(int32 NodeId);

    /** Attribute/tag/flag text for the selected node. */
    FText GetInspectorText() const;

    /** Clickable relationship rows, so a building can be walked to its street and onward. */
    TSharedRef<SWidget> BuildRelationshipList();

    /** Rebuilt whenever the selection changes; holds the relationship buttons. */
    TSharedPtr<class SVerticalBox> RelationshipBox;
    void RefreshRelationshipList();

    /** Editable asset rules; rebuilt when the graph changes. */
    TSharedPtr<class SVerticalBox> AssetRulesBox;
    void RefreshAssetRules();

    // ---- Viewport overlay ----
    void RefreshOverlay();
    void ClearOverlay();
    FReply OnToggleRelationships();

    // ---- Actions ----
    FReply OnSaveGraph();
    FReply OnRefreshOverlayClicked();
    FReply OnSetUpScene();
    FReply OnFrameRegion();

    /** Last scene-setup result, shown in the header so the action is not silent. */
    FString SceneSetupStatus;

    /** Node types present in the graph, in declaration order, for stable panel ordering. */
    TArray<EOSMNodeType> GetPresentNodeTypes() const;
};

// Copyright InviMind. All Rights Reserved.

#include "ControlCenter/SOSMControlCenter.h"
#include "Graph/FOSMGraphAssetIO.h"
#include "Scene/FOSMSceneSetup.h"
#include "Session/FOSMGraphSession.h"
#include "Generation/FOSMDryRunReport.h"
#include "Settings/UOSMWorldGenSettings.h"
#include "Elevation/FOSMGeoTIFFTile.h"
#include "Graph/UOSMCityGraph.h"
#include "DrawDebugHelpers.h"
#include "Editor.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STreeView.h"

#define LOCTEXT_NAMESPACE "OSMControlCenter"

const FName SOSMControlCenter::TabId = TEXT("OSMWorldGenControlCenter");

namespace
{
    /** Weak handle to the live panel, so OpenWithGraph can repoint an existing one. */
    TWeakPtr<SOSMControlCenter> GLiveControlCenter;

    /** Colour per node type for the viewport overlay. */
    FColor GetOverlayColour(EOSMNodeType Type)
    {
        switch (Type)
        {
        case EOSMNodeType::Building:       return FColor(230, 180, 100);
        case EOSMNodeType::RoadSegment:    return FColor(220, 220, 220);
        case EOSMNodeType::Junction:       return FColor(255, 90, 90);
        case EOSMNodeType::WaterBody:      return FColor(80, 150, 255);
        case EOSMNodeType::Waterway:       return FColor(60, 190, 255);
        case EOSMNodeType::VegetationArea: return FColor(90, 200, 90);
        case EOSMNodeType::LanduseZone:    return FColor(190, 160, 220);
        case EOSMNodeType::LeisureArea:    return FColor(150, 220, 150);
        case EOSMNodeType::Railway:        return FColor(160, 120, 80);
        case EOSMNodeType::Barrier:        return FColor(140, 140, 140);
        case EOSMNodeType::PowerLine:      return FColor(255, 230, 120);
        case EOSMNodeType::Amenity:        return FColor(255, 150, 200);
        default:                           return FColor(120, 120, 120);
        }
    }

    /** The editor world the overlay draws into. Null when no level is open. */
    UWorld* GetOverlayWorld()
    {
        return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    }
}

// ---------------------------------------------------------------------------
FString FOSMExplorerItem::GetDisplayText() const
{
    FString Text;

    switch (Kind)
    {
    case EKind::Category: Text = FString::Printf(TEXT("%s  (%d)"), *OSMNodeTypeToString(NodeType), Count); break;
    case EKind::SubType:  Text = FString::Printf(TEXT("%s  (%d)"), SubType.IsEmpty() ? TEXT("<untyped>") : *SubType, Count); break;
    default:              Text = FString::Printf(TEXT("#%d"), NodeId); break;
    }

    if (FlaggedCount > 0)
    {
        Text += FString::Printf(TEXT("   [%d flagged]"), FlaggedCount);
    }
    return Text;
}

// ---------------------------------------------------------------------------
void SOSMControlCenter::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SVerticalBox)

        + SVerticalBox::Slot().AutoHeight().Padding(8.0f)
        [
            BuildHeader()
        ]

        // Explorer | Inspector, with the lower panels stacked under the inspector. A splitter
        // rather than fixed widths because node names and tag values vary wildly in length.
        + SVerticalBox::Slot().FillHeight(1.0f).Padding(8.0f, 0.0f, 8.0f, 8.0f)
        [
            SNew(SSplitter)

            + SSplitter::Slot().Value(0.36f)
            [
                BuildExplorerPanel()
            ]

            + SSplitter::Slot().Value(0.64f)
            [
                SNew(SSplitter).Orientation(Orient_Vertical)

                + SSplitter::Slot().Value(0.55f)
                [
                    BuildInspectorPanel()
                ]

                + SSplitter::Slot().Value(0.25f)
                [
                    BuildAssetsPanel()
                ]

                + SSplitter::Slot().Value(0.20f)
                [
                    BuildIssuesPanel()
                ]
            ]
        ]
    ];

    // Reopening the panel restores whatever the session is holding, so closing this window is
    // no longer destructive — it used to discard the import, because the widget owned the only
    // reference to the graph.
    FOSMGraphSession& Session = FOSMGraphSession::Get();
    if (Session.HasGraph())
    {
        SetGraph(Session.GetGraph(), Session.GetRegion(), Session.GetReport());
    }
}

SOSMControlCenter::~SOSMControlCenter()
{
    // The overlay lives in the editor world, not in this widget, so it would otherwise outlive
    // the panel and leave lines floating with no way to clear them.
    ClearOverlay();
}

// ---------------------------------------------------------------------------
void SOSMControlCenter::SetGraph(UOSMCityGraph* InGraph, const FOSMRegion& InRegion, const FOSMGraphReport& InReport)
{
    ClearOverlay();

    // The panel still holds a strong pointer of its own, but it is no longer the only one — the
    // session keeps the graph alive independently of this widget's lifetime.
    Graph.Reset(InGraph);
    Region = InRegion;
    Report = InReport;
    SelectedNodeId = INDEX_NONE;

    // Load the elevation raster the graph was imported with, so the overlay can be draped onto
    // the real surface rather than drawn flat.
    bHasDEM = false;
    DEMBaseElevationMeters = 0.0;

    if (Graph.IsValid() && !Graph->SourceDEMFile.IsEmpty() && DEMSampler.Load(Graph->SourceDEMFile))
    {
        bHasDEM = true;

        const FOSMGeoTIFFTile& Tile = DEMSampler.GetTileMetadata();

        // Drawn relative to the lowest point rather than at absolute altitude: Bangalore sits
        // ~900 m above sea level, and an overlay 90,000 units above the origin would be off
        // screen for anyone who framed the region.
        DEMBaseElevationMeters = Tile.MinElevation;

        UE_LOG(LogTemp, Log,
            TEXT("Control Center: DEM loaded — %d x %d, elevation %.1f to %.1f m (relief %.1f m)"),
            Tile.Width, Tile.Height, Tile.MinElevation, Tile.MaxElevation,
            Tile.MaxElevation - Tile.MinElevation);
    }

    RebuildExplorer();
    RefreshRelationshipList();
    RefreshAssetRules();
    RefreshOverlay();
}

// ---------------------------------------------------------------------------
TSharedRef<SWidget> SOSMControlCenter::BuildHeader()
{
    return SNew(SBorder)
        .Padding(8.0f)
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text_Lambda([this]()
                {
                    if (!Graph.IsValid())
                    {
                        return LOCTEXT("NoGraph", "No city graph loaded. Run an import from the wizard.");
                    }
                    FString Elevation = TEXT("no DEM");
                    if (bHasDEM)
                    {
                        const FOSMGeoTIFFTile& Tile = DEMSampler.GetTileMetadata();
                        Elevation = FString::Printf(TEXT("elevation %.0f–%.0f m (%.0f m relief)"),
                            Tile.MinElevation, Tile.MaxElevation,
                            Tile.MaxElevation - Tile.MinElevation);
                    }

                    return FText::FromString(FString::Printf(
                        TEXT("Region %s  ·  %.2f km²  ·  %d nodes, %d relationships, %d groups  ·  %s"),
                        *Region.ToString(), Region.GetAreaSqKm(),
                        Graph->NumNodes(), Graph->NumEdges(), Graph->Groups.Num(), *Elevation));
                })
            ]

            + SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
            [
                SNew(SButton)
                .Text(LOCTEXT("LoadSaved", "Load Saved Graph"))
                .ToolTipText(LOCTEXT("LoadSavedTip",
                    "Reopen the last graph saved with Save Graph Asset.\n"
                    "Useful after restarting the editor, when the in-memory session is gone."))
                .Visibility_Lambda([this]()
                {
                    // Only offered when there is nothing loaded: with a graph on screen this
                    // button would be a way to lose your place, not a way to recover it.
                    return Graph.IsValid() ? EVisibility::Collapsed : EVisibility::Visible;
                })
                .OnClicked(this, &SOSMControlCenter::OnLoadSavedGraph)
            ]

            + SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
            [
                SNew(SButton)
                .Text(LOCTEXT("SetUpScene", "Set Up Scene"))
                .ToolTipText(LOCTEXT("SetUpSceneTip",
                    "Add a sun, sky light, atmosphere and fog to the level, then frame the region.\n"
                    "Creates ENVIRONMENT actors only — never buildings, roads or terrain. Reuses "
                    "anything the level already has instead of stacking duplicates."))
                .OnClicked(this, &SOSMControlCenter::OnSetUpScene)
            ]

            + SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
            [
                SNew(SButton)
                .Text(LOCTEXT("FrameRegion", "Frame Region"))
                .ToolTipText(LOCTEXT("FrameRegionTip",
                    "Point the viewport camera at the region. Touches no actors — the overlay draws "
                    "around the world origin, and this is usually why it looks like nothing rendered."))
                .OnClicked(this, &SOSMControlCenter::OnFrameRegion)
            ]

            + SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
            [
                SNew(SButton)
                .Text(LOCTEXT("RedrawOverlay", "Redraw Overlay"))
                .ToolTipText(LOCTEXT("RedrawOverlayTip",
                    "Redraw the graph in the level viewport. Debug lines only — no actors are created."))
                .OnClicked(this, &SOSMControlCenter::OnRefreshOverlayClicked)
            ]

            + SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
            [
                SNew(SButton)
                .Text_Lambda([this]()
                {
                    return bDrapeOnTerrain
                        ? LOCTEXT("DrapeOn", "Terrain: Draped")
                        : LOCTEXT("DrapeOff", "Terrain: Flat");
                })
                .ToolTipText(LOCTEXT("DrapeTip",
                    "Lift the overlay onto the DEM surface, or draw it flat.\n"
                    "Heights are shown relative to the lowest point in the raster."))
                .IsEnabled_Lambda([this]() { return bHasDEM; })
                .OnClicked(this, &SOSMControlCenter::OnToggleDrape)
            ]

            + SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
            [
                SNew(SButton)
                .Text_Lambda([this]()
                {
                    return bShowTerrain
                        ? LOCTEXT("TerrainOn", "Ground: Shown")
                        : LOCTEXT("TerrainOff", "Ground: Hidden");
                })
                .ToolTipText(LOCTEXT("TerrainTip",
                    "Draw the DEM surface as a ground grid beneath the city."))
                .IsEnabled_Lambda([this]() { return bHasDEM; })
                .OnClicked(this, &SOSMControlCenter::OnToggleTerrain)
            ]

            + SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
            [
                SNew(SButton)
                .Text(LOCTEXT("ToggleRelationships", "Toggle Relationship Lines"))
                .OnClicked(this, &SOSMControlCenter::OnToggleRelationships)
            ]

            + SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
            [
                SNew(SButton)
                .Text(LOCTEXT("DryRun", "Dry Run"))
                .ToolTipText(LOCTEXT("DryRunTip",
                    "Report exactly what generation would produce — counts, assets, ratios, "
                    "fallbacks and corridor grouping.\nCreates nothing."))
                .IsEnabled_Lambda([this]() { return Graph.IsValid(); })
                .OnClicked(this, &SOSMControlCenter::OnDryRun)
            ]

            + SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
            [
                SNew(SButton)
                .Text(LOCTEXT("SaveGraph", "Save Graph Asset"))
                .ToolTipText(LOCTEXT("SaveGraphTip",
                    "Save this graph and its configuration as a .uasset so the region can be reopened "
                    "without re-fetching."))
                .OnClicked(this, &SOSMControlCenter::OnSaveGraph)
            ]
        ];
}

// ---------------------------------------------------------------------------
FReply SOSMControlCenter::OnSetUpScene()
{
    const FOSMSceneSetup::FResult SetupResult = FOSMSceneSetup::SetUpScene(Region);
    SceneSetupStatus = SetupResult.ToString();

    UE_LOG(LogTemp, Log, TEXT("%s"), *SceneSetupStatus);

    // The overlay is cleared by level changes, so redraw once the environment exists.
    RefreshOverlay();
    return FReply::Handled();
}

FReply SOSMControlCenter::OnDryRun()
{
    if (!Graph.IsValid())
    {
        DryRunText = TEXT("No graph loaded.");
        return FReply::Handled();
    }

    const FOSMDryRunReport DryRun = FOSMDryRun::Run(*Graph);
    DryRunText = DryRun.ToDisplayString();

    // Logged as well as shown: the panel is scrollable but transient, and a dry run is the thing
    // most worth pasting into a conversation when something looks wrong.
    UE_LOG(LogTemp, Log, TEXT("OSM dry run:\n%s"), *DryRunText);

    return FReply::Handled();
}

FReply SOSMControlCenter::OnLoadSavedGraph()
{
    const UOSMWorldGenSettings* Settings = GetDefault<UOSMWorldGenSettings>();
    if (!Settings || Settings->LastSavedGraphPath.IsEmpty())
    {
        SceneSetupStatus = TEXT("No saved graph recorded yet — use Save Graph Asset first.");
        return FReply::Handled();
    }

    const FString ObjectPath = FString::Printf(TEXT("%s.%s"),
        *Settings->LastSavedGraphPath, *FPaths::GetCleanFilename(Settings->LastSavedGraphPath));

    UOSMCityGraph* Loaded = LoadObject<UOSMCityGraph>(nullptr, *ObjectPath);
    if (!Loaded)
    {
        SceneSetupStatus = FString::Printf(TEXT("Could not load '%s'."), *ObjectPath);
        UE_LOG(LogTemp, Warning, TEXT("%s"), *SceneSetupStatus);
        return FReply::Handled();
    }

    // The region is rebuilt from the bounds stored on the asset rather than from anything in the
    // UI, so a reloaded graph describes exactly the region it was built for.
    FOSMRegion LoadedRegion;
    FString Error;
    if (!FOSMRegion::FromBoundingBox(
            Loaded->RegionMinLat, Loaded->RegionMinLon,
            Loaded->RegionMaxLat, Loaded->RegionMaxLon, LoadedRegion, Error))
    {
        SceneSetupStatus = FString::Printf(TEXT("Saved graph has unusable bounds: %s"), *Error);
        return FReply::Handled();
    }

    // The graph report is not serialised with the asset — it describes one build run, not the
    // graph — so a reloaded graph starts with an empty issues panel rather than a stale one.
    SetGraph(Loaded, LoadedRegion, FOSMGraphReport());
    FOSMGraphSession::Get().Set(Loaded, LoadedRegion, FOSMGraphReport());

    SceneSetupStatus = FString::Printf(TEXT("Loaded %s"), *ObjectPath);
    return FReply::Handled();
}

FReply SOSMControlCenter::OnFrameRegion()
{
    if (!FOSMSceneSetup::FrameRegion(Region))
    {
        SceneSetupStatus = TEXT("Could not move the camera — no perspective viewport is open.");
    }
    else
    {
        SceneSetupStatus = TEXT("Camera framed on the region.");
    }
    return FReply::Handled();
}

// ---------------------------------------------------------------------------
TArray<EOSMNodeType> SOSMControlCenter::GetPresentNodeTypes() const
{
    TArray<EOSMNodeType> Types;
    if (!Graph.IsValid())
    {
        return Types;
    }

    // Declaration order, not discovery order: a panel whose rows move between runs is a panel
    // the user has to re-read every time.
    for (uint8 Index = 0; Index < static_cast<uint8>(EOSMNodeType::MAX); ++Index)
    {
        const EOSMNodeType Type = static_cast<EOSMNodeType>(Index);
        if (Graph->CountNodesOfType(Type) > 0)
        {
            Types.Add(Type);
        }
    }
    return Types;
}

// ---------------------------------------------------------------------------
void SOSMControlCenter::RebuildExplorer()
{
    RootItems.Reset();

    if (!Graph.IsValid())
    {
        if (ExplorerTree.IsValid()) ExplorerTree->RequestTreeRefresh();
        return;
    }

    const FString Filter = SearchText.TrimStartAndEnd();

    for (const EOSMNodeType Type : GetPresentNodeTypes())
    {
        TSharedPtr<FOSMExplorerItem> Category = MakeShared<FOSMExplorerItem>();
        Category->Kind = FOSMExplorerItem::EKind::Category;
        Category->NodeType = Type;

        // Group by subtype so "Buildings > residential (124)" is reachable without scrolling
        // past 124 individual rows.
        TMap<FString, TArray<int32>> BySubType;
        for (const int32 NodeId : Graph->GetNodesOfType(Type))
        {
            const FOSMGraphNode& Node = Graph->Nodes[NodeId];

            if (!Filter.IsEmpty())
            {
                const bool bMatches =
                    Node.SubType.Contains(Filter) ||
                    Node.Name.Contains(Filter) ||
                    OSMNodeTypeToString(Node.Type).Contains(Filter) ||
                    FString::FromInt(Node.Id) == Filter;

                if (!bMatches) continue;
            }

            BySubType.FindOrAdd(Node.SubType).Add(NodeId);
        }

        if (BySubType.Num() == 0) continue;

        TArray<FString> SubTypes;
        BySubType.GetKeys(SubTypes);
        SubTypes.Sort();

        for (const FString& SubType : SubTypes)
        {
            TSharedPtr<FOSMExplorerItem> SubItem = MakeShared<FOSMExplorerItem>();
            SubItem->Kind = FOSMExplorerItem::EKind::SubType;
            SubItem->NodeType = Type;
            SubItem->SubType = SubType;

            for (const int32 NodeId : BySubType[SubType])
            {
                TSharedPtr<FOSMExplorerItem> Instance = MakeShared<FOSMExplorerItem>();
                Instance->Kind = FOSMExplorerItem::EKind::Instance;
                Instance->NodeType = Type;
                Instance->SubType = SubType;
                Instance->NodeId = NodeId;
                Instance->Count = 1;
                Instance->FlaggedCount = Graph->Nodes[NodeId].IsFlagged() ? 1 : 0;

                SubItem->Children.Add(Instance);
                SubItem->FlaggedCount += Instance->FlaggedCount;
            }

            SubItem->Count = SubItem->Children.Num();
            Category->Children.Add(SubItem);
            Category->Count += SubItem->Count;
            Category->FlaggedCount += SubItem->FlaggedCount;
        }

        RootItems.Add(Category);
    }

    if (ExplorerTree.IsValid())
    {
        ExplorerTree->RequestTreeRefresh();
    }
}

// ---------------------------------------------------------------------------
TSharedRef<SWidget> SOSMControlCenter::BuildExplorerPanel()
{
    return SNew(SBorder)
        .Padding(6.0f)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
            [
                SNew(STextBlock).Text(LOCTEXT("ExplorerTitle", "NODE EXPLORER"))
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
            [
                SNew(SSearchBox)
                .HintText(LOCTEXT("SearchHint", "Search by subtype, name, or node id"))
                .OnTextChanged_Lambda([this](const FText& Text)
                {
                    SearchText = Text.ToString();
                    RebuildExplorer();
                })
            ]

            // Terrain sits above the OSM categories rather than inside the tree: it comes from
            // the DEM, not from any classified feature, and listing it as a peer of "Buildings"
            // would imply it is one. It is the ground everything else is snapped to, so it reads
            // first and it reads separately.
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [
                SNew(SBorder)
                .Padding(4.0f)
                [
                    SNew(SVerticalBox)

                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SHorizontalBox)

                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
                        [
                            SNew(SCheckBox)
                            .IsEnabled_Lambda([this]() { return bHasDEM; })
                            .IsChecked_Lambda([this]()
                            {
                                return bShowTerrain ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                            })
                            .OnCheckStateChanged_Lambda([this](ECheckBoxState)
                            {
                                OnToggleTerrain();
                            })
                        ]

                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("■")))
                            .ColorAndOpacity(FSlateColor(FLinearColor(FColor(96, 104, 84))))
                        ]

                        + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                        [
                            SNew(STextBlock)
                            .Text_Lambda([this]()
                            {
                                if (!bHasDEM)
                                {
                                    return LOCTEXT("TerrainNone", "Terrain  —  no DEM in this import");
                                }

                                const FOSMGeoTIFFTile& Tile = DEMSampler.GetTileMetadata();
                                return FText::FromString(FString::Printf(
                                    TEXT("Terrain (DEM)   %d x %d px"), Tile.Width, Tile.Height));
                            })
                        ]
                    ]

                    + SVerticalBox::Slot().AutoHeight().Padding(20, 2, 0, 0)
                    [
                        SNew(STextBlock)
                        .AutoWrapText(true)
                        .Text_Lambda([this]()
                        {
                            if (!bHasDEM)
                            {
                                return LOCTEXT("TerrainNoneHint",
                                    "Features will be drawn flat. Re-import with elevation to snap them to the ground.");
                            }

                            const FOSMGeoTIFFTile& Tile = DEMSampler.GetTileMetadata();
                            return FText::FromString(FString::Printf(
                                TEXT("elevation %.0f–%.0f m   ·   %.0f m relief   ·   ~%.0f m/px   ·   features %s"),
                                Tile.MinElevation, Tile.MaxElevation,
                                Tile.MaxElevation - Tile.MinElevation,
                                Tile.GetResolutionArcSeconds() / 3600.0 * 111320.0
                                    * FMath::Cos(FMath::DegreesToRadians(Region.IsValid() ? Region.GetCenterLat() : 0.0)),
                                bDrapeOnTerrain ? TEXT("snapped to it") : TEXT("drawn flat")));
                        })
                    ]
                ]
            ]

            + SVerticalBox::Slot().FillHeight(1.0f)
            [
                SAssignNew(ExplorerTree, STreeView<TSharedPtr<FOSMExplorerItem>>)
                .TreeItemsSource(&RootItems)
                .OnGenerateRow(this, &SOSMControlCenter::OnGenerateExplorerRow)
                .OnGetChildren(this, &SOSMControlCenter::OnGetExplorerChildren)
                .OnSelectionChanged(this, &SOSMControlCenter::OnExplorerSelectionChanged)
                .SelectionMode(ESelectionMode::Single)
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("VisibilityHint",
                    "Checkboxes control what the viewport overlay draws — untick a category to isolate the rest."))
                .AutoWrapText(true)
            ]
        ];
}

TSharedRef<ITableRow> SOSMControlCenter::OnGenerateExplorerRow(
    TSharedPtr<FOSMExplorerItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

    // Visibility toggles sit on category rows only: per-instance visibility would be a lot of
    // state for a question nobody asks ("hide this one building").
    if (Item->Kind == FOSMExplorerItem::EKind::Category)
    {
        const EOSMNodeType Type = Item->NodeType;

        Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this, Type]()
            {
                return IsNodeTypeVisible(Type) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([this, Type](ECheckBoxState)
            {
                ToggleNodeTypeVisibility(Type);
            })
        ];

        Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("■")))
            .ColorAndOpacity(FSlateColor(FLinearColor(GetOverlayColour(Type))))
        ];
    }

    Row->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center)
    [
        SNew(STextBlock)
        .Text(FText::FromString(Item->GetDisplayText()))
        .ColorAndOpacity(Item->FlaggedCount > 0
            ? FSlateColor(FLinearColor(1.0f, 0.75f, 0.4f))
            : FSlateColor::UseForeground())
    ];

    return SNew(STableRow<TSharedPtr<FOSMExplorerItem>>, OwnerTable)[ Row ];
}

void SOSMControlCenter::OnGetExplorerChildren(
    TSharedPtr<FOSMExplorerItem> Item, TArray<TSharedPtr<FOSMExplorerItem>>& OutChildren)
{
    if (Item.IsValid())
    {
        OutChildren = Item->Children;
    }
}

void SOSMControlCenter::OnExplorerSelectionChanged(TSharedPtr<FOSMExplorerItem> Item, ESelectInfo::Type)
{
    if (Item.IsValid() && Item->Kind == FOSMExplorerItem::EKind::Instance)
    {
        SelectNode(Item->NodeId);
    }
}

// ---------------------------------------------------------------------------
bool SOSMControlCenter::IsNodeTypeVisible(EOSMNodeType NodeType) const
{
    return Graph.IsValid() ? Graph->Config.IsNodeTypeVisible(NodeType) : true;
}

void SOSMControlCenter::ToggleNodeTypeVisibility(EOSMNodeType NodeType)
{
    if (!Graph.IsValid()) return;

    // Stored on the graph rather than in the widget, so isolating a category survives closing
    // the panel and is saved with the asset.
    Graph->Config.SetNodeTypeVisible(NodeType, !IsNodeTypeVisible(NodeType));
    RefreshOverlay();
}

// ---------------------------------------------------------------------------
void SOSMControlCenter::SelectNode(int32 NodeId)
{
    SelectedNodeId = NodeId;
    RefreshRelationshipList();
    RefreshOverlay();
}

TSharedRef<SWidget> SOSMControlCenter::BuildInspectorPanel()
{
    return SNew(SBorder)
        .Padding(6.0f)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
            [
                SNew(STextBlock).Text(LOCTEXT("InspectorTitle", "INSPECTOR"))
            ]

            + SVerticalBox::Slot().FillHeight(1.0f)
            [
                SNew(SScrollBox)

                + SScrollBox::Slot()
                [
                    SNew(STextBlock)
                    .AutoWrapText(true)
                    .Text(this, &SOSMControlCenter::GetInspectorText)
                ]

                + SScrollBox::Slot().Padding(0, 8, 0, 0)
                [
                    BuildRelationshipList()
                ]
            ]
        ];
}

// ---------------------------------------------------------------------------
FText SOSMControlCenter::GetInspectorText() const
{
    if (!Graph.IsValid())
    {
        return LOCTEXT("InspectorNoGraph", "No graph loaded.");
    }

    const FOSMGraphNode* Node = Graph->FindNode(SelectedNodeId);
    if (!Node)
    {
        return LOCTEXT("InspectorNoSelection",
            "Select a node in the explorer to inspect its attributes, tags, metrics, validation "
            "flags and relationships.");
    }

    TArray<FString> Lines;

    Lines.Add(FString::Printf(TEXT("%s #%d"), *OSMNodeTypeToString(Node->Type), Node->Id));
    if (!Node->Name.IsEmpty())
    {
        Lines.Add(FString::Printf(TEXT("Name:     %s"), *Node->Name));
    }
    Lines.Add(FString::Printf(TEXT("Subtype:  %s"),
        Node->SubType.IsEmpty() ? TEXT("<none>") : *Node->SubType));
    Lines.Add(FString::Printf(TEXT("OSM id:   %lld%s"),
        Node->OSMId, Node->OSMId == 0 ? TEXT("  (derived node)") : TEXT("")));

    Lines.Add(TEXT(""));
    Lines.Add(TEXT("Metrics"));
    Lines.Add(FString::Printf(TEXT("  centroid   %.6f, %.6f"),
        Node->Metrics.CentroidLatLon.X, Node->Metrics.CentroidLatLon.Y));
    if (Node->Metrics.AreaSqm > 0.0)      Lines.Add(FString::Printf(TEXT("  area       %.1f m2"), Node->Metrics.AreaSqm));
    if (Node->Metrics.LengthMeters > 0.0) Lines.Add(FString::Printf(TEXT("  length     %.1f m"), Node->Metrics.LengthMeters));
    if (Node->Metrics.HeightMeters > 0.0) Lines.Add(FString::Printf(TEXT("  height     %.1f m"), Node->Metrics.HeightMeters));
    if (Node->Metrics.WidthMeters > 0.0)  Lines.Add(FString::Printf(TEXT("  width      %.1f m"), Node->Metrics.WidthMeters));

    if (Node->IsFlagged())
    {
        Lines.Add(TEXT(""));
        Lines.Add(TEXT("Validation flags  (node kept, not dropped)"));
        for (const FString& Flag : Node->ValidationFlags)
        {
            Lines.Add(FString::Printf(TEXT("  %s"), *Flag));
        }
    }

    if (Node->Tags.Num() > 0)
    {
        Lines.Add(TEXT(""));
        Lines.Add(FString::Printf(TEXT("OSM tags (%d)"), Node->Tags.Num()));

        TArray<FString> Keys;
        Node->Tags.GetKeys(Keys);
        Keys.Sort();
        for (const FString& Key : Keys)
        {
            Lines.Add(FString::Printf(TEXT("  %s = %s"), *Key, *Node->Tags[Key]));
        }
    }

    return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

// ---------------------------------------------------------------------------
TSharedRef<SWidget> SOSMControlCenter::BuildRelationshipList()
{
    SAssignNew(RelationshipBox, SVerticalBox);
    RefreshRelationshipList();
    return RelationshipBox.ToSharedRef();
}

void SOSMControlCenter::RefreshRelationshipList()
{
    if (!RelationshipBox.IsValid())
    {
        return;
    }

    RelationshipBox->ClearChildren();

    const FOSMGraphNode* Node = Graph.IsValid() ? Graph->FindNode(SelectedNodeId) : nullptr;
    if (!Node)
    {
        return;
    }

    const TArray<int32> Outgoing = Graph->GetOutgoingEdges(Node->Id);
    const TArray<int32> Incoming = Graph->GetIncomingEdges(Node->Id);

    RelationshipBox->AddSlot().AutoHeight().Padding(0, 4)
    [
        SNew(STextBlock).Text(FText::FromString(FString::Printf(
            TEXT("Relationships (%d) — click to follow"), Outgoing.Num() + Incoming.Num())))
    ];

    // Each relationship is a button rather than a line of text, so the graph can actually be
    // walked: building -> its street -> that street's junctions -> the roads they join. Reading
    // a node id and typing it into a search box is not traversal.
    auto AddRow = [this](const FOSMGraphEdge& Edge, bool bOutgoing)
    {
        const int32 OtherId = bOutgoing ? Edge.ToNode : Edge.FromNode;
        const FOSMGraphNode* Other = Graph->FindNode(OtherId);
        if (!Other) return;

        FString Label = FString::Printf(TEXT("%s %s   %s #%d"),
            bOutgoing ? TEXT("->") : TEXT("<-"),
            *OSMRelationshipTypeToString(Edge.Type),
            *OSMNodeTypeToString(Other->Type), OtherId);

        if (!Other->Name.IsEmpty())
        {
            Label += FString::Printf(TEXT("  \"%s\""), *Other->Name);
        }
        if (Edge.IsSpatial())
        {
            // Derived edges show what they measured, so the user can judge them rather than
            // take them on faith.
            Label += FString::Printf(TEXT("   (%.1f m, cutoff %.0f m)"), Edge.Value, Edge.ToleranceMeters);
        }

        RelationshipBox->AddSlot().AutoHeight().Padding(0, 1)
        [
            SNew(SButton)
            .HAlign(HAlign_Left)
            .Text(FText::FromString(Label))
            .OnClicked_Lambda([this, OtherId]()
            {
                SelectNode(OtherId);
                return FReply::Handled();
            })
        ];
    };

    for (const int32 EdgeIndex : Outgoing) AddRow(Graph->Edges[EdgeIndex], /*bOutgoing*/ true);
    for (const int32 EdgeIndex : Incoming) AddRow(Graph->Edges[EdgeIndex], /*bOutgoing*/ false);
}

// ---------------------------------------------------------------------------
TSharedRef<SWidget> SOSMControlCenter::BuildAssetsPanel()
{
    return SNew(SBorder)
        .Padding(6.0f)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
            [
                SNew(STextBlock).Text(LOCTEXT("AssetsTitle",
                    "ASSETS & RATIOS   (configuration only — nothing is generated in this phase)"))
            ]

            + SVerticalBox::Slot().FillHeight(1.0f)
            [
                SNew(SScrollBox)
                + SScrollBox::Slot()
                [
                    SAssignNew(AssetRulesBox, SVerticalBox)
                ]
            ]
        ];
}

// ---------------------------------------------------------------------------
void SOSMControlCenter::RefreshAssetRules()
{
    if (!AssetRulesBox.IsValid())
    {
        return;
    }

    AssetRulesBox->ClearChildren();

    if (!Graph.IsValid())
    {
        AssetRulesBox->AddSlot().AutoHeight()
        [
            SNew(STextBlock).Text(LOCTEXT("AssetsNoGraph", "No graph loaded."))
        ];
        return;
    }

    for (const EOSMNodeType Type : GetPresentNodeTypes())
    {
        // Junctions are topology, not something an asset is placed on.
        if (Type == EOSMNodeType::Junction) continue;

        const int32 NodeCount = Graph->CountNodesOfType(Type);

        AssetRulesBox->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [
                SNew(STextBlock).Text(FText::FromString(FString::Printf(
                    TEXT("%s  (%d nodes)"), *OSMNodeTypeToString(Type), NodeCount)))
            ]

            + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
            [
                SNew(SButton)
                .Text(LOCTEXT("AddChoice", "+ Asset Slot"))
                .ToolTipText(LOCTEXT("AddChoiceTip",
                    "Add an asset slot for this category. Weights are relative; the percentages "
                    "shown are normalised from them."))
                .OnClicked_Lambda([this, Type]()
                {
                    FOSMAssetRule& Rule = Graph->Config.FindOrAddRule(Type, FString());
                    FOSMAssetChoice Choice;
                    Choice.Label = FString::Printf(TEXT("Slot %d"), Rule.Choices.Num() + 1);
                    Choice.Weight = 1.0f;
                    Rule.Choices.Add(Choice);
                    RefreshAssetRules();
                    return FReply::Handled();
                })
            ]
        ];

        const FOSMAssetRule* ExistingRule = Graph->Config.FindRule(Type, FString());
        if (!ExistingRule || ExistingRule->Choices.Num() == 0)
        {
            AssetRulesBox->AddSlot().AutoHeight().Padding(16, 0, 0, 2)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("NoAssets", "no assets assigned — generation will fall back"))
            ];
            continue;
        }

        // Per-corridor toggle, for roads only. This is the rule that stops one street
        // alternating mud and tarmac every ten metres.
        if (Type == EOSMNodeType::RoadSegment)
        {
            AssetRulesBox->AddSlot().AutoHeight().Padding(16, 0, 0, 2)
            [
                SNew(SCheckBox)
                .IsChecked_Lambda([this, Type]()
                {
                    const FOSMAssetRule* Rule = Graph->Config.FindRule(Type, FString());
                    return (Rule && Rule->bSelectPerCorridor) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                })
                .OnCheckStateChanged_Lambda([this, Type](ECheckBoxState NewState)
                {
                    Graph->Config.FindOrAddRule(Type, FString()).bSelectPerCorridor =
                        (NewState == ECheckBoxState::Checked);
                })
                [
                    SNew(STextBlock).Text(LOCTEXT("PerCorridor",
                        "Choose per corridor, so a street keeps one surface along its length"))
                ]
            ];
        }

        for (int32 ChoiceIndex = 0; ChoiceIndex < ExistingRule->Choices.Num(); ++ChoiceIndex)
        {
            AssetRulesBox->AddSlot().AutoHeight().Padding(16, 0, 0, 2)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
                [
                    SNew(SBox).WidthOverride(64.0f)
                    [
                        // Percentage is derived, never stored: showing it read-only keeps the
                        // weights independent, so editing one slot cannot silently rewrite the
                        // others to keep a total at 100.
                        SNew(STextBlock)
                        .Text_Lambda([this, Type, ChoiceIndex]()
                        {
                            const FOSMAssetRule* Rule = Graph->Config.FindRule(Type, FString());
                            const float Ratio = Rule ? Rule->GetNormalisedRatio(ChoiceIndex) : 0.0f;
                            return FText::FromString(FString::Printf(TEXT("%5.1f%%"), Ratio * 100.0f));
                        })
                    ]
                ]

                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
                [
                    SNew(SBox).WidthOverride(90.0f)
                    [
                        SNew(SSpinBox<float>)
                        .MinValue(0.0f)
                        .MaxValue(100.0f)
                        .Delta(0.1f)
                        .ToolTipText(LOCTEXT("WeightTip", "Relative weight, not a percentage."))
                        .Value_Lambda([this, Type, ChoiceIndex]()
                        {
                            const FOSMAssetRule* Rule = Graph->Config.FindRule(Type, FString());
                            return (Rule && Rule->Choices.IsValidIndex(ChoiceIndex))
                                ? Rule->Choices[ChoiceIndex].Weight : 0.0f;
                        })
                        .OnValueChanged_Lambda([this, Type, ChoiceIndex](float NewValue)
                        {
                            FOSMAssetRule& Rule = Graph->Config.FindOrAddRule(Type, FString());
                            if (Rule.Choices.IsValidIndex(ChoiceIndex))
                            {
                                Rule.Choices[ChoiceIndex].Weight = NewValue;
                            }
                        })
                    ]
                ]

                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                [
                    SNew(STextBlock).Text(FText::FromString(
                        ExistingRule->Choices[ChoiceIndex].Label.IsEmpty()
                            ? ExistingRule->Choices[ChoiceIndex].Asset.ToString()
                            : ExistingRule->Choices[ChoiceIndex].Label))
                ]

                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .Text(LOCTEXT("RemoveChoice", "x"))
                    .OnClicked_Lambda([this, Type, ChoiceIndex]()
                    {
                        FOSMAssetRule& Rule = Graph->Config.FindOrAddRule(Type, FString());
                        if (Rule.Choices.IsValidIndex(ChoiceIndex))
                        {
                            Rule.Choices.RemoveAt(ChoiceIndex);
                        }
                        RefreshAssetRules();
                        return FReply::Handled();
                    })
                ]
            ];
        }

        AssetRulesBox->AddSlot().AutoHeight().Padding(16, 2, 0, 4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
            [
                SNew(STextBlock).Text(LOCTEXT("SeedLabel", "seed"))
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SBox).WidthOverride(110.0f)
                [
                    // Per rule, not global: changing the buildings seed must not reshuffle the
                    // roads. An edit should only affect what it names.
                    SNew(SSpinBox<int32>)
                    .MinValue(0)
                    .Value_Lambda([this, Type]()
                    {
                        const FOSMAssetRule* Rule = Graph->Config.FindRule(Type, FString());
                        return Rule ? Rule->Seed : 0;
                    })
                    .OnValueChanged_Lambda([this, Type](int32 NewValue)
                    {
                        Graph->Config.FindOrAddRule(Type, FString()).Seed = NewValue;
                    })
                ]
            ]
        ];
    }
}

// ---------------------------------------------------------------------------
TSharedRef<SWidget> SOSMControlCenter::BuildIssuesPanel()
{
    return SNew(SBorder)
        .Padding(6.0f)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
            [
                SNew(STextBlock)
                .Text_Lambda([this]()
                {
                    const int32 Count = Report.Validation.Issues.Num();
                    return FText::FromString(FString::Printf(TEXT("ISSUES (%d)"), Count));
                })
            ]

            + SVerticalBox::Slot().FillHeight(1.0f)
            [
                SNew(SScrollBox)
                + SScrollBox::Slot()
                [
                    SNew(STextBlock)
                    .AutoWrapText(true)
                    .Text_Lambda([this]()
                    {
                        TArray<FString> Lines;

                        // Fatal first, then warnings: severity order is the only ordering that
                        // matches how the list gets read.
                        for (const EOSMIssueSeverity Severity :
                             { EOSMIssueSeverity::Fatal, EOSMIssueSeverity::Warning, EOSMIssueSeverity::Info })
                        {
                            for (const FOSMValidationIssue& Issue : Report.Validation.Issues)
                            {
                                if (Issue.Severity == Severity)
                                {
                                    Lines.Add(Issue.ToString());
                                }
                            }
                        }

                        if (Graph.IsValid())
                        {
                            const TArray<int32> Flagged = Graph->GetFlaggedNodes();
                            if (Flagged.Num() > 0)
                            {
                                Lines.Add(TEXT(""));
                                Lines.Add(FString::Printf(
                                    TEXT("%d flagged node(s) — kept, not dropped. Flagged rows are highlighted "
                                         "in the explorer."), Flagged.Num()));
                            }
                        }

                        // The dry run answers a different question from the issues list, but it
                        // is the thing you want to read right after configuring ratios, so it
                        // shares this panel rather than needing another one.
                        if (!DryRunText.IsEmpty())
                        {
                            Lines.Add(TEXT(""));
                            Lines.Add(TEXT("────────────────────────────────────────"));
                            Lines.Add(DryRunText);
                        }

                        return Lines.Num() > 0
                            ? FText::FromString(FString::Join(Lines, TEXT("\n")))
                            : LOCTEXT("NoIssues", "No issues reported.");
                    })
                ]
            ]
        ];
}

// ---------------------------------------------------------------------------
void SOSMControlCenter::ClearOverlay()
{
    if (UWorld* World = GetOverlayWorld())
    {
        FlushPersistentDebugLines(World);
    }
    bOverlayActive = false;
}

void SOSMControlCenter::RefreshOverlay()
{
    UWorld* World = GetOverlayWorld();
    if (!World || !Graph.IsValid())
    {
        return;
    }

    FlushPersistentDebugLines(World);
    bOverlayActive = true;

    // Debug lines, not meshes or actors. This is the honest picture of the graph: if a footprint
    // is wrong here, it is wrong in the data, not in some later generation step.
    //
    // Lat/lon is projected to local metres about the region centre, then scaled to Unreal
    // centimetres, so the overlay lines up with where geometry would eventually be built without
    // committing the graph itself to any projection.
    const double CentreLat = Region.IsValid() ? Region.GetCenterLat() : Graph->RegionMinLat;
    const double CentreLon = Region.IsValid() ? Region.GetCenterLon() : Graph->RegionMinLon;
    const double MetersPerDegLat = 111320.0;
    const double MetersPerDegLon = MetersPerDegLat * FMath::Cos(FMath::DegreesToRadians(CentreLat));

    // Z is the sampled ground height plus a small per-category offset. The offset alone is what
    // made everything look flat: it encodes the node's TYPE, not its elevation.
    auto ToWorld = [&](const FVector2D& LatLon, double ZOffset)
    {
        return FVector(
            (LatLon.X - CentreLat) * MetersPerDegLat * 100.0,
            (LatLon.Y - CentreLon) * MetersPerDegLon * 100.0,
            GetDrapeHeightCm(LatLon) + ZOffset);
    };

    // Ground first, city on top — both so the draw order matches how the scene reads, and so
    // the terrain is visible as the thing everything else is snapped to.
    DrawTerrainGrid(World, ToWorld);

    for (const FOSMGraphNode& Node : Graph->Nodes)
    {
        if (!IsNodeTypeVisible(Node.Type)) continue;

        const bool bSelected = (Node.Id == SelectedNodeId);
        const FColor Colour = bSelected ? FColor::Yellow : GetOverlayColour(Node.Type);
        const float Thickness = bSelected ? 12.0f : 4.0f;

        // Slight Z separation per category so overlapping features stay distinguishable, and
        // the selected node draws above everything.
        // Lifted clear of the terrain grid so outlines are not z-fighting the ground, with a
        // small per-category separation on top so overlapping features stay distinguishable.
        const double ZOffset = bSelected ? 800.0 : 150.0 + 20.0 * static_cast<double>(Node.Type);

        if (Node.Type == EOSMNodeType::Junction || !Node.HasGeometry())
        {
            DrawDebugPoint(World, ToWorld(Node.Metrics.CentroidLatLon, ZOffset + 50.0),
                bSelected ? 20.0f : 10.0f, Colour, /*bPersistent*/ true, /*LifeTime*/ -1.0f);
            continue;
        }

        const bool bIsArea = Graph->Geometry.IsArea(Node.Geometry);
        const int32 RingCount = Graph->Geometry.GetRingCount(Node.Geometry);

        for (int32 RingIndex = 0; RingIndex < RingCount; ++RingIndex)
        {
            const TArrayView<const FVector2D> Ring = Graph->Geometry.GetRing(Node.Geometry, RingIndex);
            if (Ring.Num() < 2) continue;

            const int32 SegmentCount = bIsArea ? Ring.Num() : Ring.Num() - 1;
            for (int32 Index = 0; Index < SegmentCount; ++Index)
            {
                DrawDebugLine(World,
                    ToWorld(Ring[Index], ZOffset),
                    ToWorld(Ring[(Index + 1) % Ring.Num()], ZOffset),
                    Colour, /*bPersistent*/ true, /*LifeTime*/ -1.0f, /*DepthPriority*/ 0, Thickness);
            }
        }
    }

    if (bShowRelationships)
    {
        for (const FOSMGraphEdge& Edge : Graph->Edges)
        {
            const FOSMGraphNode* From = Graph->FindNode(Edge.FromNode);
            const FOSMGraphNode* To = Graph->FindNode(Edge.ToNode);
            if (!From || !To) continue;
            if (!IsNodeTypeVisible(From->Type) || !IsNodeTypeVisible(To->Type)) continue;

            // Derived edges drawn differently from exact ones, so a questionable spatial link is
            // never mistaken for a fact read out of OSM.
            const FColor EdgeColour = Edge.IsSpatial() ? FColor(255, 120, 255) : FColor(120, 255, 255);

            DrawDebugLine(World,
                ToWorld(From->Metrics.CentroidLatLon, 500.0),
                ToWorld(To->Metrics.CentroidLatLon, 500.0),
                EdgeColour, /*bPersistent*/ true, /*LifeTime*/ -1.0f, /*DepthPriority*/ 0, 1.5f);
        }
    }
}

FReply SOSMControlCenter::OnRefreshOverlayClicked()
{
    RefreshOverlay();
    return FReply::Handled();
}

double SOSMControlCenter::GetDrapeHeightCm(const FVector2D& LatLon) const
{
    if (!bHasDEM || !bDrapeOnTerrain)
    {
        return 0.0;
    }

    const double Elevation = DEMSampler.SampleElevation(LatLon.X, LatLon.Y);

    // NaN means the coordinate falls outside the raster or on a NoData hole. Falling back to the
    // base rather than skipping the point keeps the outline closed — a footprint with one vertex
    // silently dropped would look like a geometry bug rather than a data gap.
    if (FMath::IsNaN(Elevation))
    {
        return 0.0;
    }

    return (Elevation - DEMBaseElevationMeters) * 100.0;
}

FReply SOSMControlCenter::OnToggleDrape()
{
    bDrapeOnTerrain = !bDrapeOnTerrain;
    RefreshOverlay();
    return FReply::Handled();
}

FReply SOSMControlCenter::OnToggleTerrain()
{
    bShowTerrain = !bShowTerrain;
    RefreshOverlay();
    return FReply::Handled();
}

// ---------------------------------------------------------------------------
void SOSMControlCenter::DrawTerrainGrid(UWorld* World, const TFunction<FVector(const FVector2D&, double)>& ToWorld)
{
    if (!World || !bHasDEM || !bShowTerrain)
    {
        return;
    }

    const FOSMGeoTIFFTile& Tile = DEMSampler.GetTileMetadata();
    if (Tile.Width < 2 || Tile.Height < 2)
    {
        return;
    }

    // Muted olive-grey. The ground must read as ground: a saturated colour here would compete
    // with the feature palette, and the point of drawing it is to make the CITY legible against
    // a surface, not to look at the surface.
    const FColor TerrainColour(96, 104, 84);

    // A dense raster would emit tens of thousands of segments and bury the city in wireframe.
    // Capping the drawn resolution keeps the surface readable at any DEM size; the sampling
    // underneath the city is unaffected, since that reads the raster directly.
    constexpr int32 MaxGridLines = 64;
    const int32 StepX = FMath::Max(1, Tile.Width / MaxGridLines);
    const int32 StepY = FMath::Max(1, Tile.Height / MaxGridLines);

    auto SampleAt = [&](int32 Col, int32 Row)
    {
        const double Lat = Tile.PixelToLat(static_cast<double>(Row));
        const double Lon = Tile.PixelToLon(static_cast<double>(Col));
        return ToWorld(FVector2D(Lat, Lon), 0.0);
    };

    // Rows then columns, so the grid reads as a surface rather than a set of contour lines.
    for (int32 Row = 0; Row < Tile.Height; Row += StepY)
    {
        for (int32 Col = 0; Col + StepX < Tile.Width; Col += StepX)
        {
            DrawDebugLine(World, SampleAt(Col, Row), SampleAt(Col + StepX, Row),
                TerrainColour, /*bPersistent*/ true, /*LifeTime*/ -1.0f, /*DepthPriority*/ 0, 1.0f);
        }
    }

    for (int32 Col = 0; Col < Tile.Width; Col += StepX)
    {
        for (int32 Row = 0; Row + StepY < Tile.Height; Row += StepY)
        {
            DrawDebugLine(World, SampleAt(Col, Row), SampleAt(Col, Row + StepY),
                TerrainColour, /*bPersistent*/ true, /*LifeTime*/ -1.0f, /*DepthPriority*/ 0, 1.0f);
        }
    }
}

FReply SOSMControlCenter::OnToggleRelationships()
{
    bShowRelationships = !bShowRelationships;
    RefreshOverlay();
    return FReply::Handled();
}

// ---------------------------------------------------------------------------
FReply SOSMControlCenter::OnSaveGraph()
{
    if (!Graph.IsValid())
    {
        return FReply::Handled();
    }

    FString Error;
    if (UOSMCityGraph* Saved = FOSMGraphAssetIO::SaveGraph(Graph.Get(), Region, Error))
    {
        UE_LOG(LogTemp, Log, TEXT("City graph saved to %s"), *FOSMGraphAssetIO::MakePackagePath(Region));

        // Point at the saved copy, so subsequent configuration edits land in the asset the user
        // just created rather than in a transient object that quietly diverges from it. The
        // session is updated too, or reopening the panel would resurrect the transient one.
        Graph.Reset(Saved);
        FOSMGraphSession::Get().Set(Saved, Region, Report);

        // Remembered so the panel can offer this graph back after an editor restart, when the
        // in-memory session is gone.
        if (UOSMWorldGenSettings* Settings = GetMutableDefault<UOSMWorldGenSettings>())
        {
            Settings->LastSavedGraphPath = FOSMGraphAssetIO::MakePackagePath(Region);
            Settings->SaveConfig();
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("City graph save failed: %s"), *Error);
    }

    return FReply::Handled();
}

// ---------------------------------------------------------------------------
void SOSMControlCenter::RegisterTabSpawner()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabId,
        FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
        {
            TSharedRef<SOSMControlCenter> Panel = SNew(SOSMControlCenter);
            GLiveControlCenter = Panel;

            return SNew(SDockTab)
                .TabRole(ETabRole::NomadTab)
                .Label(LOCTEXT("ControlCenterTab", "OSM Control Center"))
                [
                    Panel
                ];
        }))
        .SetDisplayName(LOCTEXT("ControlCenterMenu", "OSM Control Center"))
        .SetMenuType(ETabSpawnerMenuType::Hidden);
}

void SOSMControlCenter::OpenWithGraph(UOSMCityGraph* InGraph, const FOSMRegion& InRegion, const FOSMGraphReport& InReport)
{
    // Recorded BEFORE the tab is invoked: spawning the panel constructs it, and construction
    // restores from the session, so the session has to be current by then.
    FOSMGraphSession::Get().Set(InGraph, InRegion, InReport);

    FGlobalTabmanager::Get()->TryInvokeTab(TabId);

    // Repoint the existing panel rather than spawning a second one: two Control Centers drawing
    // into the same world would fight over the overlay.
    if (TSharedPtr<SOSMControlCenter> Panel = GLiveControlCenter.Pin())
    {
        Panel->SetGraph(InGraph, InRegion, InReport);
    }
}

#undef LOCTEXT_NAMESPACE

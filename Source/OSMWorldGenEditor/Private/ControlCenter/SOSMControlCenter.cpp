// Copyright InviMind. All Rights Reserved.

#include "ControlCenter/SOSMControlCenter.h"
#include "Graph/FOSMGraphAssetIO.h"
#include "Scene/FOSMSceneSetup.h"
#include "Session/FOSMGraphSession.h"
#include "Generation/FOSMDryRunReport.h"
#include "Generation/FOSMWorldBuilder.h"
#include "Engine/StaticMesh.h"
#include "Generation/UOSMBuildingArchetype.h"
#include "PropertyCustomizationHelpers.h"
#include "Settings/UOSMWorldGenSettings.h"
#include "Elevation/FOSMGeoTIFFTile.h"
#include "Graph/UOSMCityGraph.h"
#include "DrawDebugHelpers.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "LevelEditorViewport.h"
#include "ProceduralMeshComponent.h"
#include "Materials/FOSMThermalMaterialBuilderV2.h"
#include "Sensors/AOSMInfraredCamera.h"
#include "Sensors/UOSMThermalMPC.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Input/SSlider.h"
#include "Sensors/FOSMThermalSimulation.h"
#include "Sensors/FOSMThermalPresets.h"
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

        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SScrollBox)
            .Orientation(Orient_Horizontal)
            + SScrollBox::Slot()
            [
                BuildHeader()
            ]
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(8.0f, 8.0f, 8.0f, 0.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
            [
                SNew(SButton)
                .Text(LOCTEXT("TabCityExplorer", "City Explorer"))
                .OnClicked_Lambda([this]() { ActiveTabIndex = 0; return FReply::Handled(); })
                .IsEnabled_Lambda([this]() { return ActiveTabIndex != 0; })
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
            [
                SNew(SButton)
                .Text(LOCTEXT("TabEnvironment", "Environment & Thermal"))
                .OnClicked_Lambda([this]() { ActiveTabIndex = 1; return FReply::Handled(); })
                .IsEnabled_Lambda([this]() { return ActiveTabIndex != 1; })
            ]
        ]

        + SVerticalBox::Slot().FillHeight(1.0f).Padding(8.0f, 8.0f, 8.0f, 8.0f)
        [
            SNew(SWidgetSwitcher)
            .WidgetIndex_Lambda([this]() { return ActiveTabIndex; })
            
            // Tab 0: City Explorer
            + SWidgetSwitcher::Slot()
            [
                BuildCityExplorerTab()
            ]

            // Tab 1: Environment
            + SWidgetSwitcher::Slot()
            [
                BuildEnvironmentTab()
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
            SNew(SScrollBox)
            .Orientation(Orient_Horizontal)
            + SScrollBox::Slot()
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
                .Text(LOCTEXT("BuildCity", "Build City"))
                .ToolTipText(LOCTEXT("BuildCityTip",
                    "Generate the city as flat-coloured geometry: grey buildings, dark roads, green "
                    "vegetation, blue water.\nReplaces anything a previous build created."))
                .IsEnabled_Lambda([this]() { return Graph.IsValid(); })
                .OnClicked(this, &SOSMControlCenter::OnBuildCity)
            ]

            + SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
            [
                SNew(SButton)
                .Text(LOCTEXT("ClearCity", "Clear City"))
                .ToolTipText(LOCTEXT("ClearCityTip",
                    "Remove every actor the builder created. Nothing else in the level is touched."))
                .OnClicked(this, &SOSMControlCenter::OnClearCity)
            ]

            + SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
            [
                SNew(SButton)
                .Text(LOCTEXT("SpawnIRCam", "Spawn IR Camera"))
                .ToolTipText(LOCTEXT("SpawnIRCamTip",
                    "Spawn the OSM Infrared Thermal Camera and pilot it in full-screen dynamic view."))
                .OnClicked(this, &SOSMControlCenter::OnSpawnIRCamera)
            ]

            + SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
            [
                SNew(SButton)
                .Text(LOCTEXT("EjectIRCam", "Return to Perspective"))
                .ToolTipText(LOCTEXT("EjectIRCamTip",
                    "Exit IR Camera piloting and return to the normal editor perspective camera."))
                .OnClicked(this, &SOSMControlCenter::OnEjectIRCamera)
            ]

            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f)
            [
                SNew(SButton)
                .Text(LOCTEXT("PalIronbow", "Ironbow"))
                .ToolTipText(LOCTEXT("PalIronbowTip", "Switch thermal camera to Ironbow false-colour palette (Purple -> Magenta -> Orange -> Yellow -> White)."))
                .OnClicked(this, &SOSMControlCenter::OnSetPaletteIronbow)
            ]

            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f)
            [
                SNew(SButton)
                .Text(LOCTEXT("PalWhiteHot", "White-Hot"))
                .ToolTipText(LOCTEXT("PalWhiteHotTip", "Switch thermal camera to White-Hot FLIR palette (Cold = Black, Hot = Glowing White)."))
                .OnClicked(this, &SOSMControlCenter::OnSetPaletteWhiteHot)
            ]

            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f)
            [
                SNew(SButton)
                .Text(LOCTEXT("PalBlackHot", "Black-Hot"))
                .ToolTipText(LOCTEXT("PalBlackHotTip", "Switch thermal camera to Black-Hot FLIR palette (Cold = White, Hot = Black)."))
                .OnClicked(this, &SOSMControlCenter::OnSetPaletteBlackHot)
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

FReply SOSMControlCenter::OnBuildCity()
{
    if (!Graph.IsValid())
    {
        DryRunText = TEXT("No graph loaded.");
        return FReply::Handled();
    }

    FOSMWorldBuilder::FOptions Options;
    Options.bUseTerrain = bHasDEM;
    Options.bSpawnIRCamera = false;

    const FOSMWorldBuilder::FResult BuildResult = FOSMWorldBuilder::Build(*Graph, Region, Options);
    DryRunText = BuildResult.ToString();

    UE_LOG(LogTemp, Log, TEXT("OSM build: %s"), *DryRunText);

    // The debug overlay would sit inside the geometry it describes, so it is cleared once real
    // meshes exist. Redraw Overlay brings it back when wanted.
    ClearOverlay();

    return FReply::Handled();
}

FReply SOSMControlCenter::OnClearCity()
{
    const int32 Removed = FOSMWorldBuilder::Clear();
    DryRunText = FString::Printf(TEXT("Removed %d generated actor(s)."), Removed);
    return FReply::Handled();
}

FReply SOSMControlCenter::OnSpawnIRCamera()
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        DryRunText = TEXT("Cannot spawn IR Camera: No editor world active.");
        return FReply::Handled();
    }

    // 1. Ensure thermal post process material asset is generated and loaded with latest shader (force rebuild so updates apply)
    UMaterialInterface* ThermalMat = FOSMThermalMaterialBuilderV2::GetOrCreateThermalPostProcessMaterial(/*bForceRebuild=*/ true);

    // 2. Determine viewpoint from the user's active editor camera perspective
    FVector CameraLocation(0.0, -10000.0, 15000.0);
    FRotator CameraRotation(-35.0f, 90.0f, 0.0f);

    FLevelEditorViewportClient* ActiveViewportClient = nullptr;
    if (GEditor && GEditor->GetActiveViewport())
    {
        ActiveViewportClient = static_cast<FLevelEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient());
        if (ActiveViewportClient)
        {
            CameraLocation = ActiveViewportClient->GetViewTransform().GetLocation();
            CameraRotation = ActiveViewportClient->GetViewTransform().GetRotation();
        }
    }

    // 3. Clean up any stale IR camera so components are freshly initialized
    for (TActorIterator<AOSMInfraredCamera> It(World); It; ++It)
    {
        World->DestroyActor(*It);
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.ObjectFlags = RF_Transactional;

    AOSMInfraredCamera* TargetCam = World->SpawnActor<AOSMInfraredCamera>(
        AOSMInfraredCamera::StaticClass(), CameraLocation, CameraRotation, SpawnParams);

    if (TargetCam)
    {
        TargetCam->SetActorLabel(TEXT("OSM_InfraredCamera"));
        TargetCam->Tags.AddUnique(FOSMWorldBuilder::GetGeneratedActorTag());

        if (ThermalMat)
        {
            TargetCam->ThermalPostProcessMaterial = ThermalMat;
            TargetCam->SetupPostProcess();
        }

        // Apply UB City static baked thermal preset for photorealistic IR demo
        const FOSMThermalPreset& Preset = FOSMThermalPreset::UBCity_Afternoon();
        TargetCam->bUseStaticBakedThermal = true;
        TargetCam->AmbientTemperatureK = Preset.AmbientTempK;
        TargetCam->GroundLevelZ = Preset.GroundLevelZ;
        TargetCam->BuildingMaxHeightCm = Preset.BuildingMaxHeightCm;
        TargetCam->bEnableFLIRGhosting = true;
        TargetCam->SetHeatSensitivity(CurrentHeatSensitivity);
        TargetCam->UpdateCategoryTemperaturesOnMID(World);
        TargetCam->UpdateMaterialParameters();

        // Pilot the camera in the active editor viewport (full-screen, dynamic 60 FPS flight)
        if (ActiveViewportClient)
        {
            ActiveViewportClient->SetActorLock(TargetCam);
            ActiveViewportClient->bLockedCameraView = true;
            ActiveViewportClient->UpdateViewForLockedActor();
            ActiveViewportClient->Invalidate();
        }

        if (GEditor)
        {
            GEditor->SelectNone(true, true);
            GEditor->NoteSelectionChange();
            GEditor->RedrawAllViewports();
        }

        DryRunText = TEXT("Piloting OSM Infrared Camera! Use WASD + Right Mouse Button to fly around in full-screen dynamic thermal vision.\nClick 'Return to Perspective' or click the top-left viewport Eject icon to exit.");
    }

    return FReply::Handled();
}

FReply SOSMControlCenter::OnEjectIRCamera()
{
    if (GEditor && GEditor->GetActiveViewport())
    {
        if (FLevelEditorViewportClient* ActiveViewportClient = static_cast<FLevelEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient()))
        {
            ActiveViewportClient->SetActorLock(nullptr);
            ActiveViewportClient->bLockedCameraView = false;
            ActiveViewportClient->Invalidate();
        }
    }

    DryRunText = TEXT("Returned to Perspective camera. IR Camera remains in the world.");
    return FReply::Handled();
}

FReply SOSMControlCenter::OnSetPaletteIronbow()
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (World)
    {
        UMaterialInterface* ThermalMat = FOSMThermalMaterialBuilderV2::GetOrCreateThermalPostProcessMaterial(/*bForceRebuild=*/ true);
        int32 Count = 0;
        for (TActorIterator<AOSMInfraredCamera> It(World); It; ++It)
        {
            if (ThermalMat)
            {
                (*It)->ThermalPostProcessMaterial = ThermalMat;
                (*It)->SetupPostProcess();
            }
            (*It)->SetThermalPalette(EOSMThermalPalette::Ironbow);
            ++Count;
        }

        if (Count == 0)
        {
            OnSpawnIRCamera();
            for (TActorIterator<AOSMInfraredCamera> It(World); It; ++It)
            {
                (*It)->SetThermalPalette(EOSMThermalPalette::Ironbow);
            }
        }

        if (GEditor)
        {
            GEditor->RedrawAllViewports();
        }
        DryRunText = TEXT("Thermal camera switched to Ironbow (Vibrant False-Color spectrum).");
    }
    return FReply::Handled();
}

FReply SOSMControlCenter::OnSetPaletteWhiteHot()
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (World)
    {
        UMaterialInterface* ThermalMat = FOSMThermalMaterialBuilderV2::GetOrCreateThermalPostProcessMaterial(/*bForceRebuild=*/ true);
        int32 Count = 0;
        for (TActorIterator<AOSMInfraredCamera> It(World); It; ++It)
        {
            if (ThermalMat)
            {
                (*It)->ThermalPostProcessMaterial = ThermalMat;
                (*It)->SetupPostProcess();
            }
            (*It)->SetThermalPalette(EOSMThermalPalette::WhiteHot);
            ++Count;
        }

        if (Count == 0)
        {
            OnSpawnIRCamera();
            for (TActorIterator<AOSMInfraredCamera> It(World); It; ++It)
            {
                (*It)->SetThermalPalette(EOSMThermalPalette::WhiteHot);
            }
        }

        if (GEditor)
        {
            GEditor->RedrawAllViewports();
        }
        DryRunText = TEXT("Thermal camera switched to White-Hot FLIR (Deep Black to Glowing White).");
    }
    return FReply::Handled();
}

FReply SOSMControlCenter::OnSetPaletteBlackHot()
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (World)
    {
        UMaterialInterface* ThermalMat = FOSMThermalMaterialBuilderV2::GetOrCreateThermalPostProcessMaterial(/*bForceRebuild=*/ true);
        int32 Count = 0;
        for (TActorIterator<AOSMInfraredCamera> It(World); It; ++It)
        {
            if (ThermalMat)
            {
                (*It)->ThermalPostProcessMaterial = ThermalMat;
                (*It)->SetupPostProcess();
            }
            (*It)->SetThermalPalette(EOSMThermalPalette::BlackHot);
            ++Count;
        }

        if (Count == 0)
        {
            OnSpawnIRCamera();
            for (TActorIterator<AOSMInfraredCamera> It(World); It; ++It)
            {
                (*It)->SetThermalPalette(EOSMThermalPalette::BlackHot);
            }
        }

        if (GEditor)
        {
            GEditor->RedrawAllViewports();
        }
        DryRunText = TEXT("Thermal camera switched to Black-Hot FLIR (Inverted).");
    }
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
namespace
{
    /**
     * What an asset slot for this node type may point at.
     *
     * Buildings and roads take a STYLE — an archetype recipe applied to the feature's own
     * footprint — because a mesh cannot fit footprints spanning 19.5 to 7,814 m2. Scatter and
     * props take a real mesh, since they have no footprint to conform to.
     */
    UClass* GetAllowedAssetClass(EOSMNodeType Type)
    {
        return (Type == EOSMNodeType::Building)
            ? UOSMBuildingArchetype::StaticClass()
            : UStaticMesh::StaticClass();
    }

    const TCHAR* GetSlotNoun(EOSMNodeType Type)
    {
        return (Type == EOSMNodeType::Building) ? TEXT("+ Archetype") : TEXT("+ Asset Slot");
    }
}

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
        // Junctions are topology and terrain comes from the DEM — neither is something an asset
        // is placed on.
        if (Type == EOSMNodeType::Junction || Type == EOSMNodeType::TerrainTile) continue;

        const int32 NodeCount = Graph->CountNodesOfType(Type);

        // Subtypes present for this type, with counts. Rules are configured per subtype so
        // "commercial" can carry a different archetype library from "apartments" — the whole
        // point of archetypes is that variety is scoped to a kind of building.
        TMap<FString, int32> SubTypeCounts;
        for (const int32 NodeId : Graph->GetNodesOfType(Type))
        {
            ++SubTypeCounts.FindOrAdd(Graph->Nodes[NodeId].SubType);
        }

        TArray<FString> SubTypes;
        SubTypeCounts.GetKeys(SubTypes);

        // Most common first: with 75% of buildings untyped, the dominant bucket is the one worth
        // configuring, and burying it under alphabetical order hides that.
        SubTypes.Sort([&SubTypeCounts](const FString& A, const FString& B)
        {
            return SubTypeCounts[A] > SubTypeCounts[B];
        });

        // ---- Category header ----
        AssetRulesBox->AddSlot().AutoHeight().Padding(0, 10, 0, 2)
        [
            SNew(STextBlock).Text(FText::FromString(FString::Printf(
                TEXT("%s   —   %d node(s), %d subtype(s)"),
                *OSMNodeTypeToString(Type), NodeCount, SubTypes.Num())))
        ];

        for (const FString& SubType : SubTypes)
        {
            const int32 Count = SubTypeCounts[SubType];
            const FString Label = SubType.IsEmpty() ? TEXT("<untyped>") : SubType;

            // ---- Subtype row ----
            AssetRulesBox->AddSlot().AutoHeight().Padding(14, 4, 0, 2)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                [
                    SNew(STextBlock).Text(FText::FromString(FString::Printf(
                        TEXT("%s  (%d)"), *Label, Count)))
                ]

                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                [
                    SNew(SButton)
                    .Text(FText::FromString(GetSlotNoun(Type)))
                    .ToolTipText(LOCTEXT("AddSlotTip",
                        "Add a slot for this subtype. Weights are relative; the percentages shown "
                        "are normalised from them."))
                    .OnClicked_Lambda([this, Type, SubType]()
                    {
                        FOSMAssetRule& Rule = Graph->Config.FindOrAddRule(Type, SubType);
                        FOSMAssetChoice Choice;
                        Choice.Label = FString::Printf(TEXT("Slot %d"), Rule.Choices.Num() + 1);
                        Choice.Weight = 1.0f;
                        Rule.Choices.Add(Choice);
                        RefreshAssetRules();
                        return FReply::Handled();
                    })
                ]
            ];

            // FindRule falls back to the type-wide rule, which would make an unconfigured subtype
            // look configured. The exact-match check keeps each row honest about its own state.
            const FOSMAssetRule* ExactRule = nullptr;
            for (const FOSMAssetRule& Candidate : Graph->Config.Rules)
            {
                if (Candidate.NodeType == Type && Candidate.SubType == SubType)
                {
                    ExactRule = &Candidate;
                    break;
                }
            }

            if (!ExactRule || ExactRule->Choices.Num() == 0)
            {
                AssetRulesBox->AddSlot().AutoHeight().Padding(30, 0, 0, 2)
                [
                    SNew(STextBlock).Text(LOCTEXT("NoAssets",
                        "nothing assigned — these will fall back"))
                ];
                continue;
            }

            // Per-corridor toggle, roads only: the rule that stops one street alternating
            // surfaces every ten metres.
            if (Type == EOSMNodeType::RoadSegment)
            {
                AssetRulesBox->AddSlot().AutoHeight().Padding(30, 0, 0, 2)
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this, Type, SubType]()
                    {
                        const FOSMAssetRule* Rule = Graph->Config.FindRule(Type, SubType);
                        return (Rule && Rule->bSelectPerCorridor)
                            ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                    })
                    .OnCheckStateChanged_Lambda([this, Type, SubType](ECheckBoxState NewState)
                    {
                        Graph->Config.FindOrAddRule(Type, SubType).bSelectPerCorridor =
                            (NewState == ECheckBoxState::Checked);
                    })
                    [
                        SNew(STextBlock).Text(LOCTEXT("PerCorridor",
                            "one surface per street"))
                    ]
                ];
            }

            for (int32 ChoiceIndex = 0; ChoiceIndex < ExactRule->Choices.Num(); ++ChoiceIndex)
            {
                AssetRulesBox->AddSlot().AutoHeight().Padding(30, 0, 0, 2)
                [
                    SNew(SHorizontalBox)

                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
                    [
                        SNew(SBox).WidthOverride(58.0f)
                        [
                            // Percentage is derived, never stored: showing it read-only keeps the
                            // weights independent, so editing one slot cannot silently rewrite
                            // the others to hold a total at 100.
                            SNew(STextBlock)
                            .Text_Lambda([this, Type, SubType, ChoiceIndex]()
                            {
                                const FOSMAssetRule* Rule = Graph->Config.FindRule(Type, SubType);
                                const float Ratio = Rule ? Rule->GetNormalisedRatio(ChoiceIndex) : 0.0f;
                                return FText::FromString(FString::Printf(TEXT("%5.1f%%"), Ratio * 100.0f));
                            })
                        ]
                    ]

                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
                    [
                        SNew(SBox).WidthOverride(76.0f)
                        [
                            SNew(SSpinBox<float>)
                            .MinValue(0.0f).MaxValue(100.0f).Delta(0.1f)
                            .ToolTipText(LOCTEXT("WeightTip", "Relative weight, not a percentage."))
                            .Value_Lambda([this, Type, SubType, ChoiceIndex]()
                            {
                                const FOSMAssetRule* Rule = Graph->Config.FindRule(Type, SubType);
                                return (Rule && Rule->Choices.IsValidIndex(ChoiceIndex))
                                    ? Rule->Choices[ChoiceIndex].Weight : 0.0f;
                            })
                            .OnValueChanged_Lambda([this, Type, SubType, ChoiceIndex](float NewValue)
                            {
                                FOSMAssetRule& Rule = Graph->Config.FindOrAddRule(Type, SubType);
                                if (Rule.Choices.IsValidIndex(ChoiceIndex))
                                {
                                    Rule.Choices[ChoiceIndex].Weight = NewValue;
                                }
                            })
                        ]
                    ]

                    + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                    [
                        // The engine's own asset field, so it behaves the way every other asset
                        // picker does. Buildings accept an archetype; everything else a mesh.
                        SNew(SObjectPropertyEntryBox)
                        .AllowedClass(GetAllowedAssetClass(Type))
                        .AllowClear(true)
                        .DisplayUseSelected(true)
                        .DisplayBrowse(true)
                        .ThumbnailPool(nullptr)
                        .ObjectPath_Lambda([this, Type, SubType, ChoiceIndex]() -> FString
                        {
                            const FOSMAssetRule* Rule = Graph.IsValid()
                                ? Graph->Config.FindRule(Type, SubType) : nullptr;
                            return (Rule && Rule->Choices.IsValidIndex(ChoiceIndex))
                                ? Rule->Choices[ChoiceIndex].Asset.ToString() : FString();
                        })
                        .OnObjectChanged_Lambda([this, Type, SubType, ChoiceIndex](const FAssetData& AssetData)
                        {
                            if (!Graph.IsValid()) return;

                            FOSMAssetRule& Rule = Graph->Config.FindOrAddRule(Type, SubType);
                            if (!Rule.Choices.IsValidIndex(ChoiceIndex)) return;

                            Rule.Choices[ChoiceIndex].Asset = AssetData.ToSoftObjectPath();

                            // The label follows the asset unless renamed, so the dry run reads
                            // "Commercial_Glass_02" rather than "Slot 1".
                            const FString AssetName = AssetData.AssetName.ToString();
                            if (Rule.Choices[ChoiceIndex].Label.StartsWith(TEXT("Slot "))
                                || Rule.Choices[ChoiceIndex].Label.IsEmpty())
                            {
                                Rule.Choices[ChoiceIndex].Label = AssetName.IsEmpty()
                                    ? FString::Printf(TEXT("Slot %d"), ChoiceIndex + 1)
                                    : AssetName;
                            }

                            RefreshAssetRules();
                        })
                    ]

                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("RemoveChoice", "x"))
                        .OnClicked_Lambda([this, Type, SubType, ChoiceIndex]()
                        {
                            FOSMAssetRule& Rule = Graph->Config.FindOrAddRule(Type, SubType);
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

            AssetRulesBox->AddSlot().AutoHeight().Padding(30, 2, 0, 4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
                [
                    SNew(STextBlock).Text(LOCTEXT("SeedLabel", "seed"))
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SBox).WidthOverride(96.0f)
                    [
                        // Per rule, not global: changing the seed for commercial buildings must
                        // not reshuffle the houses. An edit should only affect what it names.
                        SNew(SSpinBox<int32>)
                        .MinValue(0)
                        .Value_Lambda([this, Type, SubType]()
                        {
                            const FOSMAssetRule* Rule = Graph->Config.FindRule(Type, SubType);
                            return Rule ? Rule->Seed : 0;
                        })
                        .OnValueChanged_Lambda([this, Type, SubType](int32 NewValue)
                        {
                            Graph->Config.FindOrAddRule(Type, SubType).Seed = NewValue;
                        })
                    ]
                ]
            ];
        }
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
TSharedRef<SWidget> SOSMControlCenter::BuildCityExplorerTab()
{
    return SNew(SSplitter)
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
        ];
}

TSharedRef<SWidget> SOSMControlCenter::BuildEnvironmentTab()
{
    TimeMultiplierOptions.Empty();
    TimeMultiplierOptions.Add(MakeShared<FString>(TEXT("1x (Realtime)")));
    TimeMultiplierOptions.Add(MakeShared<FString>(TEXT("10x (Fast)")));
    TimeMultiplierOptions.Add(MakeShared<FString>(TEXT("60x (1 min/sec)")));
    TimeMultiplierOptions.Add(MakeShared<FString>(TEXT("3600x (1 hr/sec)")));

    return SNew(SScrollBox)
        + SScrollBox::Slot().Padding(16.0f)
        [
            SNew(SVerticalBox)

            // Dynamic Thermal Simulation Header
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("EnvTabHeader", "Thermal & Night Simulation Controls"))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
            ]

            // Start / Reset Simulation Buttons
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
                [
                    SNew(SButton)
                    .Text_Lambda([]() { return FOSMThermalSimulation::GlobalParams.bIsSimulationRunning ? LOCTEXT("PauseSim", "Pause Simulation") : LOCTEXT("PlaySim", "Play Simulation"); })
                    .OnClicked(this, &SOSMControlCenter::OnToggleSimulationPlay)
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("ResetSim", "Reset (Cold Start)"))
                    .OnClicked(this, &SOSMControlCenter::OnResetSimulation)
                ]
            ]

            // Day / Night Quick Presets
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
            [
                SNew(STextBlock).Text(LOCTEXT("PresetsLabel", "Environment Presets (Day / Night Cycle)"))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("PresetNoon", "Noon (12:00)"))
                    .ToolTipText(LOCTEXT("PresetNoonTip", "Peak solar irradiance (1000 W/m²). High thermal contrast."))
                    .OnClicked(this, &SOSMControlCenter::OnSetPresetNoon)
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("PresetSunset", "Sunset (18:00)"))
                    .ToolTipText(LOCTEXT("PresetSunsetTip", "Low sun angle, strong lateral shadows and lingering wall heat."))
                    .OnClicked(this, &SOSMControlCenter::OnSetPresetSunset)
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("PresetNight", "Midnight IR (00:00)"))
                    .ToolTipText(LOCTEXT("PresetNightTip", "Zero solar irradiance. Asphalt and concrete retain thermal mass, glowing against cool vegetation and night sky."))
                    .OnClicked(this, &SOSMControlCenter::OnSetPresetNight)
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .Text(LOCTEXT("PresetDawn", "Dawn (06:00)"))
                    .ToolTipText(LOCTEXT("PresetDawnTip", "Coldest hour before sunrise. Minimum scene temperatures."))
                    .OnClicked(this, &SOSMControlCenter::OnSetPresetDawn)
                ]
            ]

            // Sliders Container
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBorder).Padding(16.0f)
                [
                    SNew(SVerticalBox)

                    // Base Temperature
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                    [
                        SNew(STextBlock).Text(LOCTEXT("BaseTempLabel", "Base Environment Temp (°C)"))
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 16, 0)
                        [
                            SNew(SSlider)
                            .MinValue(-20.0f)
                            .MaxValue(60.0f)
                            .Value_Lambda([]() { return FOSMThermalSimulation::GlobalParams.BaseEnvironmentTempC; })
                            .OnValueChanged(this, &SOSMControlCenter::OnBaseTempChanged)
                        ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(STextBlock)
                            .Text_Lambda([]() { return FText::FromString(FString::Printf(TEXT("%.1f °C"), FOSMThermalSimulation::GlobalParams.BaseEnvironmentTempC)); })
                        ]
                    ]

                    // Heat Sensitivity
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                    [
                        SNew(STextBlock).Text(LOCTEXT("SensitivityLabel", "IR Camera Heat Sensitivity"))
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 16, 0)
                        [
                            SNew(SSlider)
                            .MinValue(0.1f)
                            .MaxValue(10.0f)
                            .Value_Lambda([this]() { return CurrentHeatSensitivity; })
                            .OnValueChanged(this, &SOSMControlCenter::OnHeatSensitivityChanged)
                        ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(STextBlock)
                            .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.2fx"), CurrentHeatSensitivity)); })
                        ]
                    ]

                    // Time of Day (Sun Angle)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                    [
                        SNew(STextBlock).Text(LOCTEXT("TimeOfDayLabel2", "Time of Day (Sun Angle)"))
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 16, 0)
                        [
                            SNew(SSlider)
                            .MinValue(0.0f)
                            .MaxValue(24.0f)
                            .Value_Lambda([]() { return FOSMThermalSimulation::GlobalParams.CurrentTimeOfDayHours; })
                            .OnValueChanged(this, &SOSMControlCenter::OnTimeOfDayChanged)
                        ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(STextBlock)
                            .Text_Lambda([]() { return FText::FromString(FString::Printf(TEXT("%.1f Hrs"), FOSMThermalSimulation::GlobalParams.CurrentTimeOfDayHours)); })
                        ]
                    ]

                    // Speed
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                    [
                        SNew(STextBlock).Text(LOCTEXT("SpeedLabel2", "Simulation Speed"))
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SComboBox<TSharedPtr<FString>>)
                        .OptionsSource(&TimeMultiplierOptions)
                        .OnGenerateWidget_Lambda([](TSharedPtr<FString> InItem) { return SNew(STextBlock).Text(FText::FromString(*InItem)); })
                        .OnSelectionChanged(this, &SOSMControlCenter::OnTimeMultiplierChanged)
                        [
                            SNew(STextBlock)
                            .Text_Lambda([this]()
                            {
                                int32 SimSpeedIndex = 0;
                                if (FOSMThermalSimulation::GlobalParams.TimeMultiplier == 1.0f) SimSpeedIndex = 0;
                                else if (FOSMThermalSimulation::GlobalParams.TimeMultiplier == 10.0f) SimSpeedIndex = 1;
                                else if (FOSMThermalSimulation::GlobalParams.TimeMultiplier == 60.0f) SimSpeedIndex = 2;
                                else if (FOSMThermalSimulation::GlobalParams.TimeMultiplier == 3600.0f) SimSpeedIndex = 3;

                                if (TimeMultiplierOptions.IsValidIndex(SimSpeedIndex))
                                {
                                    return FText::FromString(*TimeMultiplierOptions[SimSpeedIndex]);
                                }
                                return FText::FromString(TEXT("1x"));
                            })
                        ]
                    ]
                ]
            ]
        ];
}

TSharedRef<SWidget> SOSMControlCenter::BuildSimulationControls()
{
    return SNullWidget::NullWidget; // Replaced by BuildEnvironmentTab
}

void SOSMControlCenter::OnBaseTempChanged(float NewValue)
{
    CurrentBaseTempC = NewValue;
    FOSMThermalSimulation::GlobalParams.BaseEnvironmentTempC = NewValue;
    FOSMThermalSimulation::UpdateEnvironmentFromTime(FOSMThermalSimulation::GlobalParams);

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (World)
    {
        UOSMThermalMPC::UpdateFromWorld(World);
        for (TActorIterator<AOSMInfraredCamera> It(World); It; ++It)
        {
            (*It)->SetAmbientTemperatureK(FOSMThermalSimulation::GlobalParams.AmbientTemperatureK);
        }
        if (GEditor) GEditor->RedrawAllViewports();
    }
}

void SOSMControlCenter::OnHeatSensitivityChanged(float NewValue)
{
    CurrentHeatSensitivity = NewValue;
    
    // Update active IR Cameras
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (World)
    {
        for (TActorIterator<AOSMInfraredCamera> It(World); It; ++It)
        {
            (*It)->SetHeatSensitivity(NewValue);
        }
        if (GEditor) GEditor->RedrawAllViewports();
    }
}

FReply SOSMControlCenter::OnToggleSimulationPlay()
{
    FOSMThermalSimulation::GlobalParams.bIsSimulationRunning = !FOSMThermalSimulation::GlobalParams.bIsSimulationRunning;
    return FReply::Handled();
}

FReply SOSMControlCenter::OnResetSimulation()
{
    FOSMThermalSimulation::GlobalParams.bIsSimulationRunning = false;
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (World)
    {
        FOSMThermalSimulation::ComputeInitialStates(World, FOSMThermalSimulation::GlobalParams);
        UOSMThermalMPC::UpdateFromWorld(World);
        if (GEditor) GEditor->RedrawAllViewports();
    }
    return FReply::Handled();
}

void SOSMControlCenter::OnTimeOfDayChanged(float NewValue)
{
    CurrentTimeOfDayHours = NewValue;
    FOSMThermalSimulation::GlobalParams.CurrentTimeOfDayHours = NewValue;
    FOSMThermalSimulation::UpdateEnvironmentFromTime(FOSMThermalSimulation::GlobalParams);

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (World)
    {
        UOSMThermalMPC::UpdateFromWorld(World);
        for (TActorIterator<AOSMInfraredCamera> It(World); It; ++It)
        {
            (*It)->SetAmbientTemperatureK(FOSMThermalSimulation::GlobalParams.AmbientTemperatureK);
            // Immediately update the sun rotation for this camera
            (*It)->UpdateSunRotation(World, NewValue);
        }
        if (GEditor) GEditor->RedrawAllViewports();
    }
}

FReply SOSMControlCenter::OnSetPresetNoon()
{
    OnTimeOfDayChanged(12.0f);
    return FReply::Handled();
}

FReply SOSMControlCenter::OnSetPresetSunset()
{
    OnTimeOfDayChanged(18.0f);
    return FReply::Handled();
}

FReply SOSMControlCenter::OnSetPresetNight()
{
    OnTimeOfDayChanged(0.0f);
    return FReply::Handled();
}

FReply SOSMControlCenter::OnSetPresetDawn()
{
    OnTimeOfDayChanged(6.0f);
    return FReply::Handled();
}

void SOSMControlCenter::OnTimeMultiplierChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo)
{
    CurrentTimeMultiplierIndex = TimeMultiplierOptions.Find(Selection);
    
    if (CurrentTimeMultiplierIndex == 0) FOSMThermalSimulation::GlobalParams.TimeMultiplier = 1.0f;
    else if (CurrentTimeMultiplierIndex == 1) FOSMThermalSimulation::GlobalParams.TimeMultiplier = 10.0f;
    else if (CurrentTimeMultiplierIndex == 2) FOSMThermalSimulation::GlobalParams.TimeMultiplier = 60.0f;
    else if (CurrentTimeMultiplierIndex == 3) FOSMThermalSimulation::GlobalParams.TimeMultiplier = 3600.0f;
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

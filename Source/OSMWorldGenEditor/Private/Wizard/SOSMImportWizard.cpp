// Copyright InviMind. All Rights Reserved.

#include "Wizard/SOSMImportWizard.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"
#include "Editor.h"

// Core headers. No generator includes — the 3D generation layer was removed in
// plan_v3_pipeline.md Phase 0; this wizard's responsibility now ends at a validated import.
#include "Parsing/FOSMParser.h"
#include "Parsing/FOSMParseResult.h"
#include "Classification/FOSMTagClassifier.h"
#include "CRS/FOSMCRSTransformer.h"
#include "Elevation/FOSMDEMSampler.h"
#include "Elevation/FOSMGeoTIFFTile.h"
#include "Fetch/FOSMRegionCache.h"
#include "Fetch/FOSMOverpassClient.h"
#include "Fetch/FOSMOpenTopographyClient.h"
#include "Settings/UOSMWorldGenSettings.h"
#include "Graph/FOSMGraphBuilder.h"
#include "Graph/UOSMCityGraph.h"
#include "ControlCenter/SOSMControlCenter.h"
#include "HAL/PlatformProcess.h"
#include "Widgets/SWindow.h"
#include "Async/Async.h"
#include "Internationalization/Regex.h"
#include "HAL/PlatformApplicationMisc.h"

void SOSMImportWizard::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SBorder)
        .Padding(16.0f)
        [
            SNew(SVerticalBox)

            // Header Step Title Bar
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 16.0f)
            [
                SNew(STextBlock)
                .Text_Lambda([this]()
                {
                    switch (CurrentStepIndex)
                    {
                        case 0: return FText::FromString(TEXT("Step 1: Select Map (.osm/.pbf) & Elevation (.tif) Files"));
                        case 1: return FText::FromString(TEXT("Step 2: Georeferencing & Coordinate Reference System"));
                        case 2: return FText::FromString(TEXT("Step 3: Feature Categories"));
                        case 3: return FText::FromString(TEXT("Step 4: Importing & Validating..."));
                        case 4: return FText::FromString(TEXT("Step 5: Import Complete"));
                        default: return FText::GetEmpty();
                    }
                })
                .Font(FCoreStyle::Get().GetFontStyle("HeadingExtraLarge"))
            ]

            // Main Dynamic Step Body. Scrollable — the region-fetch section alone (size
            // gate, place search, manual bbox, browser/paste row, fetch status) can run
            // taller than the docked tab, and there's no other way to reach the file
            // pickers or the Next button below it once that happens.
            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            [
                SNew(SScrollBox)
                + SScrollBox::Slot()
                [
                    SNew(SWidgetSwitcher)
                    .WidgetIndex_Lambda([this]() { return CurrentStepIndex; })

                    // Step 0: File Pickers
                    + SWidgetSwitcher::Slot() [ ConstructStep0_FileSelect().ToSharedRef() ]

                    // Step 1: CRS Config
                    + SWidgetSwitcher::Slot() [ ConstructStep1_CRSConfig().ToSharedRef() ]

                    // Step 2: Layer Filters
                    + SWidgetSwitcher::Slot() [ ConstructStep2_LayerFilter().ToSharedRef() ]

                    // Step 3: Execution Progress
                    + SWidgetSwitcher::Slot() [ ConstructStep3_Execution().ToSharedRef() ]

                    // Step 4: Completion Summary
                    + SWidgetSwitcher::Slot() [ ConstructStep4_Summary().ToSharedRef() ]
                ]
            ]

            // Bottom Navigation Buttons
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 16.0f, 0.0f, 0.0f)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Previous")))
                    .IsEnabled_Lambda([this]() { return CurrentStepIndex > 0 && !State.bIsGenerating && CurrentStepIndex < 4; })
                    .OnClicked(this, &SOSMImportWizard::OnPrevClicked)
                ]

                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SNew(SSpacer)
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text_Lambda([this]()
                    {
                        if (CurrentStepIndex == 2) return FText::FromString(TEXT("Run Import"));
                        if (CurrentStepIndex == 4) return FText::FromString(TEXT("Close Wizard"));
                        return FText::FromString(TEXT("Next"));
                    })
                    .IsEnabled_Lambda([this]()
                    {
                        if (State.bIsGenerating) return false;
                        if (CurrentStepIndex == 0 && State.OSMFilePath.IsEmpty()) return false;
                        return true;
                    })
                    .OnClicked(this, &SOSMImportWizard::OnNextClicked)
                ]
            ]
        ]
    ];
}

// ---------------------------------------------------------------------------
TSharedPtr<SWidget> SOSMImportWizard::ConstructStep0_FileSelect()
{
    return SNew(SVerticalBox)

        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
        [
            ConstructRegionFetchSection().ToSharedRef()
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(STextBlock).Text(FText::FromString(TEXT("— or select files manually —"))).Font(FCoreStyle::Get().GetFontStyle("Bold"))
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [
            SNew(STextBlock).Text(FText::FromString(TEXT("OpenStreetMap Vector File (.osm or .osm.pbf):"))).Font(FCoreStyle::Get().GetFontStyle("Bold"))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                SNew(SEditableTextBox)
                .Text_Lambda([this]() { return FText::FromString(State.OSMFilePath); })
                .OnTextChanged_Lambda([this](const FText& Text) { State.OSMFilePath = Text.ToString(); AnalyzeSelectedFiles(); })
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("Browse...")))
                .OnClicked(this, &SOSMImportWizard::OnBrowseOSMFile)
            ]
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 4)
        [
            SNew(STextBlock).Text(FText::FromString(TEXT("GeoTIFF DEM Elevation File (.tif, Optional):"))).Font(FCoreStyle::Get().GetFontStyle("Bold"))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                SNew(SEditableTextBox)
                .Text_Lambda([this]() { return FText::FromString(State.DEMFilePath); })
                .OnTextChanged_Lambda([this](const FText& Text) { State.DEMFilePath = Text.ToString(); AnalyzeSelectedFiles(); })
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("Browse...")))
                .OnClicked(this, &SOSMImportWizard::OnBrowseDEMFile)
            ]
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(0, 20)
        [
            SNew(SBorder)
            .Padding(12.0f)
            [
                SNew(STextBlock)
                .Text_Lambda([this]()
                {
                    if (State.OSMFilePath.IsEmpty())
                    {
                        return FText::FromString(TEXT("Status: Please select an OpenStreetMap (.osm / .osm.pbf) file to proceed."));
                    }
                    if (State.bHasValidExtents)
                    {
                        return FText::FromString(FString::Printf(TEXT("File extent scanned (%s):\nLatitude Range: %.6f to %.6f\nLongitude Range: %.6f to %.6f\nDEM Status: %s"),
                            State.Region.IsValid() ? TEXT("preview only — the fetched region is what gets imported") : TEXT("will be used as the import region"),
                            State.ScanPreviewBounds.GetMinLat(), State.ScanPreviewBounds.GetMaxLat(),
                            State.ScanPreviewBounds.GetMinLon(), State.ScanPreviewBounds.GetMaxLon(),
                            State.DEMFilePath.IsEmpty() ? TEXT("None (Flat Terrain Fallback)") : (State.bDEMOverlapsOSM ? TEXT("Overlaps OSM Bounding Box") : TEXT("Loaded"))));
                    }
                    return FText::FromString(TEXT("Analyzing file header extents..."));
                })
            ]
        ];
}

// ---------------------------------------------------------------------------
TSharedPtr<SWidget> SOSMImportWizard::ConstructRegionFetchSection()
{
    return SNew(SBorder)
        .Padding(12.0f)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("Auto-Fetch by Region (recommended — .osm and elevation data are fetched for the exact same coordinates)")))
                .Font(FCoreStyle::Get().GetFontStyle("Bold"))
            ]

            // Area is the single authoritative size control — the lat/lon box below is always
            // derived from it. Fixed Small/Medium tiers used to live here, but having both
            // them and the bbox fields meant two things could disagree about how big a region
            // was, which is exactly how a 1.5 km request once became a 500 km landscape.
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0).VAlign(VAlign_Center)
                [
                    SNew(STextBlock).Text(FText::FromString(TEXT("Area (km²):")))
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
                [
                    SNew(SBox).WidthOverride(80.0f)
                    [
                        SNew(SEditableTextBox)
                        .Text_Lambda([this]() { return FText::FromString(State.AreaInputText); })
                        .OnTextChanged_Lambda([this](const FText& Text) { State.AreaInputText = Text.ToString(); })
                        .OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type) { OnAreaTextCommitted(); })
                        .ToolTipText(FText::FromString(TEXT("Side length is sqrt(area), so 4 km² is a 2×2 km square. Press Enter to apply.")))
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Apply")))
                    .OnClicked_Lambda([this]() { OnAreaTextCommitted(); return FReply::Handled(); })
                ]
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]()
                    {
                        const double SideKm = FMath::Sqrt(FMath::Max(GOSMMinRegionAreaSqKm, State.RequestedAreaSqKm));
                        return FText::FromString(FString::Printf(
                            TEXT("= %.2f × %.2f km square (max %.0f km²)"), SideKm, SideKm, GOSMMaxRegionAreaSqKm));
                    })
                ]
            ]

            // Primary path: type a place name, resolve it to a bbox via Nominatim — zero
            // browser round-trip, zero copy/paste, zero coordinate ordering to get wrong.
            + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 4)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
                [
                    SNew(SEditableTextBox)
                    .HintText(FText::FromString(TEXT("Search a place name, e.g. \"Jakkur Lake, Bangalore\"")))
                    .Text_Lambda([this]() { return FText::FromString(State.PlaceSearchQuery); })
                    .OnTextChanged_Lambda([this](const FText& Text) { State.PlaceSearchQuery = Text.ToString(); })
                    .OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type) { OnSearchPlaceClicked(); })
                ]

                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Search")))
                    .IsEnabled_Lambda([this]() { return !State.bIsSearchingPlace; })
                    .OnClicked(this, &SOSMImportWizard::OnSearchPlaceClicked)
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("Or, for a precise custom area:")))
            ]

            // Manual bbox entry / browser fallback for cases Nominatim can't resolve.
            + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 4)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0).VAlign(VAlign_Center) [ SNew(STextBlock).Text(FText::FromString(TEXT("Min Lat:"))) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
                [
                    SNew(SBox).WidthOverride(90.0f)
                    [
                        SNew(SEditableTextBox)
                        .Text_Lambda([this]() { return FText::FromString(State.BoundsText[0]); })
                        .OnTextChanged_Lambda([this](const FText& Text) { State.BoundsText[0] = Text.ToString(); })
                        .OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type) { TrySetRegionFromBoundsText(); })
                    ]
                ]

                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0).VAlign(VAlign_Center) [ SNew(STextBlock).Text(FText::FromString(TEXT("Max Lat:"))) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
                [
                    SNew(SBox).WidthOverride(90.0f)
                    [
                        SNew(SEditableTextBox)
                        .Text_Lambda([this]() { return FText::FromString(State.BoundsText[1]); })
                        .OnTextChanged_Lambda([this](const FText& Text) { State.BoundsText[1] = Text.ToString(); })
                        .OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type) { TrySetRegionFromBoundsText(); })
                    ]
                ]

                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0).VAlign(VAlign_Center) [ SNew(STextBlock).Text(FText::FromString(TEXT("Min Lon:"))) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
                [
                    SNew(SBox).WidthOverride(90.0f)
                    [
                        SNew(SEditableTextBox)
                        .Text_Lambda([this]() { return FText::FromString(State.BoundsText[2]); })
                        .OnTextChanged_Lambda([this](const FText& Text) { State.BoundsText[2] = Text.ToString(); })
                        .OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type) { TrySetRegionFromBoundsText(); })
                    ]
                ]

                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0).VAlign(VAlign_Center) [ SNew(STextBlock).Text(FText::FromString(TEXT("Max Lon:"))) ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SBox).WidthOverride(90.0f)
                    [
                        SNew(SEditableTextBox)
                        .Text_Lambda([this]() { return FText::FromString(State.BoundsText[3]); })
                        .OnTextChanged_Lambda([this](const FText& Text) { State.BoundsText[3] = Text.ToString(); })
                        .OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type) { TrySetRegionFromBoundsText(); })
                    ]
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 4)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("The browser's address bar does NOT update with the bounding box (it only tracks map center/zoom) — ignore it. After dragging a box on the map, OSM's own \"Export\" sidebar shows four Left / Bottom / Right / Top number fields: simplest is to just type those 4 numbers straight into Min/Max Lat/Lon below (Left→Min Lon, Right→Max Lon, Bottom→Min Lat, Top→Max Lat). Or, in that same sidebar under \"Other ways to export the data\", right-click the \"Overpass API\" link → Copy Link Address, then click \"Paste from Clipboard\" below.")))
                .AutoWrapText(true)
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Open OpenStreetMap.org")))
                    .OnClicked(this, &SOSMImportWizard::OnOpenBrowserClicked)
                    .ToolTipText(FText::FromString(TEXT("Opens openstreetmap.org/export in your system browser.")))
                ]

                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Paste from Clipboard")))
                    .OnClicked(this, &SOSMImportWizard::OnPasteClipboardClicked)
                    .ToolTipText(FText::FromString(TEXT("Reads whatever you last copied (an Overpass/export link with bbox=..., or four numbers) and fills the fields below.")))
                ]

                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
                [
                    SNew(SEditableTextBox)
                    .HintText(FText::FromString(TEXT("...or paste an export link / bbox here manually, e.g. bbox=-0.0084,51.5065,0.0021,51.5108")))
                    .Text_Lambda([this]() { return FText::FromString(State.PastedBboxRawText); })
                    .OnTextChanged_Lambda([this](const FText& Text) { State.PastedBboxRawText = Text.ToString(); })
                    .OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type) { OnParseBboxClicked(); })
                ]

                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Fill Fields")))
                    .OnClicked(this, &SOSMImportWizard::OnParseBboxClicked)
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("DEM (elevation) fetch requires an OpenTopography API key in Project Settings > Plugins > OSM World Generator; without one it's skipped and terrain falls back to flat.")))
                .AutoWrapText(true)
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Fetch Region Data")))
                    .IsEnabled_Lambda([this]() { return !State.bIsFetching; })
                    .OnClicked(this, &SOSMImportWizard::OnFetchRegionClicked)
                ]

                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(12, 0, 0, 0).VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() { return State.FetchStatusText; })
                    .AutoWrapText(true)
                ]
            ]
        ];
}

// ---------------------------------------------------------------------------
TSharedPtr<SWidget> SOSMImportWizard::ConstructStep1_CRSConfig()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [
            SNew(STextBlock).Text(FText::FromString(TEXT("Georeferenced Origin Centroid (WGS84 Lat / Lon / Height):"))).Font(FCoreStyle::Get().GetFontStyle("Bold"))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
            [
                SNew(STextBlock).Text(FText::FromString(TEXT("Latitude:")))
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SBox).WidthOverride(120.0f)
                [
                    SNew(SEditableTextBox)
                    .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.6f"), State.GeoOrigin.Latitude)); })
                    .OnTextChanged_Lambda([this](const FText& Text) { State.GeoOrigin.Latitude = FCString::Atod(*Text.ToString()); })
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(16, 0, 8, 0)
            [
                SNew(STextBlock).Text(FText::FromString(TEXT("Longitude:")))
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SBox).WidthOverride(120.0f)
                [
                    SNew(SEditableTextBox)
                    .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.6f"), State.GeoOrigin.Longitude)); })
                    .OnTextChanged_Lambda([this](const FText& Text) { State.GeoOrigin.Longitude = FCString::Atod(*Text.ToString()); })
                ]
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 16)
        [
            SNew(STextBlock).Text(FText::FromString(TEXT("Projection Mode: Local Tangent Plane (ENU - 64-bit LWC Centimeters)"))).Font(FCoreStyle::Get().GetFontStyle("Bold"))
        ];
}

// ---------------------------------------------------------------------------
TSharedPtr<SWidget> SOSMImportWizard::ConstructStep2_LayerFilter()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 8)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]() { return State.bGenerateTerrain ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { State.bGenerateTerrain = (NewState == ECheckBoxState::Checked); })
            [
                SNew(STextBlock).Text(FText::FromString(TEXT("Generate Terrain Landscape (ALandscape Heightmap)")))
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 8)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]() { return State.bGenerateRoads ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { State.bGenerateRoads = (NewState == ECheckBoxState::Checked); })
            [
                SNew(STextBlock).Text(FText::FromString(TEXT("Generate Road Spline Networks (Procedural Meshes)")))
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 8)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]() { return State.bGenerateBuildings ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { State.bGenerateBuildings = (NewState == ECheckBoxState::Checked); })
            [
                SNew(STextBlock).Text(FText::FromString(TEXT("Generate Building 3D Models (GeometryScript Footprint Extrusion)")))
            ]
        ];
}

// ---------------------------------------------------------------------------
TSharedPtr<SWidget> SOSMImportWizard::ConstructStep3_Execution()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 16)
        [
            SNew(STextBlock)
            .Text_Lambda([this]() { return State.StatusText; })
            .Font(FCoreStyle::Get().GetFontStyle("Bold"))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 16)
        [
            SNew(SProgressBar)
            .Percent_Lambda([this]() { return State.ProgressPercent; })
        ];
}

// ---------------------------------------------------------------------------
TSharedPtr<SWidget> SOSMImportWizard::ConstructStep4_Summary()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 16)
        [
            SNew(STextBlock)
            .Text_Lambda([this]()
            {
                return FText::FromString(State.ImportSummary.IsEmpty()
                    ? TEXT("No import has been run yet.")
                    : State.ImportSummary);
            })
            .AutoWrapText(true)
        ];
}

// ---------------------------------------------------------------------------
FReply SOSMImportWizard::OnBrowseOSMFile()
{
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (DesktopPlatform)
    {
        TArray<FString> OutFiles;
        const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

        if (DesktopPlatform->OpenFileDialog(
            ParentWindowHandle,
            TEXT("Select OpenStreetMap File"),
            TEXT(""),
            TEXT(""),
            TEXT("OpenStreetMap Data (*.osm;*.osm.pbf)|*.osm;*.osm.pbf|All Files (*.*)|*.*"),
            EFileDialogFlags::None,
            OutFiles))
        {
            if (OutFiles.Num() > 0)
            {
                State.OSMFilePath = OutFiles[0];
                AnalyzeSelectedFiles();
            }
        }
    }
    return FReply::Handled();
}

FReply SOSMImportWizard::OnBrowseDEMFile()
{
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (DesktopPlatform)
    {
        TArray<FString> OutFiles;
        const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

        if (DesktopPlatform->OpenFileDialog(
            ParentWindowHandle,
            TEXT("Select GeoTIFF DEM File"),
            TEXT(""),
            TEXT(""),
            TEXT("GeoTIFF Elevation Maps (*.tif;*.tiff)|*.tif;*.tiff|All Files (*.*)|*.*"),
            EFileDialogFlags::None,
            OutFiles))
        {
            if (OutFiles.Num() > 0)
            {
                State.DEMFilePath = OutFiles[0];
                AnalyzeSelectedFiles();
            }
        }
    }
    return FReply::Handled();
}

void SOSMImportWizard::AnalyzeSelectedFiles()
{
    if (State.OSMFilePath.IsEmpty()) return;

    FOSMParseResult TempResult;
    if (FOSMParser::Parse(State.OSMFilePath, TempResult))
    {
        TempResult.ResolveWayCoordinates();

        // ObservedBounds, not FromBoundingBox: a scanned extent is a fact about the file, and
        // is allowed to be far larger than any importable region (Overpass returns whole ways
        // that merely cross the query box). Running it through the size-limited factory would
        // reject perfectly good files.
        FString Error;
        if (FOSMRegion::ObservedBounds(
                TempResult.MinLatLon.X, TempResult.MinLatLon.Y,
                TempResult.MaxLatLon.X, TempResult.MaxLatLon.Y,
                State.ScanPreviewBounds, Error))
        {
            State.bHasValidExtents = true;
            State.bDEMOverlapsOSM = !State.DEMFilePath.IsEmpty();

            // The origin follows the region when there is one. Only a manual file selection
            // with no region falls back to the scanned centroid.
            State.GeoOrigin = State.Region.IsValid()
                ? State.Region.GetGeoOrigin()
                : State.ScanPreviewBounds.GetGeoOrigin();
        }
        else
        {
            State.bHasValidExtents = false;
            UE_LOG(LogTemp, Warning, TEXT("OSM scan: '%s' has no usable extent: %s"),
                *State.OSMFilePath, *Error);
        }
    }
}

// ---------------------------------------------------------------------------
bool SOSMImportWizard::GetCurrentRegionCenter(double& OutLat, double& OutLon) const
{
    if (!State.Region.IsValid())
    {
        return false;
    }
    OutLat = State.Region.GetCenterLat();
    OutLon = State.Region.GetCenterLon();
    return true;
}

// ---------------------------------------------------------------------------
void SOSMImportWizard::RefreshBoundsText()
{
    if (!State.Region.IsValid())
    {
        for (FString& Text : State.BoundsText)
        {
            Text.Empty();
        }
        return;
    }

    State.BoundsText[0] = FString::Printf(TEXT("%.6f"), State.Region.GetMinLat());
    State.BoundsText[1] = FString::Printf(TEXT("%.6f"), State.Region.GetMaxLat());
    State.BoundsText[2] = FString::Printf(TEXT("%.6f"), State.Region.GetMinLon());
    State.BoundsText[3] = FString::Printf(TEXT("%.6f"), State.Region.GetMaxLon());
}

// ---------------------------------------------------------------------------
bool SOSMImportWizard::TrySetRegionFromBoundsText()
{
    const double MinLat = FCString::Atod(*State.BoundsText[0]);
    const double MaxLat = FCString::Atod(*State.BoundsText[1]);
    const double MinLon = FCString::Atod(*State.BoundsText[2]);
    const double MaxLon = FCString::Atod(*State.BoundsText[3]);

    FOSMRegion Candidate;
    FString Error;
    if (!FOSMRegion::FromBoundingBox(MinLat, MinLon, MaxLat, MaxLon, Candidate, Error))
    {
        // Typed bounds are rejected rather than partially applied: a region that only half
        // took effect is exactly the kind of silent inconsistency this phase removes.
        State.FetchStatusText = FText::FromString(Error);
        return false;
    }

    State.Region = Candidate;
    State.RequestedAreaSqKm = Candidate.GetAreaSqKm();
    State.AreaInputText = FString::Printf(TEXT("%.2f"), State.RequestedAreaSqKm);
    State.FetchStatusText = FText::FromString(FString::Printf(
        TEXT("Region set: %.2f x %.2f km (%.2f km2)."),
        Candidate.GetWidthKm(), Candidate.GetHeightKm(), Candidate.GetAreaSqKm()));
    return true;
}

// ---------------------------------------------------------------------------
void SOSMImportWizard::ApplyRequestedAreaAround(double CenterLat, double CenterLon)
{
    const double AreaSqKm = FMath::Clamp(State.RequestedAreaSqKm, GOSMMinRegionAreaSqKm, GOSMMaxRegionAreaSqKm);

    // The degree maths lives in FOSMRegion, not here. Every place that used to derive a box
    // from a centre had its own copy of the cos(latitude) conversion, and they disagreed.
    FOSMRegion Candidate;
    FString Error;
    if (!FOSMRegion::FromCenterAndArea(CenterLat, CenterLon, AreaSqKm, Candidate, Error))
    {
        State.FetchStatusText = FText::FromString(Error);
        return;
    }

    State.Region = Candidate;
    RefreshBoundsText();
}

// ---------------------------------------------------------------------------
void SOSMImportWizard::OnAreaTextCommitted()
{
    const double Parsed = FCString::Atod(*State.AreaInputText);

    if (Parsed <= 0.0)
    {
        State.FetchStatusText = FText::FromString(TEXT("Enter a positive area in km², e.g. 1.5"));
        State.AreaInputText = FString::Printf(TEXT("%.2f"), State.RequestedAreaSqKm);
        return;
    }

    const double Clamped = FMath::Clamp(Parsed, GOSMMinRegionAreaSqKm, GOSMMaxRegionAreaSqKm);
    State.RequestedAreaSqKm = Clamped;
    State.AreaInputText = FString::Printf(TEXT("%.2f"), Clamped);

    if (!FMath::IsNearlyEqual(Clamped, Parsed))
    {
        State.FetchStatusText = FText::FromString(FString::Printf(
            TEXT("Area clamped to %.2f km² (allowed range %.2f–%.0f km²)."),
            Clamped, GOSMMinRegionAreaSqKm, GOSMMaxRegionAreaSqKm));
    }

    // Reshape the existing region around its own centre. With no region yet, the area is
    // simply remembered and applied to the next place search.
    double CenterLat = 0.0, CenterLon = 0.0;
    if (GetCurrentRegionCenter(CenterLat, CenterLon))
    {
        ApplyRequestedAreaAround(CenterLat, CenterLon);
    }
}

FReply SOSMImportWizard::OnOpenBrowserClicked()
{
    // Embedding a map in-editor (SWebBrowser + WKWebView on Mac) turned out to be
    // unreliable on this engine build — the "about:" scheme and the widget's load-queue
    // both misbehaved. The system browser always works, so we send the user there and
    // parse the bbox they copy back instead of trying to render a map ourselves.
    FPlatformProcess::LaunchURL(TEXT("https://www.openstreetmap.org/export"), nullptr, nullptr);
    return FReply::Handled();
}

FReply SOSMImportWizard::OnPasteClipboardClicked()
{
    FString ClipboardText;
    FPlatformApplicationMisc::ClipboardPaste(ClipboardText);
    State.PastedBboxRawText = ClipboardText;
    return OnParseBboxClicked();
}

FReply SOSMImportWizard::OnParseBboxClicked()
{
    double MinLat = 0.0, MinLon = 0.0, MaxLat = 0.0, MaxLon = 0.0;
    if (ParseBboxFromText(State.PastedBboxRawText, MinLat, MinLon, MaxLat, MaxLon))
    {
        FOSMRegion Candidate;
        FString Error;
        if (FOSMRegion::FromBoundingBox(MinLat, MinLon, MaxLat, MaxLon, Candidate, Error))
        {
            State.Region = Candidate;
            State.RequestedAreaSqKm = Candidate.GetAreaSqKm();
            State.AreaInputText = FString::Printf(TEXT("%.2f"), State.RequestedAreaSqKm);
            RefreshBoundsText();
            State.FetchStatusText = FText::FromString(FString::Printf(
                TEXT("Parsed bounding box: %.2f x %.2f km (%.2f km2)."),
                Candidate.GetWidthKm(), Candidate.GetHeightKm(), Candidate.GetAreaSqKm()));
        }
        else
        {
            // The paste was well-formed but describes an unusable region — say which, rather
            // than silently accepting bounds that would fail later.
            State.FetchStatusText = FText::FromString(Error);
        }
    }
    else
    {
        State.FetchStatusText = FText::FromString(TEXT("Couldn't find a bounding box in that text. Paste the full export link (with bbox=...) or the four Left/Bottom/Right/Top numbers."));
    }
    return FReply::Handled();
}

bool SOSMImportWizard::ParseBboxFromText(const FString& Text, double& OutMinLat, double& OutMinLon, double& OutMaxLat, double& OutMaxLon)
{
    // Primary: OpenStreetMap's own bbox=West,South,East,North query param, present in
    // the link shown after clicking "Export" on openstreetmap.org/export, e.g.
    // https://api.openstreetmap.org/api/0.6/map?bbox=-0.0084,51.5065,0.0021,51.5108
    const FRegexPattern BboxPattern(TEXT("bbox=(-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+)"));
    FRegexMatcher BboxMatcher(BboxPattern, Text);
    if (BboxMatcher.FindNext())
    {
        OutMinLon = FCString::Atod(*BboxMatcher.GetCaptureGroup(1));
        OutMinLat = FCString::Atod(*BboxMatcher.GetCaptureGroup(2));
        OutMaxLon = FCString::Atod(*BboxMatcher.GetCaptureGroup(3));
        OutMaxLat = FCString::Atod(*BboxMatcher.GetCaptureGroup(4));
        return true;
    }

    // Fallback: four bare decimal numbers anywhere in the pasted text, read in the same
    // West,South,East,North order as OSM's Left/Bottom/Right/Top fields.
    const FRegexPattern NumberPattern(TEXT("-?[0-9]+\\.[0-9]+"));
    FRegexMatcher NumberMatcher(NumberPattern, Text);
    TArray<double> Numbers;
    while (NumberMatcher.FindNext() && Numbers.Num() < 4)
    {
        const FString Match = Text.Mid(NumberMatcher.GetMatchBeginning(), NumberMatcher.GetMatchEnding() - NumberMatcher.GetMatchBeginning());
        Numbers.Add(FCString::Atod(*Match));
    }
    if (Numbers.Num() == 4)
    {
        OutMinLon = Numbers[0];
        OutMinLat = Numbers[1];
        OutMaxLon = Numbers[2];
        OutMaxLat = Numbers[3];
        return true;
    }

    return false;
}

FReply SOSMImportWizard::OnSearchPlaceClicked()
{
    if (State.bIsSearchingPlace)
    {
        return FReply::Handled();
    }

    State.bIsSearchingPlace = true;
    State.FetchStatusText = FText::FromString(TEXT("Searching..."));

    FOSMNominatimClient::SearchAsync(State.PlaceSearchQuery,
        FOSMNominatimClient::FOnGeocodeComplete::CreateSP(this, &SOSMImportWizard::OnGeocodeComplete));

    return FReply::Handled();
}

void SOSMImportWizard::OnGeocodeComplete(bool bSuccess, const FString& ErrorMessage, const FOSMNominatimClient::FGeocodeResult& Result)
{
    State.bIsSearchingPlace = false;

    if (!bSuccess)
    {
        State.FetchStatusText = FText::FromString(ErrorMessage);
        return;
    }

    // Use only the CENTRE of the match, then size the box from the requested area. Nominatim
    // returns the feature's own extent, which for anything city-sized is tens of km across —
    // far past what the fetch and terrain paths can handle, and not what the user asked for.
    const double CenterLat = (Result.MinLat + Result.MaxLat) * 0.5;
    const double CenterLon = (Result.MinLon + Result.MaxLon) * 0.5;
    ApplyRequestedAreaAround(CenterLat, CenterLon);

    const double SideKm = FMath::Sqrt(FMath::Clamp(State.RequestedAreaSqKm, GOSMMinRegionAreaSqKm, GOSMMaxRegionAreaSqKm));
    State.FetchStatusText = FText::FromString(FString::Printf(
        TEXT("Found: %s — centred a %.2f × %.2f km box (%.2f km²) there. Click Fetch Region Data."),
        *Result.DisplayName, SideKm, SideKm, State.RequestedAreaSqKm));
}

FReply SOSMImportWizard::OnFetchRegionClicked()
{
    // Pick up any hand-edited bounds before fetching, so what is fetched is always what the
    // fields show. TrySetRegionFromBoundsText reports its own reason on rejection.
    if (State.BoundsText[0].Len() > 0 && !TrySetRegionFromBoundsText())
    {
        return FReply::Handled();
    }

    if (!State.Region.IsValid())
    {
        State.FetchStatusText = FText::FromString(
            TEXT("No region set. Search for a place, paste a bounding box, or type the four bounds."));
        return FReply::Handled();
    }

    // No size re-validation here: FOSMRegion's factories already enforce the limits, and every
    // path that can set State.Region goes through one. This used to be a second, independently
    // written check — and the two disagreed.
    const FOSMRegion& Region = State.Region;

    UE_LOG(LogTemp, Log, TEXT("OSM fetch: %.2f x %.2f km (%.2f km2) over %s"),
        Region.GetWidthKm(), Region.GetHeightKm(), Region.GetAreaSqKm(), *Region.ToString());

    State.bIsFetching = true;
    State.bOSMFetchDone = false;
    State.bDEMFetchDone = false;
    State.bOSMFetchSucceeded = false;
    State.bDEMFetchSucceeded = false;
    State.LastOSMFetchError.Empty();
    State.LastDEMFetchError.Empty();
    State.FetchStatusText = FText::FromString(TEXT("Fetching OSM data + elevation..."));

    // Cache reuse is now conditional on the fetch TERMS matching, not just the region. The
    // key used to be the bounding box alone, so changing the DEM dataset or the request
    // padding left every previously-fetched region silently serving files obtained under the
    // old settings — you would change a setting, re-run, and see an identical result.
    const UOSMWorldGenSettings* Settings = GetDefault<UOSMWorldGenSettings>();
    const FString DEMType = Settings->OpenTopographyDemType;
    const double DEMPadding = FOSMOpenTopographyClient::FetchPaddingDegrees;

    if (FOSMRegionCache::HasValidCachedOSM(Region, DEMType, DEMPadding))
    {
        OnOverpassFetchComplete(true, FString());
    }
    else
    {
        FOSMOverpassClient::FetchAsync(Region, FOSMRegionCache::GetOSMFilePath(Region),
            FOSMOverpassClient::FOnFetchComplete::CreateSP(this, &SOSMImportWizard::OnOverpassFetchComplete));
    }

    if (FOSMRegionCache::HasValidCachedDEM(Region, DEMType, DEMPadding))
    {
        OnOpenTopographyFetchComplete(true, FString());
    }
    else
    {
        FOSMOpenTopographyClient::FetchAsync(Region, FOSMRegionCache::GetDEMFilePath(Region),
            FOSMOpenTopographyClient::FOnFetchComplete::CreateSP(this, &SOSMImportWizard::OnOpenTopographyFetchComplete));
    }

    return FReply::Handled();
}

void SOSMImportWizard::OnOverpassFetchComplete(bool bSuccess, const FString& ErrorMessage)
{
    State.bOSMFetchDone = true;
    State.bOSMFetchSucceeded = bSuccess;
    State.LastOSMFetchError = ErrorMessage;

    if (bSuccess)
    {
        State.OSMFilePath = FOSMRegionCache::GetOSMFilePath(State.Region);
    }

    CheckFetchCompletion();
}

void SOSMImportWizard::OnOpenTopographyFetchComplete(bool bSuccess, const FString& ErrorMessage)
{
    State.bDEMFetchDone = true;
    State.bDEMFetchSucceeded = bSuccess;
    State.LastDEMFetchError = ErrorMessage;

    if (bSuccess)
    {
        State.DEMFilePath = FOSMRegionCache::GetDEMFilePath(State.Region);
    }
    // DEM failure is a soft failure by design (plan_v2_workflow.md §10: falls back to
    // manual .tif upload, or flat terrain if that's skipped too) — it must never block
    // the OSM fetch from being usable.

    CheckFetchCompletion();
}

void SOSMImportWizard::CheckFetchCompletion()
{
    if (!State.bOSMFetchDone || !State.bDEMFetchDone)
    {
        return; // still waiting on the other request
    }

    State.bIsFetching = false;

    if (State.bOSMFetchSucceeded)
    {
        State.FetchStatusText = FText::FromString(State.bDEMFetchSucceeded
            ? TEXT("Region data fetched successfully.")
            : FString::Printf(TEXT("OSM data fetched. DEM fetch skipped: %s"), *State.LastDEMFetchError));
        // Scanning the fetched file records what is IN it, which is legitimately larger than
        // the region: Overpass returns the full geometry of any way or relation that merely
        // overlaps the query box. That observation now lands in ScanPreviewBounds, which no
        // import path reads, so it can no longer be mistaken for the region the way it was
        // when both lived in the same fields.
        AnalyzeSelectedFiles();

        State.GeoOrigin = State.Region.GetGeoOrigin();

        // Record provenance so a later run can tell whether these files are still reusable.
        FOSMCacheManifest Manifest;
        Manifest.MinLat = State.Region.GetMinLat();
        Manifest.MaxLat = State.Region.GetMaxLat();
        Manifest.MinLon = State.Region.GetMinLon();
        Manifest.MaxLon = State.Region.GetMaxLon();
        Manifest.OverpassUrl = FOSMOverpassClient::GetLastSuccessfulEndpoint();
        Manifest.DEMType = GetDefault<UOSMWorldGenSettings>()->OpenTopographyDemType;
        Manifest.DEMPaddingDegrees = FOSMOpenTopographyClient::FetchPaddingDegrees;
        Manifest.FetchedAtUtc = FDateTime::UtcNow();
        Manifest.OSMFileSize = IFileManager::Get().FileSize(*State.OSMFilePath);
        Manifest.DEMFileSize = State.bDEMFetchSucceeded && !State.DEMFilePath.IsEmpty()
            ? IFileManager::Get().FileSize(*State.DEMFilePath)
            : 0;
        Manifest.LastValidationVerdict = TEXT("not yet validated");

        if (!FOSMRegionCache::SaveManifest(State.Region, Manifest))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("OSM fetch: could not write cache manifest for %s — these files will be re-fetched next time."),
                *State.Region.ToString());
        }
    }
    else
    {
        State.FetchStatusText = FText::FromString(FString::Printf(
            TEXT("OSM fetch failed: %s You can still use manual file upload below."), *State.LastOSMFetchError));
    }
}

FReply SOSMImportWizard::OnNextClicked()
{
    if (CurrentStepIndex == 2)
    {
        OnStartGeneration();
        return FReply::Handled();
    }

    if (CurrentStepIndex < 4)
    {
        CurrentStepIndex++;
    }
    else
    {
        TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId("OSMWorldGenWizard"));
        if (Tab.IsValid())
        {
            Tab->RequestCloseTab();
        }
    }
    return FReply::Handled();
}

FReply SOSMImportWizard::OnPrevClicked()
{
    if (CurrentStepIndex > 0 && !State.bIsGenerating)
    {
        CurrentStepIndex--;
    }
    return FReply::Handled();
}

FReply SOSMImportWizard::OnStartGeneration()
{
    CurrentStepIndex = 3;
    State.bIsGenerating = true;
    State.StatusText = FText::FromString(TEXT("Validating and importing..."));
    State.ProgressPercent = 0.1f;

    // The region is read, never recomputed. Previously this function rebuilt bounds from
    // several competing sources and picked between them here, at the point of use — which is
    // how a 1.5 km request became a 500 km landscape when one source was quietly wrong.
    if (!State.Region.IsValid())
    {
        // Manual file selection with no fetched region: the scanned extent is the only thing
        // available, so promote it explicitly and visibly rather than silently treating an
        // observation as a target.
        if (State.ScanPreviewBounds.IsValid())
        {
            FOSMRegion Promoted;
            FString Error;
            if (FOSMRegion::FromBoundingBox(
                    State.ScanPreviewBounds.GetMinLat(), State.ScanPreviewBounds.GetMinLon(),
                    State.ScanPreviewBounds.GetMaxLat(), State.ScanPreviewBounds.GetMaxLon(),
                    Promoted, Error))
            {
                State.Region = Promoted;
                UE_LOG(LogTemp, Log,
                    TEXT("OSM import: no fetched region; using the scanned extent of the selected files (%s)."),
                    *Promoted.ToString());
            }
            else
            {
                State.ImportSummary = FString::Printf(
                    TEXT("Cannot import: the selected files cover %s, which is not a usable region.\n\n%s"),
                    *State.ScanPreviewBounds.ToString(), *Error);
                State.bHasImportReport = false;
                State.StatusText = FText::FromString(TEXT("Import rejected."));
                State.ProgressPercent = 1.0f;
                State.bIsGenerating = false;
                CurrentStepIndex = 4;
                return FReply::Handled();
            }
        }
        else
        {
            State.ImportSummary = TEXT(
                "Cannot import: no region has been set.\n\n"
                "Fetch a region, or select an .osm file so its extent can be scanned.");
            State.bHasImportReport = false;
            State.StatusText = FText::FromString(TEXT("Import rejected."));
            State.ProgressPercent = 1.0f;
            State.bIsGenerating = false;
            CurrentStepIndex = 4;
            return FReply::Handled();
        }
    }

    State.StatusText = FText::FromString(TEXT("Running validation gates..."));
    State.ProgressPercent = 0.4f;

    // One call runs both file gates, the cross-file gate, and classification, in the order
    // that guarantees a malformed file never reaches the code that would choke on it.
    FOSMFeatureTable FeatureTable;
    State.ImportReport = FOSMImportValidator::Run(
        State.Region, State.OSMFilePath, State.DEMFilePath, FeatureTable);
    State.bHasImportReport = true;

    State.GeoOrigin = State.Region.GetGeoOrigin();
    State.ImportSummary = State.ImportReport.ToDisplayString();
    State.GeneratedActorCount = 0;

    // ---- Phase 2: build the city graph from the validated features ----
    //
    // Only on an accepted import. Building a graph from data that failed its gates would defeat
    // the point of having gates, and would put the defect one layer further from where it can
    // be understood.
    State.CityGraph.Reset();
    State.bHasGraph = false;

    if (State.ImportReport.IsAccepted())
    {
        State.StatusText = FText::FromString(TEXT("Building city graph..."));
        State.ProgressPercent = 0.7f;

        FOSMGraphBuildOptions GraphOptions;

        UOSMCityGraph* Graph = FOSMGraphBuilder::Build(
            FeatureTable, State.Region, GraphOptions, GetTransientPackage(), State.GraphReport);

        if (Graph)
        {
            Graph->SourceOSMFile = State.OSMFilePath;
            Graph->SourceDEMFile = State.DEMFilePath;
            State.CityGraph.Reset(Graph);
            State.bHasGraph = true;

            State.ImportSummary += TEXT("\n\n")
                TEXT("------------------------------------------------------------\n")
                + State.GraphReport.ToDisplayString()
                + TEXT("\n\n") + Graph->ToSummaryString();

            UE_LOG(LogTemp, Log, TEXT("City graph report:\n%s\n\n%s"),
                *State.GraphReport.ToDisplayString(), *Graph->ToSummaryString());

            // Hand the graph straight to the Control Center. The wizard's job ends at "the data
            // is understood"; everything after that is inspection and configuration, which is
            // what the Control Center exists for (plan_v3_pipeline.md Phase 3).
            SOSMControlCenter::OpenWithGraph(Graph, State.Region, State.GraphReport);
        }
    }

    // Full findings go to the log — including Info-level ones the summary omits — so a
    // support question can be answered from a log paste alone.
    UE_LOG(LogTemp, Log, TEXT("OSM import report:\n%s\n\nAll findings:\n%s"),
        *State.ImportSummary, *State.ImportReport.Validation.ToString());

    // Record the verdict against the cached files, so the next run can see that these exact
    // files were already judged good or bad.
    if (!State.OSMFilePath.IsEmpty())
    {
        FOSMCacheManifest Manifest;
        if (FOSMRegionCache::LoadManifest(State.Region, Manifest))
        {
            Manifest.LastValidationVerdict = State.ImportReport.IsAccepted()
                ? TEXT("accepted")
                : FString::Printf(TEXT("rejected: %s"),
                    State.ImportReport.Validation.FindFirst(EOSMIssueSeverity::Fatal)
                        ? *State.ImportReport.Validation.FindFirst(EOSMIssueSeverity::Fatal)->Code
                        : TEXT("unknown"));
            FOSMRegionCache::SaveManifest(State.Region, Manifest);
        }
    }

    State.StatusText = FText::FromString(State.ImportReport.IsAccepted()
        ? TEXT("Import complete.")
        : TEXT("Import rejected — see the summary."));
    State.ProgressPercent = 1.0f;
    State.bIsGenerating = false;
    CurrentStepIndex = 4;

    return FReply::Handled();
}

void SOSMImportWizard::OpenWizardWindow()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner("OSMWorldGenWizard", FOnSpawnTab::CreateLambda([](const FSpawnTabArgs& Args)
    {
        return SNew(SDockTab)
            .TabRole(ETabRole::NomadTab)
            .Label(FText::FromString(TEXT("OSM World Generator Wizard")))
            [
                SNew(SOSMImportWizard)
            ];
    }))
    .SetDisplayName(FText::FromString(TEXT("OSM World Generator")))
    .SetMenuType(ETabSpawnerMenuType::Hidden);

    FGlobalTabmanager::Get()->TryInvokeTab(FTabId("OSMWorldGenWizard"));
}

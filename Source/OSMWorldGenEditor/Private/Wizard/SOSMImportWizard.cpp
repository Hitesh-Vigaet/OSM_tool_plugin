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

// Core & Generator headers
#include "Parsing/FOSMParser.h"
#include "Parsing/FOSMParseResult.h"
#include "Classification/FOSMTagClassifier.h"
#include "CRS/FOSMCRSTransformer.h"
#include "Generators/FOSMGenerationContext.h"
#include "Terrain/UOSMTerrainGenerator.h"
#include "Roads/UOSMRoadGenerator.h"
#include "Buildings/UOSMBuildingGenerator.h"
#include "Areas/UOSMAreaFeatureGenerator.h"
#include "Scene/FOSMSceneSetup.h"
#include "Fetch/FOSMRegionCache.h"
#include "Fetch/FOSMOverpassClient.h"
#include "Fetch/FOSMOpenTopographyClient.h"
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
                        case 2: return FText::FromString(TEXT("Step 3: Layer Selection & Generator Options"));
                        case 3: return FText::FromString(TEXT("Step 4: Executing 3D World Generation..."));
                        case 4: return FText::FromString(TEXT("Step 5: Generation Complete!"));
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
                        if (CurrentStepIndex == 2) return FText::FromString(TEXT("Start 3D Generation"));
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
                        return FText::FromString(FString::Printf(TEXT("Geographic Bounding Box Verified:\nLatitude Range: %.6f to %.6f\nLongitude Range: %.6f to %.6f\nDEM Status: %s"),
                            State.MinLat, State.MaxLat, State.MinLon, State.MaxLon,
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
                        .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.6f"), State.FetchMinLat)); })
                        .OnTextChanged_Lambda([this](const FText& Text) { State.FetchMinLat = FCString::Atod(*Text.ToString()); })
                    ]
                ]

                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0).VAlign(VAlign_Center) [ SNew(STextBlock).Text(FText::FromString(TEXT("Max Lat:"))) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
                [
                    SNew(SBox).WidthOverride(90.0f)
                    [
                        SNew(SEditableTextBox)
                        .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.6f"), State.FetchMaxLat)); })
                        .OnTextChanged_Lambda([this](const FText& Text) { State.FetchMaxLat = FCString::Atod(*Text.ToString()); })
                    ]
                ]

                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0).VAlign(VAlign_Center) [ SNew(STextBlock).Text(FText::FromString(TEXT("Min Lon:"))) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
                [
                    SNew(SBox).WidthOverride(90.0f)
                    [
                        SNew(SEditableTextBox)
                        .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.6f"), State.FetchMinLon)); })
                        .OnTextChanged_Lambda([this](const FText& Text) { State.FetchMinLon = FCString::Atod(*Text.ToString()); })
                    ]
                ]

                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0).VAlign(VAlign_Center) [ SNew(STextBlock).Text(FText::FromString(TEXT("Max Lon:"))) ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SBox).WidthOverride(90.0f)
                    [
                        SNew(SEditableTextBox)
                        .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.6f"), State.FetchMaxLon)); })
                        .OnTextChanged_Lambda([this](const FText& Text) { State.FetchMaxLon = FCString::Atod(*Text.ToString()); })
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
                return FText::FromString(FString::Printf(TEXT("3D World Generation Successful!\n\nGeoreferenced Origin: (Lat: %.6f, Lon: %.6f)\nSpawned Level Actors: %d\n\nAll generated actors carry queryable UOSMMetadataComponent data."),
                    State.GeoOrigin.Latitude, State.GeoOrigin.Longitude, State.GeneratedActorCount));
            })
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
        State.MinLat = TempResult.MinLatLon.X;
        State.MaxLat = TempResult.MaxLatLon.X;
        State.MinLon = TempResult.MinLatLon.Y;
        State.MaxLon = TempResult.MaxLatLon.Y;
        State.bHasValidExtents = true;

        // Set centroid origin
        State.GeoOrigin.Latitude = (State.MinLat + State.MaxLat) * 0.5;
        State.GeoOrigin.Longitude = (State.MinLon + State.MaxLon) * 0.5;
        State.GeoOrigin.HeightMeters = 0.0;
        State.bDEMOverlapsOSM = !State.DEMFilePath.IsEmpty();
    }
}

// ---------------------------------------------------------------------------
bool SOSMImportWizard::GetCurrentRegionCenter(double& OutLat, double& OutLon) const
{
    if (State.FetchMaxLat > State.FetchMinLat && State.FetchMaxLon > State.FetchMinLon)
    {
        OutLat = (State.FetchMinLat + State.FetchMaxLat) * 0.5;
        OutLon = (State.FetchMinLon + State.FetchMaxLon) * 0.5;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
void SOSMImportWizard::ApplyRequestedAreaAround(double CenterLat, double CenterLon)
{
    const double AreaSqKm = FMath::Clamp(State.RequestedAreaSqKm, GOSMMinRegionAreaSqKm, GOSMMaxRegionAreaSqKm);
    const double SideKm = FMath::Sqrt(AreaSqKm);

    // A degree of latitude is ~111.32 km everywhere; a degree of longitude shrinks by
    // cos(latitude), so the box has to be widened in longitude to stay square on the ground.
    const double CosLat = FMath::Max(0.01, FMath::Cos(FMath::DegreesToRadians(CenterLat)));
    const double HalfLatDeg = (SideKm * 0.5) / 111.32;
    const double HalfLonDeg = (SideKm * 0.5) / (111.32 * CosLat);

    State.FetchMinLat = CenterLat - HalfLatDeg;
    State.FetchMaxLat = CenterLat + HalfLatDeg;
    State.FetchMinLon = CenterLon - HalfLonDeg;
    State.FetchMaxLon = CenterLon + HalfLonDeg;
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
        State.FetchMinLat = MinLat;
        State.FetchMinLon = MinLon;
        State.FetchMaxLat = MaxLat;
        State.FetchMaxLon = MaxLon;
        State.FetchStatusText = FText::FromString(TEXT("Parsed bounding box from pasted text — fields updated below."));
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
    FOSMRegionCache::FBoundingBox Bbox;
    Bbox.MinLat = State.FetchMinLat;
    Bbox.MaxLat = State.FetchMaxLat;
    Bbox.MinLon = State.FetchMinLon;
    Bbox.MaxLon = State.FetchMaxLon;

    if (!Bbox.IsValid())
    {
        State.FetchStatusText = FText::FromString(TEXT("Invalid bounding box — Max Lat/Lon must be greater than Min Lat/Lon."));
        return FReply::Handled();
    }

    // Validate whatever box is actually about to be fetched. The area field derives the box,
    // but the lat/lon fields stay hand-editable and a pasted bbox bypasses the area entirely,
    // so the real measured extent is what gets checked here rather than the requested area.
    // Equirectangular approximation, consistent with FOSMFeatureTable::GetApproxExtentKm.
    const double MidLat = (Bbox.MinLat + Bbox.MaxLat) * 0.5;
    const double LatKm = (Bbox.MaxLat - Bbox.MinLat) * 111.32;
    const double LonKm = (Bbox.MaxLon - Bbox.MinLon) * 111.32 * FMath::Cos(FMath::DegreesToRadians(MidLat));
    const double ActualAreaSqKm = LatKm * LonKm;

    if (ActualAreaSqKm > GOSMMaxRegionAreaSqKm)
    {
        State.FetchStatusText = FText::FromString(FString::Printf(
            TEXT("Region is ~%.1f km² (%.1f × %.1f km), over the %.0f km² limit. Lower the area, or shrink the bounding box."),
            ActualAreaSqKm, LatKm, LonKm, GOSMMaxRegionAreaSqKm));
        return FReply::Handled();
    }

    UE_LOG(LogTemp, Log, TEXT("OSM fetch: requested %.2f km² -> bbox %.2f x %.2f km (%.2f km²) lat[%.6f..%.6f] lon[%.6f..%.6f]"),
        State.RequestedAreaSqKm, LatKm, LonKm, ActualAreaSqKm,
        Bbox.MinLat, Bbox.MaxLat, Bbox.MinLon, Bbox.MaxLon);

    State.bIsFetching = true;
    State.bOSMFetchDone = false;
    State.bDEMFetchDone = false;
    State.bOSMFetchSucceeded = false;
    State.bDEMFetchSucceeded = false;
    State.LastOSMFetchError.Empty();
    State.LastDEMFetchError.Empty();
    State.FetchStatusText = FText::FromString(TEXT("Fetching OSM data + elevation..."));

    if (FOSMRegionCache::HasCachedOSM(Bbox))
    {
        OnOverpassFetchComplete(true, FString());
    }
    else
    {
        FOSMOverpassClient::FetchAsync(Bbox, FOSMRegionCache::GetOSMFilePath(Bbox),
            FOSMOverpassClient::FOnFetchComplete::CreateSP(this, &SOSMImportWizard::OnOverpassFetchComplete));
    }

    if (FOSMRegionCache::HasCachedDEM(Bbox))
    {
        OnOpenTopographyFetchComplete(true, FString());
    }
    else
    {
        FOSMOpenTopographyClient::FetchAsync(Bbox, FOSMRegionCache::GetDEMFilePath(Bbox),
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
        FOSMRegionCache::FBoundingBox Bbox{ State.FetchMinLat, State.FetchMaxLat, State.FetchMinLon, State.FetchMaxLon };
        State.OSMFilePath = FOSMRegionCache::GetOSMFilePath(Bbox);
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
        FOSMRegionCache::FBoundingBox Bbox{ State.FetchMinLat, State.FetchMaxLat, State.FetchMinLon, State.FetchMaxLon };
        State.DEMFilePath = FOSMRegionCache::GetDEMFilePath(Bbox);
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
        AnalyzeSelectedFiles();

        // AnalyzeSelectedFiles() rescans the fetched OSM file and can end up with bounds
        // far larger than what was actually requested: Overpass's "(._;>;)" recursion pulls
        // in the FULL geometry of any relation that merely overlaps the query bbox (e.g. an
        // administrative/state boundary spanning hundreds of km), which blows out the
        // computed extent and mis-sizes/mis-places the terrain. For an auto-fetched region
        // we already know the true requested bbox — trust that instead of the rescan.
        State.MinLat = State.FetchMinLat;
        State.MaxLat = State.FetchMaxLat;
        State.MinLon = State.FetchMinLon;
        State.MaxLon = State.FetchMaxLon;
        State.GeoOrigin.Latitude = (State.MinLat + State.MaxLat) * 0.5;
        State.GeoOrigin.Longitude = (State.MinLon + State.MaxLon) * 0.5;
        State.GeoOrigin.HeightMeters = 0.0;
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
    State.StatusText = FText::FromString(TEXT("Parsing OpenStreetMap file..."));
    State.ProgressPercent = 0.1f;

    const FString OSMPath = State.OSMFilePath;
    const bool bGenTerrain = State.bGenerateTerrain;
    const bool bGenRoads = State.bGenerateRoads;
    const bool bGenBuildings = State.bGenerateBuildings;

    // Resolve the region to generate at this single point of use, rather than trusting
    // State.MinLat/MaxLat/GeoOrigin as-is: those fields do double duty as a manual-file-scan
    // preview (rewritten by AnalyzeSelectedFiles(), which itself runs from five different call
    // sites — both file Browse buttons and the post-fetch completion handler) AND as the
    // generation source of truth, and the two purposes have been observed to fight over the
    // same fields. The bbox that was actually validated and fetched (FetchMinLat/MaxLat/...,
    // gated to a few km by OnFetchRegionClicked before any network call is made) is the one
    // value that cannot lie about what region was requested, so prefer it whenever it's set.
    double RegionMinLat = 0.0, RegionMaxLat = 0.0, RegionMinLon = 0.0, RegionMaxLon = 0.0;
    bool bHaveRegion = false;
    const bool bFetchRegionValid = (State.FetchMaxLat > State.FetchMinLat) && (State.FetchMaxLon > State.FetchMinLon);
    if (bFetchRegionValid)
    {
        RegionMinLat = State.FetchMinLat; RegionMaxLat = State.FetchMaxLat;
        RegionMinLon = State.FetchMinLon; RegionMaxLon = State.FetchMaxLon;
        bHaveRegion = true;
    }
    else if (State.MaxLat > State.MinLat && State.MaxLon > State.MinLon)
    {
        RegionMinLat = State.MinLat; RegionMaxLat = State.MaxLat;
        RegionMinLon = State.MinLon; RegionMaxLon = State.MaxLon;
        bHaveRegion = true;
    }

    FOSMGeoOrigin Origin;
    if (bHaveRegion)
    {
        Origin.Latitude = (RegionMinLat + RegionMaxLat) * 0.5;
        Origin.Longitude = (RegionMinLon + RegionMaxLon) * 0.5;
        Origin.HeightMeters = 0.0;
    }
    else
    {
        Origin = State.GeoOrigin;
    }

    // Which file is actually being generated from, and over what region — from both possible
    // sources — is the single most useful thing to know when the output is wrong.
    UE_LOG(LogTemp, Log,
        TEXT("OSM generation: file='%s' | FetchRegion(valid=%d) lat[%.6f..%.6f] lon[%.6f..%.6f] | ")
        TEXT("State.MinLat/MaxLat lat[%.6f..%.6f] lon[%.6f..%.6f] | chosen region lat[%.6f..%.6f] lon[%.6f..%.6f] | origin lat %.6f lon %.6f"),
        *OSMPath, bFetchRegionValid ? 1 : 0,
        State.FetchMinLat, State.FetchMaxLat, State.FetchMinLon, State.FetchMaxLon,
        State.MinLat, State.MaxLat, State.MinLon, State.MaxLon,
        RegionMinLat, RegionMaxLat, RegionMinLon, RegionMaxLon,
        Origin.Latitude, Origin.Longitude);

    // Parse OSM XML file
    FOSMParseResult ParseResult;
    if (!FOSMParser::Parse(OSMPath, ParseResult))
    {
        State.StatusText = FText::FromString(TEXT("Failed to parse OSM file."));
        State.bIsGenerating = false;
        return FReply::Handled();
    }

    State.StatusText = FText::FromString(TEXT("Classifying features and computing ENU coordinates..."));
    State.ProgressPercent = 0.4f;

    // Tag Classification. Clip to the requested region first: an Overpass bbox query returns
    // any relation whose bounding box merely overlaps the request (boundaries, long-distance
    // routes, rivers) in full, and a single such feature is enough to blow the computed
    // extent out to hundreds of km — which then mis-sizes the terrain and crashes the
    // renderer on float precision.
    FOSMFeatureTable FeatureTable;
    if (bHaveRegion)
    {
        FeatureTable.SetClipBounds(RegionMinLat, RegionMinLon, RegionMaxLat, RegionMaxLon);
    }

    FOSMTagClassifier Classifier;
    Classifier.ClassifyAll(ParseResult, FeatureTable);

    const FVector2D ExtentKm = FeatureTable.GetApproxExtentKm();
    UE_LOG(LogTemp, Log, TEXT("OSM generation: %d features after clipping | extent ~%.2f x %.2f km"),
        FeatureTable.Num(), ExtentKm.X, ExtentKm.Y);

    State.StatusText = FText::FromString(TEXT("Spawning 3D Actors in Unreal World..."));
    State.ProgressPercent = 0.8f;

    UWorld* TargetWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (TargetWorld)
    {
        FOSMSceneSetup::EnsureBasicSceneSetup(TargetWorld);

        UOSMCRSTransformer* Transformer = NewObject<UOSMCRSTransformer>();
        Transformer->Initialize(Origin, EOSMProjectionMode::ENU);

        FOSMGenerationContext Context;
        Context.CRSTransformer = Transformer;
        Context.TargetWorld = TargetWorld;
        Context.FeatureTable = &FeatureTable;

        // Shared ground-elevation source: terrain, roads and buildings must all snap to the
        // same surface or they end up separated by the region's absolute elevation.
        Context.DEMFilePath = State.DEMFilePath;

        // Size terrain to the region that was asked for, not to whatever the file contained.
        if (bHaveRegion)
        {
            Context.SetTargetBounds(RegionMinLat, RegionMinLon, RegionMaxLat, RegionMaxLon);
        }

        TArray<AActor*> SpawnedActors;

        if (bGenTerrain)
        {
            UOSMTerrainGenerator* TerrainGen = NewObject<UOSMTerrainGenerator>();

            // Hand the fetched/selected DEM to the generator. Without this the generator's
            // own TerrainSettings.DEMFilePath stays empty and it silently takes its
            // "No DEM specified -> flat terrain" path, so a perfectly good .tif sitting on
            // disk is downloaded, cached, and then ignored.
            TerrainGen->TerrainSettings.DEMFilePath = State.DEMFilePath;

            UE_LOG(LogTemp, Log, TEXT("OSM generation: DEM file='%s'"),
                State.DEMFilePath.IsEmpty() ? TEXT("<none - flat terrain>") : *State.DEMFilePath);

            TArray<const FOSMFeature*> TerrainFeatures = FeatureTable.GetFeaturesByType(EOSMFeatureType::Landuse);
            TerrainGen->Generate(Context, TerrainFeatures, SpawnedActors);
        }

        if (bGenRoads)
        {
            UOSMRoadGenerator* RoadGen = NewObject<UOSMRoadGenerator>();
            TArray<const FOSMFeature*> RoadFeatures = FeatureTable.GetFeaturesByType(EOSMFeatureType::Highway);
            RoadGen->Generate(Context, RoadFeatures, SpawnedActors);
        }

        if (bGenBuildings)
        {
            UOSMBuildingGenerator* BuildingGen = NewObject<UOSMBuildingGenerator>();
            TArray<const FOSMFeature*> BuildingFeatures = FeatureTable.GetFeaturesByType(EOSMFeatureType::Building);
            BuildingGen->Generate(Context, BuildingFeatures, SpawnedActors);
        }

        // Water, vegetation, land use, parks, railways, barriers, power. These were already
        // parsed and classified but no generator consumed them, so they never reached the
        // scene — which is why only buildings and roads were ever visible.
        {
            UOSMAreaFeatureGenerator* AreaGen = NewObject<UOSMAreaFeatureGenerator>();
            AreaGen->Generate(Context, SpawnedActors);
        }

        State.GeneratedActorCount = SpawnedActors.Num();
    }

    State.StatusText = FText::FromString(TEXT("Generation Complete!"));
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

// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "CRS/FOSMGeoOrigin.h"
#include "CRS/EOSMProjectionMode.h"
#include "Fetch/FOSMNominatimClient.h"
#include "Region/FOSMRegion.h"
#include "Validation/FOSMImportReport.h"

class SWidget;

/**
 * Hard cap on the requested area (plan_v2_workflow.md §3.1's size gate, now expressed as a
 * single number the user types rather than fixed tiers).
 *
 * 25 km^2 is a 5 km square, which matches the terrain generator's own MaxTerrainExtentKm
 * safety limit — beyond that a Landscape starts exceeding the renderer's float precision
 * budget, and dense urban bboxes also start timing out on the public Overpass servers.
 */
static constexpr double GOSMMaxRegionAreaSqKm = OSMRegionLimits::MaxAreaSqKm;
static constexpr double GOSMMinRegionAreaSqKm = OSMRegionLimits::MinAreaSqKm;

/**
 * 5-Step Editor Import Wizard for OSMWorldGen.
 * Provides unified file selection (.osm + .tif), automatic spatial extent verification,
 * CRS configuration, feature layer filtering, and real-time generation progress.
 */
class OSMWORLDGENEDITOR_API SOSMImportWizard : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SOSMImportWizard) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    /** Wizard import state settings */
    struct FWizardState
    {
        FString OSMFilePath;
        FString DEMFilePath;

        // ---- Region auto-fetch (plan_v2_workflow.md Stage 0-2) ----
        /**
         * Area the user asked for, in km^2. This is the authoritative size control: the
         * lat/lon box below is always derived from it around a centre point, so there is
         * exactly one place that decides how big a region is.
         */
        double RequestedAreaSqKm = 1.0;
        /** Raw text of the area box, kept separately so a half-typed value isn't clobbered. */
        FString AreaInputText = TEXT("1.0");

        /**
         * The region being imported — the single source of truth (plan_v3_pipeline.md 1.1).
         *
         * Every consumer reads this. Nothing recomputes a region from file geometry, from the
         * scan preview below, or from anything else: doing so is what previously turned a
         * 1.5 km request into a 500 km landscape.
         */
        FOSMRegion Region;

        /** Editable text for the four bounds, so a half-typed coordinate isn't clobbered. */
        FString BoundsText[4];
        FString PastedBboxRawText;
        FString PlaceSearchQuery;
        bool bIsSearchingPlace = false;
        bool bIsFetching = false;
        bool bOSMFetchDone = false;
        bool bDEMFetchDone = false;
        bool bOSMFetchSucceeded = false;
        bool bDEMFetchSucceeded = false;
        FString LastOSMFetchError;
        FString LastDEMFetchError;
        FText FetchStatusText;

        /**
         * Extent observed by scanning manually-selected files, shown as a preview before import.
         *
         * Deliberately NOT a region: this is an observation about file contents, and reading it
         * as the import target is precisely the confusion that Region above exists to prevent.
         */
        FOSMRegion ScanPreviewBounds;
        bool bHasValidExtents = false;
        bool bDEMOverlapsOSM = false;

        // CRS
        FOSMGeoOrigin GeoOrigin;
        EOSMProjectionMode ProjectionMode = EOSMProjectionMode::ENU;

        // Layer toggles
        bool bGenerateTerrain = true;
        bool bGenerateRoads = true;
        bool bGenerateBuildings = true;

        // Default fallbacks
        float DefaultBuildingHeightCm = 900.0f;
        float DefaultRoadWidthCm = 800.0f;

        // Progress
        float ProgressPercent = 0.0f;
        FText StatusText;
        bool bIsGenerating = false;
        int32 GeneratedActorCount = 0;

        /**
         * Human-readable result of the import: region, extent, per-category feature counts,
         * DEM coverage. Replaces the old "N actors spawned" summary — the wizard no longer
         * generates anything (plan_v3_pipeline.md Phase 0), so what matters is whether the
         * data was understood correctly.
         */
        FString ImportSummary;

        /** Structured outcome of the last import run. Drives the summary step. */
        FOSMImportReport ImportReport;
        bool bHasImportReport = false;
    };

    /** Opens the wizard window in Unreal Editor */
    static void OpenWizardWindow();

private:
    int32 CurrentStepIndex = 0;
    FWizardState State;

    TSharedPtr<SWidget> ConstructStep0_FileSelect();
    TSharedPtr<SWidget> ConstructRegionFetchSection();
    TSharedPtr<SWidget> ConstructStep1_CRSConfig();
    TSharedPtr<SWidget> ConstructStep2_LayerFilter();
    TSharedPtr<SWidget> ConstructStep3_Execution();
    TSharedPtr<SWidget> ConstructStep4_Summary();

    FReply OnBrowseOSMFile();
    FReply OnBrowseDEMFile();
    void AnalyzeSelectedFiles();

    FReply OnFetchRegionClicked();
    FReply OnSearchPlaceClicked();
    void OnGeocodeComplete(bool bSuccess, const FString& ErrorMessage, const FOSMNominatimClient::FGeocodeResult& Result);
    FReply OnOpenBrowserClicked();
    FReply OnParseBboxClicked();
    FReply OnPasteClipboardClicked();
    static bool ParseBboxFromText(const FString& Text, double& OutMinLat, double& OutMinLon, double& OutMaxLat, double& OutMaxLon);
    void OnOverpassFetchComplete(bool bSuccess, const FString& ErrorMessage);
    void OnOpenTopographyFetchComplete(bool bSuccess, const FString& ErrorMessage);
    void CheckFetchCompletion();

    /** Rebuild the fetch bbox as a square of RequestedAreaSqKm centred on the given point. */
    void ApplyRequestedAreaAround(double CenterLat, double CenterLon);
    /** Centre of the current fetch bbox; false if no valid region is set yet. */
    bool GetCurrentRegionCenter(double& OutLat, double& OutLon) const;
    /** Re-parse the area box and reshape the bbox around its existing centre. */
    void OnAreaTextCommitted();

    /** Rewrite the four editable bounds fields from the current region. */
    void RefreshBoundsText();
    /** Build a region from the typed bounds. Returns false and reports why on rejection. */
    bool TrySetRegionFromBoundsText();

    FReply OnNextClicked();
    FReply OnPrevClicked();
    FReply OnStartGeneration();
};

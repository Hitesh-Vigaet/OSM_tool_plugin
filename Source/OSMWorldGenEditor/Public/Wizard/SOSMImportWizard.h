// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "CRS/FOSMGeoOrigin.h"
#include "CRS/EOSMProjectionMode.h"
#include "Fetch/FOSMNominatimClient.h"

class SWidget;

/**
 * Hard cap on the requested area (plan_v2_workflow.md §3.1's size gate, now expressed as a
 * single number the user types rather than fixed tiers).
 *
 * 25 km^2 is a 5 km square, which matches the terrain generator's own MaxTerrainExtentKm
 * safety limit — beyond that a Landscape starts exceeding the renderer's float precision
 * budget, and dense urban bboxes also start timing out on the public Overpass servers.
 */
static constexpr double GOSMMaxRegionAreaSqKm = 25.0;
static constexpr double GOSMMinRegionAreaSqKm = 0.05;

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

        double FetchMinLat = 0.0, FetchMaxLat = 0.0;
        double FetchMinLon = 0.0, FetchMaxLon = 0.0;
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

        // Bounding box computed from selected files
        double MinLat = 0.0, MaxLat = 0.0;
        double MinLon = 0.0, MaxLon = 0.0;
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

    FReply OnNextClicked();
    FReply OnPrevClicked();
    FReply OnStartGeneration();
};

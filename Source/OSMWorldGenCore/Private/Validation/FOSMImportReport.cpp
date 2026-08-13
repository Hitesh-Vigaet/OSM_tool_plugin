// Copyright InviMind. All Rights Reserved.

#include "Validation/FOSMImportReport.h"
#include "Classification/FOSMTagClassifier.h"
#include "Elevation/FOSMGeoTIFFTile.h"
#include "Model/FOSMFeatureTable.h"
#include "OSMWorldGenCore.h"
#include "Parsing/FOSMParseResult.h"
#include "Misc/Paths.h"

// ---------------------------------------------------------------------------
FOSMValidationResult FOSMImportValidator::ValidateCrossFile(
    const FOSMRegion& Region,
    const FOSMRegion& OSMDataBounds,
    const FOSMRegion& DEMBounds,
    double& OutCoverageFraction)
{
    FOSMValidationResult Result;
    OutCoverageFraction = 0.0;

    if (!Region.IsValid())
    {
        Result.AddFatal(TEXT("cross.region.invalid"),
            TEXT("The import region is invalid, so the two files cannot be checked against each other."));
        return Result;
    }

    if (!DEMBounds.IsValid())
    {
        Result.AddFatal(TEXT("cross.dem.bounds.invalid"),
            TEXT("The elevation raster has no valid geographic extent."));
        return Result;
    }

    // The check the user explicitly asked for: are these two files describing the same place?
    // Comparing the two DATA extents (not the requested region) catches the case where both
    // files are individually valid but were fetched for different areas.
    if (OSMDataBounds.IsValid() && !OSMDataBounds.Intersects(DEMBounds))
    {
        Result.AddFatal(TEXT("cross.files.disjoint"),
            FString::Printf(
                TEXT("The .osm data (%s) and the elevation raster (%s) do not overlap at all — ")
                TEXT("these are files for two different places."),
                *OSMDataBounds.ToString(), *DEMBounds.ToString()));
        return Result;
    }

    OutCoverageFraction = Region.CoverageBy(DEMBounds);

    if (OutCoverageFraction < MinDEMCoverageFraction)
    {
        Result.AddFatal(TEXT("cross.dem.coverage.insufficient"),
            FString::Printf(
                TEXT("Elevation covers only %.1f%% of the requested region (%s vs raster %s); ")
                TEXT("at least %.0f%% is required. Re-fetch the DEM for this region."),
                OutCoverageFraction * 100.0, *Region.ToString(), *DEMBounds.ToString(),
                MinDEMCoverageFraction * 100.0));
        return Result;
    }

    if (!DEMBounds.Contains(Region))
    {
        // Partial shortfall above the fatal threshold. Normal and benign: DEM providers return
        // whole source-grid cells, so a raster can land a fraction of a cell inside the region.
        Result.AddWarning(TEXT("cross.dem.coverage.partial"),
            FString::Printf(
                TEXT("Elevation covers %.2f%% of the region — %.2f%% of the area has no elevation ")
                TEXT("data and will be interpolated from the raster edge."),
                OutCoverageFraction * 100.0, (1.0 - OutCoverageFraction) * 100.0));
    }

    return Result;
}

// ---------------------------------------------------------------------------
FOSMImportReport FOSMImportValidator::Run(
    const FOSMRegion& Region,
    const FString& OSMFilePath,
    const FString& DEMFilePath,
    FOSMFeatureTable& OutFeatures)
{
    const double StartTime = FPlatformTime::Seconds();

    FOSMImportReport Report;
    Report.Region = Region;
    Report.OSMFilePath = OSMFilePath;
    Report.DEMFilePath = DEMFilePath;
    Report.bHasDEM = !DEMFilePath.IsEmpty();

    if (!Region.IsValid())
    {
        Report.Validation.AddFatal(TEXT("import.region.invalid"),
            TEXT("No valid region was resolved for this import. Select or fetch a region first."));
        Report.DurationSeconds = FPlatformTime::Seconds() - StartTime;
        return Report;
    }

    // ---- Gate 1: the .osm file ----
    FOSMParseResult Parsed;
    FOSMValidationResult OSMResult =
        FOSMDataValidator::ValidateAgainstRegion(OSMFilePath, Region, Parsed, Report.OSMStats);
    Report.Validation.Append(OSMResult);

    if (OSMResult.HasFatal())
    {
        Report.DurationSeconds = FPlatformTime::Seconds() - StartTime;
        return Report;
    }

    // ---- Gate 2: the .tif file ----
    FOSMGeoTIFFTile Tile;
    TArray<float> HeightData;

    if (Report.bHasDEM)
    {
        FOSMValidationResult DEMResult =
            FOSMDEMValidator::ValidateLoaded(DEMFilePath, Tile, HeightData, Report.DEMStats);
        Report.Validation.Append(DEMResult);

        if (DEMResult.HasFatal())
        {
            Report.DurationSeconds = FPlatformTime::Seconds() - StartTime;
            return Report;
        }

        // ---- Gate 3: the two files against each other ----
        FOSMValidationResult CrossResult = ValidateCrossFile(
            Region, Report.OSMStats.DataBounds, Report.DEMStats.DataBounds, Report.DEMCoverageFraction);
        Report.Validation.Append(CrossResult);

        if (CrossResult.HasFatal())
        {
            Report.DurationSeconds = FPlatformTime::Seconds() - StartTime;
            return Report;
        }
    }
    else
    {
        Report.Validation.AddWarning(TEXT("import.dem.absent"),
            TEXT("No elevation file supplied — the region will be treated as flat ground."));
    }

    // ---- Classification, clipped to the region ----
    //
    // Clipping happens here rather than at generation time because an Overpass bbox query
    // returns any way or relation whose own bounding box merely *overlaps* the request, in
    // full. A single motorway or river crossing the box is enough to drag the extent out by
    // tens of km, which is what previously mis-sized the terrain and scattered the city.
    OutFeatures = FOSMFeatureTable();
    OutFeatures.SetClipBounds(
        Region.GetMinLat(), Region.GetMinLon(), Region.GetMaxLat(), Region.GetMaxLon());

    FOSMTagClassifier::ClassifyAll(Parsed, OutFeatures);

    Report.TotalFeatures = OutFeatures.Num();
    for (uint8 TypeIdx = 0; TypeIdx < static_cast<uint8>(EOSMFeatureType::MAX); ++TypeIdx)
    {
        const EOSMFeatureType Type = static_cast<EOSMFeatureType>(TypeIdx);
        const int32 Count = OutFeatures.GetCountByType(Type);
        if (Count > 0)
        {
            Report.FeatureCounts.Add(Type, Count);
        }
    }

    if (Report.TotalFeatures == 0)
    {
        // Every gate passed and nothing survived clipping: the file described a real place, but
        // not this one. Silently producing an empty city is worse than refusing.
        Report.Validation.AddFatal(TEXT("import.features.none"),
            FString::Printf(
                TEXT("No features remain after clipping %d nodes / %d ways to the region (%s). ")
                TEXT("The file's geometry lies outside the area you asked for."),
                Report.OSMStats.NodeCount, Report.OSMStats.WayCount, *Region.ToString()));
    }

    Report.DurationSeconds = FPlatformTime::Seconds() - StartTime;
    return Report;
}

// ---------------------------------------------------------------------------
FString FOSMImportReport::ToDisplayString() const
{
    TArray<FString> Lines;

    Lines.Add(IsAccepted()
        ? TEXT("Import ACCEPTED — data validated, no 3D assets generated (by design).")
        : TEXT("Import REJECTED — see the reasons below. Nothing was imported."));
    Lines.Add(TEXT(""));

    // A rejection must lead with its cause, not bury it under statistics.
    if (!IsAccepted())
    {
        Lines.Add(TEXT("Reason:"));
        for (const FOSMValidationIssue& Issue : Validation.Issues)
        {
            if (Issue.Severity == EOSMIssueSeverity::Fatal)
            {
                Lines.Add(FString::Printf(TEXT("  %s"), *Issue.Message));
            }
        }
        Lines.Add(TEXT(""));
    }

    Lines.Add(FString::Printf(TEXT("Region:     %s"), *Region.ToString()));
    if (Region.IsValid())
    {
        Lines.Add(FString::Printf(TEXT("Extent:     %.2f x %.2f km  (%.2f km2)"),
            Region.GetWidthKm(), Region.GetHeightKm(), Region.GetAreaSqKm()));
    }

    Lines.Add(FString::Printf(TEXT("OSM file:   %s"),
        OSMFilePath.IsEmpty() ? TEXT("<none>") : *FPaths::GetCleanFilename(OSMFilePath)));
    Lines.Add(FString::Printf(TEXT("DEM file:   %s"),
        bHasDEM ? *FPaths::GetCleanFilename(DEMFilePath) : TEXT("<none — flat ground>")));

    if (OSMStats.NodeCount > 0)
    {
        Lines.Add(TEXT(""));
        Lines.Add(FString::Printf(TEXT("Parsed:     %d nodes, %d ways, %d relations"),
            OSMStats.NodeCount, OSMStats.WayCount, OSMStats.RelationCount));
        Lines.Add(FString::Printf(TEXT("Data bounds: %s"), *OSMStats.DataBounds.ToString()));
    }

    if (TotalFeatures > 0)
    {
        Lines.Add(TEXT(""));
        Lines.Add(FString::Printf(TEXT("Features:   %d after clipping to the region"), TotalFeatures));

        // Sorted by count so the dominant categories read first.
        TArray<TPair<EOSMFeatureType, int32>> Sorted;
        for (const TPair<EOSMFeatureType, int32>& Pair : FeatureCounts)
        {
            Sorted.Add(Pair);
        }
        Sorted.Sort([](const TPair<EOSMFeatureType, int32>& A, const TPair<EOSMFeatureType, int32>& B)
        {
            return A.Value > B.Value;
        });

        for (const TPair<EOSMFeatureType, int32>& Pair : Sorted)
        {
            Lines.Add(FString::Printf(TEXT("  %-16s %d"),
                *OSMFeatureTypeToString(Pair.Key), Pair.Value));
        }
    }

    if (bHasDEM && DEMStats.Width > 0)
    {
        Lines.Add(TEXT(""));
        Lines.Add(FString::Printf(TEXT("Elevation:  %d x %d px at ~%.0f m/px"),
            DEMStats.Width, DEMStats.Height, DEMStats.ResolutionMeters));
        Lines.Add(FString::Printf(TEXT("            %.1f to %.1f m, %.1f%% NoData"),
            DEMStats.MinElevation, DEMStats.MaxElevation, DEMStats.NoDataFraction * 100.0));
        Lines.Add(FString::Printf(TEXT("            covers %.2f%% of the region"),
            DEMCoverageFraction * 100.0));
    }

    const int32 WarningCount = Validation.CountOf(EOSMIssueSeverity::Warning);
    if (WarningCount > 0)
    {
        Lines.Add(TEXT(""));
        Lines.Add(FString::Printf(TEXT("Warnings (%d):"), WarningCount));
        for (const FOSMValidationIssue& Issue : Validation.Issues)
        {
            if (Issue.Severity == EOSMIssueSeverity::Warning)
            {
                Lines.Add(FString::Printf(TEXT("  - %s"), *Issue.Message));
            }
        }
    }

    Lines.Add(TEXT(""));
    Lines.Add(FString::Printf(TEXT("Completed in %.2f s."), DurationSeconds));

    if (IsAccepted())
    {
        Lines.Add(TEXT(""));
        Lines.Add(TEXT("Next: the City Graph and Control Center (plan_v3_pipeline.md Phases 2-3) will turn "));
        Lines.Add(TEXT("these features into inspectable nodes and relationships before any geometry is built."));
    }

    return FString::Join(Lines, TEXT("\n"));
}

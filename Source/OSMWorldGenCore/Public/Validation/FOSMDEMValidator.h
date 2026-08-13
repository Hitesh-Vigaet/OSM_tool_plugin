// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Elevation/FOSMGeoTIFFTile.h"
#include "Region/FOSMRegion.h"
#include "Validation/FOSMValidation.h"

/**
 * The `.tif` validation gate (plan_v3_pipeline.md Phase 1.3).
 *
 * Structured like the OSM gate: a cheap header-only pass that can reject a file without
 * decompressing it, then a full load for the checks that genuinely need pixel data.
 *
 * Every rejection names the offending value — "compression 7 (JPEG)" rather than "unsupported
 * format" — because the whole point of this phase is that a failure tells you what to fix.
 */
class OSMWORLDGENCORE_API FOSMDEMValidator
{
public:
    /** Facts gathered while validating, for the import report. */
    struct FStats
    {
        int32 Width = 0;
        int32 Height = 0;

        /** Georeferenced extent of the raster. NOT the import region. */
        FOSMRegion DataBounds;

        double ResolutionArcSeconds = 0.0;
        /** Approximate ground sample distance in metres at the raster's centre latitude. */
        double ResolutionMeters = 0.0;

        float MinElevation = 0.0f;
        float MaxElevation = 0.0f;

        /** Fraction of pixels equal to the NoData sentinel, in [0,1]. */
        double NoDataFraction = 0.0;
    };

    /**
     * Header-level check: format, layout, and presence of georeferencing.
     * Reads the file but decodes no pixels.
     */
    static FOSMValidationResult ValidateFile(const FString& FilePath);

    /**
     * Full gate: header checks, then load, then elevation-range and NoData checks.
     *
     * @param OutTile        Receives the loaded metadata so callers need not load twice.
     * @param OutHeightData  Receives the decoded raster.
     */
    static FOSMValidationResult ValidateLoaded(
        const FString& FilePath,
        FOSMGeoTIFFTile& OutTile,
        TArray<float>& OutHeightData,
        FStats& OutStats);

    // ---- Plausibility thresholds ----

    /** Below the Dead Sea shore / above Everest means the file is not elevation in metres. */
    static constexpr double MinPlausibleElevationM = -500.0;
    static constexpr double MaxPlausibleElevationM = 9000.0;

    /**
     * Pixel scale bounds in degrees. 1e-6 deg is ~11 cm and 0.01 deg is ~1.1 km; a value
     * outside this is a sign the tags were misread or the file is in projected units (metres)
     * rather than degrees, which this reader cannot handle.
     */
    static constexpr double MinPlausiblePixelScaleDeg = 1e-6;
    static constexpr double MaxPlausiblePixelScaleDeg = 0.01;

    /** Above this fraction of NoData pixels the DEM is mostly holes. */
    static constexpr double MaxNoDataFraction = 0.5;
};

// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Elevation/FOSMGeoTIFFTile.h"
#include "CRS/FOSMCRSTransformer.h"

/**
 * Utilities for aligning DEM pixel-grid coordinates with CRS/UE world coordinates.
 *
 * Key relationships:
 *   DEM pixel  ──GeoTransform──>  WGS84 Lat/Lon
 *   WGS84 Lat/Lon  ──CRSTransformer──>  UE World Space (cm)
 *
 * This class provides the direct DEM pixel → UE world space conversion
 * and coverage diagnostics.
 */
class OSMWORLDGENCORE_API FOSMDEMCRSAlignment
{
public:
    /**
     * Convert a DEM pixel (col, row) to UE world space (cm).
     *
     * @param Tile          GeoTIFF tile metadata
     * @param Transformer   Initialized CRS transformer
     * @param Col           Pixel column (0 = leftmost/westernmost)
     * @param Row           Pixel row   (0 = topmost/northernmost)
     * @param ElevMeters    Known elevation at this pixel (meters)
     * @return              UE world-space position in cm
     */
    static FVector DEMPixelToUnreal(
        const FOSMGeoTIFFTile& Tile,
        const UOSMCRSTransformer& Transformer,
        int32 Col, int32 Row,
        double ElevMeters = 0.0);

    /**
     * Compute what fraction of the import area is covered by the DEM.
     * Returns a value in [0, 1]. < 1 means the DEM doesn't fully cover the area.
     *
     * @param Tile     Loaded DEM tile
     * @param MinLat   Import south bound
     * @param MaxLat   Import north bound
     * @param MinLon   Import west bound
     * @param MaxLon   Import east bound
     */
    static float ComputeDEMCoverage(
        const FOSMGeoTIFFTile& Tile,
        double MinLat, double MaxLat,
        double MinLon, double MaxLon);

    /**
     * Check the alignment error at a specific test point.
     * Converts pixel → WGS84 → UE and back to WGS84 via inverse transform,
     * reporting the roundtrip geographic error in arc-seconds.
     *
     * @return  Max absolute error in arc-seconds (should be < 0.01 for correct alignment)
     */
    static double MeasureAlignmentError(
        const FOSMGeoTIFFTile& Tile,
        const UOSMCRSTransformer& Transformer,
        int32 TestCol, int32 TestRow);

    /**
     * Returns the DEM pixel grid spacing in Unreal cm at the tile centre.
     * Useful for sanity-checking Landscape XY scale vs DEM resolution.
     */
    static FVector2D GetDEMPixelSizeCm(
        const FOSMGeoTIFFTile& Tile,
        const UOSMCRSTransformer& Transformer);
};

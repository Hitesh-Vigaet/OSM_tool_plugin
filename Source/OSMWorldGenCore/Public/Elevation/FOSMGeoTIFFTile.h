// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Metadata extracted from a GeoTIFF tile header.
 * Captures everything needed to geo-reference the raster.
 */
struct OSMWORLDGENCORE_API FOSMGeoTIFFTile
{
    /** Raster width in pixels */
    int32 Width = 0;

    /** Raster height in pixels */
    int32 Height = 0;

    /**
     * GDAL-style 6-coefficient affine GeoTransform:
     *   [0] = top-left corner longitude (X)
     *   [1] = pixel width  in degrees
     *   [2] = row rotation (0 for north-up)
     *   [3] = top-left corner latitude  (Y)
     *   [4] = column rotation (0 for north-up)
     *   [5] = pixel height in degrees (negative for north-up images)
     */
    double GeoTransform[6] = { 0, 1, 0, 0, 0, -1 };

    /** EPSG code of the coordinate reference system (4326 = WGS84) */
    int32 EPSG = 4326;

    /** Sentinel value representing missing/invalid data */
    double NoDataValue = -9999.0;

    /** Whether a NoData value is defined in the file */
    bool bHasNoData = false;

    /** Minimum valid elevation in meters */
    float MinElevation = 0.0f;

    /** Maximum valid elevation in meters */
    float MaxElevation = 0.0f;

    /** Source file path */
    FString FilePath;

    // ---- Geo-transform helpers ----

    /** Convert pixel column + row to WGS84 longitude */
    double PixelToLon(double Col) const
    {
        return GeoTransform[0] + Col * GeoTransform[1] + 0.5 * GeoTransform[1];
    }

    /** Convert pixel column + row to WGS84 latitude */
    double PixelToLat(double Row) const
    {
        return GeoTransform[3] + Row * GeoTransform[5] + 0.5 * GeoTransform[5];
    }

    /** Convert WGS84 lon to floating pixel column */
    double LonToPixelCol(double Lon) const
    {
        if (FMath::IsNearlyZero(GeoTransform[1])) return 0.0;
        return (Lon - GeoTransform[0] - 0.5 * GeoTransform[1]) / GeoTransform[1];
    }

    /** Convert WGS84 lat to floating pixel row */
    double LatToPixelRow(double Lat) const
    {
        if (FMath::IsNearlyZero(GeoTransform[5])) return 0.0;
        return (Lat - GeoTransform[3] - 0.5 * GeoTransform[5]) / GeoTransform[5];
    }

    /** Geographic bounds */
    double GetMinLon() const { return GeoTransform[0]; }
    double GetMaxLon() const { return GeoTransform[0] + Width  * GeoTransform[1]; }
    double GetMinLat() const { return GeoTransform[3] + Height * GeoTransform[5]; } // GeoTransform[5] < 0
    double GetMaxLat() const { return GeoTransform[3]; }

    /** Resolution in arc-seconds per pixel */
    double GetResolutionArcSeconds() const { return FMath::Abs(GeoTransform[1]) * 3600.0; }

    /** Check if a WGS84 coordinate is within this tile's extent */
    bool ContainsCoordinate(double Lat, double Lon) const
    {
        return Lon >= GetMinLon() && Lon <= GetMaxLon()
            && Lat >= GetMinLat() && Lat <= GetMaxLat();
    }
};

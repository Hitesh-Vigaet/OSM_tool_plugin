// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Elevation/FOSMGeoTIFFTile.h"

/**
 * Lightweight GeoTIFF reader that operates without GDAL.
 *
 * Supports:
 *   - GeoTIFF (.tif/.tiff) with GeoKeyDirectoryTag and ModelTransformationTag
 *   - SRTM HGT files (.hgt) — fixed 1-arcsec (3601×3601) and 3-arcsec (1201×1201)
 *   - Single-band 16-bit or 32-bit float elevation rasters
 *
 * Limitations (vs GDAL):
 *   - Does not support projected CRS (assumes EPSG:4326 / WGS84 geographic)
 *   - Does not support tiled or stripped multi-band TIFFs beyond band 0
 *   - Does not handle coordinate system reprojection
 *
 * When OSM_WITH_GDAL=1, these limitations are lifted (GDAL handles all formats).
 */
class OSMWORLDGENCORE_API FOSMGeoTIFFReader
{
public:
    /**
     * Load a GeoTIFF or SRTM HGT file into OutTile and OutHeightData.
     *
     * @param FilePath          Path to the elevation file
     * @param OutTile           Metadata struct to populate
     * @param OutHeightData     Row-major float array, size Width×Height in meters
     * @return                  True if loading succeeded
     */
    static bool Load(
        const FString& FilePath,
        FOSMGeoTIFFTile& OutTile,
        TArray<float>& OutHeightData);

private:
    /** Load a SRTM .hgt file (binary big-endian int16, fixed sizes) */
    static bool LoadHGT(
        const FString& FilePath,
        FOSMGeoTIFFTile& OutTile,
        TArray<float>& OutHeightData);

    /** Load a GeoTIFF .tif file (parse IFD, GeoKeys, and raster data) */
    static bool LoadTIFF(
        const FString& FilePath,
        FOSMGeoTIFFTile& OutTile,
        TArray<float>& OutHeightData);

    /** Parse lat/lon from SRTM HGT filename (e.g., N51W001.hgt → lat=51, lon=-1) */
    static bool ParseHGTFilename(const FString& Filename, int32& OutLat, int32& OutLon);
};

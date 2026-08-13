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
/**
 * Raw contents of a TIFF image file directory, before any interpretation.
 *
 * Extracted so the validation gate can inspect a file's structure — compression, layout,
 * georeferencing tags — without decompressing a single pixel, while still going through the
 * exact same tag-parsing code the loader uses. Two independent TIFF parsers would eventually
 * disagree, and the one the validator used would be the one that was wrong.
 */
struct OSMWORLDGENCORE_API FOSMGeoTIFFHeader
{
    bool bBigEndian = false;

    int32  Width = 0;
    int32  Height = 0;
    uint16 BitsPerSample = 16;
    uint16 SampleFormat = 2;          // TIFFConst::SAMPLEFORMAT_INT
    uint32 SamplesPerPixel = 1;
    uint16 Compression = 1;           // TIFFConst::COMPRESSION_NONE
    uint16 Predictor = 1;

    /** Striped layout. */
    uint32 StripOffset = 0;
    uint32 RowsPerStrip = 0;
    uint32 StripByteCount = 0;

    /** Tiled layout. */
    uint32 TileWidth = 0;
    uint32 TileLength = 0;
    uint32 TileOffset = 0;
    uint32 TileByteCount = 0;

    /** GeoTIFF georeferencing tags. */
    double PixelScaleX = 0.0;
    double PixelScaleY = 0.0;
    double TiepointX = 0.0;
    double TiepointY = 0.0;
    double TiepointLon = 0.0;
    double TiepointLat = 0.0;
    bool   bHasModelPixelScale = false;
    bool   bHasModelTiepoint = false;

    /** Raw GDAL_NODATA string, empty when the tag is absent. */
    FString NoDataStr;

    bool IsTiled() const { return TileWidth > 0 && TileLength > 0; }

    /** Number of tiles the raster is divided into (1 for a striped image). */
    int32 GetTileCount() const;

    /** True when the file carries enough information to be georeferenced at all. */
    bool HasGeoreferencing() const { return bHasModelPixelScale && bHasModelTiepoint; }
};

class OSMWORLDGENCORE_API FOSMGeoTIFFReader
{
public:
    /**
     * Parse the TIFF header and image file directory from an in-memory file.
     *
     * Reads tags only — no raster data is touched, so this is cheap enough to run as a
     * validation gate on every file before committing to a full load.
     *
     * @param Data      Complete file contents.
     * @param FilePath  Used only for error messages.
     * @param OutHeader Populated on success.
     * @param OutError  Populated with the specific reason on failure.
     */
    static bool ReadHeader(
        const TArray<uint8>& Data,
        const FString& FilePath,
        FOSMGeoTIFFHeader& OutHeader,
        FString& OutError);

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

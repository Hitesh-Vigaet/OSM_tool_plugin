// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Terrain/FOSMGeoTIFFTile.h"

/**
 * Samples elevation values from a loaded DEM raster using bilinear interpolation.
 *
 * Usage:
 *   FOSMDEMSampler Sampler;
 *   if (Sampler.Load(TEXT("/path/to/dem.tif")))
 *   {
 *       double Elevation = Sampler.SampleElevation(51.5007, -0.1246);
 *   }
 */
class OSMWORLDGENGENERATORS_API FOSMDEMSampler
{
public:
    /**
     * Load a GeoTIFF or SRTM HGT elevation file.
     *
     * @param FilePath     Path to .tif, .tiff, or .hgt file
     * @return             True if the file was loaded successfully
     */
    bool Load(const FString& FilePath);

    /** Check if a DEM has been successfully loaded */
    bool IsLoaded() const { return bIsLoaded; }

    /**
     * Sample elevation (meters) at the given WGS84 coordinate.
     * Uses 2×2 bilinear interpolation of the surrounding DEM pixels.
     *
     * @return  Elevation in meters, or NaN if outside DEM extent or NoData
     */
    double SampleElevation(double Latitude, double Longitude) const;

    /**
     * Sample elevation but return DefaultValue instead of NaN for out-of-bounds / NoData.
     */
    double SampleElevationSafe(double Latitude, double Longitude, double DefaultValue = 0.0) const;

    /** Returns true if the coordinate is within the loaded DEM extent */
    bool ContainsCoordinate(double Latitude, double Longitude) const;

    /** Loaded DEM metadata (tile dimensions, geotransform, bounds) */
    const FOSMGeoTIFFTile& GetTileMetadata() const { return Tile; }

    /** DEM spatial resolution in arc-seconds per pixel */
    double GetResolutionArcSeconds() const;

    /** Min and max valid elevations in the loaded DEM */
    float GetMinElevation() const { return Tile.MinElevation; }
    float GetMaxElevation() const { return Tile.MaxElevation; }

    /** Reset and release loaded data */
    void Reset();

private:
    FOSMGeoTIFFTile  Tile;
    TArray<float>    HeightData;   // Row-major, Tile.Width × Tile.Height
    bool             bIsLoaded = false;

    /**
     * Read a single pixel value. Returns NoDataValue if out of bounds.
     * Row 0 = top of raster (max latitude for north-up).
     */
    float GetPixelValue(int32 Col, int32 Row) const;

    /** True if a pixel value represents NoData */
    bool IsNoData(float Val) const;
};

// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CRS/FOSMGeoOrigin.h"

/**
 * Region size limits (plan_v3_pipeline.md Phase 1.1).
 *
 * 25 km^2 is a 5 km square. Beyond that a Landscape starts exceeding the renderer's float
 * precision budget, and dense urban bboxes start timing out on the public Overpass servers.
 * These live here, next to the only type allowed to construct a region, so there is exactly
 * one definition of "how big may a region be".
 */
namespace OSMRegionLimits
{
    static constexpr double MaxAreaSqKm = 25.0;
    static constexpr double MinAreaSqKm = 0.05;

    /** Mean meridional degree length. Good to ~0.1% for city-scale extents. */
    static constexpr double KmPerDegreeLat = 111.32;
}

/**
 * The single source of truth for "which patch of the world are we importing".
 *
 * This type exists because the same region was previously recomputed in several places from
 * different inputs — wizard state, re-parsed file geometry, generator context — and the copies
 * drifted. A 1.5 km request became a 500 km landscape because one consumer rebuilt the bounds
 * from feature geometry that included a way crossing the whole state.
 *
 * The rule this type enforces, structurally rather than by convention:
 *
 *   **A region can only be created through a validating factory, and no downstream code may
 *   derive a region from anything other than an existing FOSMRegion.**
 *
 * A default-constructed region is deliberately invalid, so a region that was never explicitly
 * built cannot be mistaken for the origin-centred box at (0, 0).
 *
 * Bounds observed while scanning a file are NOT a region — they are an observation about data,
 * and are carried separately (see FOSMImportReport::OSMDataBounds) precisely so they can never
 * be confused for the thing that defines the import.
 */
struct OSMWORLDGENCORE_API FOSMRegion
{
public:
    /** Default-constructed regions are invalid by design — see class comment. */
    FOSMRegion() = default;

    /**
     * Build a square region of the requested area centred on a point.
     * This is the wizard's primary path: the user types an area, everything else derives.
     *
     * @return false with OutError populated if the centre or area is out of range.
     */
    static bool FromCenterAndArea(
        double CenterLat, double CenterLon, double AreaSqKm,
        FOSMRegion& OutRegion, FString& OutError);

    /**
     * Build a region from an explicit bounding box (pasted coordinates, geocoder result,
     * or the extents of a manually supplied file pair).
     *
     * @return false with OutError populated if the box is malformed or outside the size limits.
     */
    static bool FromBoundingBox(
        double MinLat, double MinLon, double MaxLat, double MaxLon,
        FOSMRegion& OutRegion, FString& OutError);

    /**
     * As FromBoundingBox, but without the size limits — for describing the extent of data we
     * have observed (a DEM raster's footprint, an .osm file's geometry) rather than an import
     * target. Deliberately named so that using it as an import region reads as wrong.
     */
    static bool ObservedBounds(
        double MinLat, double MinLon, double MaxLat, double MaxLon,
        FOSMRegion& OutRegion, FString& OutError);

    bool IsValid() const { return bValid; }

    double GetMinLat() const { return MinLat; }
    double GetMaxLat() const { return MaxLat; }
    double GetMinLon() const { return MinLon; }
    double GetMaxLon() const { return MaxLon; }

    double GetCenterLat() const { return 0.5 * (MinLat + MaxLat); }
    double GetCenterLon() const { return 0.5 * (MinLon + MaxLon); }

    /** North-south extent in km. */
    double GetHeightKm() const;
    /** East-west extent in km, at the region's centre latitude. */
    double GetWidthKm() const;
    /** Area in km^2. */
    double GetAreaSqKm() const;

    /** The CRS origin this region implies: its centre. Never derive an origin any other way. */
    FOSMGeoOrigin GetGeoOrigin() const { return FOSMGeoOrigin(GetCenterLat(), GetCenterLon(), 0.0, false); }

    bool ContainsCoordinate(double Lat, double Lon) const;

    /** True if the two boxes share any area at all. */
    bool Intersects(const FOSMRegion& Other) const;

    /** True if Other lies entirely inside this region (inclusive). */
    bool Contains(const FOSMRegion& Other) const;

    /**
     * Fraction of THIS region's area that is also covered by Other, in [0,1].
     * This is the DEM coverage measure: Region.CoverageBy(DemBounds) answers "how much of what
     * I asked for do I actually have elevation for".
     */
    double CoverageBy(const FOSMRegion& Other) const;

    /**
     * Equality within a tolerance in degrees. Used by the cross-file gate to assert that the
     * .osm and .tif were fetched for the same region rather than two nearby ones.
     */
    bool EqualsWithin(const FOSMRegion& Other, double ToleranceDegrees) const;

    /** A region expanded by a margin in degrees, clamped to valid lat/lon. Never invalidates. */
    FOSMRegion Expanded(double MarginDegrees) const;

    /** "lat 12.971035..12.980018, lon 77.602181..77.611399" */
    FString ToString() const;

    /** Stable identity for cache keys: bounds rounded to ~1 cm, order-independent. */
    FString ToCacheKeyString() const;

private:
    double MinLat = 0.0;
    double MaxLat = 0.0;
    double MinLon = 0.0;
    double MaxLon = 0.0;
    bool bValid = false;

    /** Shared bounds sanity check used by every factory. bEnforceSizeLimits gates the area rules. */
    static bool BuildChecked(
        double MinLat, double MinLon, double MaxLat, double MaxLon,
        bool bEnforceSizeLimits, FOSMRegion& OutRegion, FString& OutError);
};

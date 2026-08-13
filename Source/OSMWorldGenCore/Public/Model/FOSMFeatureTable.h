// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/FOSMFeature.h"
#include "Model/EOSMFeatureType.h"
#include "Region/FOSMRegion.h"
#include "FOSMFeatureTable.generated.h"

/**
 * The feature table — the central container holding all classified OSM features.
 *
 * Produced by Stage 2 (Parse & Tag), consumed by Stages 3-5.
 * Provides indexed lookups by type, OSM ID, and spatial queries.
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMFeatureTable
{
    GENERATED_BODY()

    /** All features in insertion order */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    TArray<FOSMFeature> Features;

    /** Geographic bounding box of all features (lat/lon, before CRS transform) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    FVector2D BoundsMinLatLon = FVector2D(90.0, 180.0);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    FVector2D BoundsMaxLatLon = FVector2D(-90.0, -180.0);

    /**
     * Optional clip region (lat/lon). When set, features with no geometry anywhere near it
     * are rejected outright; features that do belong have every point CLAMPED into a padded
     * version of the region before being stored.
     *
     * This exists because an OSM file can legitimately contain features far larger than the
     * area that was asked for. Overpass's bbox filter matches any way or relation that merely
     * OVERLAPS the query box — it does not clip the geometry itself — so a single long
     * highway, administrative boundary, or river that only passes through a corner of a
     * 1.5 km query box comes back with its full real-world extent, sometimes tens or hundreds
     * of km. Verified directly against a fetched file: one road way alone spanned 0.365°
     * (~40 km) against a requested box of ~0.015° (~1.5 km). Left unclipped, that single
     * feature's far-flung points get converted to Unreal-space positions kilometers from
     * every other actor, rendering as geometry scattered off in space away from the terrain;
     * and if it dictates BoundsMin/MaxLatLon, the terrain generator sizes a Landscape to
     * match it (a ~500 km one was enough to exceed the renderer's float precision budget and
     * crash on a DoubleFloat.cpp ensure).
     *
     * Clamping (not true polygon/line clipping) is deliberate: it is a few lines, cannot
     * produce self-intersecting geometry, and the failure mode — a clamped feature's
     * out-of-region portion runs flat along the padding edge instead of stopping exactly at
     * the real crossing point — is a minor cosmetic wrinkle at the padding boundary, not a
     * scene-breaking spike kilometers away.
     */
    bool bHasClipBounds = false;
    FVector2D ClipMinLatLon = FVector2D::ZeroVector;
    FVector2D ClipMaxLatLon = FVector2D::ZeroVector;
    FVector2D PaddedClipMinLatLon = FVector2D::ZeroVector;
    FVector2D PaddedClipMaxLatLon = FVector2D::ZeroVector;

    // ---- Mutators ----

    /**
     * Restrict this table to a geographic region. Call before adding any features.
     *
     * MarginDegrees is an ABSOLUTE allowance, not a fraction of the region. It used to be a
     * fraction (0.25), which meant the permitted overhang scaled with the request — 250 m on a
     * 1 km region — and silently disagreed with the graph validator's expectation that nothing
     * lies outside the region at all. The margin exists so a building straddling the boundary
     * stays whole rather than being flattened onto the boundary line; it does not need to grow
     * with the region, because buildings do not.
     */
    void SetClipBounds(double MinLat, double MinLon, double MaxLat, double MaxLon,
                       double MarginDegrees = OSMRegionLimits::ClipMarginDegrees)
    {
        ClipMinLatLon = FVector2D(MinLat, MinLon);
        ClipMaxLatLon = FVector2D(MaxLat, MaxLon);

        PaddedClipMinLatLon = FVector2D(MinLat - MarginDegrees, MinLon - MarginDegrees);
        PaddedClipMaxLatLon = FVector2D(MaxLat + MarginDegrees, MaxLon + MarginDegrees);

        bHasClipBounds = true;
    }

    /** True if the lat/lon point falls inside the padded clip region (always true when unset). */
    bool IsPointNearClipBounds(const FVector& LatLonPoint) const
    {
        if (!bHasClipBounds)
        {
            return true;
        }
        return LatLonPoint.X >= PaddedClipMinLatLon.X && LatLonPoint.X <= PaddedClipMaxLatLon.X
            && LatLonPoint.Y >= PaddedClipMinLatLon.Y && LatLonPoint.Y <= PaddedClipMaxLatLon.Y;
    }

    /** Add a feature and update indices. Returns INDEX_NONE if clipped away. */
    int32 AddFeature(FOSMFeature&& Feature)
    {
        if (bHasClipBounds)
        {
            // Reject features with no geometry anywhere near the requested region. One that
            // merely passes through is kept (and clamped below); only ones wholly elsewhere
            // are dropped.
            bool bAnyPointInside = false;

            for (const TArray<FVector>& Ring : Feature.Polygons)
            {
                for (const FVector& Point : Ring)
                {
                    if (IsPointNearClipBounds(Point)) { bAnyPointInside = true; break; }
                }
                if (bAnyPointInside) { break; }
            }
            if (!bAnyPointInside)
            {
                for (const FVector& Point : Feature.Polyline)
                {
                    if (IsPointNearClipBounds(Point)) { bAnyPointInside = true; break; }
                }
            }
            // Point features (no rings/polyline) are judged by their centroid.
            if (!bAnyPointInside && Feature.Polygons.IsEmpty() && Feature.Polyline.IsEmpty())
            {
                const FVector2D& C = Feature.Computed.CentroidLatLon;
                bAnyPointInside = IsPointNearClipBounds(FVector(C.X, C.Y, 0.0));
            }

            if (!bAnyPointInside)
            {
                return INDEX_NONE;
            }

            // Clamp every point into the padded region. This is what actually stops a
            // feature that merely touches the request from contributing far-away geometry —
            // rejecting whole features only handles the case of no overlap at all.
            auto ClampPoint = [this](FVector& Point)
            {
                Point.X = FMath::Clamp(Point.X, PaddedClipMinLatLon.X, PaddedClipMaxLatLon.X);
                Point.Y = FMath::Clamp(Point.Y, PaddedClipMinLatLon.Y, PaddedClipMaxLatLon.Y);
            };

            for (TArray<FVector>& Ring : Feature.Polygons)
            {
                for (FVector& Point : Ring)
                {
                    ClampPoint(Point);
                }
            }
            for (FVector& Point : Feature.Polyline)
            {
                ClampPoint(Point);
            }
        }

        const int32 Index = Features.Num();
        const int64 OSMId = Feature.OSMId;
        const EOSMFeatureType Type = Feature.Type;

        // Expand geographic bounds over every geometry point, not just the centroid.
        // Centroid-only bounds under-report the true extent (a long road contributes only
        // its midpoint), which makes downstream consumers such as the terrain generator
        // size and place the Landscape too small for the data it is meant to cover. Points
        // were already clamped into the padded clip region above, so this can no longer be
        // dragged outward by a single oversized feature.
        auto ExpandBounds = [this](const FVector& LatLonPoint)
        {
            BoundsMinLatLon.X = FMath::Min(BoundsMinLatLon.X, LatLonPoint.X);
            BoundsMinLatLon.Y = FMath::Min(BoundsMinLatLon.Y, LatLonPoint.Y);
            BoundsMaxLatLon.X = FMath::Max(BoundsMaxLatLon.X, LatLonPoint.X);
            BoundsMaxLatLon.Y = FMath::Max(BoundsMaxLatLon.Y, LatLonPoint.Y);
        };

        for (const TArray<FVector>& Ring : Feature.Polygons)
        {
            for (const FVector& Point : Ring)
            {
                ExpandBounds(Point);
            }
        }
        for (const FVector& Point : Feature.Polyline)
        {
            ExpandBounds(Point);
        }

        Features.Add(MoveTemp(Feature));

        // Update indices
        OSMIdToIndex.Add(OSMId, Index);
        TypeToIndices.FindOrAdd(Type).Add(Index);

        return Index;
    }

    // ---- Queries ----

    /** Get total feature count */
    int32 Num() const { return Features.Num(); }

    /** Check if empty */
    bool IsEmpty() const { return Features.IsEmpty(); }

    /** Get feature by index */
    const FOSMFeature& GetFeature(int32 Index) const { return Features[Index]; }
    FOSMFeature& GetFeatureMutable(int32 Index) { return Features[Index]; }

    /** Find a feature by OSM ID. Returns nullptr if not found. */
    const FOSMFeature* FindByOSMId(int64 OSMId) const
    {
        const int32* Index = OSMIdToIndex.Find(OSMId);
        return Index ? &Features[*Index] : nullptr;
    }

    /** Get all features of a specific type */
    TArray<const FOSMFeature*> GetFeaturesByType(EOSMFeatureType Type) const
    {
        TArray<const FOSMFeature*> Result;
        const TArray<int32>* Indices = TypeToIndices.Find(Type);
        if (Indices)
        {
            Result.Reserve(Indices->Num());
            for (int32 Idx : *Indices)
            {
                Result.Add(&Features[Idx]);
            }
        }
        return Result;
    }

    /** Get count of features by type */
    int32 GetCountByType(EOSMFeatureType Type) const
    {
        const TArray<int32>* Indices = TypeToIndices.Find(Type);
        return Indices ? Indices->Num() : 0;
    }

    /** Get the geographic centroid of all features */
    FVector2D ComputeGeographicCentroid() const
    {
        return FVector2D(
            (BoundsMinLatLon.X + BoundsMaxLatLon.X) * 0.5,
            (BoundsMinLatLon.Y + BoundsMaxLatLon.Y) * 0.5
        );
    }

    /** Get the geographic extent in degrees */
    FVector2D GetGeographicExtent() const
    {
        return BoundsMaxLatLon - BoundsMinLatLon;
    }

    /** Get approximate geographic extent in kilometers */
    FVector2D GetApproxExtentKm() const
    {
        const double MidLat = (BoundsMinLatLon.X + BoundsMaxLatLon.X) * 0.5;
        const double DeltaLat = BoundsMaxLatLon.X - BoundsMinLatLon.X;
        const double DeltaLon = BoundsMaxLatLon.Y - BoundsMinLatLon.Y;

        // Approximate: 1 degree latitude ≈ 111.32 km
        // 1 degree longitude ≈ 111.32 * cos(latitude) km
        const double LatKm = DeltaLat * 111.32;
        const double LonKm = DeltaLon * 111.32 * FMath::Cos(FMath::DegreesToRadians(MidLat));

        return FVector2D(LatKm, LonKm);
    }

    /** Clear all features and indices */
    void Reset()
    {
        Features.Empty();
        OSMIdToIndex.Empty();
        TypeToIndices.Empty();
        BoundsMinLatLon = FVector2D(90.0, 180.0);
        BoundsMaxLatLon = FVector2D(-90.0, -180.0);
    }

    /**
     * Generate a summary string for logging / UI display.
     */
    FString GetSummary() const
    {
        FString Summary = FString::Printf(TEXT("Feature Table: %d total features\n"), Features.Num());

        for (uint8 TypeIdx = 0; TypeIdx < static_cast<uint8>(EOSMFeatureType::MAX); ++TypeIdx)
        {
            EOSMFeatureType Type = static_cast<EOSMFeatureType>(TypeIdx);
            const int32 Count = GetCountByType(Type);
            if (Count > 0)
            {
                Summary += FString::Printf(TEXT("  %s: %d\n"), *OSMFeatureTypeToString(Type), Count);
            }
        }

        const FVector2D ExtentKm = GetApproxExtentKm();
        Summary += FString::Printf(TEXT("  Extent: ~%.1f km × %.1f km\n"), ExtentKm.X, ExtentKm.Y);

        return Summary;
    }

private:
    /** Internal index: OSM ID → feature array index */
    TMap<int64, int32> OSMIdToIndex;

    /** Internal index: feature type → array of feature indices */
    TMap<EOSMFeatureType, TArray<int32>> TypeToIndices;
};

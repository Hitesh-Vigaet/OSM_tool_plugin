// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/EOSMFeatureType.h"
#include "Model/FOSMTagDictionary.h"
#include "FOSMFeature.generated.h"

/**
 * Properties computed from OSM tags during classification (Stage 2).
 * These are tag-derived values resolved through a priority fallback chain.
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMComputedProperties
{
    GENERATED_BODY()

    // ---- Building properties ----

    /** Resolved height in meters (from height/building:levels/defaults) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computed")
    float HeightMeters = 0.0f;

    /** Number of building levels (0 if unknown) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computed")
    int32 Levels = 0;

    /** Whether the height was derived from actual tags vs fallback defaults */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computed")
    bool bHeightFromTags = false;

    // ---- Road properties ----

    /** Resolved road width in meters */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computed")
    float WidthMeters = 0.0f;

    /** Number of traffic lanes */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computed")
    int32 LaneCount = 0;

    /** Whether this is a one-way road */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computed")
    bool bIsOneWay = false;

    /** Road surface type (e.g., "asphalt", "gravel", "paved") */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computed")
    FString Surface;

    // ---- Waterway properties ----

    /** Water feature name (from name=* tag) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computed")
    FString Name;

    // ---- Spatial properties (computed after CRS transform in Stage 3) ----

    /** Footprint area in square meters (for polygon features) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computed|Spatial")
    float FootprintAreaSqm = 0.0f;

    /** Geographic centroid as lat/lon (before CRS transform) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computed|Spatial")
    FVector2D CentroidLatLon = FVector2D::ZeroVector;

    /** Perimeter length in meters (for polygon features) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computed|Spatial")
    float PerimeterMeters = 0.0f;

    /** Polyline length in meters (for linear features) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computed|Spatial")
    float LengthMeters = 0.0f;
};

/**
 * A classified OSM feature — the core unit of the internal data model.
 *
 * Created during Stage 2 from raw OSM ways/relations. Each feature has:
 *   - A type (Building, Highway, Waterway, etc.)
 *   - Geometry (polygons with holes for areas, polylines for linear features)
 *   - The complete original tag dictionary
 *   - Computed properties derived from tags
 *   - A bounding box (computed after CRS transform in Stage 3)
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMFeature
{
    GENERATED_BODY()

    /** Original OSM element ID (way or relation ID) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    int64 OSMId = 0;

    /** Classified feature type */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    EOSMFeatureType Type = EOSMFeatureType::Unknown;

    /**
     * Sub-type string derived from the primary tag value.
     * Examples: "residential" (building), "motorway" (highway), "river" (waterway)
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    FString SubType;

    /**
     * Polygon rings for area features.
     * - Polygons[0] = outer ring (the main boundary)
     * - Polygons[1..N] = inner rings (holes — courtyards, islands)
     *
     * Coordinates are initially lat/lon (as FVector with Z=0),
     * then transformed to UE world coordinates (cm) during Stage 3.
     *
     * Empty for linear features (use Polyline instead).
     */
    TArray<TArray<FVector>> Polygons;

    /**
     * Polyline vertices for linear features (roads, streams, fences).
     *
     * Coordinates are initially lat/lon (as FVector with Z=0),
     * then transformed to UE world coordinates (cm) during Stage 3.
     *
     * Empty for area features (use Polygons instead).
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Geometry")
    TArray<FVector> Polyline;

    /** Complete original OSM tag dictionary (preserved for user queries) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    TMap<FString, FString> Tags;

    /** Properties computed from tags (height, width, area, etc.) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    FOSMComputedProperties Computed;

    /**
     * Axis-aligned bounding box in UE world coordinates (cm).
     * Only valid after Stage 3 (CRS transform). Initially invalid.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Spatial")
    FBox BoundingBox;

    /** Whether this is an area (polygon) or linear (polyline) feature */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    bool bIsArea = false;

    /** Whether this feature originated from a relation (vs a simple way) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    bool bFromRelation = false;

    /** Default constructor */
    FOSMFeature()
        : BoundingBox(ForceInit)
    {
    }

    // ---- Convenience accessors ----

    /** Check if this feature has polygon geometry */
    bool HasPolygon() const
    {
        return Polygons.Num() > 0 && Polygons[0].Num() >= 3;
    }

    /** Check if this feature has polyline geometry */
    bool HasPolyline() const
    {
        return Polyline.Num() >= 2;
    }

    /** Get the outer polygon ring (first polygon). Returns empty array if none. */
    const TArray<FVector>& GetOuterRing() const
    {
        static const TArray<FVector> Empty;
        return Polygons.Num() > 0 ? Polygons[0] : Empty;
    }

    /** Get inner rings (holes). Returns empty array if none. */
    TArrayView<const TArray<FVector>> GetInnerRings() const
    {
        if (Polygons.Num() <= 1)
        {
            return TArrayView<const TArray<FVector>>();
        }
        return MakeArrayView(Polygons.GetData() + 1, Polygons.Num() - 1);
    }

    /** Get a tag value with default */
    FString GetTag(const FString& Key, const FString& DefaultValue = FString()) const
    {
        const FString* Value = Tags.Find(Key);
        return Value ? *Value : DefaultValue;
    }

    /** Check if a tag exists */
    bool HasTag(const FString& Key) const
    {
        return Tags.Contains(Key);
    }

    /**
     * Compute the axis-aligned bounding box from current geometry.
     * Call this after CRS transform (Stage 3) to update BoundingBox.
     */
    void ComputeBoundingBox()
    {
        BoundingBox.Init();

        if (bIsArea)
        {
            for (const TArray<FVector>& Ring : Polygons)
            {
                for (const FVector& Pt : Ring)
                {
                    BoundingBox += Pt;
                }
            }
        }
        else
        {
            for (const FVector& Pt : Polyline)
            {
                BoundingBox += Pt;
            }
        }
    }
};

// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FOSMGeometryStore.generated.h"

/**
 * Reference to one geometry in an FOSMGeometryStore.
 *
 * Nodes hold one of these rather than their own vertex arrays. That is what keeps the graph
 * light enough to inspect, diff and hash: the node list stays small and uniform, while the bulk
 * of the data sits in flat arrays that serialise efficiently.
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMGeometryHandle
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    int32 RecordIndex = INDEX_NONE;

    bool IsValid() const { return RecordIndex != INDEX_NONE; }
};

/** One contiguous run of points within the store — a polygon ring or a whole polyline. */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMRingSpan
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    int32 Offset = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    int32 Count = 0;
};

/**
 * One geometry: an area (1 outer ring + N inner rings) or a polyline (exactly 1 span).
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMGeometryRecord
{
    GENERATED_BODY()

    /** Index of the first ring span belonging to this record. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    int32 FirstRing = 0;

    /** Ring spans in this record. 1 for a polyline; 1 + hole count for an area. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    int32 RingCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    bool bIsArea = false;
};

/**
 * Flat vertex storage shared by every node in a city graph (plan_v3_pipeline.md Phase 2.1).
 *
 * Points are WGS84 (X = latitude, Y = longitude), matching FOSMFeature. They are deliberately
 * NOT converted to world centimetres here: the graph is a description of the world, and the
 * projection is a rendering concern that belongs to whatever eventually builds geometry. Baking
 * a projection into the representation is what made the old pipeline impossible to re-inspect
 * after the fact.
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMGeometryStore
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    TArray<FVector2D> Points;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    TArray<FOSMRingSpan> Rings;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Graph")
    TArray<FOSMGeometryRecord> Records;

    void Reset()
    {
        Points.Reset();
        Rings.Reset();
        Records.Reset();
    }

    int32 NumRecords() const { return Records.Num(); }

    /** Store an area. Rings[0] is the outer boundary; the rest are holes. */
    FOSMGeometryHandle AddArea(const TArray<TArray<FVector>>& InRings);

    /** Store a polyline. */
    FOSMGeometryHandle AddPolyline(const TArray<FVector>& InPoints);

    /** Ring count for a record, or 0 if the handle is invalid. */
    int32 GetRingCount(const FOSMGeometryHandle& Handle) const;

    /** Points of one ring. Returns an empty view for an invalid handle or ring index. */
    TArrayView<const FVector2D> GetRing(const FOSMGeometryHandle& Handle, int32 RingIndex) const;

    /** Convenience for the common case: ring 0. */
    TArrayView<const FVector2D> GetOuterRing(const FOSMGeometryHandle& Handle) const
    {
        return GetRing(Handle, 0);
    }

    bool IsArea(const FOSMGeometryHandle& Handle) const;

    /** Lat/lon bounds of a geometry. Returns false when the handle is invalid or empty. */
    bool GetBounds(const FOSMGeometryHandle& Handle, FVector2D& OutMin, FVector2D& OutMax) const;

    /** Area-weighted centroid for areas, midpoint of length for polylines. */
    bool GetCentroid(const FOSMGeometryHandle& Handle, FVector2D& OutCentroid) const;

    /** Total point count across all records — the store's real size. */
    int32 NumPoints() const { return Points.Num(); }
};

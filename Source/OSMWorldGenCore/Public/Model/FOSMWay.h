// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/FOSMTagDictionary.h"
#include "FOSMWay.generated.h"

/**
 * Represents an OSM way — an ordered list of node references forming a polyline or polygon.
 *
 * A closed way (first node == last node) typically represents an area (building footprint,
 * park boundary, lake). An open way represents a linear feature (road, stream, fence).
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMWay
{
    GENERATED_BODY()

    /** Unique OSM way ID */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    int64 Id = 0;

    /**
     * Ordered list of node IDs that compose this way.
     * These are references into the node table — resolved during parsing.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    TArray<int64> NodeRefs;

    /**
     * Resolved geographic coordinates for each node (lat, lon).
     * Populated during the parse phase after node lookup.
     * Index-aligned with NodeRefs.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    TArray<FVector2D> ResolvedCoords;

    /** Whether this way forms a closed ring (first node == last node) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    bool bIsClosed = false;

    /** OSM tags for this way */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    TMap<FString, FString> Tags;

    /** Default constructor */
    FOSMWay() = default;

    /** Get the number of nodes in this way */
    int32 GetNodeCount() const
    {
        return NodeRefs.Num();
    }

    /** Check if coordinates have been resolved */
    bool AreCoordinatesResolved() const
    {
        return ResolvedCoords.Num() == NodeRefs.Num() && NodeRefs.Num() > 0;
    }

    /** Check if this way has a specific tag key */
    bool HasTag(const FString& Key) const
    {
        return Tags.Contains(Key);
    }

    /** Get tag value with default */
    FString GetTag(const FString& Key, const FString& DefaultValue = FString()) const
    {
        const FString* Value = Tags.Find(Key);
        return Value ? *Value : DefaultValue;
    }

    /**
     * Compute the geographic centroid of the way (average of resolved coords).
     * Returns (0,0) if coordinates are not resolved.
     */
    FVector2D ComputeCentroid() const
    {
        if (!AreCoordinatesResolved())
        {
            return FVector2D::ZeroVector;
        }

        double SumLat = 0.0;
        double SumLon = 0.0;
        for (const FVector2D& Coord : ResolvedCoords)
        {
            SumLat += Coord.X;
            SumLon += Coord.Y;
        }

        const double Inv = 1.0 / static_cast<double>(ResolvedCoords.Num());
        return FVector2D(SumLat * Inv, SumLon * Inv);
    }
};

// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/FOSMTagDictionary.h"
#include "FOSMNode.generated.h"

/**
 * Represents a single OSM node — a point with geographic coordinates and tags.
 * Nodes are the atomic elements of OSM data. Ways reference chains of nodes.
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMNode
{
    GENERATED_BODY()

    /** Unique OSM node ID (globally unique within the OSM dataset) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    int64 Id = 0;

    /** WGS84 latitude in degrees (-90 to 90) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    double Latitude = 0.0;

    /** WGS84 longitude in degrees (-180 to 180) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    double Longitude = 0.0;

    /** OSM tags attached to this node (usually empty for non-POI nodes) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    TMap<FString, FString> Tags;

    /** Default constructor */
    FOSMNode() = default;

    /** Convenience constructor */
    FOSMNode(int64 InId, double InLat, double InLon)
        : Id(InId), Latitude(InLat), Longitude(InLon)
    {
    }

    /** Get position as FVector2D (Latitude, Longitude) */
    FVector2D GetLatLon() const
    {
        return FVector2D(Latitude, Longitude);
    }

    /** Check if this node has any tags */
    bool HasTags() const
    {
        return Tags.Num() > 0;
    }

    /** Check if this node has a specific tag key */
    bool HasTag(const FString& Key) const
    {
        return Tags.Contains(Key);
    }

    /** Get tag value, returns empty string if not found */
    FString GetTag(const FString& Key, const FString& DefaultValue = FString()) const
    {
        const FString* Value = Tags.Find(Key);
        return Value ? *Value : DefaultValue;
    }
};

// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EOSMFeatureType.generated.h"

/**
 * Classified type of an OSM feature.
 *
 * Determined from OSM tags during Stage 2 (Parse & Tag).
 * Each generator handles one or more feature types.
 */
UENUM(BlueprintType)
enum class EOSMFeatureType : uint8
{
    /** No classification rule matched */
    Unknown      UMETA(DisplayName = "Unknown"),

    /** building=* */
    Building     UMETA(DisplayName = "Building"),

    /** highway=* (motorway, primary, residential, footway, etc.) */
    Highway      UMETA(DisplayName = "Highway"),

    /** waterway=river|stream|canal|drain (linear features) */
    Waterway     UMETA(DisplayName = "Waterway"),

    /** natural=water, waterway=riverbank (area features) */
    WaterArea    UMETA(DisplayName = "Water Area"),

    /** landuse=* (residential, commercial, industrial, farmland, etc.) */
    Landuse      UMETA(DisplayName = "Land Use"),

    /** natural=wood|scrub|sand|beach|wetland|grassland, etc. */
    NaturalArea  UMETA(DisplayName = "Natural Area"),

    /** railway=rail|subway|tram|light_rail */
    Railway      UMETA(DisplayName = "Railway"),

    /** amenity=* (point features — schools, hospitals, etc.) */
    Amenity      UMETA(DisplayName = "Amenity"),

    /** barrier=fence|wall|hedge (linear features) */
    Barrier      UMETA(DisplayName = "Barrier"),

    /** admin boundaries — boundary=administrative */
    Boundary     UMETA(DisplayName = "Boundary"),

    /** leisure=park|garden|playground|pitch */
    Leisure      UMETA(DisplayName = "Leisure"),

    /** power=line|tower */
    Power        UMETA(DisplayName = "Power"),

    MAX          UMETA(Hidden)
};

/**
 * Utility: Get a display-friendly name for a feature type.
 */
inline FString OSMFeatureTypeToString(EOSMFeatureType Type)
{
    switch (Type)
    {
    case EOSMFeatureType::Unknown:     return TEXT("Unknown");
    case EOSMFeatureType::Building:    return TEXT("Building");
    case EOSMFeatureType::Highway:     return TEXT("Highway");
    case EOSMFeatureType::Waterway:    return TEXT("Waterway");
    case EOSMFeatureType::WaterArea:   return TEXT("Water Area");
    case EOSMFeatureType::Landuse:     return TEXT("Land Use");
    case EOSMFeatureType::NaturalArea: return TEXT("Natural Area");
    case EOSMFeatureType::Railway:     return TEXT("Railway");
    case EOSMFeatureType::Amenity:     return TEXT("Amenity");
    case EOSMFeatureType::Barrier:     return TEXT("Barrier");
    case EOSMFeatureType::Boundary:    return TEXT("Boundary");
    case EOSMFeatureType::Leisure:     return TEXT("Leisure");
    case EOSMFeatureType::Power:       return TEXT("Power");
    default:                           return TEXT("Unknown");
    }
}

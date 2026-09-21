// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EOSMSurfaceCategory.generated.h"

/**
 * Standard surface material categories for physical and electromagnetic/thermal simulation.
 * Stencil values (1..16) directly map to these categories for post-process sensor rendering.
 */
UENUM(BlueprintType)
enum class EOSMSurfaceCategory : uint8
{
    Unknown     = 0  UMETA(DisplayName = "Unknown"),
    Concrete    = 1  UMETA(DisplayName = "Concrete"),
    Brick       = 2  UMETA(DisplayName = "Brick"),
    Stone       = 3  UMETA(DisplayName = "Stone"),
    Metal       = 4  UMETA(DisplayName = "Metal"),
    Glass       = 5  UMETA(DisplayName = "Glass"),
    Wood        = 6  UMETA(DisplayName = "Wood"),
    Plastic     = 7  UMETA(DisplayName = "Plastic"),
    Plaster     = 8  UMETA(DisplayName = "Plaster/Stucco"),
    Asphalt     = 9  UMETA(DisplayName = "Asphalt"),
    Soil        = 10 UMETA(DisplayName = "Soil/Earth"),
    Grass       = 11 UMETA(DisplayName = "Grass"),
    Vegetation  = 12 UMETA(DisplayName = "Vegetation/Foliage"),
    Water       = 13 UMETA(DisplayName = "Water"),
    CementBlock = 14 UMETA(DisplayName = "Cement Block"),
    RoofTile    = 15 UMETA(DisplayName = "Roof Tile"),
    MAX         = 16 UMETA(Hidden)
};

/** Convert EOSMSurfaceCategory to human-readable string */
inline FString OSMSurfaceCategoryToString(EOSMSurfaceCategory Category)
{
    switch (Category)
    {
    case EOSMSurfaceCategory::Concrete:    return TEXT("Concrete");
    case EOSMSurfaceCategory::Brick:       return TEXT("Brick");
    case EOSMSurfaceCategory::Stone:       return TEXT("Stone");
    case EOSMSurfaceCategory::Metal:       return TEXT("Metal");
    case EOSMSurfaceCategory::Glass:       return TEXT("Glass");
    case EOSMSurfaceCategory::Wood:        return TEXT("Wood");
    case EOSMSurfaceCategory::Plastic:     return TEXT("Plastic");
    case EOSMSurfaceCategory::Plaster:     return TEXT("Plaster");
    case EOSMSurfaceCategory::Asphalt:     return TEXT("Asphalt");
    case EOSMSurfaceCategory::Soil:        return TEXT("Soil");
    case EOSMSurfaceCategory::Grass:       return TEXT("Grass");
    case EOSMSurfaceCategory::Vegetation:  return TEXT("Vegetation");
    case EOSMSurfaceCategory::Water:       return TEXT("Water");
    case EOSMSurfaceCategory::CementBlock: return TEXT("CementBlock");
    case EOSMSurfaceCategory::RoofTile:    return TEXT("RoofTile");
    case EOSMSurfaceCategory::Unknown:
    default:                               return TEXT("Unknown");
    }
}

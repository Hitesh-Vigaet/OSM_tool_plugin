// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EOSMProjectionMode.generated.h"

/**
 * Projection mode used to transform WGS84 geographic coordinates (lat/lon)
 * to Unreal Engine 3D world space (centimeters).
 */
UENUM(BlueprintType)
enum class EOSMProjectionMode : uint8
{
    /** Local Tangent Plane (East-North-Up). Best for areas < ~10 km². Fast, simple, highly accurate locally. */
    ENU         UMETA(DisplayName = "Local Tangent Plane (ENU)"),

    /** Universal Transverse Mercator. Auto-detects matching zone. Best for areas 10–500 km². */
    UTM         UMETA(DisplayName = "UTM (Auto-detect zone)"),

    /** Cesium-compatible Earth-Centered Earth-Fixed (ECEF) transform. */
    CesiumECEF  UMETA(DisplayName = "Cesium ECEF"),
};

// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FOSMGeoOrigin.generated.h"

/**
 * Geographic reference origin mapping to Unreal Engine's world origin (0, 0, 0).
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMGeoOrigin
{
    GENERATED_BODY()

    /** WGS84 latitude in degrees (-90 to 90) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|CRS", meta = (ClampMin = "-90.0", ClampMax = "90.0"))
    double Latitude = 0.0;

    /** WGS84 longitude in degrees (-180 to 180) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|CRS", meta = (ClampMin = "-180.0", ClampMax = "180.0"))
    double Longitude = 0.0;

    /** WGS84 height in meters */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|CRS")
    double HeightMeters = 0.0;

    /** Auto-detect origin from dataset centroid */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|CRS")
    bool bAutoDetect = true;

    FOSMGeoOrigin() = default;

    FOSMGeoOrigin(double InLat, double InLon, double InH = 0.0, bool bInAuto = false)
        : Latitude(InLat), Longitude(InLon), HeightMeters(InH), bAutoDetect(bInAuto)
    {
    }

    FVector2D GetLatLon() const { return FVector2D(Latitude, Longitude); }
};

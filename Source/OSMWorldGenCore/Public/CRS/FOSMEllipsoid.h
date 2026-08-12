// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * WGS84 Ellipsoid mathematical constants and conversion helpers.
 */
struct FOSMEllipsoid
{
    /** Semi-major axis a (meters) */
    static constexpr double WGS84_A = 6378137.0;

    /** Semi-minor axis b (meters) */
    static constexpr double WGS84_B = 6356752.314245179;

    /** Flattening f */
    static constexpr double WGS84_F = 1.0 / 298.257223563;

    /** First eccentricity squared e^2 = (a^2 - b^2) / a^2 */
    static constexpr double WGS84_E2 = 0.0066943799901413165;

    /** Scale factor to convert meters to Unreal units (centimeters) */
    static constexpr double MetersToUE = 100.0;

    /** Convert Geodetic (Lat, Lon, H) to ECEF (X, Y, Z) Cartesian coordinates */
    static FVector3d GeodeticToECEF(double LatDeg, double LonDeg, double HeightMeters)
    {
        const double LatRad = FMath::DegreesToRadians(LatDeg);
        const double LonRad = FMath::DegreesToRadians(LonDeg);

        const double SinLat = FMath::Sin(LatRad);
        const double CosLat = FMath::Cos(LatRad);
        const double SinLon = FMath::Sin(LonRad);
        const double CosLon = FMath::Cos(LonRad);

        const double N = WGS84_A / FMath::Sqrt(1.0 - WGS84_E2 * SinLat * SinLat);

        const double X = (N + HeightMeters) * CosLat * CosLon;
        const double Y = (N + HeightMeters) * CosLat * SinLon;
        const double Z = (N * (1.0 - WGS84_E2) + HeightMeters) * SinLat;

        return FVector3d(X, Y, Z);
    }
};

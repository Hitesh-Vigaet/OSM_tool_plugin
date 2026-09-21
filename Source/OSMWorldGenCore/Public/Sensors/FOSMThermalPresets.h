// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Static thermal configuration preset for demo scenarios.
 * Contains all temperature calibration values for a specific location/time.
 */
struct OSMWORLDGENCORE_API FOSMThermalPreset
{
    FString Name;
    float AmbientTempK;
    float SunElevationDeg;
    float CategoryTempsK[16];
    float WallGradientStrengthK;     // Max K drop from top to bottom of wall
    float SurfaceNoiseAmplitudeK;    // Micro-noise ±K
    float PerBuildingVariationK;     // Max ±K per building
    float GroundLevelZ;
    float BuildingMaxHeightCm;

    static const FOSMThermalPreset& UBCity_Afternoon()
    {
        static FOSMThermalPreset P;
        static bool bInitialized = false;
        if (!bInitialized)
        {
            P.Name = TEXT("UB City, Bangalore — Afternoon");
            P.AmbientTempK = 308.00f;
            P.SunElevationDeg = 60.0f;
            P.CategoryTempsK[0]  = 308.00f;  // Unknown
            P.CategoryTempsK[1]  = 308.50f;  // Concrete (tight minute decimal cluster)
            P.CategoryTempsK[2]  = 308.20f;  // Brick
            P.CategoryTempsK[3]  = 308.10f;  // Stone
            P.CategoryTempsK[4]  = 309.00f;  // Metal facade
            P.CategoryTempsK[5]  = 307.80f;  // Glass
            P.CategoryTempsK[6]  = 308.00f;  // Wood
            P.CategoryTempsK[7]  = 308.50f;  // Plastic
            P.CategoryTempsK[8]  = 308.00f;  // Plaster
            P.CategoryTempsK[9]  = 319.50f;  // Asphalt (warm road)
            P.CategoryTempsK[10] = 305.00f;  // Soil
            P.CategoryTempsK[11] = 301.50f;  // Grass (cool lawn)
            P.CategoryTempsK[12] = 298.00f;  // Vegetation (cool trees)
            P.CategoryTempsK[13] = 296.00f;  // Water
            P.CategoryTempsK[14] = 308.40f;  // CementBlock
            P.CategoryTempsK[15] = 318.00f;  // RoofTile (warm roof)
            P.WallGradientStrengthK = 9.0f;
            P.SurfaceNoiseAmplitudeK = 0.2f;
            P.PerBuildingVariationK = 0.25f; // Minute decimal variation (no jumping between buildings!)
            P.GroundLevelZ = 0.0f;
            P.BuildingMaxHeightCm = 5000.0f;
            bInitialized = true;
        }
        return P;
    }
};

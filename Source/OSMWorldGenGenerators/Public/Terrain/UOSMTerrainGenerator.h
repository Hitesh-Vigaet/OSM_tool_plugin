// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Generators/UOSMGeneratorBase.h"
#include "Terrain/FOSMTerrainSettings.h"
#include "Terrain/FOSMHeightmapBuilder.h"
#include "UOSMTerrainGenerator.generated.h"

/**
 * Terrain generator — Stage 4.
 *
 * Converts a DEM GeoTIFF (or flat fallback) into a UE ALandscape actor
 * that is correctly geo-referenced to the CRS origin.
 *
 * Pipeline:
 *   1. Load DEM via FOSMDEMSampler (skip if DEMFilePath is empty)
 *   2. Compute target Landscape dimensions from FOSMTerrainSettings
 *   3. Resample DEM → uint16 heightmap via FOSMHeightmapBuilder
 *   4. Create ALandscape via editor API
 *   5. Import heightmap into Landscape
 *   6. Set Landscape transform so pixels align with CRS/ENU coordinates
 */
UCLASS(BlueprintType)
class OSMWORLDGENGENERATORS_API UOSMTerrainGenerator : public UOSMGeneratorBase
{
    GENERATED_BODY()

public:
    // ---- UOSMGeneratorBase overrides ----

    /**
     * Generate the terrain Landscape actor.
     * Features array is unused (terrain comes from DEM, not OSM features).
     */
    virtual bool Generate(
        const FOSMGenerationContext& Context,
        const TArray<const FOSMFeature*>& Features,
        TArray<AActor*>& OutActors) override;

    virtual TArray<EOSMFeatureType> GetAcceptedFeatureTypes() const override
    {
        return {}; // Terrain is driven by DEM data, not OSM feature types
    }

    virtual FText GetDisplayName() const override
    {
        return NSLOCTEXT("OSM", "TerrainGen", "Terrain (DEM -> Landscape)");
    }

    virtual float GetEstimatedWeight() const override { return 2.0f; }

    // ---- Settings ----

    /** Terrain configuration (DEM path, section size, vertical scale, etc.) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    FOSMTerrainSettings TerrainSettings;

    /**
     * If set, auto-compute ComponentCount from the import area extent.
     * Overrides TerrainSettings.ComponentCount.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    bool bAutoComputeComponentCount = true;

    /**
     * Maximum terrain side length in km. Bounds larger than this are clamped around their
     * centre rather than passed to the engine: a Landscape spanning hundreds of km exceeds
     * the renderer's float precision budget and trips an ensure in DoubleFloat.cpp during
     * distance-field updates, which takes the editor down.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0.1"))
    double MaxTerrainExtentKm = 25.0;

    // ---- Public helpers ----

    /**
     * Sample the elevation in meters at a given WGS84 lat/lon from the
     * currently loaded DEM. Returns 0 if no DEM is loaded.
     * Useful for snapping buildings and roads to terrain.
     */
    UFUNCTION(BlueprintCallable, Category = "OSM|Terrain")
    double SampleTerrainElevation(double Latitude, double Longitude) const;

    /** True if a DEM was loaded successfully */
    UFUNCTION(BlueprintCallable, Category = "OSM|Terrain")
    bool HasDEM() const;

private:
    /** Loaded DEM sampler (shared with other generators after terrain stage) */
    TSharedPtr<class FOSMDEMSampler> DEMSampler;

    /** Spawn and configure the ALandscape actor using the Landscape editor API */
    AActor* CreateLandscapeActor(
        const FOSMGenerationContext& Context,
        const FOSMHeightmapResult& Heightmap,
        double MinLat, double MaxLat,
        double MinLon, double MaxLon);
};

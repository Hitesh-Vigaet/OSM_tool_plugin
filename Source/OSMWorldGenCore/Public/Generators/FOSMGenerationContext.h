// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CRS/FOSMCRSTransformer.h"
#include "Model/FOSMFeatureTable.h"
#include "Generators/FOSMImportWarning.h"
#include "FOSMGenerationContext.generated.h"

/**
 * Shared context passed to all generators during Stage 4.
 *
 * Guarantees that EVERY generator uses the exact same UOSMCRSTransformer instance,
 * targeting the same UWorld, with shared progress callbacks and warning accumulator.
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMGenerationContext
{
    GENERATED_BODY()

    /** THE shared CRS transformer. All generators MUST use this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Context")
    TObjectPtr<UOSMCRSTransformer> CRSTransformer = nullptr;

    /** Target world to spawn generated actors into */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Context")
    TObjectPtr<UWorld> TargetWorld = nullptr;

    /** Read-only pointer to the full feature table */
    const FOSMFeatureTable* FeatureTable = nullptr;

    /**
     * DEM (GeoTIFF) to sample ground elevation from. Empty means flat ground at elevation 0.
     *
     * Shared here so every generator snaps to the SAME surface. Previously terrain loaded a
     * DEM while roads and buildings each used a default-constructed FOSMDEMSampler that was
     * never loaded, silently returning elevation 0 — which puts roads and buildings at Z=0
     * while the landscape sits at the region's true elevation (~890 m in Bangalore), i.e.
     * the terrain and the city separated by most of a kilometre vertically.
     *
     * A path rather than a shared FOSMDEMSampler because that type lives in the Generators
     * module and this context lives in Core, which cannot depend on it. DEM tiles for a
     * few-km region are a couple of KB, so loading one per generator costs nothing.
     */
    FString DEMFilePath;

    /**
     * The region the user actually asked for (lat/lon), when known.
     *
     * Generators must prefer this over feature-table bounds: the feature table describes
     * whatever geometry the source file happened to contain, which can be far larger than
     * the requested area (see FOSMFeatureTable's clip-bounds comment). Terrain in particular
     * must be sized to the request, not to an incidental 500 km relation.
     */
    bool bHasTargetBounds = false;
    double TargetMinLat = 0.0, TargetMaxLat = 0.0;
    double TargetMinLon = 0.0, TargetMaxLon = 0.0;

    void SetTargetBounds(double MinLat, double MinLon, double MaxLat, double MaxLon)
    {
        TargetMinLat = MinLat; TargetMinLon = MinLon;
        TargetMaxLat = MaxLat; TargetMaxLon = MaxLon;
        bHasTargetBounds = (MaxLat > MinLat) && (MaxLon > MinLon);
    }

    /** Thread-safe cancel flag */
    FThreadSafeBool* bCancelRequested = nullptr;

    /** Warning accumulator */
    TArray<FOSMImportWarning>* Warnings = nullptr;

    /** Report a warning message */
    void LogWarning(EOSMImportWarningLevel Level, int64 OSMId, const FText& Message) const
    {
        if (Warnings)
        {
            Warnings->Emplace(Level, OSMId, Message);
        }
    }
};

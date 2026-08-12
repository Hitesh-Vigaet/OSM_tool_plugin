// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Generators/UOSMGeneratorBase.h"
#include "UOSMRoadGenerator.generated.h"

class UOSMRoadTypeDataAsset;
class FOSMDEMSampler;
class UProceduralMeshComponent;

/**
 * Stage 4 Generator for Road Networks (Highway features).
 * 
 * Pipeline:
 *  1. Iterates over all features with Type == Highway
 *  2. Resolves coordinates into a USplineComponent
 *  3. Applies Terrain Snapping (if enabled and DEM available)
 *  4. Extrudes procedural mesh cross-sections along the spline
 *  5. Detects 3+ way intersections and generates junction patches
 */
UCLASS(BlueprintType)
class OSMWORLDGENGENERATORS_API UOSMRoadGenerator : public UOSMGeneratorBase
{
    GENERATED_BODY()

public:
    virtual bool Generate(
        const FOSMGenerationContext& Context,
        const TArray<const FOSMFeature*>& Features,
        TArray<AActor*>& OutActors) override;

    virtual TArray<EOSMFeatureType> GetAcceptedFeatureTypes() const override
    {
        return { EOSMFeatureType::Highway };
    }

    virtual FText GetDisplayName() const override
    {
        return NSLOCTEXT("OSM", "RoadGen", "Road Networks");
    }

    virtual float GetEstimatedWeight() const override { return 3.0f; }

    // ---- Settings ----

    /** Default fallback road type properties */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads")
    TObjectPtr<UOSMRoadTypeDataAsset> DefaultRoadType = nullptr;

    /** Map of OSM highway subtypes (e.g. "motorway", "residential") to specific data assets */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads")
    TMap<FString, TObjectPtr<UOSMRoadTypeDataAsset>> RoadTypeOverrides;

    /** If true, roads will raycast/sample against the terrain DEM and snap to the surface */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads")
    bool bSnapToTerrain = true;

    /** Vertical offset above the terrain when snapping (prevents z-fighting) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads", meta = (EditCondition = "bSnapToTerrain"))
    float SnappingOffsetZ = 5.0f;

    /** If true, detects shared nodes and generates procedural intersection patches */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads")
    bool bGenerateIntersections = true;

private:
    /** Resolves the road asset for a given feature subtype */
    const UOSMRoadTypeDataAsset* GetRoadAssetForSubtype(const FString& SubType) const;

    /** Create the root actor for the entire road network (contains all ProcMesh components) */
    AActor* CreateRoadNetworkActor(const FOSMGenerationContext& Context);
};

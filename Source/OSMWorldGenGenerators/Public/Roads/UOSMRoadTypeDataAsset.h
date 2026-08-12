// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Materials/MaterialInterface.h"
#include "UOSMRoadTypeDataAsset.generated.h"

/**
 * Defines the visual and physical properties for a specific OSM highway type
 * (e.g., motorway, residential, footway).
 */
UCLASS(BlueprintType)
class OSMWORLDGENGENERATORS_API UOSMRoadTypeDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    /** Human-readable display name for this road type */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Type")
    FText DisplayName;

    /**
     * Total road width in meters.
     * Overridden by the OSM "width" or "lanes" tag if present on the feature,
     * unless bForceAssetWidth is true.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Type", meta = (ClampMin = "1"))
    float DefaultWidthMeters = 6.0f;

    /** If true, ignore OSM width tags and strictly use DefaultWidthMeters */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Type")
    bool bForceAssetWidth = false;

    /** Material applied to the extruded road surface */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Type")
    TObjectPtr<UMaterialInterface> SurfaceMaterial = nullptr;

    /** UV tiling rate (V-axis tiles per meter along the road length) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Type")
    float UVTilingPerMeter = 0.5f;

    /**
     * Priority for intersection resolution.
     * When roads cross, the one with higher priority continues seamlessly,
     * while the lower priority road stops at the intersection boundary.
     * (e.g., Motorway > Primary > Residential)
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Type")
    int32 IntersectionPriority = 0;

    /** Whether this road type generates raised sidewalks */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sidewalks")
    bool bHasSidewalks = false;

    /** Sidewalk width in meters (applied to both sides) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sidewalks", meta = (EditCondition = "bHasSidewalks", ClampMin = "0.5"))
    float SidewalkWidth = 1.5f;

    /** Sidewalk height above the road surface in meters */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sidewalks", meta = (EditCondition = "bHasSidewalks", ClampMin = "0.05"))
    float SidewalkHeight = 0.15f;

    /** Material applied to the sidewalks */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sidewalks", meta = (EditCondition = "bHasSidewalks"))
    TObjectPtr<UMaterialInterface> SidewalkMaterial = nullptr;
};

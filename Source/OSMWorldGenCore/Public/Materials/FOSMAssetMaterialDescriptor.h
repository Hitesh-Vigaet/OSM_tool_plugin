// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Materials/EOSMSurfaceCategory.h"
#include "FOSMAssetMaterialDescriptor.generated.h"

/**
 * Describes the material composition and optional active thermal properties
 * of a placeable 3D asset (water tank, solar panel, AC unit, etc.).
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMAssetMaterialDescriptor
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset")
    FString AssetName;

    /** Maps mesh material slot index -> surface category. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset")
    TMap<int32, EOSMSurfaceCategory> SlotCategories;

    /** Active thermal source? (AC exhaust, lit lamp, etc.) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset|Thermal")
    bool bIsActiveThermalSource = false;

    /** Heat output in Watts when active. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset|Thermal",
              meta = (EditCondition = "bIsActiveThermalSource"))
    float ActiveHeatOutputWatts = 0.0f;

    /** Surface temperature of the active source in K. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset|Thermal",
              meta = (EditCondition = "bIsActiveThermalSource"))
    float ActiveSourceTemperatureK = 293.15f;
};

// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Materials/EOSMSurfaceCategory.h"

class UWorld;
class UMaterialParameterCollection;

/**
 * Manages the Material Parameter Collection that bridges thermal simulation to rendering.
 *
 * Writes per-category average temperatures from the thermal simulation into
 * MPC scalar parameters that materials and post-process shaders can read.
 *
 * Parameter naming: "Temp_Concrete", "Temp_Brick", etc. (16 scalar params total)
 */
class OSMWORLDGENCORE_API UOSMThermalMPC
{
public:
    /** Get the default package asset path for the thermal MPC. */
    static const FString& GetDefaultMPCPath();

    /**
     * Find or create the MPC asset.
     */
    static UMaterialParameterCollection* GetOrCreateMPC(UObject* Outer = nullptr);

    /**
     * Update all 16 category temperature parameters in the MPC from current world simulation state.
     */
    static void UpdateFromWorld(UWorld* World, UMaterialParameterCollection* MPC = nullptr);

    /**
     * Set a single category's temperature in the MPC.
     */
    static void SetCategoryTemperature(
        UWorld* World,
        UMaterialParameterCollection* MPC,
        EOSMSurfaceCategory Category,
        float TemperatureK);

    /** Get standard parameter name for a surface category. */
    static FName GetParameterNameForCategory(EOSMSurfaceCategory Category);
};

// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

/**
 * Procedural generator for the IR / Thermal post-process material asset (M_IRPostProcess).
 * Constructs the custom HLSL shader graph dynamically and saves it as a reusable .uasset.
 *
 * @deprecated Use FOSMThermalMaterialBuilderV2 instead.
 * This class used a stencil-indexed LUT approach that has been superseded
 * by the per-material MPC-driven thermal rendering pipeline.
 */
class OSMWORLDGENEDITOR_API FOSMThermalMaterialBuilder
{
public:
    /**
     * Finds or programmatically creates the master M_IRPostProcess material asset.
     */
    static UMaterialInterface* GetOrCreateThermalPostProcessMaterial(bool bForceRebuild = false);

    /** Get the default package path for the thermal material asset. */
    static const FString& GetDefaultMaterialAssetPath();
};

// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

/**
 * Production procedural generator for the IR / Thermal post-process material asset (M_IRPostProcessV2).
 * Constructs the material graph dynamically and saves it as a reusable .uasset.
 *
 * Supersedes FOSMThermalMaterialBuilder by utilizing MPC parameters and real radiometric scaling.
 */
class OSMWORLDGENEDITOR_API FOSMThermalMaterialBuilderV2
{
public:
    /**
     * Finds or programmatically creates the master M_IRPostProcessV2 material asset.
     */
    static UMaterialInterface* GetOrCreateThermalPostProcessMaterial(bool bForceRebuild = false);

    /** Get the default package path for the V2 thermal material asset. */
    static const FString& GetDefaultMaterialAssetPath();
};

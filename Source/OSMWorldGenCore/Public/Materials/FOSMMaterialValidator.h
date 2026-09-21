// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWorld;
class UProceduralMeshComponent;

/**
 * Post-generation validation pass.
 * Verifies that every surface in the generated world has coherent visual + physical materials.
 */
class OSMWORLDGENCORE_API FOSMMaterialValidator
{
public:
    struct FValidationResult
    {
        int32 TotalComponents = 0;
        int32 TotalSections = 0;
        int32 MissingPhysMat = 0;
        int32 MissingVisualMat = 0;
        int32 MismatchedCategory = 0;
        TArray<FString> Errors;

        bool IsValid() const { return MissingPhysMat == 0 && MismatchedCategory == 0 && MissingVisualMat == 0; }
        FString ToString() const;
    };

    /**
     * Validate all generated actors in the world.
     * Checks every ProceduralMeshComponent's material sections.
     */
    static FValidationResult ValidateWorld(UWorld* World);

    /**
     * Validate a single component's material assignments.
     */
    static bool ValidateComponent(
        UProceduralMeshComponent* MeshComp,
        TArray<FString>& OutErrors,
        int32& OutSectionsChecked,
        int32& OutMissingPhysMat,
        int32& OutMissingVisualMat);
};

// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Materials/EOSMSurfaceCategory.h"
#include "FOSMMaterialIdentityGroup.generated.h"

class UOSMPhysicalMaterial;
class UMaterialInterface;

/**
 * Material Identity Group (MIG): a coherent bundle of visual + physical material.
 *
 * Enforces that every visual variant within this group shares the SAME physical material,
 * making texture ↔ physics mismatches structurally impossible.
 *
 * Multiple visual variants (clean concrete, weathered concrete, mossy concrete) provide
 * visual variety while guaranteeing identical sensor response.
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMMaterialIdentityGroup
{
    GENERATED_BODY()

    /** Which surface category this MIG represents */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MIG")
    EOSMSurfaceCategory Category = EOSMSurfaceCategory::Unknown;

    /**
     * The single physical material asset shared by ALL visual variants in this group.
     * Contains emissivity, dielectric, reflectivity, thermal conductivity, etc.
     * When null, properties are created programmatically from the property table.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MIG")
    TObjectPtr<UOSMPhysicalMaterial> PhysicalMaterial = nullptr;

    /**
     * Visual material variants. All MUST have their UMaterial::PhysMaterial
     * slot pointing to the same PhysicalMaterial above.
     *
     * When empty, the system falls back to grey-box MakeColouredMaterial() / CreateGreyBoxMaterial().
     * Variant selection is deterministic from the node's seed/OSM ID.
     *
     * Target: 3-5 variants for common categories (Concrete, Brick, Plaster),
     *         1 for rare ones (Water, Vegetation).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MIG")
    TArray<TSoftObjectPtr<UMaterialInterface>> VisualVariants;

    /** Whether this MIG has a valid physical material. */
    bool HasPhysicalMaterial() const { return PhysicalMaterial != nullptr; }

    /** Whether this MIG has authored visual materials (not grey-box). */
    bool HasVisualMaterials() const { return VisualVariants.Num() > 0; }

    /**
     * Select a visual material variant deterministically from a seed (typically the OSM node ID).
     * Returns nullptr if no visual variants are registered (caller should fall back to grey-box).
     */
    UMaterialInterface* SelectVisualVariant(int32 Seed) const;
};

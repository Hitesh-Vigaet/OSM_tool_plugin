// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Materials/EOSMSurfaceCategory.h"
#include "Materials/FOSMMaterialIdentityGroup.h"
#include "UOSMMaterialPalette.generated.h"

class UMaterialInterface;
class UOSMPhysicalMaterial;

/**
 * Master material lookup table for procedural world generation.
 * Maps EOSMSurfaceCategory → Material Identity Group (MIG).
 *
 * Each MIG bundles a physical material with visual variants,
 * ensuring texture ↔ physics coherence is guaranteed at the data level.
 */
UCLASS(BlueprintType)
class OSMWORLDGENCORE_API UOSMMaterialPalette : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Palette")
    TArray<FOSMMaterialIdentityGroup> Groups;

    /** Find the MIG for a given category. Returns nullptr if not found. */
    const FOSMMaterialIdentityGroup* FindGroup(EOSMSurfaceCategory Category) const;

    /** Get the physical material for a given category. Returns nullptr if not found. */
    UOSMPhysicalMaterial* GetPhysicalMaterial(EOSMSurfaceCategory Category) const;

    /** Select a visual material variant for a category, deterministically from seed. */
    UMaterialInterface* SelectVisualMaterial(EOSMSurfaceCategory Category, int32 Seed) const;

    /** Add or update a group for a category. */
    void SetGroup(const FOSMMaterialIdentityGroup& Group);
};

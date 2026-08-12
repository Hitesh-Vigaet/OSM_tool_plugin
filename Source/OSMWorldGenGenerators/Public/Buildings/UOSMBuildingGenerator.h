// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Generators/UOSMGeneratorBase.h"
#include "UOSMBuildingGenerator.generated.h"

class UMaterialInterface;

/**
 * Stage 4 Generator for Buildings.
 *
 * Pipeline per feature:
 *   1. Resolve height via FOSMBuildingHeightResolver
 *   2. Ground the building to terrain via FOSMDEMSampler (if available)
 *   3. Extrude footprint → UDynamicMesh via FOSMBuildingExtruder
 *   4. Bake DynamicMesh → UStaticMesh with LODs
 *   5. Spawn AActor with UStaticMeshComponent in the world
 *   6. Attach UOSMMetadataComponent
 */
UCLASS(BlueprintType)
class OSMWORLDGENGENERATORS_API UOSMBuildingGenerator : public UOSMGeneratorBase
{
    GENERATED_BODY()

public:
    virtual bool Generate(
        const FOSMGenerationContext& Context,
        const TArray<const FOSMFeature*>& Features,
        TArray<AActor*>& OutActors) override;

    virtual TArray<EOSMFeatureType> GetAcceptedFeatureTypes() const override
    {
        return { EOSMFeatureType::Building };
    }

    virtual FText GetDisplayName() const override
    {
        return NSLOCTEXT("OSM", "BuildingGen", "Buildings");
    }

    virtual float GetEstimatedWeight() const override { return 4.0f; }

    // ── Height Settings ─────────────────────────────────────────────────────

    /** Floor-to-floor height in meters, used when only "levels" tag is present */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings|Height",
        meta = (ClampMin = "2.0", ClampMax = "5.0"))
    float DefaultFloorHeight = 3.0f;

    /** Global fallback height in meters when no tag provides a clue */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings|Height",
        meta = (ClampMin = "3.0"))
    float DefaultBuildingHeight = 9.0f;

    // ── Materials ───────────────────────────────────────────────────────────

    /** Default wall material (applied to all vertical faces, slot 0) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings|Materials")
    TObjectPtr<UMaterialInterface> DefaultWallMaterial = nullptr;

    /** Default roof material (applied to the flat top cap, slot 1) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings|Materials")
    TObjectPtr<UMaterialInterface> DefaultRoofMaterial = nullptr;

    /**
     * Procedural fallback only (plan_v2_workflow.md §7.4): when true, each building gets a
     * per-node dynamic material instance with a deterministic (seeded by OSM ID) color
     * variation applied, instead of every fallback building sharing one flat, uniform look.
     * Only has a visible effect if the resolved wall/roof material actually exposes a
     * recognized color parameter (checked by name: "BaseColor", "Color", "TintColor",
     * "Albedo") — it is a no-op on materials without one, e.g. the built-in grid fallback.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings|Materials")
    bool bVaryFallbackMaterialColor = true;

    // ── LOD Settings ────────────────────────────────────────────────────────

    /** Whether to generate LOD1 / LOD2 meshes */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings|LOD")
    bool bGenerateLODs = true;

    /**
     * Camera-distance thresholds (in cm) at which each LOD activates.
     * Index 0 → LOD1 threshold, Index 1 → LOD2 threshold.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings|LOD",
        meta = (EditCondition = "bGenerateLODs"))
    TArray<float> LODDistances = { 50000.0f, 150000.0f };

    // ── Asset Storage ───────────────────────────────────────────────────────

    /**
     * Content-browser folder where generated StaticMesh assets will be saved.
     * A timestamp sub-folder is appended automatically per import.
     * Example: "/Game/OSMWorldGen/GeneratedMeshes"
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings|Storage")
    FString MeshSavePath = TEXT("/Game/OSMWorldGen/GeneratedMeshes");

private:
    /** Derive a safe, unique asset name from the OSM feature ID */
    FString MakeAssetName(int64 FeatureId, int32 FallbackIndex) const;

    /**
     * Deterministic (same OSMId always produces the same result) HSV-random color,
     * used to give procedurally-generated fallback buildings visual variety.
     * SatRange/ValRange are both in [0,1] and bias the result toward plausible
     * building-material tones rather than the full saturated hue wheel.
     */
    static FLinearColor MakeVariedColor(int64 OSMId, float SatMin, float SatMax, float ValMin, float ValMax);

    /** Compute the per-import save sub-folder path */
    FString GetTimestampedSavePath() const;

    /** Cached timestamped path (set on first Generate call) */
    mutable FString CachedSavePath;
};

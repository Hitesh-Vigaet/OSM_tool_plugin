// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UOSMBuildingArchetype.generated.h"

/** Roof forms the mesher can build (plan_v3_pipeline.md Phase 5.1). */
UENUM(BlueprintType)
enum class EOSMRoofForm : uint8
{
    /** Flat slab with an optional parapet. The default across most of the sampled city. */
    Flat     UMETA(DisplayName = "Flat"),
    /** Flat, but raised on a low upstand — reads as a weathering course. */
    Terrace  UMETA(DisplayName = "Terrace"),
    /** Pitched from the footprint's long axis. */
    Gable    UMETA(DisplayName = "Gable"),
    /** Pitched inward from every edge. */
    Hip      UMETA(DisplayName = "Hip"),

    MAX      UMETA(Hidden)
};

/**
 * A recipe for generating one kind of building (plan_v3_pipeline.md Phase 5.1).
 *
 * This is the answer to a problem a mesh library cannot solve. Footprints in real data span
 * 19.5 m^2 to 7,814 m^2 — a 401x range — so a modelled building either fits its own footprint or
 * looks varied, never both. An archetype separates the two: the SHAPE comes from the data, the
 * CHARACTER comes from this asset. Ten or twenty archetypes per subtype then give real variety
 * without any of them being wrong for the building they land on.
 *
 * Holds no geometry. The mesher reads these parameters and builds against the actual footprint.
 */
UCLASS(BlueprintType)
class OSMWORLDGENCORE_API UOSMBuildingArchetype : public UDataAsset
{
    GENERATED_BODY()

public:
    /** Shown in the Control Center and the dry run. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FString DisplayName;

    /** Free-text note for whoever authored it — "1980s plastered walk-up", "glass office". */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (MultiLine = true))
    FString Description;

    // ---- Massing ----

    /**
     * Storey height in metres.
     *
     * Load-bearing in two directions: it converts `building:levels` into a height when no metric
     * height is tagged, and it decides where storey lines and window rows land. Only 21% of
     * buildings in the sampled data record levels, so this value shapes most of the city.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Massing", meta = (ClampMin = "2.0", ClampMax = "8.0"))
    float FloorHeightMeters = 3.2f;

    /** Height used when the data gives neither a height nor a level count. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Massing", meta = (ClampMin = "2.0"))
    float DefaultHeightMeters = 9.6f;

    /** Random height variation in metres, applied deterministically from the node seed. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Massing", meta = (ClampMin = "0.0", ClampMax = "10.0"))
    float HeightJitterMeters = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Massing")
    EOSMRoofForm RoofForm = EOSMRoofForm::Flat;

    /** Roof rise in metres for pitched forms; ignored when flat. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Massing", meta = (ClampMin = "0.0"))
    float RoofRiseMeters = 2.0f;

    /** Parapet wall height above a flat roof. Zero for none. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Massing", meta = (ClampMin = "0.0", ClampMax = "3.0"))
    float ParapetHeightMeters = 0.9f;

    // ---- Facade ----

    /**
     * Nominal bay width in metres.
     *
     * Walls are divided into whole bays, so window spacing stays constant whatever the wall
     * length. This is precisely what a scaled mesh cannot do, and the reason a 7,800 m^2 block
     * and a 20 m^2 hut can share an archetype without either looking wrong.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facade", meta = (ClampMin = "1.5", ClampMax = "8.0"))
    float BayWidthMeters = 3.0f;

    /** Wall material. Tiled by world-space UVs, so it reads identically at any building size. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facade")
    TSoftObjectPtr<UMaterialInterface> WallMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facade")
    TSoftObjectPtr<UMaterialInterface> RoofMaterial;

    /** Ground floor treatment, for shopfronts. Falls back to WallMaterial when unset. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facade")
    TSoftObjectPtr<UMaterialInterface> GroundFloorMaterial;

    /** Plinth height in metres. Buildings sit above pavement level. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facade", meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float PlinthHeightMeters = 0.3f;

    // ---- Openings ----
    //
    // Meshes placed per bay by a later task. Recorded here so an archetype is complete as a
    // description, even before the placement pass exists.

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openings")
    TSoftObjectPtr<UStaticMesh> WindowMesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openings")
    TSoftObjectPtr<UStaticMesh> WindowGrilleMesh;

    /** Projecting concrete sunshade over windows. Its shadow line is a defining local feature. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openings")
    TSoftObjectPtr<UStaticMesh> ChajjaMesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openings")
    TSoftObjectPtr<UStaticMesh> DoorMesh;

    /** Fraction of bays carrying a window, 0..1. Below 1 leaves blank wall between openings. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openings", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float WindowDensity = 1.0f;

    /** Sill height above each floor level, in metres. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openings", meta = (ClampMin = "0.0"))
    float SillHeightMeters = 0.9f;

    // ---- Variation ----

    /**
     * Colour jitter applied per building, driven by the node seed.
     *
     * Deterministic, so a rebuild reproduces the same city rather than reshuffling it.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ColourJitter = 0.12f;

    /** Resolve the height for a building, given what the data supplied. */
    float ResolveHeightMeters(float TaggedHeight, int32 TaggedLevels, int32 Seed) const;
};

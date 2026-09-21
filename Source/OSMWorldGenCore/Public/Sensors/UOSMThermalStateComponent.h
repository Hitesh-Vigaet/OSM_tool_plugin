// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Materials/EOSMSurfaceCategory.h"
#include "UOSMThermalStateComponent.generated.h"

class UOSMPhysicalMaterial;

/**
 * One thermal zone — typically one per mesh section (wall, roof, ground floor).
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMThermalZone
{
    GENERATED_BODY()

    /** Which mesh section this zone covers */
    UPROPERTY(VisibleAnywhere, Category = "Thermal")
    int32 MeshSectionIndex = 0;

    /** Surface category for property lookup */
    UPROPERTY(VisibleAnywhere, Category = "Thermal")
    EOSMSurfaceCategory Category = EOSMSurfaceCategory::Unknown;

    /** Current surface temperature in Kelvin */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Thermal")
    float CurrentTemperatureK = 293.15f;

    /** Steady-state temperature (pre-computed at generation time) */
    UPROPERTY(VisibleAnywhere, Category = "Thermal")
    float SteadyStateTemperatureK = 293.15f;

    /** Surface area of this zone in m² (computed from mesh geometry) */
    UPROPERTY(VisibleAnywhere, Category = "Thermal")
    float SurfaceAreaSqm = 0.0f;

    /** Average face normal of this zone (for solar angle calculation) */
    UPROPERTY(VisibleAnywhere, Category = "Thermal")
    FVector AverageNormal = FVector::UpVector;

    /** Cached pointer to the physical material for fast property access */
    UPROPERTY(Transient)
    TObjectPtr<UOSMPhysicalMaterial> CachedMaterial = nullptr;
};

/**
 * Per-actor thermal state. Holds thermal zones and is ticked by FOSMThermalSimulation.
 */
UCLASS(ClassGroup = (OSMWorldGen), meta = (BlueprintSpawnableComponent))
class OSMWORLDGENCORE_API UOSMThermalStateComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UOSMThermalStateComponent();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Thermal")
    TArray<FOSMThermalZone> Zones;

    /** Initialise zones from mesh sections and material assignments */
    void InitializeZones(
        const TArray<EOSMSurfaceCategory>& SectionCategories,
        const TArray<UOSMPhysicalMaterial*>& SectionMaterials,
        const TArray<float>& SectionAreas,
        const TArray<FVector>& SectionNormals);

    /**
     * Update zone temperatures dynamically using the Lumped Capacitance method.
     *   T_new = T_old + (Q_net / C_area) * (DeltaTime * TimeMultiplier)
     */
    void TickThermalState(
        float DeltaTime,
        float TimeMultiplier,
        float SolarIrradianceWm2,
        const FVector& SunDirection,
        float AmbientTempK,
        float SkyTempK,
        float WindSpeedMps);

    /** Get the temperature of a specific zone (mesh section). */
    float GetZoneTemperature(int32 MeshSection) const;
};

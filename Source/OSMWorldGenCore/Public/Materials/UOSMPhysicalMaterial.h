// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Materials/EOSMSurfaceCategory.h"
#include "UOSMPhysicalMaterial.generated.h"

/**
 * Physical Material subclass holding thermal and electromagnetic properties
 * for drone sensor simulations (IR thermal cameras, and extensible to Radar/LiDAR).
 */
UCLASS(BlueprintType)
class OSMWORLDGENCORE_API UOSMPhysicalMaterial : public UPhysicalMaterial
{
    GENERATED_BODY()

public:
    UOSMPhysicalMaterial();

    // ──── Identity ────

    /** Standard material category for fast lookup. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OSM|Identity")
    EOSMSurfaceCategory SurfaceCategory = EOSMSurfaceCategory::Unknown;

    /** Human-readable label (e.g., "Concrete", "Asphalt"). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OSM|Identity")
    FString MaterialLabel;

    // ──── Thermal / IR Surface Properties ────

    /**
     * Thermal emissivity (0..1). How efficiently the surface radiates IR energy.
     * High (~0.9+): concrete, vegetation, water — radiate at true temperature.
     * Low (~0.05-0.25): metals — act as IR mirrors reflecting cold sky / ambient.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Thermal|Surface",
              meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Emissivity = 0.9f;

    /**
     * Fraction of incoming solar radiation absorbed by the surface (0..1).
     * Dark surfaces like asphalt ~0.9, light/reflective surfaces like white paint ~0.3.
     * Drives solar heating in the thermal simulation.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Thermal|Surface",
              meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SolarAbsorptivity = 0.65f;

    /**
     * Base surface temperature in Kelvin under standard ambient conditions (20°C = 293.15K).
     * Sun-heated surfaces (asphalt ~323K, tile ~313K), cool surfaces (water ~291K, foliage ~293K).
     * Used as steady-state initial temperature before dynamic simulation begins.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Thermal|Surface",
              meta = (ClampMin = "100.0", ClampMax = "500.0"))
    float BaseTemperatureK = 293.15f;

    // ──── Thermal Volume Properties (Heat Conduction) ────

    /**
     * Thermal conductivity in W/(m·K). How fast heat flows through the material volume.
     * Metal ~50.0, Concrete ~1.7, Wood ~0.14, Air ~0.025.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Thermal|Volume")
    float ThermalConductivity = 1.7f;

    /**
     * Specific heat capacity in J/(kg·K). Energy required to raise 1 kg by 1 Kelvin.
     * Water ~4186, Concrete ~880, Metal ~500.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Thermal|Volume")
    float SpecificHeatCapacity = 880.0f;

    /**
     * Material density in kg/m³.
     * Steel ~7800, Concrete ~2300, Water ~1000, Wood ~600.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Thermal|Volume")
    float ThermalDensity = 2300.0f;

    /**
     * Standard thickness of this material layer in meters.
     * External wall ~0.25, roof tile ~0.05, metal sheet ~0.003, glass ~0.006.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Thermal|Volume")
    float ThicknessMeters = 0.25f;

    // ──── Radar / Electromagnetic Properties ────

    /** Relative permittivity (dielectric constant). Air=1, Concrete=4.5, Water=80, Metal=1e6. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Radar")
    float DielectricConstant = 1.0f;

    /** Electrical conductivity in S/m. Higher = more EM absorption. Steel ~3.8e7. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Radar")
    float Conductivity = 0.0f;

    /** RCS modifier: multiplier applied to geometric radar cross section. 1.0 = neutral. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Radar")
    float RCSModifier = 1.0f;

    /** Whether radar waves can penetrate this material (e.g., dry wood, thin plastic). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|Radar")
    bool bRadarTransparent = false;

    // ──── LiDAR Properties ────

    /** Lambertian reflectivity at 905 nm (0..1). Most automotive LiDAR operates here. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|LiDAR",
              meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float LiDARReflectivity905nm = 0.5f;

    /** Lambertian reflectivity at 1550 nm (0..1). Eye-safe LiDAR wavelength. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|LiDAR",
              meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float LiDARReflectivity1550nm = 0.45f;

    /** Whether this surface is specular (glass, polished metal, water) vs diffuse (concrete, grass). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|LiDAR")
    bool bSpecularReflector = false;

    // ──── RF / Radio Properties ────

    /** Signal attenuation in dB per standard-thickness penetration. ITU-R P.2040 based. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|RF")
    float AttenuationDB = 0.0f;

    /** RF reflection coefficient (0..1): fraction of signal energy reflected. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM|RF",
              meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float RFReflectionCoefficient = 0.5f;

    // ──── Derived Thermal Helpers ────

    /** Thermal inertia: sqrt(K × ρ × Cp). Higher = slower surface temperature response. */
    UFUNCTION(BlueprintCallable, Category = "OSM|Thermal")
    float GetThermalInertia() const;

    /** Thermal diffusivity α = K/(ρ·Cp) in m²/s. How fast the temperature profile evolves. */
    UFUNCTION(BlueprintCallable, Category = "OSM|Thermal")
    float GetThermalDiffusivity() const;

    /**
     * Calculates the apparent normalized radiance (0..1) based on Stefan-Boltzmann law:
     * Apparent Radiance = ε * (T_surface/T_ref)^4 + (1 - ε) * (T_ambient/T_ref)^4
     * where T_ref is a reference high temperature (e.g. 373.15K / 100°C).
     */
    UFUNCTION(BlueprintCallable, Category = "OSM|Thermal")
    float GetNormalizedRadiance(float AmbientTemperatureK = 293.15f) const;
};

// Copyright InviMind. All Rights Reserved.

#include "Sensors/UOSMThermalStateComponent.h"
#include "Materials/UOSMPhysicalMaterial.h"

UOSMThermalStateComponent::UOSMThermalStateComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UOSMThermalStateComponent::InitializeZones(
    const TArray<EOSMSurfaceCategory>& SectionCategories,
    const TArray<UOSMPhysicalMaterial*>& SectionMaterials,
    const TArray<float>& SectionAreas,
    const TArray<FVector>& SectionNormals)
{
    Zones.Reset();
    const int32 NumSections = SectionCategories.Num();
    Zones.Reserve(NumSections);

    for (int32 i = 0; i < NumSections; ++i)
    {
        FOSMThermalZone Zone;
        Zone.MeshSectionIndex = i;
        Zone.Category = SectionCategories[i];
        Zone.CachedMaterial = SectionMaterials.IsValidIndex(i) ? SectionMaterials[i] : nullptr;
        Zone.SurfaceAreaSqm = SectionAreas.IsValidIndex(i) ? SectionAreas[i] : 0.0f;
        Zone.AverageNormal = SectionNormals.IsValidIndex(i) ? SectionNormals[i] : FVector::UpVector;

        const float BaseTemp = Zone.CachedMaterial ? Zone.CachedMaterial->BaseTemperatureK : 293.15f;
        Zone.CurrentTemperatureK = BaseTemp;
        Zone.SteadyStateTemperatureK = BaseTemp;

        Zones.Add(Zone);
    }
}

void UOSMThermalStateComponent::TickThermalState(
    float DeltaTime,
    float TimeMultiplier,
    float SolarIrradianceWm2,
    const FVector& SunDirection,
    float AmbientTempK,
    float SkyTempK,
    float WindSpeedMps)
{
    constexpr double Sigma = 5.670374419e-8; // Stefan-Boltzmann constant
    const double HConv = 5.7 + 3.8 * FMath::Max(0.0f, WindSpeedMps);
    const double SkyRad = FMath::Pow(static_cast<double>(SkyTempK), 4.0);
    const FVector NormalizedSunDir = SunDirection.GetSafeNormal();

    for (FOSMThermalZone& Zone : Zones)
    {
        float Emissivity = 0.90f;
        float Absorptivity = 0.65f;
        double Density = 2000.0;
        double SpecificHeat = 800.0;
        double Thickness = 0.2;

        if (Zone.CachedMaterial)
        {
            Emissivity = Zone.CachedMaterial->Emissivity;
            Absorptivity = Zone.CachedMaterial->SolarAbsorptivity;
            Density = Zone.CachedMaterial->ThermalDensity;
            SpecificHeat = Zone.CachedMaterial->SpecificHeatCapacity;
            Thickness = Zone.CachedMaterial->ThicknessMeters;
        }
        else
        {
            // Fallback distinct profiles for 6 core materials
            switch (Zone.Category)
            {
            case EOSMSurfaceCategory::Asphalt:
                Emissivity = 0.93f; Absorptivity = 0.90f; Density = 2400.0; SpecificHeat = 920.0;
                break;
            case EOSMSurfaceCategory::Concrete:
                Emissivity = 0.92f; Absorptivity = 0.60f; Density = 2300.0; SpecificHeat = 880.0;
                break;
            case EOSMSurfaceCategory::Soil:
                Emissivity = 0.92f; Absorptivity = 0.75f; Density = 1500.0; SpecificHeat = 1400.0;
                break;
            case EOSMSurfaceCategory::Grass:
            case EOSMSurfaceCategory::Vegetation:
                Emissivity = 0.95f; Absorptivity = 0.70f; Density = 300.0; SpecificHeat = 2500.0; Thickness = 0.05;
                break;
            case EOSMSurfaceCategory::Water:
                Emissivity = 0.96f; Absorptivity = 0.85f; Density = 1000.0; SpecificHeat = 4184.0; Thickness = 1.0;
                break;
            default:
                break;
            }
        }
        
        // Heat Capacity per unit area = Density * SpecificHeat * Thickness
        const double ArealHeatCapacity = Density * SpecificHeat * Thickness;

        // Incident solar flux on face
        const float CosTheta = FMath::Max(0.0f, FVector::DotProduct(Zone.AverageNormal.GetSafeNormal(), -NormalizedSunDir));
        const double QSolar = static_cast<double>(Absorptivity * SolarIrradianceWm2 * CosTheta);

        // Current T
        const double T = static_cast<double>(Zone.CurrentTemperatureK);
        const double T4 = T * T * T * T;

        // Radiative and Convective heat loss
        const double QRad = Emissivity * Sigma * (T4 - SkyRad);
        const double QConv = HConv * (T - AmbientTempK);

        // Net heat flux into the surface (W/m^2)
        const double QNet = QSolar - QRad - QConv;

        // Temperature change dT = (QNet / C_area) * dt
        const double EffectiveDeltaTime = static_cast<double>(DeltaTime * TimeMultiplier);
        const double DeltaT = (QNet / ArealHeatCapacity) * EffectiveDeltaTime;

        // Cap DeltaT to prevent numerical instability at extreme time multipliers
        const double ClampedDeltaT = FMath::Clamp(DeltaT, -50.0, 50.0);

        const float FinalTempK = FMath::Clamp(static_cast<float>(T + ClampedDeltaT), 220.0f, 420.0f);
        Zone.CurrentTemperatureK = FinalTempK;
        // Keep SteadyState equal to current for compatibility or debugging
        Zone.SteadyStateTemperatureK = FinalTempK;
    }
}

float UOSMThermalStateComponent::GetZoneTemperature(int32 MeshSection) const
{
    for (const FOSMThermalZone& Zone : Zones)
    {
        if (Zone.MeshSectionIndex == MeshSection)
        {
            return Zone.CurrentTemperatureK;
        }
    }
    return 293.15f;
}

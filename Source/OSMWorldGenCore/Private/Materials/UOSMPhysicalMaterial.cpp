// Copyright InviMind. All Rights Reserved.

#include "Materials/UOSMPhysicalMaterial.h"

UOSMPhysicalMaterial::UOSMPhysicalMaterial()
{
    SurfaceCategory = EOSMSurfaceCategory::Unknown;
    MaterialLabel = TEXT("Unknown");
    Emissivity = 0.9f;
    SolarAbsorptivity = 0.65f;
    BaseTemperatureK = 293.15f;
    ThermalConductivity = 1.7f;
    SpecificHeatCapacity = 880.0f;
    ThermalDensity = 2300.0f;
    ThicknessMeters = 0.25f;
    DielectricConstant = 1.0f;
    Conductivity = 0.0f;
    RCSModifier = 1.0f;
    bRadarTransparent = false;
    LiDARReflectivity905nm = 0.5f;
    LiDARReflectivity1550nm = 0.45f;
    bSpecularReflector = false;
    AttenuationDB = 0.0f;
    RFReflectionCoefficient = 0.5f;
}

float UOSMPhysicalMaterial::GetThermalInertia() const
{
    // Thermal inertia P = sqrt(k * rho * c)
    return FMath::Sqrt(ThermalConductivity * ThermalDensity * SpecificHeatCapacity);
}

float UOSMPhysicalMaterial::GetThermalDiffusivity() const
{
    const float Denominator = ThermalDensity * SpecificHeatCapacity;
    if (Denominator <= 0.0f) return 0.0f;
    return ThermalConductivity / Denominator;
}

float UOSMPhysicalMaterial::GetNormalizedRadiance(float AmbientTemperatureK) const
{
    // Normalization reference: 373.15K (100°C) as high reference, 250K as low reference
    constexpr float RefMaxTempK = 373.15f;
    
    // Effective radiated energy = Emitted + Reflected Ambient
    const float SurfaceEnergy = Emissivity * FMath::Pow(BaseTemperatureK / RefMaxTempK, 4.0f);
    const float ReflectedEnergy = (1.0f - Emissivity) * FMath::Pow(AmbientTemperatureK / RefMaxTempK, 4.0f);
    
    const float TotalRadiance = SurfaceEnergy + ReflectedEnergy;
    return FMath::Clamp(TotalRadiance, 0.0f, 1.0f);
}

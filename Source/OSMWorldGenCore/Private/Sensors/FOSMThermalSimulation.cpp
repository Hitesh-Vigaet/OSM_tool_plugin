// Copyright InviMind. All Rights Reserved.

#include "Sensors/FOSMThermalSimulation.h"
#include "Sensors/UOSMThermalStateComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

FOSMThermalSimulation::FSimulationParams FOSMThermalSimulation::GlobalParams;

void FOSMThermalSimulation::UpdateEnvironmentFromTime(FSimulationParams& Params)
{
    // Sun direction: 0.0 to 24.0 hours maps to -90 to +270 degrees
    // We'll simplify: 0 = midnight (sun below ground), 6 = sunrise, 12 = noon, 18 = sunset
    float Hour = FMath::Fmod(Params.CurrentTimeOfDayHours, 24.0f);
    
    // Sun elevation angle: peaks at 12:00
    float SunElevation = (Hour - 6.0f) / 12.0f * PI; // 0 at 6:00, PI at 18:00
    
    float BaseTempK = Params.BaseEnvironmentTempC + 273.15f;

    if (Hour < 6.0f || Hour > 18.0f)
    {
        // Night time
        Params.SolarIrradianceWm2 = 0.0f;
        Params.SunDirection = FVector(0.0f, 0.0f, 1.0f); // Pointing up (irrelevant since irradiance is 0)
        Params.AmbientTemperatureK = BaseTempK; // Settle at Base Temp
    }
    else
    {
        // Day time
        float PeakIrradiance = 1000.0f; // Max solar irradiance at noon
        Params.SolarIrradianceWm2 = FMath::Sin(SunElevation) * PeakIrradiance;
        
        // Sun direction (Z is down in this vector logic, pointing towards ground)
        // Simplified path: rises in East (+Y), sets in West (-Y)
        float YDir = FMath::Cos(SunElevation); // +1 at 6:00, -1 at 18:00
        float ZDir = -FMath::Sin(SunElevation); // -1 at noon (straight down)
        
        Params.SunDirection = FVector(0.0f, YDir, ZDir).GetSafeNormal();
        
        // Ambient temp peaks around 14:00 (2:00 PM)
        float TempPhase = (Hour - 8.0f) / 12.0f * PI;
        TempPhase = FMath::Clamp(TempPhase, 0.0f, PI);
        Params.AmbientTemperatureK = BaseTempK + FMath::Sin(TempPhase) * 15.0f; // Ambient air heats up above base during day
    }
}

void FOSMThermalSimulation::ComputeInitialStates(UWorld* World, const FSimulationParams& Params)
{
    if (!World) return;

    float BaseTempK = Params.BaseEnvironmentTempC + 273.15f;
    
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        UOSMThermalStateComponent* ThermalComp = It->FindComponentByClass<UOSMThermalStateComponent>();
        if (!ThermalComp) continue;

        // Reset all zones in this component to BaseTempK
        for (FOSMThermalZone& Zone : ThermalComp->Zones)
        {
            Zone.CurrentTemperatureK = BaseTempK;
        }
    }
}

void FOSMThermalSimulation::TickNearSensor(
    UWorld* World,
    const FVector& SensorLocation,
    float RangeMeters,
    float DeltaTime,
    const FSimulationParams& Params)
{
    if (!World) return;
    if (!Params.bIsSimulationRunning) return;

    const double RangeSqCm = FMath::Square(static_cast<double>(RangeMeters) * 100.0);

    // Keep track of which actor we're at across ticks to distribute the workload
    static int32 CurrentActorIndex = 0;
    constexpr int32 MaxActorsPerTick = 256;
    int32 Processed = 0;
    int32 Skipped = 0;
    
    bool bReachedEnd = true;

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (Skipped < CurrentActorIndex)
        {
            Skipped++;
            continue;
        }

        if (Processed >= MaxActorsPerTick)
        {
            bReachedEnd = false;
            CurrentActorIndex = Skipped + Processed;
            break;
        }

        if (FVector::DistSquared(It->GetActorLocation(), SensorLocation) <= RangeSqCm)
        {
            if (UOSMThermalStateComponent* ThermalComp = It->FindComponentByClass<UOSMThermalStateComponent>())
            {
                ThermalComp->TickThermalState(
                    DeltaTime,
                    Params.TimeMultiplier,
                    Params.SolarIrradianceWm2,
                    Params.SunDirection,
                    Params.AmbientTemperatureK,
                    Params.SkyTemperatureK,
                    Params.WindSpeedMps);
            }
        }

        ++Processed;
    }

    if (bReachedEnd)
    {
        CurrentActorIndex = 0;
    }
}

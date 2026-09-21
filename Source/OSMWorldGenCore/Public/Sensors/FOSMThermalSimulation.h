// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Global thermal simulation manager.
 *
 * At generation time: pre-computes steady-state temperatures for ALL actors.
 * At runtime: dynamically simulates only actors within drone sensor range.
 *
 * Option C architecture: steady-state everywhere + dynamic near-drone.
 */
class OSMWORLDGENCORE_API FOSMThermalSimulation
{
public:
    struct FSimulationParams
    {
        // Environmental controls
        float BaseEnvironmentTempC = 20.0f;  // Starting/Midnight ambient temperature

        // Global time controls
        float CurrentTimeOfDayHours = 12.0f; // 0.0 to 24.0 (controls sun position)
        float TimeMultiplier = 1.0f;         // 1.0 = realtime, 3600.0 = 1 hr/sec
        bool bIsSimulationRunning = false;   // Whether dynamic heat accumulates

        // Dynamic environmental variables (calculated from TimeOfDay)
        float SolarIrradianceWm2 = 800.0f;     
        FVector SunDirection = FVector(0.0f, -0.5f, -0.866f);
        float AmbientTemperatureK = 293.15f;    
        float SkyTemperatureK = 253.15f;        
        float WindSpeedMps = 2.0f;
    };

    /** Global state for the UI to push to and the camera/simulation to read from. */
    static FSimulationParams GlobalParams;

    /**
     * Calculate dynamic environmental variables (sun position, temp) based on TimeOfDay.
     */
    static void UpdateEnvironmentFromTime(FSimulationParams& Params);

    /**
     * Initialize all components to the Base Environment Temperature.
     * Called when the simulation resets or starts.
     */
    static void ComputeInitialStates(UWorld* World, const FSimulationParams& Params);

    /**
     * Dynamic tick: update temperatures transiently for actors within range.
     * Called each frame/tick by the global control or sensor system.
     */
    static void TickNearSensor(
        UWorld* World,
        const FVector& SensorLocation,
        float RangeMeters,
        float DeltaTime,
        const FSimulationParams& Params);
};

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Sensors/UOSMThermalStateComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMThermalSimulationHeatingTest, "OSM.Sensors.ThermalHeatingComparison", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOSMThermalSimulationHeatingTest::RunTest(const FString& Parameters)
{
    // Setup a dummy ThermalStateComponent
    UOSMThermalStateComponent* ThermalState = NewObject<UOSMThermalStateComponent>();

    FOSMThermalZone AsphaltZone;
    AsphaltZone.MeshSectionIndex = 0;
    AsphaltZone.Category = EOSMSurfaceCategory::Asphalt;
    AsphaltZone.CurrentTemperatureK = 293.15f; // 20C
    AsphaltZone.AverageNormal = FVector::UpVector;

    FOSMThermalZone WaterZone;
    WaterZone.MeshSectionIndex = 1;
    WaterZone.Category = EOSMSurfaceCategory::Water;
    WaterZone.CurrentTemperatureK = 293.15f; // 20C
    WaterZone.AverageNormal = FVector::UpVector;

    ThermalState->Zones.Add(AsphaltZone);
    ThermalState->Zones.Add(WaterZone);

    // Simulate 3 hours (10800 seconds) in one large step for testing, or smaller steps
    // Midday sun directly above
    float SolarIrradiance = 1000.0f;
    FVector SunDirection = FVector(0, 0, -1);
    float AmbientTempK = 293.15f;
    float SkyTempK = 280.0f;
    float WindSpeed = 2.0f;

    // Simulate 3 hours (3 * 3600 seconds)
    float DeltaTime = 10.0f;
    float TimeMultiplier = 1.0f;
    int32 NumSteps = (3 * 3600) / DeltaTime;

    for (int32 i = 0; i < NumSteps; ++i)
    {
        ThermalState->TickThermalState(DeltaTime, TimeMultiplier, SolarIrradiance, SunDirection, AmbientTempK, SkyTempK, WindSpeed);
    }

    float FinalAsphaltTemp = ThermalState->GetZoneTemperature(0);
    float FinalWaterTemp = ThermalState->GetZoneTemperature(1);

    // Asphalt should heat up significantly more than water
    // Water has very high specific heat and evaporates (though evaporation isn't explicitly modeled, its density*cp is huge)
    // Actually, water's thermal mass is much higher.

    TestTrue(TEXT("Asphalt heats up more than Water"), FinalAsphaltTemp > FinalWaterTemp);
    TestTrue(TEXT("Asphalt temperature should rise above ambient"), FinalAsphaltTemp > AmbientTempK);

    UE_LOG(LogTemp, Log, TEXT("Final Asphalt Temp: %f K"), FinalAsphaltTemp);
    UE_LOG(LogTemp, Log, TEXT("Final Water Temp: %f K"), FinalWaterTemp);

    return true;
}

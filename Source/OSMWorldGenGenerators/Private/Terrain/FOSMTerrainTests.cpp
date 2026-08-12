// Copyright InviMind. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Terrain/FOSMDEMSampler.h"
#include "Terrain/FOSMGeoidCorrection.h"
#include "Terrain/FOSMHeightmapBuilder.h"

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMGeoidCorrectionTest, "OSMWorldGen.Terrain.GeoidCorrection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOSMGeoidCorrectionTest::RunTest(const FString& Parameters)
{
    // Test EGM96 geoid separation at known points
    // Equator, Prime Meridian (Lat 0, Lon 0)
    // EGM96 lookup gives ~15m
    double N1 = FOSMGeoidCorrection::GetGeoidSeparation(0.0, 0.0);
    TestTrueExpr(FMath::IsNearlyEqual(N1, 15.0, 2.0));

    // Mount Everest area (Lat 28, Lon 87)
    // EGM96 lookup is roughly -30m
    double N2 = FOSMGeoidCorrection::GetGeoidSeparation(28.0, 87.0);
    TestTrueExpr(N2 < 0.0);

    return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMHeightmapBuilderTest, "OSMWorldGen.Terrain.HeightmapBuilder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOSMHeightmapBuilderTest::RunTest(const FString& Parameters)
{
    // Test flat terrain generation (no DEM)
    FOSMDEMSampler EmptySampler;
    FOSMTerrainSettings Settings;
    Settings.SectionSize = 63;
    Settings.SectionsPerComponent = 1;
    Settings.ComponentCount = 2; // Size = (63-1)*1*2 + 1 = 125
    Settings.FlatElevationMeters = 100.0f;
    Settings.ElevationRangeMeters = 5000.0f;

    FOSMHeightmapResult Result;
    bool bSuccess = FOSMHeightmapBuilder::Build(
        EmptySampler,
        Settings,
        0.0, 0.1, // MinLat, MaxLat
        0.0, 0.1, // MinLon, MaxLon
        Result
    );

    TestTrue("Build success", bSuccess);
    TestTrue("Is valid", Result.IsValid());
    TestEqual("Size matches 125", Result.Size, 125);
    TestFalse("Not from DEM", Result.bFromDEM);

    // Verify all pixels have the same flat value
    if (Result.Data.Num() > 0)
    {
        uint16 FirstVal = Result.Data[0];
        bool bAllSame = true;
        for (uint16 V : Result.Data)
        {
            if (V != FirstVal)
            {
                bAllSame = false;
                break;
            }
        }
        TestTrue("All flat pixels identical", bAllSame);
    }

    return true;
}

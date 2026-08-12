// Copyright InviMind. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "CRS/FOSMCRSTransformer.h"
#include "OSMWorldGenCore.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMCRSTransformTest, "OSMWorldGen.CRS.ENUTransform", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMCRSTransformTest::RunTest(const FString& Parameters)
{
    // Origin at London (Big Ben)
    FOSMGeoOrigin Origin(51.5007, -0.1246, 0.0, false);

    UOSMCRSTransformer* Transformer = NewObject<UOSMCRSTransformer>();
    Transformer->Initialize(Origin, EOSMProjectionMode::ENU);

    // 1. Origin point should map exactly to (0, 0, 0)
    FVector OriginUE = Transformer->TransformToUnreal(Origin.Latitude, Origin.Longitude, Origin.HeightMeters);
    TestNearlyEqual(TEXT("Origin maps to (0,0,0) X"), OriginUE.X, 0.0, 0.1);
    TestNearlyEqual(TEXT("Origin maps to (0,0,0) Y"), OriginUE.Y, 0.0, 0.1);
    TestNearlyEqual(TEXT("Origin maps to (0,0,0) Z"), OriginUE.Z, 0.0, 0.1);

    // 2. Point displaced slightly North (+0.01 deg lat ~ 1113 meters -> 111300 cm)
    FVector NorthUE = Transformer->TransformToUnreal(Origin.Latitude + 0.01, Origin.Longitude, 0.0);
    TestTrue(TEXT("North displacement is positive Y"), NorthUE.Y > 100000.0);
    TestNearlyEqual(TEXT("North displacement X near zero"), NorthUE.X, 0.0, 500.0);

    // 3. Inverse transform roundtrip test
    double TestLat = 51.5050;
    double TestLon = -0.1200;
    double TestHeight = 10.0;

    FVector UEPos = Transformer->TransformToUnreal(TestLat, TestLon, TestHeight);

    double OutLat, OutLon, OutH;
    Transformer->TransformToWGS84(UEPos, OutLat, OutLon, OutH);

    TestNearlyEqual(TEXT("Inverse Lat roundtrip"), OutLat, TestLat, 0.00001);
    TestNearlyEqual(TEXT("Inverse Lon roundtrip"), OutLon, TestLon, 0.00001);
    TestNearlyEqual(TEXT("Inverse Height roundtrip"), OutH, TestHeight, 0.1);

    return true;
}

#endif

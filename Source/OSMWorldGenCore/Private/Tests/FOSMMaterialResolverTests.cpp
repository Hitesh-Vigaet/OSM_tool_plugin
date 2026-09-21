// Copyright InviMind. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Materials/EOSMSurfaceCategory.h"
#include "Materials/FOSMMaterialResolver.h"
#include "Materials/UOSMPhysicalMaterial.h"
#include "Materials/FOSMMaterialIdentityGroup.h"
#include "Materials/UOSMMaterialPalette.h"
#include "Materials/UOSMAssetPropertyRegistry.h"
#include "Sensors/UOSMThermalStateComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMMaterialResolverExplicitBuildingTest,
    "OSMWorldGen.Materials.ExplicitBuildingTags",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMMaterialResolverExplicitBuildingTest::RunTest(const FString& Parameters)
{
    // Test 1: Explicit brick wall + tile roof
    {
        TMap<FString, FString> Tags;
        Tags.Add(TEXT("building:material"), TEXT("brick"));
        Tags.Add(TEXT("roof:material"), TEXT("tiles"));

        const FOSMMaterialAssignment Mat = FOSMMaterialResolver::Resolve(
            EOSMNodeType::Building, TEXT("yes"), Tags);

        TestEqual(TEXT("Brick wall parsed"), Mat.WallCategory, EOSMSurfaceCategory::Brick);
        TestEqual(TEXT("Tile roof parsed"), Mat.RoofCategory, EOSMSurfaceCategory::RoofTile);
        TestEqual(TEXT("Dominant category is wall"), Mat.GetDominantCategory(EOSMNodeType::Building), EOSMSurfaceCategory::Brick);
    }

    // Test 2: Case insensitivity and whitespace trimming
    {
        TMap<FString, FString> Tags;
        Tags.Add(TEXT("building:material"), TEXT("  GLASS  "));
        Tags.Add(TEXT("roof:material"), TEXT("  METAL  "));

        const FOSMMaterialAssignment Mat = FOSMMaterialResolver::Resolve(
            EOSMNodeType::Building, TEXT("office"), Tags);

        TestEqual(TEXT("Glass wall case-insensitive"), Mat.WallCategory, EOSMSurfaceCategory::Glass);
        TestEqual(TEXT("Metal roof case-insensitive"), Mat.RoofCategory, EOSMSurfaceCategory::Metal);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMMaterialResolverSubtypeInferenceTest,
    "OSMWorldGen.Materials.SubtypeInference",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMMaterialResolverSubtypeInferenceTest::RunTest(const FString& Parameters)
{
    // Test 1: Industrial building with no tags -> Metal walls and Metal roof
    {
        TMap<FString, FString> Tags;
        const FOSMMaterialAssignment Mat = FOSMMaterialResolver::Resolve(
            EOSMNodeType::Building, TEXT("industrial"), Tags);

        TestEqual(TEXT("Industrial wall inferred as Metal"), Mat.WallCategory, EOSMSurfaceCategory::Metal);
        TestEqual(TEXT("Industrial roof inferred as Metal"), Mat.RoofCategory, EOSMSurfaceCategory::Metal);
    }

    // Test 2: Low-rise residential -> Plaster walls and RoofTile
    {
        TMap<FString, FString> Tags;
        Tags.Add(TEXT("building:levels"), TEXT("2"));
        const FOSMMaterialAssignment Mat = FOSMMaterialResolver::Resolve(
            EOSMNodeType::Building, TEXT("residential"), Tags);

        TestEqual(TEXT("Low-rise residential wall inferred as Plaster"), Mat.WallCategory, EOSMSurfaceCategory::Plaster);
        TestEqual(TEXT("Residential roof inferred as RoofTile"), Mat.RoofCategory, EOSMSurfaceCategory::RoofTile);
    }

    // Test 3: High-rise commercial -> Glass walls and Concrete roof
    {
        TMap<FString, FString> Tags;
        Tags.Add(TEXT("building:levels"), TEXT("12"));
        const FOSMMaterialAssignment Mat = FOSMMaterialResolver::Resolve(
            EOSMNodeType::Building, TEXT("commercial"), Tags);

        TestEqual(TEXT("High-rise commercial wall inferred as Glass"), Mat.WallCategory, EOSMSurfaceCategory::Glass);
        TestEqual(TEXT("High-rise commercial roof inferred as Concrete"), Mat.RoofCategory, EOSMSurfaceCategory::Concrete);
    }

    // Test 4: Church -> Stone walls and RoofTile
    {
        TMap<FString, FString> Tags;
        const FOSMMaterialAssignment Mat = FOSMMaterialResolver::Resolve(
            EOSMNodeType::Building, TEXT("church"), Tags);

        TestEqual(TEXT("Church wall inferred as Stone"), Mat.WallCategory, EOSMSurfaceCategory::Stone);
        TestEqual(TEXT("Church roof inferred as RoofTile"), Mat.RoofCategory, EOSMSurfaceCategory::RoofTile);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMMaterialResolverRoadAndAreaTest,
    "OSMWorldGen.Materials.RoadAndAreaClassification",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMMaterialResolverRoadAndAreaTest::RunTest(const FString& Parameters)
{
    // Test 1: Default road -> Asphalt
    {
        TMap<FString, FString> Tags;
        const FOSMMaterialAssignment Mat = FOSMMaterialResolver::Resolve(
            EOSMNodeType::RoadSegment, TEXT("primary"), Tags);

        TestEqual(TEXT("Default road surface is Asphalt"), Mat.SurfaceCategory, EOSMSurfaceCategory::Asphalt);
    }

    // Test 2: Cobblestone surface tag on road
    {
        TMap<FString, FString> Tags;
        Tags.Add(TEXT("surface"), TEXT("cobblestone"));
        const FOSMMaterialAssignment Mat = FOSMMaterialResolver::Resolve(
            EOSMNodeType::RoadSegment, TEXT("residential"), Tags);

        TestEqual(TEXT("Cobblestone road surface is Stone"), Mat.SurfaceCategory, EOSMSurfaceCategory::Stone);
    }

    // Test 3: WaterBody -> Water
    {
        TMap<FString, FString> Tags;
        const FOSMMaterialAssignment Mat = FOSMMaterialResolver::Resolve(
            EOSMNodeType::WaterBody, TEXT("lake"), Tags);

        TestEqual(TEXT("WaterBody surface is Water"), Mat.SurfaceCategory, EOSMSurfaceCategory::Water);
    }

    // Test 4: VegetationArea -> Vegetation
    {
        TMap<FString, FString> Tags;
        const FOSMMaterialAssignment Mat = FOSMMaterialResolver::Resolve(
            EOSMNodeType::VegetationArea, TEXT("forest"), Tags);

        TestEqual(TEXT("VegetationArea surface is Vegetation"), Mat.SurfaceCategory, EOSMSurfaceCategory::Vegetation);
    }

    // Test 5: Railway -> Stone (ballast)
    {
        TMap<FString, FString> Tags;
        const FOSMMaterialAssignment Mat = FOSMMaterialResolver::Resolve(
            EOSMNodeType::Railway, TEXT("rail"), Tags);

        TestEqual(TEXT("Railway surface is Stone"), Mat.SurfaceCategory, EOSMSurfaceCategory::Stone);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMPhysicalMaterialRadianceTest,
    "OSMWorldGen.Materials.PhysicalMaterialRadiance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMPhysicalMaterialRadianceTest::RunTest(const FString& Parameters)
{
    UOSMPhysicalMaterial* PhysMat = NewObject<UOSMPhysicalMaterial>(GetTransientPackage());

    // Asphalt: hot surface (323K = 50°C), high emissivity (0.93)
    PhysMat->Emissivity = 0.93f;
    PhysMat->BaseTemperatureK = 323.15f;
    const float AsphaltRadiance = PhysMat->GetNormalizedRadiance(293.15f);

    // Metal: hot surface (318K = 45°C), but very low emissivity (0.15), reflecting cool sky (293K)
    PhysMat->Emissivity = 0.15f;
    PhysMat->BaseTemperatureK = 318.15f;
    const float MetalRadiance = PhysMat->GetNormalizedRadiance(293.15f);

    // Water: cool surface (291K = 18°C), high emissivity (0.96)
    PhysMat->Emissivity = 0.96f;
    PhysMat->BaseTemperatureK = 291.15f;
    const float WaterRadiance = PhysMat->GetNormalizedRadiance(293.15f);

    // Verify relative thermal signatures:
    // Asphalt is hottest -> highest radiance
    // Water is cool -> lower radiance
    // Metal appears cool in thermal because low emissivity reflects the ambient environment
    TestTrue(TEXT("Asphalt radiance > Water radiance"), AsphaltRadiance > WaterRadiance);
    TestTrue(TEXT("Asphalt radiance > Metal apparent radiance"), AsphaltRadiance > MetalRadiance);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMPhysicalMaterialPropertiesTest,
    "OSMWorldGen.Materials.PhysicalMaterialProperties",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMPhysicalMaterialPropertiesTest::RunTest(const FString& Parameters)
{
    // Concrete test
    UOSMPhysicalMaterial* Concrete = NewObject<UOSMPhysicalMaterial>(GetTransientPackage());
    Concrete->MaterialLabel = TEXT("Concrete");
    Concrete->SurfaceCategory = EOSMSurfaceCategory::Concrete;
    Concrete->ThermalConductivity = 1.7f;
    Concrete->SpecificHeatCapacity = 880.0f;
    Concrete->ThermalDensity = 2300.0f;
    Concrete->DielectricConstant = 4.5f;
    Concrete->LiDARReflectivity905nm = 0.55f;

    // Wood test
    UOSMPhysicalMaterial* Wood = NewObject<UOSMPhysicalMaterial>(GetTransientPackage());
    Wood->MaterialLabel = TEXT("Wood");
    Wood->SurfaceCategory = EOSMSurfaceCategory::Wood;
    Wood->ThermalConductivity = 0.14f;
    Wood->SpecificHeatCapacity = 1700.0f;
    Wood->ThermalDensity = 600.0f;

    const float ConcreteInertia = Concrete->GetThermalInertia();
    const float WoodInertia = Wood->GetThermalInertia();

    TestTrue(TEXT("Concrete thermal inertia (~1854) > Wood (~377)"), ConcreteInertia > WoodInertia);
    TestTrue(TEXT("Concrete thermal diffusivity > 0"), Concrete->GetThermalDiffusivity() > 0.0f);
    TestEqual(TEXT("Concrete dielectric constant is 4.5"), Concrete->DielectricConstant, 4.5f);
    TestEqual(TEXT("Concrete LiDAR 905nm is 0.55"), Concrete->LiDARReflectivity905nm, 0.55f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMMaterialIdentityGroupTest,
    "OSMWorldGen.Materials.MaterialIdentityGroup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMMaterialIdentityGroupTest::RunTest(const FString& Parameters)
{
    UOSMPhysicalMaterial* SharedPhysMat = NewObject<UOSMPhysicalMaterial>(GetTransientPackage());
    SharedPhysMat->SurfaceCategory = EOSMSurfaceCategory::Concrete;
    SharedPhysMat->Emissivity = 0.92f;

    FOSMMaterialIdentityGroup Group;
    Group.Category = EOSMSurfaceCategory::Concrete;
    Group.PhysicalMaterial = SharedPhysMat;

    TestTrue(TEXT("MIG has physical material"), Group.HasPhysicalMaterial());
    TestFalse(TEXT("Empty visual variants falls back gracefully"), Group.HasVisualMaterials());

    // Deterministic selection returns nullptr when no variants are loaded
    UMaterialInterface* MatA = Group.SelectVisualVariant(42);
    UMaterialInterface* MatB = Group.SelectVisualVariant(42);
    TestEqual(TEXT("Deterministic selection is stable"), MatA, MatB);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMThermalSteadyStateSimulationTest,
    "OSMWorldGen.Materials.ThermalSteadyStateSimulation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMThermalSteadyStateSimulationTest::RunTest(const FString& Parameters)
{
    UOSMThermalStateComponent* Comp = NewObject<UOSMThermalStateComponent>(GetTransientPackage());

    UOSMPhysicalMaterial* Asphalt = NewObject<UOSMPhysicalMaterial>(GetTransientPackage());
    Asphalt->SurfaceCategory = EOSMSurfaceCategory::Asphalt;
    Asphalt->Emissivity = 0.93f;
    Asphalt->SolarAbsorptivity = 0.90f;
    Asphalt->BaseTemperatureK = 323.15f;

    UOSMPhysicalMaterial* Water = NewObject<UOSMPhysicalMaterial>(GetTransientPackage());
    Water->SurfaceCategory = EOSMSurfaceCategory::Water;
    Water->Emissivity = 0.96f;
    Water->SolarAbsorptivity = 0.06f;
    Water->BaseTemperatureK = 291.15f;

    Comp->InitializeZones(
        { EOSMSurfaceCategory::Asphalt, EOSMSurfaceCategory::Water },
        { Asphalt, Water },
        { 100.0f, 500.0f },
        { FVector::UpVector, FVector::UpVector }
    );

    // Noon simulation: 800 W/m2 sun overhead, 32 C (305.15 K) ambient, 2 m/s wind
    // Simulate for 5 hours (18000 seconds) to approach steady state
    Comp->TickThermalState(1.0f, 18000.0f, 800.0f, FVector(0, 0, -1), 305.15f, 253.15f, 2.0f);

    const float AsphaltTemp = Comp->GetZoneTemperature(0);
    const float WaterTemp = Comp->GetZoneTemperature(1);

    // Asphalt should absorb heavy solar radiation and heat significantly above ambient
    TestTrue(TEXT("Asphalt steady state temp > 310K"), AsphaltTemp > 310.0f);
    // Water has very low solar absorptivity (0.06) and should stay cool
    TestTrue(TEXT("Water steady state temp < 305K"), WaterTemp < 305.0f);
    // Thermal gradient: Asphalt significantly hotter than Water
    TestTrue(TEXT("Asphalt significantly hotter than Water"), (AsphaltTemp - WaterTemp) > 10.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMAssetPropertyRegistryTest,
    "OSMWorldGen.Materials.AssetPropertyRegistry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMAssetPropertyRegistryTest::RunTest(const FString& Parameters)
{
    UOSMAssetPropertyRegistry* Registry = UOSMAssetPropertyRegistry::CreateDefaultRegistry(GetTransientPackage());
    TestNotNull(TEXT("Registry created"), Registry);
    TestTrue(TEXT("Registry has standard descriptors"), Registry->Descriptors.Num() >= 5);

    const FOSMAssetMaterialDescriptor* ACDesc = Registry->FindByName(TEXT("AC_Unit"));
    TestNotNull(TEXT("AC_Unit descriptor exists"), ACDesc);
    if (ACDesc)
    {
        TestTrue(TEXT("AC_Unit is active thermal source"), ACDesc->bIsActiveThermalSource);
        TestTrue(TEXT("AC_Unit has heat output"), ACDesc->ActiveHeatOutputWatts >= 1000.0f);
        TestTrue(TEXT("AC_Unit exhaust is hot"), ACDesc->ActiveSourceTemperatureK > 320.0f);
    }

    const FOSMAssetMaterialDescriptor* TankPlastic = Registry->FindByName(TEXT("WaterTank_Plastic"));
    TestNotNull(TEXT("WaterTank_Plastic descriptor exists"), TankPlastic);
    if (TankPlastic)
    {
        TestTrue(TEXT("WaterTank_Plastic slot 0 is Plastic"),
            TankPlastic->SlotCategories.Contains(0) && TankPlastic->SlotCategories[0] == EOSMSurfaceCategory::Plastic);
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

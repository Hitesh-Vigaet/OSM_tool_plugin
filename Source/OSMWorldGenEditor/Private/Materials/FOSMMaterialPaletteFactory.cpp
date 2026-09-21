// Copyright InviMind. All Rights Reserved.

#include "Materials/FOSMMaterialPaletteFactory.h"
#include "Materials/UOSMMaterialPalette.h"
#include "Materials/UOSMPhysicalMaterial.h"
#include "Materials/FOSMMaterialIdentityGroup.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
    struct FOSMPropertyRow
    {
        float Emissivity;
        float SolarAbsorptivity;
        float BaseTemperatureK;
        float ThermalConductivity;    // W/(m·K)
        float SpecificHeatCapacity;   // J/(kg·K)
        float Density;                // kg/m³
        float ThicknessMeters;
        float DielectricConstant;
        float Conductivity;           // S/m
        float RCSModifier;
        bool  bRadarTransparent;
        float LiDARReflectivity905nm;
        float LiDARReflectivity1550nm;
        bool  bSpecularReflector;
        float AttenuationDB;
        float RFReflectionCoefficient;
        FLinearColor DebugColour;
    };

    // Indexed by EOSMSurfaceCategory (0..15)
    static const FOSMPropertyRow PropertyTable[] = {
        // 0: Unknown
        { 0.90f, 0.50f, 293.15f, 1.0f, 880.0f, 2000.0f, 0.20f, 3.0f, 0.01f, 0.5f, false, 0.40f, 0.35f, false, 8.0f, 0.30f, {0.40f, 0.40f, 0.40f} },
        // 1: Concrete
        { 0.92f, 0.65f, 303.15f, 1.7f, 880.0f, 2300.0f, 0.25f, 4.5f, 0.01f, 1.0f, false, 0.55f, 0.50f, false, 15.0f, 0.50f, {0.55f, 0.55f, 0.55f} },
        // 2: Brick
        { 0.93f, 0.70f, 305.15f, 0.72f, 840.0f, 1900.0f, 0.23f, 4.0f, 0.01f, 0.9f, false, 0.42f, 0.38f, false, 10.0f, 0.30f, {0.55f, 0.22f, 0.15f} },
        // 3: Stone
        { 0.90f, 0.60f, 300.15f, 2.3f, 840.0f, 2600.0f, 0.30f, 6.0f, 0.01f, 1.0f, false, 0.48f, 0.42f, false, 12.0f, 0.40f, {0.60f, 0.60f, 0.58f} },
        // 4: Metal
        { 0.15f, 0.40f, 318.15f, 50.0f, 500.0f, 7800.0f, 0.003f, 1e6f, 3.8e7f, 2.0f, false, 0.85f, 0.80f, true, 50.0f, 0.95f, {0.70f, 0.75f, 0.80f} },
        // 5: Glass
        { 0.92f, 0.20f, 298.15f, 1.0f, 840.0f, 2500.0f, 0.006f, 5.0f, 1e-12f, 0.7f, false, 0.10f, 0.08f, true, 3.0f, 0.20f, {0.30f, 0.60f, 0.80f} },
        // 6: Wood
        { 0.90f, 0.55f, 300.15f, 0.14f, 1700.0f, 600.0f, 0.15f, 2.5f, 0.001f, 0.3f, true, 0.48f, 0.42f, false, 5.0f, 0.10f, {0.45f, 0.30f, 0.15f} },
        // 7: Plastic
        { 0.92f, 0.50f, 305.15f, 0.20f, 1500.0f, 1200.0f, 0.005f, 2.5f, 1e-14f, 0.1f, true, 0.40f, 0.35f, false, 2.0f, 0.05f, {0.80f, 0.80f, 0.60f} },
        // 8: Plaster
        { 0.91f, 0.45f, 302.15f, 0.70f, 840.0f, 1800.0f, 0.03f, 3.0f, 0.005f, 0.7f, false, 0.52f, 0.47f, false, 3.0f, 0.15f, {0.85f, 0.82f, 0.75f} },
        // 9: Asphalt
        { 0.93f, 0.90f, 323.15f, 0.75f, 920.0f, 2300.0f, 0.10f, 4.0f, 0.01f, 0.8f, false, 0.20f, 0.15f, false, 8.0f, 0.25f, {0.15f, 0.15f, 0.15f} },
        // 10: Soil
        { 0.92f, 0.75f, 298.15f, 1.5f, 1000.0f, 1600.0f, 1.0f, 3.0f, 0.01f, 0.5f, false, 0.30f, 0.25f, false, 5.0f, 0.15f, {0.35f, 0.25f, 0.15f} },
        // 11: Grass
        { 0.96f, 0.25f, 295.15f, 0.25f, 3500.0f, 800.0f, 0.05f, 20.0f, 0.05f, 0.4f, false, 0.60f, 0.28f, false, 2.0f, 0.05f, {0.20f, 0.55f, 0.15f} },
        // 12: Vegetation
        { 0.97f, 0.20f, 293.15f, 0.20f, 3800.0f, 700.0f, 0.10f, 20.0f, 0.05f, 0.4f, false, 0.55f, 0.32f, false, 3.0f, 0.05f, {0.10f, 0.40f, 0.10f} },
        // 13: Water
        { 0.96f, 0.06f, 291.15f, 0.60f, 4186.0f, 1000.0f, 1.0f, 80.0f, 0.01f, 1.5f, false, 0.05f, 0.02f, true, 20.0f, 0.75f, {0.10f, 0.30f, 0.60f} },
        // 14: CementBlock
        { 0.91f, 0.65f, 303.15f, 1.0f, 880.0f, 2000.0f, 0.20f, 4.0f, 0.01f, 0.9f, false, 0.50f, 0.45f, false, 12.0f, 0.45f, {0.60f, 0.62f, 0.60f} },
        // 15: RoofTile
        { 0.90f, 0.88f, 313.15f, 0.84f, 880.0f, 2000.0f, 0.05f, 5.0f, 0.01f, 0.9f, false, 0.38f, 0.32f, false, 8.0f, 0.30f, {0.65f, 0.25f, 0.10f} },
    };

    const FOSMPropertyRow& GetRow(EOSMSurfaceCategory Cat)
    {
        const int32 Index = FMath::Clamp(static_cast<int32>(Cat), 0, 15);
        return PropertyTable[Index];
    }
}

float FOSMMaterialPaletteFactory::GetStandardEmissivity(EOSMSurfaceCategory Category)
{
    return GetRow(Category).Emissivity;
}

float FOSMMaterialPaletteFactory::GetStandardSolarAbsorptivity(EOSMSurfaceCategory Category)
{
    return GetRow(Category).SolarAbsorptivity;
}

float FOSMMaterialPaletteFactory::GetStandardBaseTemperatureK(EOSMSurfaceCategory Category)
{
    return GetRow(Category).BaseTemperatureK;
}

float FOSMMaterialPaletteFactory::GetStandardThermalConductivity(EOSMSurfaceCategory Category)
{
    return GetRow(Category).ThermalConductivity;
}

float FOSMMaterialPaletteFactory::GetStandardSpecificHeatCapacity(EOSMSurfaceCategory Category)
{
    return GetRow(Category).SpecificHeatCapacity;
}

float FOSMMaterialPaletteFactory::GetStandardDensity(EOSMSurfaceCategory Category)
{
    return GetRow(Category).Density;
}

float FOSMMaterialPaletteFactory::GetStandardThicknessMeters(EOSMSurfaceCategory Category)
{
    return GetRow(Category).ThicknessMeters;
}

float FOSMMaterialPaletteFactory::GetStandardDielectricConstant(EOSMSurfaceCategory Category)
{
    return GetRow(Category).DielectricConstant;
}

float FOSMMaterialPaletteFactory::GetStandardConductivity(EOSMSurfaceCategory Category)
{
    return GetRow(Category).Conductivity;
}

float FOSMMaterialPaletteFactory::GetStandardRCSModifier(EOSMSurfaceCategory Category)
{
    return GetRow(Category).RCSModifier;
}

bool FOSMMaterialPaletteFactory::GetStandardRadarTransparent(EOSMSurfaceCategory Category)
{
    return GetRow(Category).bRadarTransparent;
}

float FOSMMaterialPaletteFactory::GetStandardLiDARReflectivity905nm(EOSMSurfaceCategory Category)
{
    return GetRow(Category).LiDARReflectivity905nm;
}

float FOSMMaterialPaletteFactory::GetStandardLiDARReflectivity1550nm(EOSMSurfaceCategory Category)
{
    return GetRow(Category).LiDARReflectivity1550nm;
}

bool FOSMMaterialPaletteFactory::GetStandardSpecularReflector(EOSMSurfaceCategory Category)
{
    return GetRow(Category).bSpecularReflector;
}

float FOSMMaterialPaletteFactory::GetStandardAttenuationDB(EOSMSurfaceCategory Category)
{
    return GetRow(Category).AttenuationDB;
}

float FOSMMaterialPaletteFactory::GetStandardRFReflectionCoefficient(EOSMSurfaceCategory Category)
{
    return GetRow(Category).RFReflectionCoefficient;
}

FLinearColor FOSMMaterialPaletteFactory::GetCategoryDebugColour(EOSMSurfaceCategory Category)
{
    return GetRow(Category).DebugColour;
}

UOSMPhysicalMaterial* FOSMMaterialPaletteFactory::CreatePhysicalMaterial(UObject* Outer, EOSMSurfaceCategory Category)
{
    UOSMPhysicalMaterial* PM = NewObject<UOSMPhysicalMaterial>(Outer);
    if (!PM) return nullptr;

    const FOSMPropertyRow& Row = GetRow(Category);

    PM->SurfaceCategory = Category;
    PM->MaterialLabel = OSMSurfaceCategoryToString(Category);

    // Thermal surface
    PM->Emissivity = Row.Emissivity;
    PM->SolarAbsorptivity = Row.SolarAbsorptivity;
    PM->BaseTemperatureK = Row.BaseTemperatureK;

    // Thermal volume
    PM->ThermalConductivity = Row.ThermalConductivity;
    PM->SpecificHeatCapacity = Row.SpecificHeatCapacity;
    PM->Density = Row.Density;
    PM->ThicknessMeters = Row.ThicknessMeters;

    // Radar
    PM->DielectricConstant = Row.DielectricConstant;
    PM->Conductivity = Row.Conductivity;
    PM->RCSModifier = Row.RCSModifier;
    PM->bRadarTransparent = Row.bRadarTransparent;

    // LiDAR
    PM->LiDARReflectivity905nm = Row.LiDARReflectivity905nm;
    PM->LiDARReflectivity1550nm = Row.LiDARReflectivity1550nm;
    PM->bSpecularReflector = Row.bSpecularReflector;

    // RF
    PM->AttenuationDB = Row.AttenuationDB;
    PM->RFReflectionCoefficient = Row.RFReflectionCoefficient;

    return PM;
}

UMaterialInstanceDynamic* FOSMMaterialPaletteFactory::CreateGreyBoxMaterial(
    UObject* Outer,
    EOSMSurfaceCategory Category,
    UOSMPhysicalMaterial* PhysMat)
{
    static UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(
        nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

    if (!BaseMat) return nullptr;

    UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMat, Outer);
    if (!MID) return nullptr;

    // Set the grey-box debug colour for visual identification
    MID->SetVectorParameterValue(TEXT("Color"), GetCategoryDebugColour(Category));

    // Wire the physical material — this is the critical link for sensor traces
    if (PhysMat)
    {
        MID->PhysMaterial = PhysMat;
    }

    return MID;
}

UOSMMaterialPalette* FOSMMaterialPaletteFactory::CreateDefaultPalette(UObject* Outer)
{
    UOSMMaterialPalette* Palette = NewObject<UOSMMaterialPalette>(Outer);
    if (!Palette) return nullptr;

    for (uint8 CatIdx = 0; CatIdx < static_cast<uint8>(EOSMSurfaceCategory::MAX); ++CatIdx)
    {
        const EOSMSurfaceCategory Cat = static_cast<EOSMSurfaceCategory>(CatIdx);

        FOSMMaterialIdentityGroup Group;
        Group.Category = Cat;
        Group.PhysicalMaterial = CreatePhysicalMaterial(Palette, Cat);
        // VisualVariants left empty -> fallback to CreateGreyBoxMaterial()

        Palette->Groups.Add(Group);
    }

    return Palette;
}

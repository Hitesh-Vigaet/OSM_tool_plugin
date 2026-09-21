// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Materials/EOSMSurfaceCategory.h"

class UOSMMaterialPalette;
class UOSMPhysicalMaterial;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * Factory for creating default physical materials and procedural material palettes
 * populated with standard real-world thermal emissivity, electromagnetic, and thermodynamic properties.
 */
class OSMWORLDGENEDITOR_API FOSMMaterialPaletteFactory
{
public:
    /** Create a complete in-memory palette with all 16 surface categories populated with MIGs. */
    static UOSMMaterialPalette* CreateDefaultPalette(UObject* Outer);

    /** Create a physical material with standard physical/thermal/EM properties for a given category. */
    static UOSMPhysicalMaterial* CreatePhysicalMaterial(UObject* Outer, EOSMSurfaceCategory Category);

    /**
     * Create a grey-box UMaterialInstanceDynamic tinted to the category debug colour,
     * with its PhysMaterial slot wired to the correct UOSMPhysicalMaterial.
     *
     * This is the production replacement for MakeColouredMaterial() —
     * it produces a material that is visually grey-box but physically correct for sensor traces.
     */
    static UMaterialInstanceDynamic* CreateGreyBoxMaterial(
        UObject* Outer,
        EOSMSurfaceCategory Category,
        UOSMPhysicalMaterial* PhysMat);

    // ──── Thermal / IR Surface Properties ────

    /** Get standard thermal emissivity for a category (0..1). */
    static float GetStandardEmissivity(EOSMSurfaceCategory Category);

    /** Get standard solar absorptivity for a category (0..1). */
    static float GetStandardSolarAbsorptivity(EOSMSurfaceCategory Category);

    /** Get standard base surface temperature in Kelvin for a category. */
    static float GetStandardBaseTemperatureK(EOSMSurfaceCategory Category);

    // ──── Thermal Volume Properties (Heat Conduction) ────

    /** Get standard thermal conductivity in W/(m·K). */
    static float GetStandardThermalConductivity(EOSMSurfaceCategory Category);

    /** Get standard specific heat capacity in J/(kg·K). */
    static float GetStandardSpecificHeatCapacity(EOSMSurfaceCategory Category);

    /** Get standard material density in kg/m³. */
    static float GetStandardDensity(EOSMSurfaceCategory Category);

    /** Get standard material layer thickness in meters. */
    static float GetStandardThicknessMeters(EOSMSurfaceCategory Category);

    // ──── Radar / Electromagnetic Properties ────

    /** Get standard dielectric constant (relative permittivity). */
    static float GetStandardDielectricConstant(EOSMSurfaceCategory Category);

    /** Get standard electrical conductivity in S/m. */
    static float GetStandardConductivity(EOSMSurfaceCategory Category);

    /** Get standard RCS modifier. */
    static float GetStandardRCSModifier(EOSMSurfaceCategory Category);

    /** Get whether material is radar-transparent. */
    static bool GetStandardRadarTransparent(EOSMSurfaceCategory Category);

    // ──── LiDAR Properties ────

    /** Get standard LiDAR reflectivity at 905nm. */
    static float GetStandardLiDARReflectivity905nm(EOSMSurfaceCategory Category);

    /** Get standard LiDAR reflectivity at 1550nm. */
    static float GetStandardLiDARReflectivity1550nm(EOSMSurfaceCategory Category);

    /** Get whether surface is specular reflector. */
    static bool GetStandardSpecularReflector(EOSMSurfaceCategory Category);

    // ──── RF / Radio Properties ────

    /** Get standard RF attenuation in dB. */
    static float GetStandardAttenuationDB(EOSMSurfaceCategory Category);

    /** Get standard RF reflection coefficient. */
    static float GetStandardRFReflectionCoefficient(EOSMSurfaceCategory Category);

    // ──── Visual Debug ────

    /** Get debug preview colour for a category. */
    static FLinearColor GetCategoryDebugColour(EOSMSurfaceCategory Category);
};

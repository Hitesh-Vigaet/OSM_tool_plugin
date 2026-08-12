# OSM World Generator — UE 5.8 Plugin Implementation Design & Plan

> **Version:** 1.0 — Initial Design  
> **Date:** 2026-08-04  
> **Target Engine:** Unreal Engine 5.8 (last major UE5 release before UE6)  
> **Status:** Draft — awaiting review

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Scope & Constraints](#2-scope--constraints)
3. [System Architecture Overview](#3-system-architecture-overview)
4. [Plugin Module Breakdown](#4-plugin-module-breakdown)
5. [Third-Party Dependencies](#5-third-party-dependencies)
6. [Data Pipeline — Five-Stage Design](#6-data-pipeline--five-stage-design)
7. [Coordinate Reference System — Deep Dive](#7-coordinate-reference-system--deep-dive)
8. [Core C++ Class Hierarchy](#8-core-c-class-hierarchy)
9. [Stage 1 — Import Subsystem](#9-stage-1--import-subsystem)
10. [Stage 2 — Parse & Tag Subsystem](#10-stage-2--parse--tag-subsystem)
11. [Stage 3 — Geographic Transform Subsystem](#11-stage-3--geographic-transform-subsystem)
12. [Stage 4 — Geometry Generation Subsystem](#12-stage-4--geometry-generation-subsystem)
13. [Stage 5 — Tagging, Registration & Asset Replacement](#13-stage-5--tagging-registration--asset-replacement)
14. [Editor UX — Import Wizard](#14-editor-ux--import-wizard)
15. [Performance & Scalability Architecture](#15-performance--scalability-architecture)
16. [World Partition Integration](#16-world-partition-integration)
17. [Testing & Validation Strategy](#17-testing--validation-strategy)
18. [Phased Roadmap](#18-phased-roadmap)
19. [Risk Register](#19-risk-register)
20. [Licensing & Attribution](#20-licensing--attribution)
21. [Appendices](#21-appendices)

---

## 1. Executive Summary

This document specifies the complete implementation design for an Unreal Engine 5.8 editor plugin that imports OpenStreetMap (`.osm` / `.osm.pbf`) data and generates a georeferenced 3D world — terrain, roads, buildings, water, and land use — inside the Unreal Editor. Every generated actor carries queryable OSM metadata and can be swapped for authored assets via rule-based matching.

### Design Pillars

| Pillar | Rationale |
|--------|-----------|
| **GIS-grade accuracy** | Single shared CRS transform function; sub-meter precision for areas < 50 km² |
| **Production terrain** | UE 5.8 Landscape system (heightmap), NOT experimental Mesh Terrain |
| **Streaming at scale** | World Partition, async generation, chunked parsing for city-scale PBFs |
| **Extensibility** | Each generator (terrain, road, building) is a pluggable `UOSMGenerator` subclass |
| **No reinventing geodesy** | Lean on Cesium-for-Unreal or mirror its georeferencing; lean on libosmium for parsing |

---

## 2. Scope & Constraints

### 2.1 UE 5.8 Context

- **Last major UE5 release** before UE6 (confirmed at Unreal Fest, June 2026).
- Ships **Mesh Terrain** (experimental — true 3D mesh, handles overhangs/caves) alongside the mature **Landscape** system.
- **Interchange** framework is now the default import pipeline for FBX/glTF/USD.
- **PCG framework** (Procedural Content Generation) is production-ready and doubled-down on in 5.8.
- **Large World Coordinates (LWC)** — 64-bit double-precision — on by default since UE 5.0.
- **World Partition** replaces legacy World Composition / Level Streaming.
- **Geometry Scripting** plugin provides high-level procedural mesh APIs (`AppendExtrudePolygon`, booleans, bevels).

### 2.2 In Scope (v1)

- Import `.osm` (XML) and `.osm.pbf` (Protocol Buffers Binary) files
- Parse all primary OSM feature types: buildings, highways, waterways, landuse, natural areas
- DEM import (GeoTIFF — SRTM / Copernicus GLO-30 / USGS) → Landscape heightmap
- Road spline generation with width/material by highway classification
- Building footprint extrusion with height resolution from tags
- Full OSM metadata on every actor (queryable, filterable)
- Rule-based asset replacement with Interchange import pipeline
- Editor import wizard with progress, cancel, and validation
- World Partition streaming for large imports

### 2.3 Out of Scope (v2+)

- Runtime / in-game import (requires separating editor-only code; deferred)
- Complex roof shapes (`roof:shape` beyond flat/hipped)
- Bridge/tunnel elevation handling
- Real-time OSM tile streaming (Overpass API)
- Vegetation scattering from `natural=tree` point data
- Experimental Mesh Terrain backend
- Bundled 3D asset library (user supplies their own)

---

## 3. System Architecture Overview

```
┌────────────────────────────────────────────────────────────────────────────────┐
│                              OSMWorldGen Plugin                                │
│                                                                                │
│  ┌─────────────────────┐  ┌─────────────────────┐  ┌───────────────────────┐   │
│  │   OSMWorldGenCore   │  │  OSMWorldGenEditor  │  │  OSMWorldGenRuntime   │   │
│  │   (Runtime module)  │  │  (Editor module)    │  │  (Optional, future)   │   │
│  │                     │  │                     │  │                       │   │
│  │  • OSM Parser       │  │  • Import Wizard    │  │  • Runtime metadata   │   │
│  │  • Feature Model    │  │  • Progress UI      │  │    queries            │   │
│  │  • CRS Transform    │  │  • Asset Match UI   │  │  • Blueprint API      │   │
│  │  • Tag Classifier   │  │  • Validation       │  │                       │   │
│  │  • Generator Base   │  │  • Settings Panel   │  │                       │   │
│  └────────┬────────────┘  └────────┬────────────┘  └───────────────────────┘   │
│           │                        │                                           │
│  ┌────────▼────────────────────────▼────────────────────────────────────────┐  │
│  │                    OSMWorldGenGenerators (Editor module)                 │  │
│  │                                                                          │  │
│  │  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐ ┌────────────────┐   │  │
│  │  │ Terrain Gen  │ │  Road Gen    │ │ Building Gen │ │ Water/Land Gen │   │  │
│  │  │ (Landscape)  │ │  (Splines)   │ │ (Extrusion)  │ │ (Flat polys)   │   │  │
│  │  └──────────────┘ └──────────────┘ └──────────────┘ └────────────────┘   │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │                     Third-Party (ThirdParty/ folder)                     │  │
│  │                                                                          │  │
│  │  libosmium (header-only) · protozero · zlib · expat · GDAL (optional)    │  │
│  │  OR Cesium for Unreal (plugin dependency for georeferencing)             │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Plugin Module Breakdown

### 4.1 `.uplugin` Descriptor

```json
{
    "FileVersion": 3,
    "Version": 1,
    "VersionName": "1.0.0",
    "FriendlyName": "OSM World Generator",
    "Description": "Import OpenStreetMap data and generate georeferenced 3D worlds in UE 5.8",
    "Category": "GIS / World Building",
    "CreatedBy": "InviMind",
    "CanContainContent": true,
    "IsBetaVersion": true,
    "Modules": [
        {
            "Name": "OSMWorldGenCore",
            "Type": "Runtime",
            "LoadingPhase": "Default"
        },
        {
            "Name": "OSMWorldGenEditor",
            "Type": "Editor",
            "LoadingPhase": "Default"
        },
        {
            "Name": "OSMWorldGenGenerators",
            "Type": "Editor",
            "LoadingPhase": "Default"
        }
    ],
    "Plugins": [
        { "Name": "GeometryScripting", "Enabled": true },
        { "Name": "PCG", "Enabled": true },
        { "Name": "ProceduralMeshComponent", "Enabled": true }
    ]
}
```

### 4.2 Module Responsibilities

| Module | Type | Responsibilities | Key Dependencies |
|--------|------|-----------------|------------------|
| **OSMWorldGenCore** | Runtime | OSM parsing (libosmium wrapper), internal feature data model, CRS/geodesy math, tag classification engine, generator base classes, metadata component | `Core`, `CoreUObject`, `Engine`, libosmium, protozero, zlib, expat |
| **OSMWorldGenEditor** | Editor | Import wizard (Slate/UMG widget), progress reporting, asset-matching rule editor, settings panel, import validation, undo/redo integration | `UnrealEd`, `Slate`, `SlateCore`, `EditorFramework`, `PropertyEditor`, OSMWorldGenCore |
| **OSMWorldGenGenerators** | Editor | Terrain generator (Landscape API), road generator (splines + PCG), building generator (Geometry Scripting extrusion), water/landuse generator, asset replacement engine | `Landscape`, `PCG`, `GeometryScriptingCore`, `ProceduralMeshComponent`, `InterchangeCore`, OSMWorldGenCore |

### 4.3 Directory Structure

```
Plugins/OSMWorldGen/
├── OSMWorldGen.uplugin
├── Config/
│   └── DefaultOSMWorldGen.ini          # Default settings (CRS mode, fallback heights, etc.)
├── Content/
│   ├── Materials/                      # Default road/building/water materials
│   ├── DataAssets/                      # Default road type definitions, building style rules
│   └── Icons/                          # Plugin toolbar icons
├── Resources/
│   └── Icon128.png
├── Source/
│   ├── OSMWorldGenCore/
│   │   ├── OSMWorldGenCore.Build.cs
│   │   ├── Public/
│   │   │   ├── OSMWorldGenCore.h
│   │   │   ├── Parsing/
│   │   │   │   ├── FOSMParser.h
│   │   │   │   ├── FOSMNode.h
│   │   │   │   ├── FOSMWay.h
│   │   │   │   ├── FOSMRelation.h
│   │   │   │   └── FOSMParseResult.h
│   │   │   ├── Model/
│   │   │   │   ├── FOSMFeature.h
│   │   │   │   ├── EOSMFeatureType.h
│   │   │   │   ├── FOSMFeatureTable.h
│   │   │   │   └── FOSMTagDictionary.h
│   │   │   ├── CRS/
│   │   │   │   ├── FOSMGeoOrigin.h
│   │   │   │   ├── FOSMCRSTransformer.h
│   │   │   │   ├── EOSMProjectionMode.h
│   │   │   │   └── FOSMEllipsoid.h
│   │   │   ├── Classification/
│   │   │   │   ├── FOSMTagClassifier.h
│   │   │   │   └── FOSMClassificationRules.h
│   │   │   ├── Generators/
│   │   │   │   ├── UOSMGeneratorBase.h
│   │   │   │   └── FOSMGenerationContext.h
│   │   │   └── Components/
│   │   │       └── UOSMMetadataComponent.h
│   │   └── Private/
│   │       ├── OSMWorldGenCoreModule.cpp
│   │       ├── Parsing/
│   │       ├── Model/
│   │       ├── CRS/
│   │       ├── Classification/
│   │       ├── Generators/
│   │       └── Components/
│   ├── OSMWorldGenEditor/
│   │   ├── OSMWorldGenEditor.Build.cs
│   │   ├── Public/
│   │   │   ├── OSMWorldGenEditor.h
│   │   │   ├── Wizard/
│   │   │   │   ├── SOSMImportWizard.h
│   │   │   │   ├── SOSMStepFileSelect.h
│   │   │   │   ├── SOSMStepCRSConfig.h
│   │   │   │   ├── SOSMStepFeatureFilter.h
│   │   │   │   ├── SOSMStepGeneration.h
│   │   │   │   └── SOSMStepReview.h
│   │   │   ├── AssetMatching/
│   │   │   │   ├── SOSMAssetMatchEditor.h
│   │   │   │   └── FOSMAssetMatchRule.h
│   │   │   └── Settings/
│   │   │       └── UOSMWorldGenSettings.h
│   │   └── Private/
│   │       ├── OSMWorldGenEditorModule.cpp
│   │       ├── Wizard/
│   │       ├── AssetMatching/
│   │       └── Settings/
│   └── OSMWorldGenGenerators/
│       ├── OSMWorldGenGenerators.Build.cs
│       ├── Public/
│       │   ├── OSMWorldGenGenerators.h
│       │   ├── Terrain/
│       │   │   ├── UOSMTerrainGenerator.h
│       │   │   ├── FOSMDEMImporter.h
│       │   │   └── FOSMHeightmapBuilder.h
│       │   ├── Roads/
│       │   │   ├── UOSMRoadGenerator.h
│       │   │   ├── FOSMRoadProfile.h
│       │   │   └── UOSMRoadTypeDataAsset.h
│       │   ├── Buildings/
│       │   │   ├── UOSMBuildingGenerator.h
│       │   │   ├── FOSMBuildingExtruder.h
│       │   │   └── FOSMRoofGenerator.h
│       │   ├── Water/
│       │   │   └── UOSMWaterGenerator.h
│       │   ├── Landuse/
│       │   │   └── UOSMLanduseGenerator.h
│       │   └── AssetReplacement/
│       │       ├── UOSMAssetReplacer.h
│       │       ├── FOSMReplacementRule.h
│       │       └── FOSMInterchangeImporter.h
│       └── Private/
│           ├── OSMWorldGenGeneratorsModule.cpp
│           ├── Terrain/
│           ├── Roads/
│           ├── Buildings/
│           ├── Water/
│           ├── Landuse/
│           └── AssetReplacement/
└── ThirdParty/
    ├── libosmium/
    │   ├── include/                    # Header-only library
    │   └── libosmium.tps              # Third-party software notice
    ├── protozero/
    │   ├── include/
    │   └── protozero.tps
    ├── zlib/                           # Or use UE's bundled zlib
    └── expat/                          # XML parsing for .osm
```

---

## 5. Third-Party Dependencies

### 5.1 Decision Matrix

| Library | Purpose | License | Integration Strategy | Bundled Size |
|---------|---------|---------|---------------------|--------------|
| **libosmium** | Parse `.osm` / `.osm.pbf` files | Boost 1.0 (permissive) | Header-only, include directly | ~2 MB headers |
| **protozero** | PBF decoding (libosmium dependency) | BSD-2-Clause | Header-only, include directly | ~200 KB |
| **zlib** | Compressed PBF block decompression | zlib license | Use UE's bundled `ThirdParty/zlib` | 0 (already in engine) |
| **expat** | XML parsing for `.osm` files | MIT | Bundle or use platform package | ~300 KB |
| **GDAL** (optional) | GeoTIFF DEM reading, CRS projection | MIT/X11 | Optional plugin dependency; fallback to raw GeoTIFF reader | ~30 MB if bundled |
| **Cesium for Unreal** (optional) | Georeferencing, WGS84 ellipsoid math | Apache-2.0 | Plugin dependency, or mirror approach standalone | 0 (marketplace plugin) |

### 5.2 Build.cs Integration Pattern

```csharp
// OSMWorldGenCore.Build.cs (simplified)
public class OSMWorldGenCore : ModuleRules
{
    public OSMWorldGenCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp17;

        // UE modules
        PublicDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine"
        });

        // libosmium (header-only)
        string OsmiumPath = Path.Combine(PluginDirectory, "ThirdParty", "libosmium", "include");
        PublicIncludePaths.Add(OsmiumPath);

        // protozero (header-only, libosmium dependency)
        string ProtozeroPath = Path.Combine(PluginDirectory, "ThirdParty", "protozero", "include");
        PublicIncludePaths.Add(ProtozeroPath);

        // Use UE's bundled zlib
        AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");

        // expat for .osm XML
        string ExpatPath = Path.Combine(PluginDirectory, "ThirdParty", "expat");
        PublicIncludePaths.Add(Path.Combine(ExpatPath, "include"));
        // Link expat static lib per platform...

        // Optional: GDAL
        if (Target.Platform == UnrealTargetPlatform.Win64 || 
            Target.Platform == UnrealTargetPlatform.Mac ||
            Target.Platform == UnrealTargetPlatform.Linux)
        {
            PublicDefinitions.Add("OSM_WITH_GDAL=1");
            // Add GDAL include/lib paths...
        }
    }
}
```

---

## 6. Data Pipeline — Five-Stage Design

```
┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
│  STAGE 1 │───▶│  STAGE 2 │───▶│  STAGE 3 │───▶│  STAGE 4 │───▶│  STAGE 5 │
│  IMPORT  │    │  PARSE & │    │  GEO     │    │ GEOMETRY │    │  TAG &   │
│          │    │  TAG     │    │ TRANSFORM│    │ GENERATE │    │ REPLACE  │
│          │    │          │    │          │    │          │    │          │
│ .osm     │    │ Classify │    │ WGS84 →  │    │ Terrain  │    │ Metadata │
│ .osm.pbf │    │ by tags  │    │ ENU/UTM  │    │ Roads    │    │ component│
│ .tif DEM │    │ Resolve  │    │ → UE XYZ │    │ Buildings│    │ Asset    │
│          │    │ polygons │    │          │    │ Water    │    │ matching │
└──────────┘    └──────────┘    └──────────┘    └──────────┘    └──────────┘
     │                │                │                │                │
     ▼                ▼                ▼                ▼                ▼
  FOSMRawData    FOSMFeatureTable  FOSMFeatureTable  AActor instances  AActor instances
  (nodes/ways/   (typed features   (features now     in the level     with metadata +
   relations)     with tags)        in UE coords)    (procedural)     authored meshes
```

### Pipeline Contract

Each stage has:
- **Input type** — strongly typed C++ struct/class
- **Output type** — the next stage's input
- **Progress callback** — `TFunction<void(float Percent, const FText& Status)>`
- **Cancellation token** — `FThreadSafeBool* bCancelRequested`
- **Error accumulator** — `TArray<FOSMImportWarning>& OutWarnings`

---

## 7. Coordinate Reference System — Deep Dive

> [!CAUTION]
> **This is the #1 source of bugs in GIS-to-game-engine pipelines.** The entire plugin's accuracy depends on a single, shared, rigorously tested CRS transform path.

### 7.1 The Problem

OSM stores coordinates as **WGS84 lat/lon** (EPSG:4326). Unreal uses a **flat Cartesian XYZ** system in centimeters. You cannot naively treat lat/lon as X/Y without introducing:
- **Scale distortion** — 1° longitude ≈ 111 km at the equator but ≈ 0 km at the poles
- **Shape distortion** — rectangles in lat/lon become trapezoids in reality at higher latitudes
- **Elevation misalignment** — WGS84 ellipsoidal height ≠ mean sea level ≠ DEM orthometric height

### 7.2 Design Decision: Dual-Mode Projection

```cpp
UENUM(BlueprintType)
enum class EOSMProjectionMode : uint8
{
    /** Local tangent plane (East-North-Up). Best for areas < ~10 km². Fast, simple. */
    ENU         UMETA(DisplayName = "Local Tangent Plane (ENU)"),

    /** Universal Transverse Mercator. Best for areas 10-500 km². Requires PROJ/GDAL. */
    UTM         UMETA(DisplayName = "UTM (Auto-detect zone)"),

    /** Cesium-compatible ECEF → UE transform. For integration with Cesium for Unreal. */
    CesiumECEF  UMETA(DisplayName = "Cesium ECEF (requires Cesium plugin)")
};
```

### 7.3 Geographic Origin

```cpp
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMGeoOrigin
{
    GENERATED_BODY()

    /** WGS84 latitude in degrees (-90 to 90) */
    UPROPERTY(EditAnywhere, meta = (ClampMin = "-90", ClampMax = "90"))
    double Latitude = 0.0;

    /** WGS84 longitude in degrees (-180 to 180) */
    UPROPERTY(EditAnywhere, meta = (ClampMin = "-180", ClampMax = "180"))
    double Longitude = 0.0;

    /** WGS84 ellipsoidal height in meters */
    UPROPERTY(EditAnywhere)
    double HeightMeters = 0.0;

    /** If true, auto-compute origin as the centroid of the imported OSM data bounds */
    UPROPERTY(EditAnywhere)
    bool bAutoDetect = true;
};
```

### 7.4 ENU Transform — Mathematical Specification

For the **ENU (East-North-Up)** local tangent plane, the transform from geodetic `(lat, lon, h)` to local `(E, N, U)` relative to origin `(lat₀, lon₀, h₀)` is:

**Step 1: Geodetic → ECEF**

```
N(φ) = a / sqrt(1 - e² sin²(φ))

X = (N(φ) + h) cos(φ) cos(λ)
Y = (N(φ) + h) cos(φ) sin(λ)
Z = (N(φ)(1 - e²) + h) sin(φ)

where:
  a = 6378137.0             (WGS84 semi-major axis, meters)
  b = 6356752.314245        (semi-minor axis)
  e² = 1 - (b/a)²          (first eccentricity squared)
  φ = latitude in radians
  λ = longitude in radians
```

**Step 2: ECEF → ENU**

```
ΔX = X - X₀,  ΔY = Y - Y₀,  ΔZ = Z - Z₀

┌ E ┐   ┌ -sin(λ₀)          cos(λ₀)           0       ┐ ┌ ΔX ┐
│ N │ = │ -sin(φ₀)cos(λ₀)  -sin(φ₀)sin(λ₀)   cos(φ₀) │ │ ΔY │
└ U ┘   └  cos(φ₀)cos(λ₀)   cos(φ₀)sin(λ₀)   sin(φ₀) ┘ └ ΔZ ┘
```

**Step 3: ENU → Unreal**

```
UE_X =  E * 100.0    (East  → Unreal X, meters to centimeters)
UE_Y =  N * 100.0    (North → Unreal Y)
UE_Z =  U * 100.0    (Up    → Unreal Z)
```

> [!IMPORTANT]
> UE uses a **left-handed** coordinate system (X=forward, Y=right, Z=up) but for top-down GIS data, mapping E→X, N→Y, U→Z is the most intuitive. Verify axis alignment against the engine's world directions and adjust if your convention differs.

### 7.5 CRS Transformer API

```cpp
UCLASS()
class OSMWORLDGENCORE_API UOSMCRSTransformer : public UObject
{
    GENERATED_BODY()

public:
    /** Initialize with origin and projection mode. Must be called once before any transforms. */
    void Initialize(const FOSMGeoOrigin& Origin, EOSMProjectionMode Mode);

    /** Transform a WGS84 coordinate to Unreal world space (centimeters). Thread-safe. */
    FVector TransformToUnreal(double Latitude, double Longitude, double HeightMeters) const;

    /** Batch transform — avoids per-call overhead. Thread-safe. */
    void BatchTransformToUnreal(
        const TArrayView<const FVector2D>& LatLonPairs,
        double DefaultHeight,
        TArray<FVector>& OutPositions) const;

    /** Inverse: Unreal world → WGS84. Useful for debugging/display. */
    void TransformToWGS84(const FVector& UnrealPos,
                          double& OutLat, double& OutLon, double& OutHeight) const;

    /** Get the UTM zone string if using UTM mode (e.g., "32N"). */
    FString GetUTMZone() const;

private:
    FOSMGeoOrigin CachedOrigin;
    EOSMProjectionMode ProjectionMode;

    // Pre-computed ECEF origin and rotation matrix for ENU
    FVector ECEFOrigin;           // double-precision
    FMatrix44d ENURotationMatrix; // 3x3 rotation stored in 4x4

    // WGS84 ellipsoid constants
    static constexpr double WGS84_A = 6378137.0;
    static constexpr double WGS84_B = 6356752.314245;
    static constexpr double WGS84_E2 = 1.0 - (WGS84_B * WGS84_B) / (WGS84_A * WGS84_A);
    static constexpr double MetersToUE = 100.0; // 1 meter = 100 UE units (cm)
};
```

### 7.6 Critical Invariant

> **Every generator — terrain, roads, buildings, water, landuse — MUST use the same `UOSMCRSTransformer` instance.** This is enforced by passing it through `FOSMGenerationContext` and never allowing generators to construct their own transform.

```cpp
USTRUCT()
struct FOSMGenerationContext
{
    GENERATED_BODY()

    /** THE shared CRS transformer. All generators use this. No exceptions. */
    UPROPERTY()
    TObjectPtr<UOSMCRSTransformer> CRSTransformer;

    /** The world to spawn into */
    UPROPERTY()
    TObjectPtr<UWorld> TargetWorld;

    /** Progress/cancel */
    TFunction<void(float, const FText&)> OnProgress;
    FThreadSafeBool* bCancelRequested = nullptr;

    /** Warning accumulator */
    TArray<FOSMImportWarning>* Warnings = nullptr;

    /** The full feature table (read-only after Stage 2) */
    const FOSMFeatureTable* FeatureTable = nullptr;
};
```

---

## 8. Core C++ Class Hierarchy

### 8.1 Feature Model

```
FOSMNode
├── int64 Id
├── double Latitude
├── double Longitude
└── TMap<FString, FString> Tags

FOSMWay
├── int64 Id
├── TArray<int64> NodeRefs
├── TArray<FVector2D> ResolvedCoords  (lat/lon, populated during parse)
├── TMap<FString, FString> Tags
└── bool bIsClosed                    (first node == last node)

FOSMRelation
├── int64 Id
├── TArray<FOSMRelationMember> Members  (role + type + ref)
├── TMap<FString, FString> Tags
└── EOSMRelationType ResolvedType       (multipolygon, route, boundary, etc.)

FOSMFeature
├── int64 OSMId
├── EOSMFeatureType Type                (Building, Highway, Waterway, etc.)
├── FString SubType                     (e.g., "residential", "motorway", "river")
├── TArray<TArray<FVector>> Polygons    (outer ring + inner rings for holes)
├── TArray<FVector> Polyline            (for linear features like roads)
├── TMap<FString, FString> Tags         (complete raw OSM tag dictionary)
├── FOSMComputedProperties Computed     (height, width, area, centroid, etc.)
└── FBox BoundingBox                    (in UE space, computed after Stage 3)
```

### 8.2 Feature Type Enum

```cpp
UENUM(BlueprintType)
enum class EOSMFeatureType : uint8
{
    Unknown,
    Building,
    Highway,
    Waterway,
    WaterArea,
    Landuse,
    NaturalArea,
    Railway,
    Amenity,
    Barrier,
    Boundary,
    Leisure,
    Power,
    // Extensible — add new types without breaking existing code
};
```

### 8.3 Generator Base Class

```cpp
UCLASS(Abstract)
class OSMWORLDGENCORE_API UOSMGeneratorBase : public UObject
{
    GENERATED_BODY()

public:
    /**
     * Generate actors for all features matching this generator's accepted types.
     *
     * @param Context     Shared context with CRS transformer, world ref, progress callback
     * @param Features    The subset of features this generator should handle
     * @param OutActors   Generated actors, appended to this array
     * @return            True if generation completed (even with warnings); false if cancelled/fatal
     */
    virtual bool Generate(
        const FOSMGenerationContext& Context,
        const TArray<const FOSMFeature*>& Features,
        TArray<AActor*>& OutActors) PURE_VIRTUAL(, return false;);

    /** Which feature types this generator handles. Used for dispatch. */
    virtual TArray<EOSMFeatureType> GetAcceptedFeatureTypes() const PURE_VIRTUAL(, return {};);

    /** Human-readable name for the progress UI. */
    virtual FText GetDisplayName() const PURE_VIRTUAL(, return FText::GetEmpty(););

    /** Estimated time weight relative to other generators (for progress bar accuracy). */
    virtual float GetEstimatedWeight() const { return 1.0f; }
};
```

### 8.4 Metadata Component

```cpp
UCLASS(ClassGroup=(OSMWorldGen), meta=(BlueprintSpawnableComponent))
class OSMWORLDGENCORE_API UOSMMetadataComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    /** Original OpenStreetMap element ID */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    int64 OSMId = 0;

    /** Classified feature type */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    EOSMFeatureType FeatureType = EOSMFeatureType::Unknown;

    /** Sub-type string (e.g., "residential", "motorway") */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    FString SubType;

    /** Complete original OSM tag dictionary */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    TMap<FString, FString> Tags;

    /** Footprint centroid in WGS84 (lat, lon) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Spatial")
    FVector2D CentroidLatLon = FVector2D::ZeroVector;

    /** Footprint area in square meters */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Spatial")
    float FootprintAreaSqm = 0.0f;

    /** Whether this actor has been replaced with an authored asset */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Replacement")
    bool bIsReplacedAsset = false;

    /** The rule that matched this actor for replacement (empty if procedural) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Replacement")
    FString MatchedReplacementRule;
};
```

---

## 9. Stage 1 — Import Subsystem

### 9.1 Design

```cpp
class OSMWORLDGENCORE_API FOSMParser
{
public:
    /** 
     * Parse an OSM file (XML or PBF, auto-detected by extension/magic bytes).
     * Streams the file — does NOT load entire file into memory.
     * Thread-safe: can be called from a background thread.
     */
    static bool Parse(
        const FString& FilePath,
        FOSMParseResult& OutResult,
        TFunction<void(float)> OnProgress = nullptr,
        FThreadSafeBool* bCancel = nullptr);

private:
    /** Handler wrapping libosmium for streaming PBF/XML */
    class FLibosmiumHandler;

    /** Fallback: minimal custom XML parser using expat (if libosmium is disabled) */
    class FExpatXMLHandler;
};
```

### 9.2 libosmium Integration

```cpp
// Private/Parsing/FOSMParser.cpp

#include <osmium/io/any_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
#include <osmium/area/multipolygon_manager.hpp>
#include <osmium/area/assembler.hpp>

class FOSMParser::FLibosmiumHandler : public osmium::handler::Handler
{
public:
    FOSMParseResult* Result;
    FThreadSafeBool* bCancel;
    int64 TotalEntities;
    int64 ProcessedEntities;

    void node(const osmium::Node& node)
    {
        if (bCancel && *bCancel) return;

        FOSMNode ParsedNode;
        ParsedNode.Id = node.id();
        ParsedNode.Latitude = node.location().lat();
        ParsedNode.Longitude = node.location().lon();

        for (const auto& tag : node.tags())
        {
            ParsedNode.Tags.Add(
                FString(UTF8_TO_TCHAR(tag.key())),
                FString(UTF8_TO_TCHAR(tag.value()))
            );
        }

        Result->Nodes.Add(ParsedNode.Id, MoveTemp(ParsedNode));
        ProcessedEntities++;
    }

    void way(const osmium::Way& way)
    {
        if (bCancel && *bCancel) return;

        FOSMWay ParsedWay;
        ParsedWay.Id = way.id();

        for (const auto& nr : way.nodes())
        {
            ParsedWay.NodeRefs.Add(nr.ref());
        }

        ParsedWay.bIsClosed = way.is_closed();

        for (const auto& tag : way.tags())
        {
            ParsedWay.Tags.Add(
                FString(UTF8_TO_TCHAR(tag.key())),
                FString(UTF8_TO_TCHAR(tag.value()))
            );
        }

        Result->Ways.Add(ParsedWay.Id, MoveTemp(ParsedWay));
        ProcessedEntities++;
    }

    void relation(const osmium::Relation& relation)
    {
        if (bCancel && *bCancel) return;

        FOSMRelation ParsedRelation;
        ParsedRelation.Id = relation.id();

        for (const auto& member : relation.members())
        {
            FOSMRelationMember m;
            m.Type = CharToMemberType(member.type());
            m.Ref = member.ref();
            m.Role = FString(UTF8_TO_TCHAR(member.role()));
            ParsedRelation.Members.Add(MoveTemp(m));
        }

        for (const auto& tag : relation.tags())
        {
            ParsedRelation.Tags.Add(
                FString(UTF8_TO_TCHAR(tag.key())),
                FString(UTF8_TO_TCHAR(tag.value()))
            );
        }

        Result->Relations.Add(ParsedRelation.Id, MoveTemp(ParsedRelation));
        ProcessedEntities++;
    }
};
```

### 9.3 Memory Management for Large Files

- libosmium streams PBF blocks, decompresses each with zlib, and delivers entities via callbacks — **never holds the full file in memory**
- For a city-scale PBF (e.g., Greater London ≈ 1 GB), peak memory is dominated by the node location index (~300 MB for 50M nodes as a flat array lookup)
- Use `osmium::index::map::SparseMemArray` for memory-constrained systems, or `osmium::index::map::FlexMem` for best performance
- The `FOSMParseResult` stores nodes in a `TMap<int64, FOSMNode>` — for very large datasets, consider switching to `TSortedMap` or a flat array sorted by ID for cache coherence

---

## 10. Stage 2 — Parse & Tag Subsystem

### 10.1 Tag Classification Engine

```cpp
class OSMWORLDGENCORE_API FOSMTagClassifier
{
public:
    /**
     * Classify a way or relation into a typed feature based on its OSM tags.
     * Returns EOSMFeatureType::Unknown if no classification rule matches.
     */
    static EOSMFeatureType Classify(
        const TMap<FString, FString>& Tags,
        FString& OutSubType);

    /**
     * Resolve computed properties (height, width, lane count, etc.) from tags.
     */
    static FOSMComputedProperties ResolveProperties(
        EOSMFeatureType Type,
        const FString& SubType,
        const TMap<FString, FString>& Tags);
};
```

### 10.2 Classification Rules (Priority-Ordered)

```
┌──────────────┬──────────────────────────────────────────────────┬─────────────────┐
│ Feature Type │ Tag Match (checked in order)                     │ SubType Source  │
├──────────────┼──────────────────────────────────────────────────┼─────────────────┤
│ Building     │ building=*                                       │ building value  │
│ Highway      │ highway=* (excluding highway=proposed)           │ highway value   │
│ Railway      │ railway=rail|subway|tram|light_rail              │ railway value   │
│ Waterway     │ waterway=river|stream|canal|drain (linear)      │ waterway value  │
│ WaterArea    │ natural=water OR waterway=riverbank              │ water=* or ww   │
│ NaturalArea  │ natural=wood|scrub|sand|beach|wetland|grassland │ natural value   │
│ Landuse      │ landuse=* (residential|commercial|industrial…)  │ landuse value   │
│ Leisure      │ leisure=park|garden|playground|pitch             │ leisure value   │
│ Amenity      │ amenity=* (point features → future)             │ amenity value   │
│ Barrier      │ barrier=fence|wall|hedge (linear)               │ barrier value   │
│ Power        │ power=line|tower                                 │ power value     │
│ Unknown      │ No match                                         │ ""              │
└──────────────┴──────────────────────────────────────────────────┴─────────────────┘
```

### 10.3 Height Resolution Strategy

For **buildings**, the height is resolved in this priority order:

| Priority | Tag Checked | Conversion | Example |
|----------|-------------|------------|---------|
| 1 | `height=<meters>` | Direct parse as float | `height=12.5` → 12.5m |
| 2 | `building:height=<meters>` | Direct parse as float | `building:height=15` → 15m |
| 3 | `building:levels=<n>` | `n × DefaultFloorHeight` (default 3.0m) | `building:levels=4` → 12m |
| 4 | `levels=<n>` | Same as above | `levels=3` → 9m |
| 5 | SubType-based default | Lookup from config table | `building=house` → 8m |
| 6 | Global fallback | `DefaultBuildingHeight` setting | → 9m |

For **roads**, the width is resolved:

| Priority | Tag Checked | Conversion |
|----------|-------------|------------|
| 1 | `width=<meters>` | Direct parse |
| 2 | `lanes=<n>` | `n × DefaultLaneWidth` (3.5m) |
| 3 | Highway subtype default | Lookup: motorway=14m, primary=10m, residential=6m, footway=2m |

### 10.4 Multipolygon Resolution

OSM represents complex polygons (buildings with courtyards, lakes with islands) as `type=multipolygon` relations. The classification pipeline must:

1. Identify relations with `type=multipolygon`
2. Collect all member ways with `role=outer` and `role=inner`
3. Assemble closed rings from potentially fragmented ways (ways that share endpoints)
4. Resolve winding order (outer = CCW, inner = CW in geographic coords)
5. Tag the assembled area with the relation's tags (relation tags override member tags in OSM convention)

Use libosmium's built-in `MultipolygonManager` for this — it handles the ring assembly and winding order correction:

```cpp
// Use osmium::area::Assembler for multipolygon resolution
osmium::area::Assembler::config_type assembler_config;
assembler_config.create_empty_areas = false;

osmium::area::MultipolygonManager<osmium::area::Assembler> mp_manager{assembler_config};

// First pass: collect relations
osmium::relations::read_relations(reader, mp_manager);

// Second pass: build areas
osmium::apply(reader, mp_manager.handler([this](const osmium::Area& area) {
    // Process completed multipolygon area
    ProcessArea(area);
}));
```

---

## 11. Stage 3 — Geographic Transform Subsystem

### 11.1 Transform Pipeline

```
  For each feature in FOSMFeatureTable:
  ┌─────────────────────────────────────────────────────────────────┐
  │                                                                 │
  │  1. Read raw lat/lon from feature polygons/polylines            │
  │                                                                 │
  │  2. If DEM is loaded:                                           │
  │     → Sample DEM elevation at each lat/lon                      │
  │     → Correct for geoid-to-ellipsoid offset if needed           │
  │                                                                 │
  │  3. Call CRSTransformer.TransformToUnreal(lat, lon, height)     │
  │     → Uses the SINGLE shared transformer instance               │
  │     → Returns FVector in UE centimeters                         │
  │                                                                 │
  │  4. Update feature's polygon/polyline arrays in-place           │
  │     → Now stored as UE world coordinates                        │
  │                                                                 │
  │  5. Compute UE-space bounding box for each feature              │
  │                                                                 │
  └─────────────────────────────────────────────────────────────────┘
```

### 11.2 DEM Elevation Sampling

```cpp
class OSMWORLDGENCORE_API FOSMDEMSampler
{
public:
    /** Load a GeoTIFF DEM file. Returns false if format unsupported or file invalid. */
    bool LoadFromGeoTIFF(const FString& FilePath);

    /**
     * Sample elevation at a given WGS84 lat/lon.
     * Uses bilinear interpolation between the 4 nearest DEM pixels.
     * Returns NaN if the coordinate is outside the DEM extent.
     */
    double SampleElevation(double Latitude, double Longitude) const;

    /** Check if a coordinate falls within the loaded DEM bounds */
    bool ContainsCoordinate(double Latitude, double Longitude) const;

    /** Get the DEM resolution in arc-seconds */
    double GetResolutionArcSeconds() const;

private:
    TArray<float> HeightData;       // Raw raster data (row-major)
    int32 Width = 0;
    int32 Height = 0;
    double GeoTransform[6];         // GDAL-style affine transform
    // [0] = top-left X, [1] = pixel width, [2] = rotation,
    // [3] = top-left Y, [4] = rotation, [5] = pixel height (negative for north-up)
    FString SourceCRS;              // e.g., "EPSG:4326"
    double NoDataValue = -9999.0;
};
```

### 11.3 Geoid Correction

> [!WARNING]
> SRTM elevations are orthometric (referenced to EGM96 geoid). If your CRS transform expects WGS84 ellipsoidal heights, you must apply a geoid separation correction. For most game-dev use cases, the ~±100m offset is visually indistinguishable if applied consistently. But if you're integrating with Cesium (which uses ellipsoidal heights), you MUST correct.

```
Ellipsoidal Height = Orthometric Height (SRTM) + Geoid Separation (N)

For a quick approximation, use a lookup table of N values per 1°×1° grid cell.
For precision, bundle the EGM96 grid file (~2 MB) and interpolate.
```

---

## 12. Stage 4 — Geometry Generation Subsystem

### 12.1 Terrain Generator

```cpp
UCLASS()
class OSMWORLDGENGENERATORS_API UOSMTerrainGenerator : public UOSMGeneratorBase
{
    GENERATED_BODY()

public:
    virtual bool Generate(const FOSMGenerationContext& Context,
                          const TArray<const FOSMFeature*>& Features,
                          TArray<AActor*>& OutActors) override;

    virtual TArray<EOSMFeatureType> GetAcceptedFeatureTypes() const override
    {
        return {}; // Terrain doesn't consume OSM features; it consumes DEM data
    }

    virtual FText GetDisplayName() const override
    {
        return NSLOCTEXT("OSM", "TerrainGen", "Terrain (DEM → Landscape)");
    }

    /** Optional DEM file path. If empty, generates flat terrain. */
    UPROPERTY(EditAnywhere, Category = "Terrain")
    FString DEMFilePath;

    /** Landscape section size (UE Landscape concept). 63 or 127 are standard. */
    UPROPERTY(EditAnywhere, Category = "Terrain", meta = (ClampMin = "7", ClampMax = "255"))
    int32 SectionSize = 63;

    /** Number of sections per component. 1 or 2 are standard. */
    UPROPERTY(EditAnywhere, Category = "Terrain", meta = (ClampMin = "1", ClampMax = "2"))
    int32 SectionsPerComponent = 1;

    /** Vertical scale factor applied to elevation values */
    UPROPERTY(EditAnywhere, Category = "Terrain", meta = (ClampMin = "0.1"))
    float VerticalScale = 1.0f;
};
```

**Landscape Heightmap Generation Algorithm:**

```
1. Determine the geographic bounds of the import area
2. Load DEM and clip to these bounds
3. Determine target Landscape resolution:
   - Landscape width/height must be ((SectionSize - 1) * ComponentsX * SectionsPerComponent) + 1
   - Choose ComponentsX/Y to cover the import area at roughly DEM resolution
4. Resample DEM to target resolution using bilinear interpolation
5. Normalize elevation values to uint16 range [0, 65535]:
   - Map MinElevation → 0, MaxElevation → 65535
   - Store the inverse mapping for later Z-coordinate calculations
6. Create ALandscapeProxy via ULandscapeEditorUtils::CreateLandscape()
7. Import heightmap via FLandscapeEditDataInterface
8. Apply landscape transform so that:
   - Landscape world position aligns with CRS origin
   - Landscape XY scale maps heightmap pixels to real-world meters
   - Landscape Z scale maps uint16 range to real-world elevation range
```

### 12.2 Road Generator

```cpp
UCLASS()
class OSMWORLDGENGENERATORS_API UOSMRoadGenerator : public UOSMGeneratorBase
{
    GENERATED_BODY()

public:
    virtual bool Generate(const FOSMGenerationContext& Context,
                          const TArray<const FOSMFeature*>& Features,
                          TArray<AActor*>& OutActors) override;

    virtual TArray<EOSMFeatureType> GetAcceptedFeatureTypes() const override
    {
        return { EOSMFeatureType::Highway };
    }

    /** Data assets defining visual properties per highway type */
    UPROPERTY(EditAnywhere, Category = "Roads")
    TMap<FString, TObjectPtr<UOSMRoadTypeDataAsset>> RoadTypeOverrides;

    /** Whether to snap road vertices to terrain elevation */
    UPROPERTY(EditAnywhere, Category = "Roads")
    bool bSnapToTerrain = true;

    /** Whether to generate intersection geometry at road junctions */
    UPROPERTY(EditAnywhere, Category = "Roads")
    bool bGenerateIntersections = true;
};
```

**Road Type Data Asset:**

```cpp
UCLASS(BlueprintType)
class UOSMRoadTypeDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    /** Display name */
    UPROPERTY(EditAnywhere, Category = "Road Type")
    FText DisplayName;

    /** Total road width in meters */
    UPROPERTY(EditAnywhere, Category = "Road Type", meta = (ClampMin = "1"))
    float WidthMeters = 6.0f;

    /** Material to apply to road surface */
    UPROPERTY(EditAnywhere, Category = "Road Type")
    TObjectPtr<UMaterialInterface> SurfaceMaterial;

    /** Whether this road type has sidewalks */
    UPROPERTY(EditAnywhere, Category = "Road Type")
    bool bHasSidewalks = false;

    /** Sidewalk width in meters (each side) */
    UPROPERTY(EditAnywhere, Category = "Road Type", meta = (EditCondition = "bHasSidewalks"))
    float SidewalkWidth = 1.5f;

    /** UV tiling rate (tiles per meter along road length) */
    UPROPERTY(EditAnywhere, Category = "Road Type")
    float UVTilingPerMeter = 0.5f;

    /** Priority for intersection resolution (higher = on top) */
    UPROPERTY(EditAnywhere, Category = "Road Type")
    int32 IntersectionPriority = 0;
};
```

**Road Generation Algorithm:**

```
For each Highway feature:
  1. Extract the polyline (already in UE coordinates from Stage 3)
  2. If bSnapToTerrain: raycast each vertex down to Landscape surface
  3. Create AActor with USplineComponent
     - Set spline points from polyline vertices
     - Set spline tangents (Catmull-Rom or from OSM node density)
  4. Look up road type: Tags["highway"] → RoadTypeDataAsset
  5. Generate road mesh along spline:
     Option A (PCG): Feed spline into PCG Graph → Static Mesh Spawner
     Option B (Procedural): Generate cross-section mesh along spline with:
       - Vertices at (left edge, left lane, center, right lane, right edge)
       - UV mapping: U = across road, V = along road
       - Triangulate between adjacent cross-sections
  6. If bGenerateIntersections:
     - Detect nodes shared by 3+ ways
     - Generate intersection patch (circle or shaped polygon) at shared node
     - Trim incoming road meshes to intersection edge
  7. Attach UOSMMetadataComponent with OSM tags
```

### 12.3 Building Generator

```cpp
UCLASS()
class OSMWORLDGENGENERATORS_API UOSMBuildingGenerator : public UOSMGeneratorBase
{
    GENERATED_BODY()

public:
    virtual bool Generate(const FOSMGenerationContext& Context,
                          const TArray<const FOSMFeature*>& Features,
                          TArray<AActor*>& OutActors) override;

    virtual TArray<EOSMFeatureType> GetAcceptedFeatureTypes() const override
    {
        return { EOSMFeatureType::Building };
    }

    /** Default floor-to-floor height in meters (used when only level count is known) */
    UPROPERTY(EditAnywhere, Category = "Buildings", meta = (ClampMin = "2.0", ClampMax = "5.0"))
    float DefaultFloorHeight = 3.0f;

    /** Global fallback height if no height/level tags exist */
    UPROPERTY(EditAnywhere, Category = "Buildings", meta = (ClampMin = "3.0"))
    float DefaultBuildingHeight = 9.0f;

    /** Whether to generate LOD meshes for dense areas */
    UPROPERTY(EditAnywhere, Category = "Buildings")
    bool bGenerateLODs = true;

    /** Distance thresholds for LOD levels (in UE units / cm) */
    UPROPERTY(EditAnywhere, Category = "Buildings", meta = (EditCondition = "bGenerateLODs"))
    TArray<float> LODDistances = { 50000.0f, 150000.0f, 500000.0f };

    /** Material for building walls (default) */
    UPROPERTY(EditAnywhere, Category = "Buildings")
    TObjectPtr<UMaterialInterface> DefaultWallMaterial;

    /** Material for building roofs (default) */
    UPROPERTY(EditAnywhere, Category = "Buildings")
    TObjectPtr<UMaterialInterface> DefaultRoofMaterial;
};
```

**Building Extrusion Algorithm (using Geometry Scripting):**

```
For each Building feature:
  1. Extract outer polygon ring (UE coordinates) and any inner rings (holes)
  2. Resolve height:
     a. Check height → building:height → building:levels → levels → subtype default → global
     b. Compute total height in UE cm: heightMeters * 100.0
  3. If terrain exists: sample ground elevation at polygon centroid
  4. Create building mesh via Geometry Scripting:
     a. Create UDynamicMesh
     b. AppendPolygon(outerRing) for the floor face
     c. For each inner ring: subtract (boolean difference) from floor polygon
     d. AppendExtrudePolygon(floor, heightVector) to create walls + roof cap
     e. Optionally: apply different material IDs to walls vs roof faces
  5. Convert UDynamicMesh to UStaticMesh for performance (editor-time bake)
  6. Create AActor with UStaticMeshComponent
  7. Position at ground level (centroid Z + terrain elevation)
  8. Attach UOSMMetadataComponent
  9. If bGenerateLODs:
     a. LOD0: full mesh
     b. LOD1: simplified (merge coplanar faces, reduce vertex count 50%)
     c. LOD2: box approximation (bounding box extruded to building height)
```

### 12.4 Water Generator

```
For each WaterArea feature:
  1. Extract polygon(s) in UE coordinates
  2. Create flat polygon mesh at water surface elevation:
     - If DEM exists: average elevation of polygon vertices
     - Otherwise: at Z=0 (ground level)
  3. Apply water material (translucent, animated UV)
  4. Optionally integrate with UE 5.8's Water plugin (WaterBodyCustom)
  5. Attach UOSMMetadataComponent

For each Waterway (linear) feature:
  1. Extract polyline in UE coordinates
  2. Create spline-based water strip (similar to road generation but with water material)
  3. Width from tags or default (river=20m, stream=3m, canal=8m)
```

### 12.5 Landuse / Natural Area Generator

```
For each Landuse or NaturalArea feature:
  1. Extract polygon in UE coordinates
  2. Create ground-plane polygon mesh (or Landscape layer paint data)
  3. Apply material based on type:
     - residential → muted gray/beige
     - commercial → light concrete
     - forest/wood → dark green
     - farmland → light green/brown
     - industrial → dark gray
  4. Attach UOSMMetadataComponent
  5. These features are primarily for visual context — low geometry priority
```

---

## 13. Stage 5 — Tagging, Registration & Asset Replacement

### 13.1 Metadata Registration

Every generated actor automatically receives a `UOSMMetadataComponent` (see §8.4). Additionally, all actors are registered in a central **`UOSMWorldRegistry`** actor placed in the level:

```cpp
UCLASS()
class OSMWORLDGENCORE_API AOSMWorldRegistry : public AActor
{
    GENERATED_BODY()

public:
    /** The geographic origin used for this import */
    UPROPERTY(VisibleAnywhere, Category = "OSM")
    FOSMGeoOrigin GeoOrigin;

    /** The projection mode used */
    UPROPERTY(VisibleAnywhere, Category = "OSM")
    EOSMProjectionMode ProjectionMode;

    /** Timestamp of the import */
    UPROPERTY(VisibleAnywhere, Category = "OSM")
    FDateTime ImportTimestamp;

    /** Source file path */
    UPROPERTY(VisibleAnywhere, Category = "OSM")
    FString SourceFilePath;

    /** Quick lookup: find all actors by feature type */
    UFUNCTION(BlueprintCallable, Category = "OSM")
    TArray<AActor*> GetActorsByFeatureType(EOSMFeatureType Type) const;

    /** Quick lookup: find actor by OSM ID */
    UFUNCTION(BlueprintCallable, Category = "OSM")
    AActor* GetActorByOSMId(int64 OSMId) const;

    /** Query actors by tag value */
    UFUNCTION(BlueprintCallable, Category = "OSM")
    TArray<AActor*> QueryByTag(const FString& Key, const FString& Value) const;

private:
    /** Internal index: OSM ID → weak actor pointer */
    TMap<int64, TWeakObjectPtr<AActor>> OSMIdIndex;

    /** Internal index: feature type → array of weak actor pointers */
    TMap<EOSMFeatureType, TArray<TWeakObjectPtr<AActor>>> FeatureTypeIndex;
};
```

### 13.2 Asset Replacement System

```cpp
USTRUCT(BlueprintType)
struct FOSMReplacementRule
{
    GENERATED_BODY()

    /** Human-readable rule name */
    UPROPERTY(EditAnywhere)
    FString RuleName;

    /** Feature type to match */
    UPROPERTY(EditAnywhere)
    EOSMFeatureType MatchFeatureType = EOSMFeatureType::Building;

    /** Sub-type to match (empty = any) */
    UPROPERTY(EditAnywhere)
    FString MatchSubType;

    /** Footprint area range in m² (0 = no minimum/maximum) */
    UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
    float MinFootprintArea = 0.0f;

    UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
    float MaxFootprintArea = 0.0f;

    /** Height range in meters (0 = no minimum/maximum) */
    UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
    float MinHeight = 0.0f;

    UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
    float MaxHeight = 0.0f;

    /** Required tag key-value pairs (all must match) */
    UPROPERTY(EditAnywhere)
    TMap<FString, FString> RequiredTags;

    /** The replacement asset (Static Mesh or Blueprint class) */
    UPROPERTY(EditAnywhere)
    TSoftObjectPtr<UObject> ReplacementAsset;

    /** How to scale the replacement to fit the footprint */
    UPROPERTY(EditAnywhere)
    EOSMReplacementScaleMode ScaleMode = EOSMReplacementScaleMode::FitFootprint;

    /** Priority — higher priority rules are evaluated first */
    UPROPERTY(EditAnywhere)
    int32 Priority = 0;
};

UENUM(BlueprintType)
enum class EOSMReplacementScaleMode : uint8
{
    /** Scale uniformly to fit within the footprint bounding box */
    FitFootprint,

    /** Scale to match footprint width, maintain aspect ratio */
    MatchWidth,

    /** Use asset's native scale, only adjust position/rotation */
    NativeScale,

    /** Scale XY to exactly match footprint, Z to match height */
    ExactMatch
};
```

**Replacement Pipeline:**

```
1. Collect all replacement rules, sorted by Priority (descending)
2. For each generated actor with UOSMMetadataComponent:
   a. Iterate rules until a match is found:
      - Check FeatureType match
      - Check SubType match (if specified)
      - Check footprint area in range (if specified)
      - Check height in range (if specified)
      - Check all RequiredTags present
   b. If matched:
      - Load ReplacementAsset via async soft pointer resolve
      - Import via Interchange if it's an FBX/glTF file reference
      - Calculate transform:
        * Position = actor's current position (preserve CRS-accurate placement)
        * Rotation = align to footprint orientation (longest edge → forward)
        * Scale = compute based on ScaleMode
      - Replace mesh component (or spawn Blueprint instance)
      - Set bIsReplacedAsset = true on metadata component
      - Set MatchedReplacementRule on metadata component
   c. If no match: keep procedural placeholder
3. Log summary: X actors replaced, Y skipped, Z warnings
```

---

## 14. Editor UX — Import Wizard

### 14.1 Wizard Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                     OSM Import Wizard                           │
│                                                                 │
│  Step 1: File Selection                                         │
│  ┌─────────────────────────────────────────────────────┐       │
│  │  [📂 Browse .osm / .osm.pbf]     path/to/file.pbf  │       │
│  │  [📂 Browse DEM .tif (optional)] path/to/dem.tif    │       │
│  │                                                     │       │
│  │  File info: 245 MB · PBF format · ~2.1M nodes       │       │
│  │  DEM info:  SRTM 1-arcsec · 3601×3601              │       │
│  └─────────────────────────────────────────────────────┘       │
│                                                                 │
│  Step 2: CRS Configuration                                      │
│  ┌─────────────────────────────────────────────────────┐       │
│  │  Geographic Origin:                                  │       │
│  │    ○ Auto-detect (centroid of data)                  │       │
│  │    ○ Manual: Lat [51.5074]  Lon [-0.1278]           │       │
│  │                                                     │       │
│  │  Projection Mode:                                    │       │
│  │    ○ Local Tangent Plane (ENU) — recommended < 10km │       │
│  │    ○ UTM (auto-detect zone)                          │       │
│  │    ○ Cesium ECEF (requires Cesium plugin)            │       │
│  │                                                     │       │
│  │  ⚠ Data extent: ~15 km. Consider UTM for accuracy.  │       │
│  └─────────────────────────────────────────────────────┘       │
│                                                                 │
│  Step 3: Feature Filtering                                      │
│  ┌─────────────────────────────────────────────────────┐       │
│  │  ☑ Buildings (14,328 features)                       │       │
│  │  ☑ Roads (8,921 features)                            │       │
│  │  ☐ Railways (342 features)                           │       │
│  │  ☑ Water areas (89 features)                         │       │
│  │  ☑ Waterways (156 features)                          │       │
│  │  ☑ Land use (1,204 features)                         │       │
│  │  ☐ Natural areas (567 features)                      │       │
│  │  ☐ Barriers (2,100 features) — usually not needed   │       │
│  └─────────────────────────────────────────────────────┘       │
│                                                                 │
│  Step 4: Generation Settings                                    │
│  ┌─────────────────────────────────────────────────────┐       │
│  │  Terrain:                                            │       │
│  │    Landscape section size: [63 ▼]                    │       │
│  │    Vertical exaggeration: [1.0x]                     │       │
│  │                                                     │       │
│  │  Buildings:                                          │       │
│  │    Default height: [9.0 m]                           │       │
│  │    Default floor height: [3.0 m]                     │       │
│  │    Generate LODs: [☑]                                │       │
│  │                                                     │       │
│  │  Roads:                                              │       │
│  │    Snap to terrain: [☑]                              │       │
│  │    Generate intersections: [☑]                       │       │
│  │                                                     │       │
│  │  World Partition:                                    │       │
│  │    Enable streaming: [☑]                             │       │
│  │    Grid cell size: [25600 cm ▼] (256m)               │       │
│  └─────────────────────────────────────────────────────┘       │
│                                                                 │
│  Step 5: Review & Generate                                      │
│  ┌─────────────────────────────────────────────────────┐       │
│  │  Summary:                                            │       │
│  │    Source: greater_london.osm.pbf (245 MB)           │       │
│  │    Origin: 51.5074°N, 0.1278°W                      │       │
│  │    Projection: UTM 30N                               │       │
│  │    Features: 24,698 total (14,328 buildings, ...)    │       │
│  │    Estimated generation time: ~8 minutes             │       │
│  │                                                     │       │
│  │  ⚠ Warnings:                                        │       │
│  │    • 1,203 buildings have no height data (fallback)  │       │
│  │    • DEM does not cover full import extent (NE edge) │       │
│  │                                                     │       │
│  │  [◀ Back]            [❌ Cancel]      [✅ Generate]  │       │
│  └─────────────────────────────────────────────────────┘       │
│                                                                 │
│  Progress (during generation):                                  │
│  ┌─────────────────────────────────────────────────────┐       │
│  │  [████████████░░░░░░░░░░░░░░] 47%                   │       │
│  │  Stage: Building Generation (7,892 / 14,328)        │       │
│  │  Elapsed: 3m 42s · Remaining: ~4m 10s               │       │
│  │                                                     │       │
│  │  [❌ Cancel Generation]                              │       │
│  └─────────────────────────────────────────────────────┘       │
└─────────────────────────────────────────────────────────────────┘
```

### 14.2 Implementation

The wizard is implemented as a **Slate compound widget** (`SOSMImportWizard`) with a `SWizard`-style tab/step navigation. Each step is a separate `SCompoundWidget`:

- **`SOSMStepFileSelect`** — file browser, file validation, size/format detection
- **`SOSMStepCRSConfig`** — projection mode picker, origin config, extent warnings
- **`SOSMStepFeatureFilter`** — feature type checkboxes with counts (requires a quick pre-parse)
- **`SOSMStepGeneration`** — per-generator settings (delegates to each generator's UPROPERTY reflection)
- **`SOSMStepReview`** — summary, warnings, generate button

Generation runs on a **background thread** (`FRunnable` or `Async(EAsyncExecution::ThreadPool, ...)`), posting progress updates to the game thread via `AsyncTask(ENamedThreads::GameThread, ...)`. Actor creation must happen on the game thread — the background thread prepares mesh data and queues creation commands.

---

## 15. Performance & Scalability Architecture

### 15.1 Async Generation Pipeline

```
Main Thread (Game Thread)                Background Thread(s)
─────────────────────────                ─────────────────────
                                         ┌─────────────────┐
                                         │ Parse OSM file  │ ← libosmium's thread pool
                                         │ (Stage 1)       │   handles PBF decompression
                                         └────────┬────────┘
                                                  │
                                         ┌────────▼────────┐
                                         │ Classify tags   │ ← single-threaded, fast
                                         │ (Stage 2)       │
                                         └────────┬────────┘
                                                  │
                                         ┌────────▼────────┐
                                         │ CRS transform   │ ← batch transform, SIMD-friendly
                                         │ (Stage 3)       │
                                         └────────┬────────┘
                                                  │
                                         ┌────────▼────────┐
                                         │ Generate mesh   │ ← parallel per feature
                                         │ data (vertices, │   (buildings are embarrassingly
                                         │  triangles, UVs)│   parallel — each is independent)
                                         │ (Stage 4 prep)  │
                                         └────────┬────────┘
                                                  │
  ┌────────────────────────┐             ┌────────▼────────┐
  │ Create UE actors       │◄────────────│ Queue actor     │
  │ (MUST be game thread)  │  batch of   │ creation cmds   │
  │ Set mesh components    │  N actors   │                 │
  │ Attach metadata        │             └─────────────────┘
  └────────────────────────┘
```

### 15.2 Chunking Strategy

For city-scale imports (millions of features), process in spatial chunks:

```
1. Divide the import bounding box into a grid of NxN cells
2. Default cell size: 500m × 500m (configurable)
3. Assign each feature to its cell (by centroid)
4. Process cells sequentially (or in small batches)
5. Each cell = one batch of actor creation on game thread
6. Between cells, yield to game thread (Tick) for UI responsiveness
7. Report progress: cell M of N complete
```

### 15.3 Memory Budget

| Dataset Size | Nodes | Peak Parse Memory | Peak Generation Memory | Recommendation |
|-------------|-------|-------------------|----------------------|----------------|
| Small (< 10 MB) | < 100K | < 200 MB | < 500 MB | Load-all OK |
| Medium (10-100 MB) | 100K-1M | 200-800 MB | 500 MB - 2 GB | Streaming parse |
| Large (100 MB - 1 GB) | 1M-10M | 800 MB - 4 GB | 2-8 GB | Streaming + chunked gen |
| City-scale (> 1 GB) | > 10M | > 4 GB | > 8 GB | Mandatory streaming + spatial chunking |

### 15.4 LOD Strategy for Dense Areas

```
Building density (per 100m²):
  < 5 buildings  → LOD0 for all (full detail)
  5-20 buildings → LOD0 within 500m, LOD1 within 2km, LOD2 beyond
  > 20 buildings → LOD0 within 200m, LOD1 within 1km, LOD2 within 5km, cull beyond

LOD mesh complexity:
  LOD0: Full extruded mesh with material IDs (walls/roof)
  LOD1: Simplified mesh (50% vertex reduction, merged coplanar faces)
  LOD2: Bounding box approximation (12 vertices per building)
  LOD3: Imposter (billboard card, optional future feature)
```

---

## 16. World Partition Integration

### 16.1 Integration Points

```cpp
// During generation, if World Partition is enabled:

// 1. Ensure the level is WP-enabled (Editor check)
UWorldPartition* WP = World->GetWorldPartition();
if (!WP)
{
    AddWarning("World Partition is not enabled on this level. Large imports may cause performance issues.");
}

// 2. For each generated actor, set World Partition properties:
Actor->SetIsSpatiallyLoaded(true);
Actor->SetDataLayerInstances(GetOSMDataLayers());

// 3. Create OSM-specific data layers for selective loading:
//    - OSM_Terrain
//    - OSM_Roads
//    - OSM_Buildings
//    - OSM_Water
//    - OSM_Landuse

// 4. Grid cell size recommendation:
//    For OSM-generated worlds, 256m (25600 cm) cells work well
//    as they roughly match the density of a city block
```

### 16.2 One File Per Actor (OFPA)

With World Partition, each actor is saved to its own file. This is beneficial for OSM imports:
- Thousands of building actors don't bloat a single `.umap`
- Version control merges are trivial
- Selective loading/unloading by area
- **Caution:** Generating 50,000+ actor files can be slow on disk I/O — batch with `FScopedSlowTask`

---

## 17. Testing & Validation Strategy

### 17.1 Unit Tests

```
Module: OSMWorldGenCore
├── Test_FOSMParser
│   ├── ParseSmallOSMXML_ReturnsCorrectNodeCount
│   ├── ParseSmallPBF_ReturnsCorrectNodeCount
│   ├── ParseEmptyFile_ReturnsEmptyResult
│   ├── ParseCorruptPBF_ReturnsFalse
│   ├── ParseCancellation_StopsEarly
│   └── ParseLargeFile_MemoryStaysUnderBudget
│
├── Test_FOSMTagClassifier
│   ├── BuildingTag_ReturnsBuilding
│   ├── HighwayResidential_ReturnsHighway_Residential
│   ├── NaturalWater_ReturnsWaterArea
│   ├── MultipolygonRelation_ClassifiedByRelationTags
│   ├── NoMatchingTags_ReturnsUnknown
│   └── HeightResolution_PriorityOrder
│
├── Test_UOSMCRSTransformer
│   ├── ENU_KnownCoordinate_London_MatchesExpected        ← ± 0.1m accuracy
│   ├── ENU_KnownCoordinate_Tokyo_MatchesExpected
│   ├── ENU_KnownCoordinate_NewYork_MatchesExpected
│   ├── ENU_OriginMapsToZero
│   ├── ENU_InverseRoundTrip_SubMeterAccuracy
│   ├── UTM_KnownCoordinate_MatchesExpected
│   ├── BatchTransform_MatchesSingleTransform
│   └── LargeOffset_DoublePrecision_NoJitter
│
└── Test_FOSMDEMSampler
    ├── LoadGeoTIFF_ValidFile_Succeeds
    ├── SampleElevation_KnownPoint_MatchesDEM
    ├── SampleElevation_OutOfBounds_ReturnsNaN
    ├── BilinearInterpolation_MatchesManualCalc
    └── NoDataValues_HandledGracefully
```

### 17.2 Integration Tests

```
Test_FullPipelineSmall
├── Import a 500 KB .osm file (known small area with ~100 buildings, ~50 roads)
├── Verify all 5 stages complete without errors
├── Verify building count matches expected
├── Verify road spline count matches expected
├── Spot-check 3 known buildings:
│   ├── Position within 1m of expected real-world coordinate
│   ├── Height matches OSM tag
│   └── Metadata component has correct tags
├── Spot-check 3 known roads:
│   ├── Spline points follow expected route
│   ├── Width matches highway type
│   └── Road snaps to terrain (if terrain present)
└── Verify AOSMWorldRegistry has correct indices
```

### 17.3 Real-World Validation Points

Use known, well-mapped locations with precise survey data:

| Location | Lat/Lon | Known Feature | Validation Check |
|----------|---------|---------------|-----------------|
| Eiffel Tower, Paris | 48.8584, 2.2945 | `building=yes`, `height=330` | Position ±5m, height = 330m |
| Empire State Building, NYC | 40.7484, -73.9857 | `building=yes`, `height=443` | Position ±5m, height = 443m |
| Buckingham Palace, London | 51.5014, -0.1419 | `building=yes`, large footprint | Position ±5m, polygon shape matches |
| Shibuya Crossing, Tokyo | 35.6595, 139.7004 | Highway intersection | Roads intersect at correct point |
| Sydney Opera House | -33.8568, 151.2153 | Southern hemisphere test | Correct hemisphere, no sign flip |

---

## 18. Phased Roadmap

### Phase 1 — Data Foundation (Weeks 1-3)

| Task | Duration | Deliverables |
|------|----------|-------------|
| Plugin scaffolding | 2 days | `.uplugin`, all modules, Build.cs, empty classes |
| libosmium + protozero integration | 3 days | ThirdParty folder, headers compiling, basic parse test |
| `FOSMParser` implementation | 4 days | Parse .osm and .pbf, streaming, cancel support |
| Feature data model | 2 days | All structs: Node, Way, Relation, Feature, FeatureTable |
| `FOSMTagClassifier` | 3 days | Full classification rules, height/width resolution |
| `UOSMCRSTransformer` (ENU mode) | 4 days | ENU transform, batch transform, inverse, unit tests |
| CRS validation tests | 2 days | Known-coordinate tests for 5+ world locations |
| `UOSMMetadataComponent` | 1 day | Blueprint-accessible component |

**Phase 1 Exit Criteria:** Can parse a real-world `.osm.pbf`, classify all features, transform coordinates to UE space, and verify accuracy against known survey points.

---

### Phase 2 — Terrain (Weeks 4-5)

| Task | Duration | Deliverables |
|------|----------|-------------|
| `FOSMDEMSampler` | 3 days | GeoTIFF loading, bilinear sampling, bounds checking |
| DEM ↔ CRS alignment | 2 days | Ensure DEM pixels map to correct UE coordinates |
| `FOSMHeightmapBuilder` | 3 days | Resample DEM to Landscape-compatible resolution |
| `UOSMTerrainGenerator` | 3 days | Create Landscape actor, import heightmap, apply transform |
| Terrain integration tests | 2 days | Known elevation points match DEM values |
| Flat-terrain fallback | 1 day | Generate flat Landscape when no DEM provided |

**Phase 2 Exit Criteria:** Can import a DEM GeoTIFF and generate a correctly positioned, correctly scaled UE Landscape. Elevation values match the DEM within quantization error.

---

### Phase 3 — Roads (Weeks 6-8)

| Task | Duration | Deliverables |
|------|----------|-------------|
| `UOSMRoadTypeDataAsset` | 1 day | Data asset class, default instances for all highway types |
| Spline generation from polylines | 3 days | Smooth splines from OSM way nodes |
| Road mesh generation (procedural) | 4 days | Cross-section extrusion along splines, UV mapping |
| Terrain snapping | 2 days | Raycast road vertices to Landscape surface |
| Intersection detection | 2 days | Identify 3+ way junctions, generate patch geometry |
| Material assignment by type | 1 day | Motorway, primary, residential, footway materials |
| Road integration tests | 2 days | Known roads match expected position/width |

**Phase 3 Exit Criteria:** All highway types generate as correctly positioned, width-appropriate road meshes. Roads snap to terrain. Basic intersections are handled.

---

### Phase 4 — Buildings (Weeks 9-11)

| Task | Duration | Deliverables |
|------|----------|-------------|
| `FOSMBuildingExtruder` | 4 days | Polygon extrusion via Geometry Scripting, hole support |
| Height resolution pipeline | 2 days | Full priority chain, sane fallbacks |
| Roof cap generation | 2 days | Flat roofs (v1), triangulated top face |
| LOD generation | 3 days | LOD0/1/2 mesh creation, distance thresholds |
| Material assignment | 1 day | Wall/roof material IDs, defaults |
| Building integration tests | 2 days | Known buildings match position/height/footprint |
| Performance profiling (1000+ buildings) | 1 day | Identify bottlenecks, optimize |

**Phase 4 Exit Criteria:** Buildings extrude correctly from footprints with accurate heights. LODs are generated. Can handle 10,000+ buildings in a city area.

---

### Phase 5 — Tagging & Asset Replacement (Weeks 12-14)

| Task | Duration | Deliverables |
|------|----------|-------------|
| `AOSMWorldRegistry` | 2 days | Central registry actor, index queries |
| `FOSMReplacementRule` system | 3 days | Rule definition, matching logic, priority ordering |
| Asset replacement pipeline | 3 days | Mesh swap, transform computation, scale modes |
| Interchange integration | 2 days | FBX/glTF import via Interchange for replacement assets |
| Asset match editor UI | 3 days | Slate editor for defining/editing replacement rules |
| Replacement integration tests | 2 days | Rules match correct actors, meshes swap correctly |

**Phase 5 Exit Criteria:** Users can define replacement rules that match procedural actors by type/size/tags and swap them for authored assets.

---

### Phase 6 — Editor UX & Polish (Weeks 15-16)

| Task | Duration | Deliverables |
|------|----------|-------------|
| `SOSMImportWizard` | 4 days | 5-step wizard with all settings |
| Progress/cancel UI | 2 days | Real-time progress bar, cancel button, elapsed/remaining time |
| Validation warnings | 1 day | Missing DEM, unmapped tags, large file warnings |
| Water/landuse generators | 3 days | Flat polygon generators with materials |
| Plugin settings panel | 1 day | Project Settings integration for defaults |
| User documentation | 2 days | Quick start guide, API reference, FAQ |

**Phase 6 Exit Criteria:** Complete editor workflow from file selection to generated world, with clear progress reporting and actionable warnings.

---

### Phase 7 — Performance & QA (Weeks 17-18+, ongoing)

| Task | Duration | Deliverables |
|------|----------|-------------|
| World Partition integration | 3 days | Data layers, spatial loading, grid config |
| City-scale profiling | 3 days | Profile with London/Tokyo/NYC .pbf extracts |
| Memory optimization | 2 days | Streaming parse tuning, mesh pooling |
| Accuracy QA | 2 days | Validate against 20+ known survey points |
| Edge case hardening | 2 days | Self-intersecting polygons, degenerate ways, corrupt tags |
| UTM projection mode | 2 days | Add UTM support to CRS transformer |

**Phase 7 Exit Criteria:** Can handle a full Greater London .pbf (~1 GB) without OOM, with correct placement of sampled features.

---

## 19. Risk Register

| # | Risk | Probability | Impact | Mitigation |
|---|------|-------------|--------|------------|
| R1 | **Mesh Terrain dependency** — building on experimental API | High | Critical | **Use Landscape (v1).** Abstract terrain behind `UOSMGeneratorBase`; swap backend in v2. |
| R2 | **Data volume OOM** — city PBFs with millions of nodes | High | High | Streaming parse (libosmium default), spatial chunking for generation, async with yield. |
| R3 | **CRS precision drift** — different generators use slightly different transforms | Medium | Critical | **Single `UOSMCRSTransformer` enforced by `FOSMGenerationContext`**. Unit tests for round-trip accuracy. |
| R4 | **OSM data quality** — missing heights, informal road classes, inconsistent tags | High | Medium | Comprehensive fallback chain for every property. Never crash on missing data. Log warnings. |
| R5 | **libosmium UBT integration** — header-only C++ lib may conflict with UE's build system | Medium | High | Isolate behind a compilation firewall (no libosmium headers leak into Public/). Test early on all platforms. |
| R6 | **GDAL dependency weight** — 30+ MB, complex build | Medium | Medium | Make GDAL optional. Implement lightweight GeoTIFF reader fallback for simple DEM loading. |
| R7 | **Landscape size limits** — very large areas exceed max Landscape dimensions | Low | Medium | Auto-tile: create multiple Landscape actors arranged in a grid. Document max supported area. |
| R8 | **Game thread bottleneck** — actor creation must be on game thread | Medium | High | Batch actor creation in groups of 100-500. Yield between batches. Background threads prepare mesh data only. |
| R9 | **Multipolygon assembly** — complex OSM relations with fragmented ways | Medium | Medium | Use libosmium's `MultipolygonManager` (battle-tested). Add fallback: skip unassemblable relations with warning. |
| R10 | **ODbL licensing compliance** — OSM data is ODbL | Low | Medium | Plugin is a tool (no bundled data). Document attribution requirements. Provide template attribution text. |

---

## 20. Licensing & Attribution

### 20.1 Plugin License

The plugin code itself should be licensed under a standard commercial or open-source license (your choice). The plugin **does not bundle any OSM data**.

### 20.2 OSM Data Attribution

OSM data is licensed under the **Open Data Commons Open Database License (ODbL)**. Any use of OSM data requires:

1. **Attribution:** "© OpenStreetMap contributors" must appear in any output that uses OSM data
2. **Share-Alike:** Derivative databases must be released under ODbL
3. **Note:** Rendered outputs (3D worlds, screenshots, videos) are considered "Produced Works" and are NOT subject to share-alike, but DO require attribution

The plugin should:
- Auto-generate an attribution text file alongside the imported world
- Include attribution in the `AOSMWorldRegistry` actor's properties
- Display attribution in the import wizard's review step

### 20.3 Third-Party Licenses

| Library | License | Attribution Required |
|---------|---------|---------------------|
| libosmium | Boost Software License 1.0 | Include license text in distribution |
| protozero | BSD-2-Clause | Include license text |
| zlib | zlib License | Include license text |
| expat | MIT | Include license text |
| GDAL | MIT/X11 | Include license text |
| Cesium for Unreal | Apache-2.0 | Include NOTICE file |

---

## 21. Appendices

### Appendix A — OSM Highway Type Defaults

| Highway Type | Default Width (m) | Lanes | Surface Material | Speed Indicator |
|-------------|-------------------|-------|------------------|-----------------|
| `motorway` | 14.0 | 4 | Dark asphalt | High speed |
| `trunk` | 12.0 | 3-4 | Dark asphalt | High speed |
| `primary` | 10.0 | 2-3 | Asphalt | Medium speed |
| `secondary` | 8.0 | 2 | Asphalt | Medium speed |
| `tertiary` | 7.0 | 2 | Asphalt | Low-medium speed |
| `residential` | 6.0 | 2 | Light asphalt | Low speed |
| `unclassified` | 5.0 | 1-2 | Asphalt/gravel | Low speed |
| `service` | 4.0 | 1 | Light asphalt | Very low speed |
| `living_street` | 5.0 | 1 | Cobblestone | Walking speed |
| `pedestrian` | 4.0 | 0 | Paving stones | Walking |
| `footway` | 2.0 | 0 | Concrete/paving | Walking |
| `cycleway` | 2.5 | 0 | Asphalt (tinted) | Cycling |
| `path` | 1.5 | 0 | Dirt/gravel | Walking |
| `track` | 3.0 | 1 | Gravel/dirt | Low speed |

### Appendix B — Building SubType Height Defaults

| Building SubType | Default Height (m) | Default Levels |
|-----------------|-------------------|----------------|
| `house` | 8 | 2 |
| `detached` | 8 | 2 |
| `residential` | 12 | 4 |
| `apartments` | 18 | 6 |
| `commercial` | 15 | 4 |
| `office` | 30 | 10 |
| `industrial` | 10 | 2 |
| `retail` | 6 | 1 |
| `warehouse` | 8 | 1 |
| `garage` / `garages` | 3 | 1 |
| `shed` | 3 | 1 |
| `church` | 15 | — |
| `cathedral` | 30 | — |
| `school` | 12 | 3 |
| `hospital` | 20 | 5 |
| `hotel` | 24 | 8 |
| `yes` (generic) | 9 | 3 |

### Appendix C — Glossary

| Term | Definition |
|------|-----------|
| **CRS** | Coordinate Reference System — defines how coordinates map to positions on Earth |
| **DEM** | Digital Elevation Model — a raster grid of terrain elevation values |
| **ECEF** | Earth-Centered, Earth-Fixed — a 3D Cartesian frame with origin at Earth's center |
| **ENU** | East-North-Up — a local tangent-plane coordinate system |
| **EPSG:4326** | The CRS code for WGS84 geographic coordinates (lat/lon in degrees) |
| **GeoTIFF** | A TIFF image format with embedded geographic metadata |
| **LWC** | Large World Coordinates — UE5's double-precision coordinate system |
| **ODbL** | Open Data Commons Open Database License — OSM's data license |
| **OSM** | OpenStreetMap — a collaborative open-source geographic database |
| **PBF** | Protocol Buffer Binary Format — OSM's compact binary data format |
| **PCG** | Procedural Content Generation — UE5's framework for rule-based content placement |
| **SRTM** | Shuttle Radar Topography Mission — a near-global DEM dataset |
| **UTM** | Universal Transverse Mercator — a global map projection system with 60 zones |
| **WGS84** | World Geodetic System 1984 — the standard Earth ellipsoid model used by GPS |
| **World Partition** | UE5's automatic level streaming system replacing World Composition |

### Appendix D — Quick Reference: Key File Locations

| File | Purpose |
|------|---------|
| `OSMWorldGen.uplugin` | Plugin descriptor |
| `Config/DefaultOSMWorldGen.ini` | Default settings |
| `Source/OSMWorldGenCore/Public/CRS/FOSMCRSTransformer.h` | THE coordinate transform — the most critical file in the plugin |
| `Source/OSMWorldGenCore/Public/Model/FOSMFeature.h` | Internal feature data model |
| `Source/OSMWorldGenCore/Public/Components/UOSMMetadataComponent.h` | Per-actor OSM metadata |
| `Source/OSMWorldGenGenerators/Public/Buildings/UOSMBuildingGenerator.h` | Building extrusion |
| `Source/OSMWorldGenGenerators/Public/Roads/UOSMRoadGenerator.h` | Road spline generation |
| `Source/OSMWorldGenEditor/Public/Wizard/SOSMImportWizard.h` | Import wizard UI |

---

> **Next Steps:** Review this design document. Once approved, implementation begins with Phase 1 (Data Foundation).

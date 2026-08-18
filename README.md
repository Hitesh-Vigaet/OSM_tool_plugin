# OSM World Generator — Unreal Engine 5.8 Plugin

Georeferenced OpenStreetMap (`.osm` / `.osm.pbf`) world generator plugin for Unreal Engine 5.8.

> [!WARNING]
> **Mac Only (Currently)**: This plugin has been developed and tested exclusively on macOS. If you are building for Windows, you will need to edit the codebase, particularly the build scripts and path handling for third-party libraries (e.g., `libosmium`).

## What is this repo about?
The OSM World Generator is a tool designed to ingest real-world geographic data from OpenStreetMap and automatically generate a validated, inspectable city graph directly within Unreal Engine 5.8. It allows developers to procedurally generate expansive terrains, road networks, and building masses using real-world map data.

## How it works
The plugin works by processing OSM XML or binary PBF data through a robust parsing and classification pipeline. It translates real-world geodetic coordinates (Latitude/Longitude) into Unreal's local coordinate system using a double-precision Geographic Coordinate Reference System (CRS) Transformer.

The architecture is split into 3 core modules:
1. **`OSMWorldGenCore`** (Runtime): Handles data parsing (`libosmium` bridge), classification rules, geographic CRS transformations, and the underlying data model (`FOSMNode`, `FOSMWay`, `FOSMRelation`).
2. **`OSMWorldGenGenerators`** (Editor): Responsible for the procedural generation of landscape heightmaps, spline-based roads via PCG, and building extrusions via Geometry Scripting.
3. **`OSMWorldGenEditor`** (Editor): Provides the user interface, including a 5-step import wizard widget, matching rule editors, and progress tracking.

## How to make it work (Setup)

1. **Clone the Plugin:** 
   Place this repository inside the `Plugins` folder of your Unreal Engine 5.8 project.
   ```bash
   cd YourProject/Plugins/
   git clone <repo_url> OSM_plugin
   ```

2. **Fetch Binary Dependencies (.osm.pbf support):**
   To enable high-speed `.osm.pbf` binary file parsing, you must download the third-party dependencies. Run the included script:
   ```bash
   cd OSM_plugin/ThirdParty
   chmod +x download_thirdparty.sh
   ./download_thirdparty.sh
   ```
   *Unreal Build Tool will automatically detect `ThirdParty/libosmium/include` and compile PBF support (`OSM_WITH_LIBOSMIUM=1`).*

3. **Enable Required Engine Plugins:**
   Ensure the following plugins are enabled in your project's `.uproject` file:
   - `GeometryScripting`
   - `PCG` (Procedural Content Generation Framework)
   - `ProceduralMeshComponent`

4. **Compile and Run:**
   Re-generate your project files and compile the project via Xcode or your preferred IDE. Once the engine loads, the OSM World Generator tools will be available in the Editor.

## Current State
Phase 1 (Data Foundation) is complete, featuring the full C++ feature data model, the parser engine, the tag classifier, geographic CRS transformer, and the abstract generator framework.

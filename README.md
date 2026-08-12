# OSM World Generator — Unreal Engine 5.8 Plugin

Georeferenced OpenStreetMap (`.osm` / `.osm.pbf`) world generator plugin for Unreal Engine 5.8.

## Plugin Structure

The plugin is structured into 3 distinct modules:

1. **`OSMWorldGenCore`** (Runtime Module):
   - **Parsing**: `FOSMParser`, `FOSMXMLParser`, `FOSMPBFParser` (libosmium / PBF bridge)
   - **Data Model**: `FOSMNode`, `FOSMWay`, `FOSMRelation`, `FOSMFeature`, `FOSMFeatureTable`
   - **Classification**: `FOSMTagClassifier`, `FOSMClassificationRules`
   - **CRS / Geodesy**: `UOSMCRSTransformer`, `FOSMGeoOrigin`, `FOSMEllipsoid` (Double-precision ENU & UTM projection)
   - **Metadata Component**: `UOSMMetadataComponent`
   - **Generator Framework**: `UOSMGeneratorBase`, `FOSMGenerationContext`

2. **`OSMWorldGenGenerators`** (Editor Module):
   - Terrain generator (Landscape heightmaps)
   - Road generator (Splines + PCG framework integration)
   - Building generator (Geometry Scripting extrusions)
   - Water / Landuse generators
   - Asset replacement engine (Interchange pipeline)

3. **`OSMWorldGenEditor`** (Editor Module):
   - 5-step import wizard widget
   - Asset-matching rule editor
   - Progress & cancellation UI

## Phase 1 Deliverables Summary

All Phase 1 (Data Foundation) components have been implemented:
- Full C++ feature data model (`FOSMNode`, `FOSMWay`, `FOSMRelation`, `FOSMFeature`, `FOSMFeatureTable`)
- Stage 1 Parser engine (`FOSMParser`, `FOSMXMLParser`, `FOSMPBFParser` with libosmium bridge)
- Stage 2 Tag classifier engine (`FOSMTagClassifier`) with priority rules & fallback height/width resolution
- Stage 3 Geographic CRS Transformer (`UOSMCRSTransformer`, `FOSMEllipsoid`) with double-precision Geodetic → ECEF → ENU transform & inverse roundtrip
- Metadata Component (`UOSMMetadataComponent`) for attaching queryable OSM tags to UE actors
- Abstract Generator Base (`UOSMGeneratorBase`, `FOSMGenerationContext`) enforcing a single shared CRS transformer
- Dev automation unit test suite (`FOSMCRSTests.cpp`)

## Binary PBF (.osm.pbf) Support

To enable high-speed `.osm.pbf` binary file parsing:
```bash
cd ThirdParty
chmod +x download_thirdparty.sh
./download_thirdparty.sh
```
Unreal Build Tool will automatically detect `ThirdParty/libosmium/include` and compile PBF support (`OSM_WITH_LIBOSMIUM=1`).

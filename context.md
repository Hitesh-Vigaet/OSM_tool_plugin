# OSM World Generator — Progress Context

> Companion to [plan.md](plan.md). States where implementation currently stands against the phased roadmap (plan.md §18). Written 2026-08-10.

---

## 1. Snapshot

| Phase | Plan Weeks | Status | Notes |
|-------|-----------|--------|-------|
| **Phase 1 — Data Foundation** | 1-3 | ✅ **Done** | Parser, model, classifier, CRS (ENU), metadata component, unit tests all present |
| **Phase 2 — Terrain** | 4-5 | ✅ **Done** | DEM sampler, GeoTIFF reader, geoid correction, heightmap builder, terrain generator, tests all present |
| **Phase 3 — Roads** | 6-8 | ✅ **Mostly done** | Road generator, mesh builder, intersection builder, data asset, tests present. One placeholder left (intersection registration) |
| **Phase 4 — Buildings** | 9-11 | ✅ **Mostly done** | Extruder (incl. roof cap), height resolver, generator, tests present. LOD wiring exists but unverified against real UE build |
| **Phase 5 — Tagging & Asset Replacement** | 12-14 | ❌ **Not started** | No `AOSMWorldRegistry`, no `FOSMReplacementRule`, no asset-replacement engine, no Interchange integration, no asset-match editor UI |
| **Phase 6 — Editor UX & Polish** | 15-16 | 🟡 **Partial** | Single-file `SOSMImportWizard` implements all 5 steps internally (not the per-step widget files plan.md specifies). No Water/Landuse generators, no plugin Settings panel, no user docs beyond README |
| **Phase 7 — Performance & QA** | 17-18+ | ❌ **Not started** | No World Partition integration, no city-scale profiling, no UTM projection mode (enum value exists but transformer only implements ENU) |

**Overall: Phases 1-4 (core pipeline: import → parse → transform → terrain/roads/buildings) are implemented. Phases 5-7 (asset replacement, full wizard UX, water/landuse, performance/QA, UTM) are not yet started or only partially stubbed.**

---

## 2. What exists today (verified against Source/)

### OSMWorldGenCore (Runtime module) — Phase 1
- `Parsing/`: `FOSMParser`, `FOSMXMLParser`, `FOSMPBFParser` (libosmium-gated via `OSM_WITH_LIBOSMIUM`)
- `Model/`: `FOSMNode`, `FOSMWay`, `FOSMRelation` (implicit via parse result), `FOSMFeature`, `FOSMFeatureTable`, `FOSMTagDictionary`
- `Classification/`: `FOSMTagClassifier`, `FOSMClassificationRules`
- `CRS/`: `FOSMCRSTransformer` (ENU mode only — see gap below), `FOSMGeoOrigin`, `FOSMEllipsoid`, `EOSMProjectionMode`
- `Components/`: `UOSMMetadataComponent`
- `Generators/`: `UOSMGeneratorBase`, `FOSMGenerationContext`, `FOSMImportWarning`
- Tests: `FOSMCRSTests.cpp`

### OSMWorldGenGenerators (Editor module) — Phases 2-4
- `Terrain/`: `FOSMDEMSampler`, `FOSMGeoTIFFReader`, `FOSMGeoTIFFTile`, `FOSMGeoidCorrection`, `FOSMDEMCRSAlignment`, `FOSMHeightmapBuilder`, `UOSMTerrainGenerator`, `FOSMTerrainSettings`, tests
- `Roads/`: `UOSMRoadGenerator`, `FOSMRoadMeshBuilder`, `FOSMIntersectionBuilder`, `UOSMRoadTypeDataAsset`, tests
- `Buildings/`: `UOSMBuildingGenerator`, `FOSMBuildingExtruder` (walls + roof cap), `FOSMBuildingHeightResolver`, tests
- **Missing entirely**: `Water/`, `Landuse/`, `AssetReplacement/` (no `UOSMWaterGenerator`, `UOSMLanduseGenerator`, `UOSMAssetReplacer`, `FOSMReplacementRule`, `FOSMInterchangeImporter`)

### OSMWorldGenEditor (Editor module) — Phase 6
- `Wizard/`: `SOSMImportWizard` (single class, internally implements all 5 steps as `ConstructStep0..4_*` methods — plan.md's per-step widget split, e.g. `SOSMStepFileSelect.h`, was not followed, but functional coverage looks equivalent)
- **Missing entirely**: `AssetMatching/` (no `SOSMAssetMatchEditor`, `FOSMAssetMatchRule`), `Settings/` (no `UOSMWorldGenSettings` / Project Settings panel)

### ThirdParty
- `libosmium` / `protozero` are **not vendored** — only `README.md` + `download_thirdparty.sh` are present. PBF parsing compiles with `OSM_WITH_LIBOSMIUM=0` until a dev runs the download script. XML `.osm` parsing works via UE's `FXmlFile` without any third-party dependency.

### Content / Config
- `Config/DefaultOSMWorldGen.ini` exists.
- `Content/` (Materials, DataAssets, Icons per plan.md §4.3) is **empty** — no default materials or data assets shipped yet.

---

## 3. Known gaps / loose ends

1. **UTM projection mode** — `EOSMProjectionMode::UTM` exists as an enum value but `FOSMCRSTransformer` (134 lines) only implements the ENU math from plan.md §7.4. Selecting UTM in the wizard would currently be a no-op or fall through to ENU.
2. **Road intersection registration** — `UOSMRoadGenerator.cpp:171` has a comment marking intersection registration as a placeholder; `FOSMIntersectionBuilder` exists but the generator-side wiring isn't finished.
3. **No asset replacement pipeline** — Phase 5 is the biggest missing chunk: no registry actor, no rule matching, no Interchange-based mesh swap, no UI to author rules.
4. **No Water/Landuse generators** — flat-polygon generation for `natural=water`, `landuse=*`, `leisure=*` isn't implemented, even though `FOSMTagClassifier` already classifies these feature types.
5. **No World Partition / streaming integration** — Phase 7 scope (data layers, spatial loading, city-scale profiling) untouched.
6. **No Settings panel or asset-match editor UI** — plugin currently has no Project Settings integration; all generator parameters are per-instance `UPROPERTY`s on the generator objects rather than centrally configurable defaults.
7. **libosmium/protozero not vendored** — anyone building today gets XML-only `.osm` import; `.osm.pbf` needs the manual download step.
8. **No LODs verified in-editor** — `bGenerateLODs`/`LODDistances` are wired into `UOSMBuildingGenerator.cpp` but this hasn't been confirmed to actually produce LOD0-2 static mesh LODs inside a running UE 5.8 editor (no build/test log found).
9. **No accuracy QA against real survey points or city-scale `.pbf` files** yet (plan.md §17, §18 Phase 7).

---

## 4. Suggested next steps (for prioritization, not yet started)

- Close out Phase 3/4 loose ends first (intersection registration, LOD verification) since they're small and closest to done.
- Phase 5 (asset replacement) is the largest remaining architectural piece and blocks the "swap procedural for authored assets" pillar called out in plan.md's Executive Summary.
- Water/Landuse generators are comparatively small (flat polygon generation, similar to existing building floor-polygon code in `FOSMBuildingExtruder`) and would round out Phase 6.
- UTM mode and World Partition integration are lower priority — plan.md scopes them as Phase 7 (ongoing) and the ENU mode already covers areas < 10 km² adequately for early testing.

---

*Note: `/Users/hiteshprajapathi/Desktop/Test_3d_project/Plugins/pcg-skill.md` (currently open in the IDE) is a separate "UnrealWorldBuilder" agent-persona/skill file covering World Partition, Landscape, HLOD, and PCG best practices — it's guidance/standards, not a plan or tracked deliverable, so it isn't part of this progress assessment.*

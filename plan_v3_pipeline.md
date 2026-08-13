# OSM World Generator — v3 Implementation Plan
**Import → Representation → Control Center → (only then) Generation**

Status: proposed, nothing executed yet.
Supersedes the generation portions of `plan_v2_workflow.md`. The import/fetch design there still holds.

---

## 0. Why we are restarting the generation layer

The import side works. The generation side produced a scene shattered across several Z-levels with
roads and buildings in unrelated places. That is not a bug to patch — it is the predictable result of
one architectural decision.

**Root fault: there is no inspectable intermediate.** `OnStartGeneration()` does
parse → classify → spawn actors in a single pass. Nothing between "file on disk" and "geometry in the
viewport" can be examined, validated, or corrected. Every defect this session was therefore only
discoverable *as broken geometry*, after the most expensive step had already run.

That is also why the same class of bug kept recurring. The record from this session:

| Symptom | Actual root cause | Class |
|---|---|---|
| 500 km landscape, renderer precision crash | One OSM way spanning 40 km, unclipped, dictated bounds | No validation between parse and use |
| Terrain always flat | `TerrainSettings.DEMFilePath` never assigned from `State.DEMFilePath` | Silent wiring gap |
| Terrain 890 m above the city | Roads/buildings used a never-loaded `DummySampler` | Three components, three ground truths |
| "Invalid strip offset" on a valid DEM | Reader assumed uncompressed striped TIFF; file is tiled + LZW | Unvalidated input assumption |
| Region size ignored | `State.MinLat` served as both file-scan scratch and generation input | Two sources of truth |
| Editor crash on generate | `Entry.Add(Entry[0])` — TArray aliasing assert | Verified with `std::vector`, which has no such check |

Five of six are *representation* failures, not rendering failures. They would all have been caught by a
stage that says "here is what I understood from your files" before anything is spawned.

**Therefore: build the representation, make it inspectable, and only then generate.**

### Guiding principles (apply to every phase)

1. **One source of truth.** Any value with two owners eventually disagrees. The region, the ground
   surface, and the feature set each get exactly one authority.
2. **Validate at every boundary.** Data crossing a module boundary is checked and rejected loudly.
   No silent fallbacks that look like success.
3. **Inspectable intermediate.** The City Graph is a real, savable, viewable artifact.
4. **Verify against real files before wiring.** Prototype outside the engine, confirm against actual
   data, then port. This is what actually found the LZW and 40 km-way bugs.
5. **Deterministic.** Same inputs + same seed ⇒ same output, always.
6. **Idempotent.** Re-running any stage replaces its output; it never stacks.

---

## Phase 0 — Safety net and teardown

### 0.1 Put the project under version control *first* — blocking

**The plugin is not currently a git repository.** Deleting the generation code today is irreversible.
Nothing else in Phase 0 should happen until this is done.

```
cd ~/Desktop/OSM_plugin
git init
git add -A
git commit -m "Snapshot before v3 teardown: working import, broken generation"
git tag v2-final
```

This also gives every later phase a cheap rollback, which matters because Phases 2–4 involve
significant restructuring.

### 0.2 File disposition

The critical distinction: **DEM/GeoTIFF reading is import, not generation.** It currently lives in the
Generators module and must be preserved. Deleting the module wholesale would destroy the LZW/tiled
reader that was just written and verified.

**KEEP — import & data (no changes in Phase 0)**

| Path | Role |
|---|---|
| `Core/Parsing/*` | OSM XML/PBF parsing |
| `Core/CRS/*` | Projection, ENU transform, ellipsoid |
| `Core/Model/*` | Node/Way/Relation/Feature/FeatureTable |
| `Core/Classification/*` | Tag → feature type |
| `Editor/Fetch/*` | Overpass, OpenTopography, Nominatim, cache |
| `Editor/Settings/*`, `Editor/Wizard/*` | API keys, import wizard |
| `Generators/Terrain/FOSMGeoTIFFReader.*` | **Move to Core/Elevation/** |
| `Generators/Terrain/FOSMGeoTIFFTile.h` | **Move to Core/Elevation/** |
| `Generators/Terrain/FOSMDEMSampler.*` | **Move to Core/Elevation/** |
| `Generators/Terrain/FOSMDEMCRSAlignment.*` | **Move to Core/Elevation/** |
| `Generators/Terrain/FOSMGeoidCorrection.*` | **Move to Core/Elevation/** |

**REMOVE — 3D generation**

| Path | Reason |
|---|---|
| `Generators/Areas/UOSMAreaFeatureGenerator.*` | Mesh generation |
| `Generators/Buildings/*` | Extruder, height resolver, generator, tests |
| `Generators/Roads/*` | Mesh builder, intersection builder, generator, road data asset, tests |
| `Generators/Scene/FOSMSceneSetup.*` | Spawns lights/sky |
| `Generators/Terrain/UOSMTerrainGenerator.*` | Landscape spawning |
| `Generators/Terrain/FOSMHeightmapBuilder.*` | Landscape heightmap |
| `Generators/Terrain/FOSMTerrainSettings.h` | Generation settings |
| `Generators/Terrain/FOSMTerrainTests.cpp` | Tests for removed code |
| `Core/Generators/UOSMGeneratorBase.*` | Generation interface |
| `Core/Generators/FOSMGenerationContext.h` | Generation-time context |

**Result:** the `OSMWorldGenGenerators` module is deleted entirely. Elevation reading moves to Core.
A fresh module is created later when generation returns — with a clean contract.

Height *resolution* logic (`FOSMBuildingHeightResolver`) is worth reading before deleting: the
tag-parsing rules (`height`, `building:levels`, defaults) are data interpretation and belong in
Phase 2, not generation. Port the rules, delete the file.

### 0.3 Detach generation from the wizard

`OnStartGeneration()` loses its spawn calls. Step 4 becomes "Build City Graph". The wizard's job ends
at producing a validated graph.

### 0.4 Acceptance criteria

- [ ] `git log` shows the pre-teardown snapshot and tag
- [ ] Project compiles with `OSMWorldGenGenerators` removed
- [ ] Editor launches with no missing-module errors
- [ ] Wizard still fetches and parses `.osm` + `.tif`
- [ ] Generating spawns **nothing** — no actors, no landscape
- [ ] `FOSMGeoTIFFReader` still decodes the tiled LZW test file (regression test from Phase 1.5)

---

## Phase 1 — Rock-solid import

Mostly built; this phase hardens it and makes correctness *provable*. Requirement: no corrupt or
wrongly-georeferenced file may ever reach Phase 2.

### 1.1 `FOSMRegion` — single source of truth

One struct owns the region, constructed only through validating factories:

```
FOSMRegion::FromCenterAndArea(CenterLat, CenterLon, AreaSqKm)
FOSMRegion::FromBoundingBox(MinLat, MinLon, MaxLat, MaxLon)
```

Carries bbox, centre, area, and the derived CRS origin. **Nothing downstream may recompute a region
from anything else** — that is precisely the bug that made a 1.5 km request into a 500 km landscape.
Wizard state stores one `FOSMRegion`; the file-scan preview becomes a separate, clearly-named field
that no generation path reads.

### 1.2 Acquisition

- Overpass: mirror list with sequential fallback *(done)*
- OpenTopography: DEM by bbox *(done)*
- Nominatim: place → centre *(done)*
- Cache keyed by bbox hash *(done)*
- **Add:** cache manifest (`region.json`) recording requested bbox, source URLs, fetch timestamp,
  file hashes, validation verdict. Prevents silently reusing a file fetched under different terms.

### 1.3 Validation gates — the core of this phase

**`.osm` gate**
| Check | Failure |
|---|---|
| Non-empty; parses as XML | Fatal |
| Root is `<osm>`; has `<node>` elements | Fatal |
| All lat ∈ [-90,90], lon ∈ [-180,180], finite | Fatal |
| Node/way/relation counts > 0 | Fatal |
| Data bbox intersects requested region | Fatal |
| Data bbox ≤ 3× requested area | Warn + clip |
| Ways referencing missing nodes | Warn + count |

**`.tif` gate**
| Check | Failure |
|---|---|
| TIFF magic (`II*`/`MM*`) | Fatal |
| Width/height > 0 | Fatal |
| BitsPerSample ∈ {16,32}; SampleFormat supported | Fatal |
| Compression ∈ {none, LZW, Deflate} | Fatal, naming the value |
| Layout striped or single-tile | Fatal, naming tile count |
| `ModelPixelScale` + `ModelTiepoint` present | Fatal — no geo-tags means no georeferencing |
| CRS is WGS84 / EPSG:4326 | Fatal (reprojection is out of scope) |
| Pixel scale plausible (1e-6 … 0.01°) | Fatal |
| Elevation range ⊂ [-500, 9000] m | Warn |
| NoData coverage < 50% | Warn |

**Cross-file gate (this one is the user's explicit requirement)**
| Check | Failure |
|---|---|
| DEM bbox **contains** OSM bbox | Warn + report uncovered fraction |
| DEM/OSM overlap ≥ 90% | Fatal below that |
| Both resolve to the same `FOSMRegion` | Fatal |

### 1.4 Import report

A structured `FOSMImportReport` — counts by feature type, both bboxes, coverage %, elevation range,
every warning with OSM id — shown in the wizard and logged. **This is the first thing that makes
import verifiable instead of hopeful.**

### 1.5 Regression corpus

Real files, committed to `Tests/Data/`, each of which previously broke something:

1. Tiled + LZW SRTM tile (the "invalid strip offset" file)
2. Uncompressed striped GeoTIFF
3. `.osm` containing a 40 km way crossing the bbox
4. `.osm` saved as UTF-16 with a UTF-8 declaration
5. Truncated `.tif` (must fail cleanly, not crash)
6. `.osm` with ways referencing absent nodes

Each gets an automation test asserting accept/reject **and** the specific reason.

### 1.6 Acceptance criteria

- [x] Every corpus file produces the expected verdict
- [x] No malformed file reaches Phase 2
- [x] Every rejection names the actual cause
- [x] Import report displayed after fetch
- [x] Fetch → validated files is reliably one click

**Status: complete.** 7 automation tests pass (`Automation RunTests OSMWorldGen`, exit code 0).

Delivered:

| Task | Where |
|---|---|
| 1.1 `FOSMRegion` | `Core/{Public,Private}/Region/FOSMRegion.*` — validating factories, invalid by default |
| 1.2 Cache manifest | `Editor/.../FOSMRegionCache.*` — `region.json`, reuse gated on fetch terms |
| 1.3 Validation gates | `Core/.../Validation/FOSMDataValidator`, `FOSMDEMValidator`, cross-file in `FOSMImportReport.cpp` |
| 1.4 Import report | `Core/.../Validation/FOSMImportReport.*`, shown at wizard step 5 |
| 1.5 Regression corpus | `Tests/make_corpus.py` → `Tests/Data/` (16 files), `Core/Private/Tests/FOSMValidationTests.cpp` |

Two decisions worth recording, both made because the alternative had already caused a bug:

- **Acceptance is computed from the issue list, not stored alongside it.** `FOSMValidationResult`
  has no `bSucceeded` flag; `IsAccepted()` is `!HasFatal()`. A result cannot claim success while
  carrying a fatal issue.
- **Observed bounds are not a region.** `FOSMRegion::ObservedBounds()` skips the size limits and is
  named so that using it as an import target reads as wrong. The wizard's file-scan preview writes
  to `ScanPreviewBounds`, which no import path reads — the two used to share fields, and that is
  precisely how a 1.5 km request became a 500 km landscape.

Also fixed in passing: the region cache was keyed on the bounding box alone, so changing the DEM
dataset or fetch padding silently reused files fetched under the old terms. The manifest now
records those terms and reuse is conditional on them matching.

---

## Phase 2 — City Graph (representation only, **no generation**)

The missing layer. A persistent, inspectable description of the city, correct on its own terms before
anything is built.

### 2.1 Storage split

- **`UOSMCityGraph`** (UObject asset, savable): nodes, relationships, metadata
- **`FOSMGeometryStore`**: polygon/polyline vertex data, indexed
- Nodes hold a geometry *handle*, not vertices — keeps the graph light enough to inspect and diff

Saved as a `.uasset` so a region can be reopened without re-fetching.

### 2.2 Node types

| Node | Source | Key attributes |
|---|---|---|
| `Building` | `building=*` | footprint, height, levels, type, address |
| `RoadSegment` | `highway=*` | polyline, class, width, lanes, surface, oneway |
| `Junction` | shared way endpoints | position, connected segments |
| `WaterBody` | `natural=water` | polygon, area |
| `Waterway` | `waterway=*` | polyline, width |
| `VegetationArea` | `natural=wood\|scrub`, `landuse=forest` | polygon, density class |
| `LanduseZone` | `landuse=*` | polygon, class |
| `LeisureArea` | `leisure=*` | polygon, class |
| `Railway` | `railway=*` | polyline, gauge |
| `Barrier` / `PowerLine` | `barrier=*` / `power=*` | polyline |
| `TerrainTile` | DEM | bounds, elevation stats, heightmap handle |
| `Block` | derived | road-enclosed polygon, contained buildings |

Every node: stable id, OSM id(s), category, subtype, full tag dictionary, computed metrics
(area, length, centroid), validation flags.

### 2.3 Relationship types

Both kinds, explicitly — they answer different questions:

**Topological (from OSM structure, exact):**
- `SharesNode` (ways meeting at a junction)
- `ConnectsTo` (road segment ↔ road segment, via junction)
- `PartOf` (way → multipolygon relation)
- `Bounds` (outer/inner ring → area)

**Spatial (derived, tolerance-based):**
- `Contains` (zone → building)
- `FrontsOnto` (building → nearest road segment)
- `AdjacentTo` (shared/near boundary)
- `Crosses` (bridge/tunnel over waterway)

Each edge carries type, endpoints, and derivation confidence. Spatial edges record the tolerance used
so results are reproducible and reviewable.

> **Decided:** `FrontsOnto` is computed **always** — it is cheap with a uniform grid index and high
> value (building orientation, setback, street-side asset choice all depend on it). `Block`
> extraction is **opt-in**, since polygon enclosure over the road graph is the expensive derivation
> and is only needed once per-block grouping is actually in use.
>
> Implementation notes for 2.4: build a uniform grid over road segments sized to the mean segment
> length; for each building, query the 3×3 neighbouring cells and keep the nearest segment within a
> distance cutoff (default 50 m). Record the distance and cutoff on the edge so the result is
> reviewable, and flag buildings with no street within the cutoff rather than silently leaving them
> unlinked.

### 2.4 Grouping

Your point about buildings — grouping belongs in the *graph*, not the actor layer:

- **Category groups**: all buildings, all roads (default UI grouping)
- **Blocks**: buildings grouped by enclosing road loop — the geographically meaningful unit
- **Corridors**: road segments sharing a name/ref, so a street is one entity, not 40 fragments

Individual nodes stay addressable underneath. This gives "one Buildings group" for management while
preserving per-building attributes — the thing that merging into a single mesh would have destroyed.

### 2.5 Build pipeline

```
FOSMParseResult ──▶ Classifier ──▶ FeatureTable ──▶ GraphBuilder ──▶ UOSMCityGraph
                                                          │
                                                          ├─ NodeBuilder
                                                          ├─ TopologyBuilder
                                                          ├─ SpatialBuilder
                                                          ├─ GroupBuilder
                                                          └─ GraphValidator
```

Each builder is a pure function over its input — unit-testable without an editor, which is what makes
Phase 2 verifiable in a way the old pipeline never was.

### 2.6 Graph validation

- Degenerate geometry (< 3 unique ring points, zero area/length)
- Self-intersecting footprints
- Duplicate OSM ids
- Orphan nodes (no relationships where some are expected)
- Roads with no junction (disconnected network islands)
- Buildings outside every zone
- Geometry outside the region bbox (must be zero after clipping)
- Elevation coverage gaps

Output: `FOSMGraphReport`, surfaced in the Control Center. **A node failing validation is flagged, not
silently dropped** — invisible dropping is how "where did my water go?" happens.

### 2.7 Acceptance criteria

- [x] Every classified feature becomes a node or a reported rejection — nothing vanishes
- [x] Node/edge counts match expected values for the test corpus
- [ ] Graph saves and reloads with identical content — *deferred to Phase 3, see below*
- [x] Same input + seed ⇒ identical graph (hash comparison)
- [x] Zero geometry outside the region bbox (within the shared clip margin)
- [x] Build completes with **no actors spawned**

**Status: complete except asset round-trip.** 14 automation tests pass, exit code 0.

| Task | Where |
|---|---|
| 2.1 Storage | `Core/Public/Graph/FOSMGeometryStore.h`, `UOSMCityGraph.h` |
| 2.2 Node builders | `FOSMGraphBuilder::BuildNodes` — 11 node types mapped from classification |
| 2.3 Topology | `BuildTopology` — junctions by shared coordinate, `SharesNode` + `ConnectsTo` |
| 2.4 Spatial | `BuildSpatial` — `Contains` (point-in-polygon, hole-aware), `FrontsOnto` (uniform grid) |
| 2.5 Grouping | `BuildGroups` — category groups + name-based corridors |
| 2.6 Validation | `ValidateGraph` + `FOSMGraphReport` |

Three decisions made during implementation:

- **`EOSMRelationshipType`, not `EOSMRelationType`** — the latter already exists for OSM relation
  members. Two different concepts with one name is how the wrong enum gets used.
- **One shared clip margin.** Feature clipping padded by 25% of the region (250 m on a 1 km region)
  while the graph validator demanded zero, so a correctly clipped import produced a graph that
  failed its own validation. Now `OSMRegionLimits::ClipMarginDegrees` (~111 m, matching the Overpass
  node padding) is the single number the fetch, the clip and the validator all agree on.
- **Distances without a projection.** Metres are derived by scaling degrees at the region's centre
  latitude. Over a 5 km-capped region the error is under a metre, and it keeps the graph free of any
  projection choice — baking one in is what made the old pipeline impossible to re-inspect.

**Asset round-trip is deferred.** `UOSMCityGraph` is a `UObject` with fully `UPROPERTY`-serialised
state, so it *can* be saved; nothing yet creates a package for it, because nothing needs to reopen a
graph until the Control Center exists. Phase 3.1 should add the save/load path and the round-trip
test together, so the test covers a code path something actually uses.

**The corpus caught two fixture defects** that would have made these tests theatre: the baseline was
initially roads-only, so `FrontsOnto` and `Contains` passed against a graph containing no buildings;
and the fixture filtered by latitude alone, so most features fell outside the region's longitude and
were clipped away at import. Both are fixed — the baseline is now category-balanced and filtered on
both axes, and the graph it produces contains buildings, roads, junctions, vegetation and zones.

---

## Phase 3 — Control Center UI

Opens automatically after a successful import. The place where the city is understood and configured
before anything is built.

### 3.1 Layout

```
┌──────────────────────────────────────────────────────────────┐
│ Region: Jakkur Lake · 2.25 km² · 13.084–13.094, 77.600–77.614│
├───────────────┬──────────────────────────┬───────────────────┤
│ NODE EXPLORER │      VIEWPORT OVERLAY    │    INSPECTOR      │
│               │                          │                   │
│ ▾ Buildings   │   (debug draw of graph   │  Building #1247   │
│   ▾ yes (124) │    geometry — lines and  │  ─────────────    │
│     #1247     │    polygons, colour per  │  Attributes       │
│   ▸ apts (4)  │    category. NOT meshes) │   height   12.0 m │
│ ▾ Roads       │                          │   levels   4      │
│   ▸ resid(120)│                          │  Tags             │
│ ▾ Water (3)   │                          │   building=yes    │
│ ▾ Vegetation  │                          │  Relationships    │
│               │                          │   FrontsOnto →    │
│ [visibility]  │                          │    RoadSeg #88    │
├───────────────┴──────────────────────────┴───────────────────┤
│ ISSUES (3)   │  ASSETS & RATIOS  │  IMPORT REPORT            │
└──────────────────────────────────────────────────────────────┘
```

### 3.2 Node Explorer
Tree: category → subtype → instances, with counts. Search and filter. Per-level visibility toggles
driving the viewport overlay, so any category can be isolated. Multi-select for bulk configuration.

### 3.3 Viewport overlay
Debug-draw only (`UWorld` line/polygon batching): footprints as outlines, roads as centrelines with
width bands, areas as translucent polygons, junctions as points, relationships as connector lines
(toggleable). **No meshes, no actors** — this is the honest picture of the graph, and it is how the
"is my representation right?" question gets answered before generation exists.

### 3.4 Inspector
Selected node's attributes, raw OSM tags, computed metrics, validation flags, and clickable
relationships for traversal (building → its street → that street's junctions).

### 3.5 Assets & ratios
Per category/subtype: asset set (Data Asset), ratio sliders normalised to 100%, seed, and fallback
behaviour when no asset is assigned. Config saved with the graph. **Configuration only in this phase
— nothing is spawned.**

Road continuity constraint from the earlier discussion is enforced here as a rule: selection is
per-corridor, not per-segment, so one street cannot alternate mud/tar every 10 m.

### 3.6 Issues panel
`FOSMGraphReport` entries, click-to-select, grouped by severity.

### 3.7 Acceptance criteria

- [x] Opens automatically after import
- [x] Every graph node visible and selectable
- [x] Category toggles isolate correctly in the viewport
- [x] Inspector shows complete attributes and relationships
- [x] Relationship traversal works
- [x] Ratios editable, normalised, persisted
- [x] Still zero generated actors

**Status: complete.** 17 automation tests pass, exit code 0.

| Task | Where |
|---|---|
| 3.1 Shell + asset I/O | `Editor/ControlCenter/SOSMControlCenter.*`, `Editor/Graph/FOSMGraphAssetIO.*` |
| 3.2 Node explorer | Category → subtype → instance tree, search, per-category visibility |
| 3.3 Viewport overlay | `RefreshOverlay` — persistent debug lines, colour per type, no actors |
| 3.4 Inspector | Attributes, tags, metrics, flags + clickable relationship traversal |
| 3.5 Assets & ratios | `Core/Graph/FOSMGenerationConfig.*`, editable weights/seed/per-corridor |
| 3.6 Issues panel | Severity-ordered `FOSMGraphReport`, flagged rows highlighted in the explorer |

**Graph save/load landed here**, closing the criterion deferred from 2.7. The round-trip test
asserts equality by content hash rather than field-by-field, so a property added later cannot be
silently lost by a test that only checks the fields someone remembered to list.

Two design points worth recording:

- **Ratios are stored as weights, shown as percentages.** Percentages that must total 100 make every
  edit a multi-field edit, which is how ratios drift. Weights stay independent; the percentage is
  derived and read-only.
- **Visibility lives on the graph, not the widget**, so isolating a category survives closing the
  panel and is saved with the asset.

The overlay is deliberately debug-draw only. If a footprint looks wrong there, it is wrong in the
data — there is no generation step in between to blame.

---

## Phase 4 — PCG bridge (design + dry run, no final generation)

### 4.1 Graph → PCG data
- Buildings → point set (transform, footprint bounds, height, chosen asset)
- Roads → splines with width/class attributes
- Areas → surface polygons for scatter
- Terrain → heightfield reference

### 4.2 Deterministic selection
Seeded per node id, so the same node always resolves to the same asset. Ratios are *distribution
targets*, honoured across a corridor/block, not coin-flipped per element.

### 4.3 Fallback chain
`Specific asset → category asset → procedural primitive → flagged placeholder`. Never silent —
a placeholder is a visible, counted state.

### 4.4 Dry run
Report what *would* be generated (counts per category, asset resolution, fallback usage, estimated
actor/triangle budget) with nothing spawned. The last checkpoint before geometry exists.

### 4.5 On the PCG skill you asked about

**There is no PCG skill available in this environment** — the installed skill set is unrelated
(artifacts, code-review, scheduling, etc.), and I can't install one. Rather than pretend, the plan is:

- The engine's **PCG plugin is present** (`UE_5.8/Engine/Plugins/PCG`, plus `PCGInterops`), and
  `PCG` is already a dependency of the current build files.
- Write our own `docs/pcg-reference.md` in Phase 4.0, derived from the engine's PCG source and
  sample content: node graph basics, `UPCGComponent`/`UPCGGraph` setup, custom C++ PCG element
  authoring, attribute sets, point data, spline sampling, mesh spawner + instanced static meshes,
  determinism/seeding, and the editor-time execution path.
- Grounding it in the installed engine source (which we can read) is more reliable than a generic
  guide anyway, since PCG's API has moved considerably across 5.x versions.

---

**Phase 4 status: 4.0, 4.2, 4.3, 4.4 complete.** 23 automation tests pass.

| Task | Where |
|---|---|
| 4.0 `docs/pcg-reference.md` | Read out of `Engine/Plugins/PCG/Source/PCG/Public/` in this install |
| 4.2 Deterministic selection | `Core/Generation/FOSMAssetSelector.*` |
| 4.3 Fallback chain | Same — every node reports an outcome, never an absence |
| 4.4 Dry run | `Core/Generation/FOSMDryRunReport.*`, "Dry Run" button in the Control Center |

**4.1 (graph → live PCG data) is deliberately not built yet.** The dry run states which PCG type
each category maps to, which is what the decision needs; constructing `UPCGBasePointData` and
`UPCGSplineData` for real belongs with Phase 5, where something consumes them. Building the
converter now would mean writing against an API with no consumer to prove it right — the same
mistake as the graph save path that sat unexercised until the Control Center needed it.

The most consequential thing the reference turned up: **`UPCGPointData` is deprecated as of 5.6**
in favour of `UPCGBasePointData` / `UPCGPointArrayData`, and the concrete class is chosen by a
CVar, so point data must come from `FPCGContext::NewPointData_AnyThread` rather than being
allocated directly. Most PCG material still shows the old form.

---

## Phase 5 — Generation (only after 1–4 are signed off)

### 5.0 Decided: footprints are generated, not swapped for prefabs

Measured on a real 1 km² Bangalore region, 1,314 buildings:

| | |
|---|---|
| smallest footprint | 19.5 m² |
| median | 155.9 m² |
| largest | **7,814 m² — 401x the smallest** |
| aspect ratio | median 1.41, p95 2.05, max 6.1 |
| vertices | 4 to 34; **95% have <= 5 corners** |

No library of prefab meshes survives a 401x size range. Scaling one mesh across it produces 20 m²
huts and 7,800 m² apartment blocks built from the same stretched geometry, with windows and doors
scaled to nonsense. This is not a content problem to be solved by authoring more meshes — it is
the wrong mapping.

**The rule:**

- **Area and linear features generate geometry from their own outline.** A building is an
  extrusion of *its* polygon to *its* height; a road is a mesh swept along *its* spline at *its*
  width. Every feature fits because it is built from itself. The 95% that are near-rectangular
  extrude trivially; the 67 complex ones need real triangulation, at a harmless 34 vertices max.
- **Point features use instanced meshes.** Trees, lamps, benches, vehicles — things with no
  footprint to conform to, where a prefab is exactly right.

**Consequence for the asset system (Phase 3.5 / 4.2):** the ratio machinery stays, but for areas
and lines it rations a **style**, not a mesh:

| Feature | Generated as | Rule selects |
|---|---|---|
| Building | extruded footprint | facade style — wall/roof material, window density, floor height |
| RoadSegment | swept spline mesh | surface material + profile |
| WaterBody | surface at polygon | water material |
| VegetationArea | PCG scatter inside polygon | tree/shrub **meshes** |
| Amenity | point instance | prop **mesh** |

"60% concrete, 30% brick, 10% glass" across 1,317 buildings then works at every footprint size,
which a mesh library cannot do. The UI currently restricts slots to `UStaticMesh`; that becomes
per-category — a style data asset for buildings and roads, a mesh for scatter and props.

This is also what the established OSM-to-3D pipelines do (osm2world, Cesium OSM Buildings, Blosm),
for the same reason.



Deliberately not detailed yet — it should be designed against a working graph, not imagined now.
Non-negotiable rules carried forward from this session's failures:

1. **One ground reference.** All generators sample the same elevation source through the graph.
2. **Idempotent.** Every generator destroys its previous output before creating new output.
3. **Grouped actors.** One actor per category or block, not thousands of loose actors.
4. **Bounded.** Hard extent limits with loud clamping, never silent.
5. **Clearance.** Buildings and roads negotiate space; road widths come from data, not a constant.
6. **Incremental.** Generate one category at a time, verifiable independently.

---

## Cross-cutting standards

**Error taxonomy**
| Level | Meaning | Behaviour |
|---|---|---|
| Fatal | Cannot proceed | Abort stage, explain, suggest fix |
| Error | Feature unusable | Skip feature, count it, continue |
| Warning | Suspicious but usable | Proceed, record |
| Info | Notable | Log only |

Every message states **what failed, why, and what to do** — `"Region is 40.7 × 16.0 km, over the
25 km² limit — lower the area"`, not `"invalid bounding box"`.

**Testing discipline** (the practice that actually worked this session): prototype the algorithm
outside the engine, verify against the real file, port, then re-verify with a harness that uses the
*same container semantics as the engine*. The `std::vector`-vs-`TArray` aliasing crash is exactly what
happens when that last step is skipped.

**Logging**: one category per module; every stage logs inputs, counts, and a one-line summary.

---

## Execution order

| # | Task | Size | Gate |
|---|---|---|---|
| 0.1 | `git init` + snapshot commit | XS | **Blocking — nothing before this** |
| 0.2 | Move elevation files to Core | S | Compiles |
| 0.3 | Delete generation code + module | S | Compiles, spawns nothing |
| 0.4 | Detach generation from wizard | S | Import still works |
| 1.1 | `FOSMRegion` single source of truth | M | Region unambiguous |
| 1.2 | Cache manifest | S | Provenance recorded |
| 1.3 | Validation gates | L | Corpus verdicts correct |
| 1.4 | Import report | M | Shown in wizard |
| 1.5 | Regression corpus + tests | M | All pass |
| 2.1 | Graph + geometry store types | M | Saves/loads |
| 2.2 | Node builders | L | All features → nodes |
| 2.3 | Topology builder | M | Junctions correct |
| 2.4 | Spatial builder | M | `FrontsOnto` sane |
| 2.5 | Grouping (blocks, corridors) | M | Groups correct |
| 2.6 | Graph validator + report | M | Issues surfaced |
| 3.1 | Control Center shell | M | Opens post-import |
| 3.2 | Node explorer + visibility | L | Isolation works |
| 3.3 | Viewport overlay | L | Graph visible, no actors |
| 3.4 | Inspector + traversal | M | Relationships navigable |
| 3.5 | Assets & ratios | L | Persisted |
| 3.6 | Issues panel | S | Click-to-select |
| 4.0 | `docs/pcg-reference.md` | M | Grounded in engine source |
| 4.1 | Graph → PCG data | L | Data correct |
| 4.2 | Deterministic selection | M | Reproducible |
| 4.3 | Fallback chain | M | Never silent |
| 4.4 | Dry run report | M | Accurate, spawns nothing |

XS < 1h · S ≈ half day · M ≈ 1–2 days · L ≈ 3–5 days

---

## Open questions

1. ~~**Spatial inference depth** (§2.3)~~ — **decided**: `FrontsOnto` always, `Block` opt-in.
2. **Graph asset size** — at 25 km² a graph could hold ~100k nodes. If `.uasset` serialization gets
   unwieldy we may need a binary side-car. Worth measuring at Phase 2.1 rather than guessing.
3. **Terrain representation** — one `TerrainTile` node, or a tiled grid? Depends on whether terrain
   is ever edited per-region in the Control Center.
4. **Multi-tile DEM** — currently single-tile only. Needed if region caps ever rise above 25 km².

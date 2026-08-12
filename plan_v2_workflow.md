# OSM World Generator — Workflow Redesign (v2 Addendum)

> **Status:** Draft — awaiting your review. No implementation has started.
> **Relationship to [plan.md](plan.md):** This document does **not** replace plan.md. It restructures the *workflow* around the pipeline plan.md already specifies, and inserts one new stage (graph nodes + ratio configuration) between parsing and mesh generation. Phases 1-4 of plan.md (parser, tag classifier, CRS transform, DEM/terrain, road/building generators) are reused as-is at the engine level.
> **Why a v2 doc and not edits to plan.md:** plan.md is the record of the original design; this captures a real pivot in how the plugin is used, decided after implementation had already started. Keeping them separate preserves the history of *why* things changed.

---

## 0. What's changing and why

Today: you manually download a `.osm` file and a `.tif` DEM file, upload both through the wizard, and hope their bounds line up. Roads render as bare lines against a black, unlit level, and every feature is generated straight into a final mesh in one pass.

Going forward: you pick a region on a map inside the editor. The plugin fetches the `.osm` and DEM data for that exact bounding box automatically, so the two always match. Generation happens in explicit, individually re-runnable stages — terrain, then a lightweight **graph** of what exists (no meshes yet), then a **configuration** pass where you set style ratios and constraints per feature category, then final mesh placement. The level has basic sky and lighting from the moment a region is imported.

**Revision note (this update):** the procedural fallback used whenever no library mesh matches a node is now treated as a **permanent, first-class generation path**, not a temporary stand-in for a missing library — because for the foreseeable future (no mesh library exists yet), it *is* the primary output. §7 and the new §10 spell out what "fallback" means at every stage, not just mesh-gen, and the implementation order in §12 now builds the fallback PCG path before the graph-node/ratio UI, since the fallback has to exist and work well regardless of whether library-matching is layered on top later.

---

## 1. Verified: this is safe to layer on top of existing code

Before committing to "layer, don't rewrite," I read the actual (not just planned) headers:

- `UOSMGeneratorBase::Generate(const FOSMGenerationContext& Context, const TArray<const FOSMFeature*>& Features, TArray<AActor*>& OutActors)` — [UOSMGeneratorBase.h](Source/OSMWorldGenCore/Public/Generators/UOSMGeneratorBase.h) — matches plan.md exactly, as implemented.
- `FOSMGenerationContext` — [FOSMGenerationContext.h](Source/OSMWorldGenCore/Public/Generators/FOSMGenerationContext.h) — currently holds `CRSTransformer`, `TargetWorld`, `FeatureTable`, cancel flag, warnings.

**Conclusion:** the new "which style/ratio was resolved for this node" information can be added as a **new field on `FOSMGenerationContext`** (e.g. `const FOSMResolvedStyleTable* ResolvedStyles`), without touching `Generate()`'s signature at all. I also checked the three concrete subclasses, not just the base — `UOSMTerrainGenerator::Generate`, `UOSMRoadGenerator::Generate`, and `UOSMBuildingGenerator::Generate` all use the identical `(Context, Features, OutActors)` signature and all iterate `for (const FOSMFeature* Feature : Features)`. That loop body is exactly where each generator would add one line — look up `Context.ResolvedStyles->Find(Feature->OSMId)` — without restructuring the loop, changing the signature, or touching how the wizard dispatches features to generators. This is a genuine layer, verified against the actual implementations, not inferred from plan.md's proposed design.

---

## 2. New end-to-end flow

```
┌──────────────┐  ┌───────────────┐  ┌─────────────┐  ┌──────────┐  ┌────────────┐  ┌─────────────┐  ┌─────────────┐
│ 0. Region    │─▶│ 1. Map region │─▶│ 2. Auto-    │─▶│ 3. Parse │─▶│ 4. Terrain │─▶│ 5. Graph    │─▶│ 6. Configure│
│    size gate │  │    picker     │  │    fetch    │  │  & tag   │  │   build    │  │    build    │  │    ratios   │
│ (Small/Med)  │  │  (in-editor)  │  │ (+ cache)   │  │(existing)│  │ (existing) │  │   (NEW)     │  │   (NEW)     │
└──────────────┘  └───────────────┘  └─────────────┘  └──────────┘  └────────────┘  └─────────────┘  └──────┬──────┘
                                                                                                              │
                                                                            ┌─────────────────────────────────┘
                                                                            ▼
                                                                    ┌───────────────┐
                                                                    │ 7. Mesh-gen   │
                                                                    │  (hybrid PCG) │
                                                                    └───────────────┘
```

Stages 0-2 are new (region selection + auto-fetch). Stage 3 (`FOSMParser` + `FOSMTagClassifier`) and stage 4 (`UOSMTerrainGenerator`) are reused unmodified from plan.md Phases 1-2. Stages 5-6 are the new graph/configuration layer. Stage 7 reuses the *math and mesh-building logic* already in `FOSMRoadMeshBuilder` / `FOSMBuildingExtruder`, invoked from PCG rather than called directly.

**Flow model (confirmed):** stages run sequentially the first time through a region. Once a stage has completed, it becomes independently re-runnable — e.g. after stage 6 you can change ratios and re-run stage 7 alone without re-fetching data or rebuilding terrain. Scene setup (sky/lighting, §6) happens automatically the moment stage 2 completes, so the level is never black even before terrain exists.

---

## 3. Stage 0-1 — Region size gate + map picker

### 3.1 Region size gate
A dialog/step shown **before** the map, per your request:

| Option | Status |
|--------|--------|
| Small (~1-2 km²) | Enabled |
| Medium (~10-20 km²) | Enabled |
| Large (100+ km²) | Visible but disabled/greyed out, labeled "requires a high-end machine — coming later" |

This selection constrains the map picker's max-draggable-bbox size and is passed to the fetch stage to decide whether Overpass queries need chunking (Medium may).

### 3.2 In-editor map picker
- New Slate widget hosting a `WebBrowser` widget (requires enabling UE's `WebBrowserWidget` plugin dependency) pointed at a small bundled local HTML/Leaflet page (OSM tile layer, draw-a-box control).
- The HTML page posts the drawn bbox (`minLat, minLon, maxLat, maxLon`) back to the Slate widget via the `WebBrowser`'s JS-bridge (`UWebBrowser::CallBindUObject` / `ExecuteJavascript` postMessage pattern).
- Bbox is clamped/validated against the size gate from §3.1 before "Next" is enabled.

**Flagged assumption (needs your confirmation before build):** the `WebBrowser` plugin's availability/behavior varies by platform (Windows/Mac support differs, e.g. CEF vs WKWebView backing). If that turns out to be unreliable on your dev machine, fall back to manual lat/lon bbox entry (§10 table, row 1) — worth a quick spike before committing engineering time to the embedded map.

---

## 4. Stage 2 — Auto-fetch + cache

### 4.1 Sources
| Data | Source | Auth |
|------|--------|------|
| `.osm` (vector features) | Overpass API, queried by bbox | None required |
| DEM (`.tif`-equivalent elevation) | OpenTopography API, queried by same bbox | Requires a free API key |

> **Blocking prerequisite, not just an open item:** OpenTopography requires an API key tied to your account, and nothing downstream of stage 2 (terrain build, graph nodes with real elevation, any end-to-end test) can be validated against real data until that key exists. This needs to happen **before** implementation reaches stage 2, not be decided "during implementation" — please register for a key ([opentopography.org](https://opentopography.org)) whenever convenient, and let me know where you'd like it stored (Project Settings field is the natural default — see §9). Overpass needs no key but does rate-limit; Medium-sized regions may need the request chunked/retried.

### 4.2 Caching
- Fetched files land in `Saved/OSMWorldGen/RegionCache/<bbox-hash>/region.osm` and `.../dem.tif`.
- If a cache entry exists for the exact bbox, stage 2 reuses it instead of re-fetching.
- A "Refresh Data" action forces re-download and overwrites the cache entry.
- Cache is local-only (per-machine `Saved/` folder, not source-controlled) — consistent with how UE treats `Saved/`.

### 4.3 New classes (OSMWorldGenCore or a new OSMWorldGenEditor subfolder — TBD at implementation time)
- `FOSMOverpassClient` — builds/sends the Overpass QL query for a bbox, async HTTP via `FHttpModule`, writes result to the cache path.
- `FOSMOpenTopographyClient` — same pattern for DEM tiles.
- `FOSMRegionCache` — bbox → cache-path hashing, existence checks, refresh/invalidate.

---

## 5. Stage 5 — Graph node build (NEW)

### 5.1 What a node is
Confirmed: **pure data, no actors.** A new data model, separate from (but built from) the existing `FOSMFeatureTable`:

```
FOSMGraphNode
├── int64 OSMId
├── EOSMGraphNodeType NodeType      (Building | RoadWay | VegetationArea | WaterArea | ...)
├── EOSMFeatureType SourceFeatureType   (existing enum, from FOSMTagClassifier)
├── FString SubType                     (existing — e.g. "residential", "primary")
├── TArray<FVector> Geometry            (already-transformed UE-space polyline/polygon, reused from FOSMFeature)
├── FName ResolvedStyleId               (set during stage 6, empty until then)
└── TMap<FString, float> ResolvedConstraints  (e.g. "HeightOverrideM" — set during stage 6)

FOSMGraphEdge   (roads only, for intersection/topology — reuses FOSMIntersectionBuilder's existing detection)
├── int64 FromNodeOSMId
├── int64 ToNodeOSMId
└── int64 SharedOSMNodeId
```

- **One `FOSMGraphNode` per OSM way/relation** for buildings and roads — this is what makes the road-continuity fix in §7.3 work for free: a road graph node already spans a whole segment between intersections, matching how OSM already splits ways.
- **Vegetation is its own node type**, one per `natural=wood` / `landuse=forest` (etc.) *area* polygon — not per tree. See §8.
- **Unknown features (confirmed):** anything `FOSMTagClassifier` returns `EOSMFeatureType::Unknown` for is logged as a warning and excluded from the graph — matches current behavior, no new "generic node" type is introduced.

### 5.2 Visible output of this stage
- Roads: a `USplineComponent` actor per road graph node, spawned in the level — **persistent**, editable, and reused directly as input to stage 7's mesh generation (confirmed — no throwaway/rebuild).
- Buildings: a simple flat footprint outline (line-loop or thin flat mesh) so the layout is visible before real extrusion.
- Vegetation areas: outline only, no scatter yet.

This is also the point where the "map view before 3D generation" you asked for naturally exists — after stage 4 (terrain) + stage 5 (graph), you're looking at a lit, terrain-shaped scene with roads-as-splines and building footprints, no meshes yet.

---

## 6. Stage 6 — Configure ratios & constraints (NEW)

### 6.1 Style-set data assets (library-driven, per your answer)
New Data Asset types, one per category, authored by whoever builds the mesh library:

```cpp
UCLASS(BlueprintType)
class UOSMBuildingStyleSet : public UDataAsset
{
    UPROPERTY(EditAnywhere)
    TArray<FOSMBuildingStyleEntry> Styles;   // { FName StyleId; UStaticMesh* (or PCG-consumable ref); TArray<FString> OSMSubTypeFilters; }
};
```
(Equivalent `UOSMRoadStyleSet`, `UOSMVegetationStyleSet`.)

Since the library doesn't exist yet, these Data Assets can ship **empty** — the §7.4 procedural fallback covers the "no entries" case natively (100% of ratio routes to "Procedural"), so there's no need to author placeholder box entries just to make the pipeline testable. Dropping in real meshes later means adding entries to the Data Asset, no code change.

### 6.2 Ratio UI
- The config panel reads whichever `StyleSet` Data Asset is assigned per category and **dynamically builds sliders** — one per style entry currently in the asset. No hardcoded "good/normal" categories in C++.
- Every category's slider list also includes one **implicit "Procedural (fallback)" entry**, always present regardless of what's in the StyleSet — this is what routes unmatched nodes to the §7.4 procedural branch. With an empty StyleSet it's the only entry and silently holds 100% of the ratio; as real entries are added, it just becomes another slider the user can raise or lower like any style.
- **Scoping (confirmed):** ratios apply per OSM sub-type/zone bucket — e.g. `landuse=residential` buildings get one ratio mix, `landuse=commercial` another — read from `SubType`/`SourceFeatureType` already on each `FOSMGraphNode`, no separate zone-painting tool needed for v1.
- **Constraints:** alongside ratio sliders, relevant numeric min/max fields per category (e.g. building height clamp, road width variance) — scoped to what's meaningful per category, not a generic catch-all. Exact field list per category is an open item (§10) since it depends on what the mesh library ends up needing.

### 6.3 Resolution + determinism
- A **seed** value (defaults to a hash of the region bbox, user-overridable) drives weighted-random style assignment per node.
- Resolving produces an `FOSMResolvedStyleTable` (OSMId → chosen `StyleId` + resolved constraint values) — this is the new field added to `FOSMGenerationContext` per §1.
- Re-running stage 6 with the same seed + ratios always reproduces the same assignment (confirmed requirement).

---

## 7. Stage 7 — Mesh generation (hybrid PCG, with a permanent procedural fallback)

### 7.1 Division of responsibility (confirmed hybrid approach)
- **C++ (existing + extended):** owns parsing, classification, CRS transform, graph-node building, and ratio resolution. Produces `FOSMResolvedStyleTable`.
- **PCG Graphs (new):** one PCG Graph per category (Buildings, Roads, Vegetation), fed by the graph-node geometry + resolved style table as external Point Data / attribute input. PCG handles the actual weighted mesh spawning, using its native Static Mesh Spawner + attribute-based selection — this is exactly what PCG is built for, so it saves reimplementing weighted placement logic by hand.
- The *existing* mesh-building math (`FOSMBuildingExtruder`, `FOSMRoadMeshBuilder`) is the **procedural fallback path** — see §7.4. It's invoked from a branch inside each category's PCG graph, not bypassed by it.

### 7.2 Why hybrid over full PCG migration
Full migration would also re-architect terrain and the already-working generator base classes, which §1 confirmed don't need to change. Hybrid gets PCG's weighted-selection and per-point attribute tooling exactly where it's needed (stage 7) without touching what already works (stages 3-4).

### 7.3 Road continuity (your mud/tar concern — resolved)
Because a road graph node = one whole OSM way (§5.1), the ratio-resolution step (§6.3) picks **one style per way**, not per meter. A PCG graph consuming that way's spline with a single resolved `StyleId` attribute cannot alternate mid-segment — the fix is structural (in the node granularity), not a runtime rule PCG has to enforce.

### 7.4 The procedural fallback is permanent, per-node, and built first (confirmed)

This is the main change in this revision. Every category's PCG graph has two branches per node, chosen by whether `ResolvedStyleId` (§6.3) points at a library entry or not:

```
Per node in PCG graph:
  ┌─ ResolvedStyleId matches a StyleSet entry? ──yes──▶ Static Mesh Spawner (library asset)
  │
  └─ no (no entry, or StyleSet is empty/unassigned) ──▶ Procedural branch:
        1. Geometry: existing FOSMBuildingExtruder / FOSMRoadMeshBuilder logic, unchanged
           (already resolves height/width from OSM tags per plan.md §10.3 — this math
           doesn't need to be rebuilt, only exposed as a PCG-callable step)
        2. Material: a small set of default parametrized materials (e.g. one facade
           Material Instance with color/tiling/brick-vs-concrete params, one road-surface
           Material Instance with asphalt/dirt/color params, one foliage-card material),
           with the params driven per-node from PCG attributes (a hash of OSMId, or the
           node's SubType) so procedural output is *varied*, not flat and uniform.
```

**Quality bar (confirmed):** reuse the existing geometry generation as-is — it already handles height/width resolution correctly — and invest the new work in **material variation**, not new geometry logic (e.g. no procedural window/facade detailing in this pass; that's the "richer procedural system" option you didn't pick). This is a deliberate, bounded scope: good enough to look intentional and varied across a whole region, not photoreal.

**Per-node, not per-category (confirmed):** the fallback triggers per individual node, so a region can show a mix of library-matched and procedural buildings side by side the moment *any* library entries exist for *some* sub-types — you don't need a complete library before results look reasonable.

**Ratio UI implication (§6.2 update):** every category's ratio slider list implicitly includes a **"Procedural (fallback)"** entry alongside whatever real style entries exist in the StyleSet Data Asset. With an empty/unassigned StyleSet, that entry is the only one and gets 100% of the ratio automatically — no extra step needed to "activate" fallback mode.

---

## 8. Vegetation (included, as area-scatter)

Confirmed in scope, but mechanically different from buildings/roads:

- Graph node = one per vegetation-area polygon (`natural=wood`, `landuse=forest`, `landuse=grass`, etc.), not per tree — OSM rarely tags individual trees.
- Stage 6 config: a **density** control (points per m²) plus the same style-ratio sliders (e.g. oak:pine:shrub = 3:2:1), scoped per vegetation sub-type same as buildings/roads.
- Stage 7: PCG's native **point-generation-inside-polygon + scatter** nodes fill the area, then weighted mesh selection per point uses the same resolved style table mechanism as buildings/roads (density and ratio are just additional attributes PCG reads).
- This reuses PCG's built-in scattering tools rather than needing new C++ for area-fill — the main new work is exposing density/ratio as PCG graph inputs from the resolved style table.

---

## 9. Automatic scene setup

On first successful stage 2 (data fetched) for a level, if not already present:
- `ASkyAtmosphere`
- `ADirectionalLight` (sun)
- `ASkyLight`
- A light-weight `APostProcessVolume` (unbound, minimal overrides)

Added once per level (checked via `TActorIterator` before spawning, so re-running import doesn't duplicate them). This directly fixes the "black level by default" problem — confirmed as full basic setup, not the lat/lon-driven sun-angle variant (that stays a possible future refinement, not in this pass).

---

## 10. Fallback behavior — every stage

You asked for fallbacks at every stage, not just mesh-gen. Consolidated here so it's checkable in one place; each row cross-references the section that owns the detail.

| Stage | Primary path | Fallback when it fails/is unavailable | Confirmed / assumed |
|-------|-------------|----------------------------------------|----------------------|
| 0-1. Region size + map picker | Embedded in-editor Leaflet map (§3.2) | Manual lat/lon bbox entry fields (no map) | Assumed — this was Q1's deprioritized option, now formalized as the *fallback*, not a discarded alternative. Applies if the `WebBrowser` widget spike (§11.1) fails on your platform. |
| 2. Auto-fetch (.osm) | Overpass API by bbox (§4.1) | Manual `.osm` file upload for that bbox | **Confirmed.** Per-file — if only DEM fails, `.osm` still auto-fetches. |
| 2. Auto-fetch (DEM) | OpenTopography API by bbox (§4.1) | Manual `.tif` file upload for that bbox, **or** flat terrain if no DEM supplied at all (existing `UOSMTerrainGenerator` behavior, plan.md §12.1: "Optional DEM file path. If empty, generates flat terrain") | **Confirmed** (manual upload) + already-implemented (flat terrain). Two-level fallback: try auto-fetch → offer manual upload → flat terrain if the user skips both. |
| 3. Parse & classify | `FOSMTagClassifier` resolves a known `EOSMFeatureType` | Unknown-type features skipped, logged as `FOSMImportWarning` | **Confirmed**, matches current behavior — no change. |
| 3. Multipolygon assembly | libosmium `MultipolygonManager` | Skip unassemblable relations with a warning (plan.md Risk R9, already the documented mitigation) | Already covered by plan.md — no change needed here. |
| 4. Terrain | DEM-derived heightmap | Flat Landscape (already implemented per plan.md §12.1) | Already implemented — carried forward unchanged. |
| 5. Graph build | Well-formed polygon/polyline geometry | Skip degenerate/self-intersecting features with a warning (plan.md Risk R9 territory, same mitigation pattern) | Consistent with existing risk mitigation; not a new mechanism. |
| 6. Ratio/constraint config | User-authored ratios per StyleSet entry | If a category's StyleSet is empty/unassigned, "Procedural (fallback)" silently takes 100% of that category's ratio (§7.4) | **Confirmed**, new this revision. |
| 7. Mesh generation | Library `StaticMesh` via PCG Static Mesh Spawner | Procedural geometry (existing extruder/mesh-builder code) + parametrized default material, chosen **per node** (§7.4) | **Confirmed**, new this revision — this is the main fallback system this update adds. |
| Scene setup | N/A (always runs) | N/A — §9's sky/lighting actors are themselves the fallback for "black level by default," added unconditionally on first fetch | Already the design — listed here for completeness. |

**What's genuinely new in this table vs. before:** rows 2 (manual-upload safety net) and 6-7 (procedural fallback as a permanent, per-node path) are new decisions from this session. The rest were either already implemented in Phases 1-4 or already documented as risk mitigations in plan.md — restated here so "fallback coverage" is auditable in one place rather than scattered.

---

## 11. Open items — need your input before or during implementation

These didn't have a clean single answer in the Q&A and are called out rather than silently decided:

1. **WebBrowser widget platform reliability** — worth a short spike before committing to the embedded-map approach (§3.2).
2. **Exact min/max constraint fields per category** — you asked for "as much control as possible" but the concrete field list (height clamp? road width variance? setback distance?) depends on what the mesh library needs. Suggest defining this once the first style-set Data Asset is drafted, not speculatively now.
3. **Medium region Overpass chunking strategy** — single query vs. tiled sub-queries; affects fetch stage complexity. Worth deciding once you've tried a real Medium-sized region and see whether Overpass free tier handles it in one call.

(The OpenTopography API key is *not* in this list — see the blocking-prerequisite callout in §4.1. It needs to be resolved before implementation reaches auto-fetch, not during it.)

1. **WebBrowser widget platform reliability** — worth a short spike before committing to the embedded-map approach (§3.2).
2. **Exact min/max constraint fields per category** — you asked for "as much control as possible" but the concrete field list (height clamp? road width variance? setback distance?) depends on what the mesh library needs. Suggest defining this once the first style-set Data Asset is drafted, not speculatively now.
3. **Medium region Overpass chunking strategy** — single query vs. tiled sub-queries; affects fetch stage complexity. Worth deciding once you've tried a real Medium-sized region and see whether Overpass free tier handles it in one call.
4. **Default parametrized fallback materials (§7.4)** — who authors the small set of default facade/road/foliage Material Instances the procedural branch uses? This plan assumes a simple hand-authored set (a handful of Material Instances with color/tiling params), not a generated/procedural material graph — flag if you want something more elaborate here.

---

## 12. Suggested implementation order (revised — fallback PCG first)

Given "layer, don't break Phases 1-4," and your call that the procedural fallback should be built **before** the graph-node/ratio-config UI, since without a library almost everything routes through fallback anyway:

1. **Scene auto-setup** (§9) — small, immediately visible fix for the black-level problem, no dependency on anything else.
2. **Procedural PCG fallback, one category at a time — Buildings, then Roads, then Vegetation** (§7.4) — built and tested against the pipeline that already works today (existing parse → classify → CRS transform → manual-uploaded DEM/osm), *without* waiting on the graph-node stage, ratio UI, or auto-fetch subsystem. Buildings first (simplest category, existing extruder already works), then Roads (validates the §7.3 continuity fix once graph nodes exist), then Vegetation (validates area-scatter, §8) — not all three in one pass. **Scope at this step:** this delivers the procedural geometry + varied-material generation logic itself, invoked directly against Stage 3 `FOSMFeature` output for every node — it is *not yet* gated by a per-node library-vs-fallback choice, since the yes/no branch in §7.4's diagram is keyed on `ResolvedStyleId`, which only exists once step 5 builds the `FOSMResolvedStyleTable`. Until then, every node simply goes procedural. This reuses `FOSMBuildingExtruder`/`FOSMRoadMeshBuilder` geometry as-is and adds the parametrized-material variation layer. Gives you real, visibly-varied generated output to look at soonest, and de-risks the PCG integration (the biggest technical unknown) before anything else is built on top of it.
3. **Region size gate + fetch/cache subsystem, with manual-upload fallback** (§3.1, §4) — replaces the manual bounds-matching pain; the manual-upload path from step 2 becomes the formal fallback (§10 table) rather than being retired.
4. **Graph node build** (§5) — data model + spline/outline visualization, reusing existing parse/classify/CRS output. Once this exists, stage 7 is refactored to consume `FOSMGraphNode`s instead of calling the fallback PCG graphs against raw features directly.
5. **Style-set Data Assets + ratio config UI** (§6) — layers library-vs-procedural *selection* per node on top of the fallback path already built and proven in step 2. Because the fallback already handles the "no entries" case, StyleSets can ship empty and still produce a fully generated region.
6. **Embedded map picker** (§3.2) — UI-only, doesn't block anything above; testable via the manual-coordinate-entry fallback (§10 table, row 1) in the meantime.

This order gets you a fully generated (if procedurally-plain) region as early as possible, proves out the riskiest piece (PCG) first, and only then layers in the workflow/UX changes (fetch, graph, config, map) around it.
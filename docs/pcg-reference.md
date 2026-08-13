# PCG Reference — UE 5.8

Grounded in the engine source shipped with this install:
`/Users/Shared/Epic Games/UE_5.8/Engine/Plugins/PCG/Source/PCG/Public/`

Every claim below was read out of those headers rather than recalled. Where the API changed
recently the deprecation is quoted, because the older form is what most tutorials still show and
following them on 5.8 produces deprecation warnings or wrong-class assumptions.

---

## 1. The point representation changed in 5.6

This is the single most important thing to know before writing any PCG integration on 5.8.

The old form was an array of `FPCGPoint` structs on `UPCGPointData`. The current form is a
**structure-of-arrays** exposed through `UPCGBasePointData`, with `UPCGPointArrayData` as the
concrete implementation.

`PCGPointData.h` carries deprecations from 5.6:

```cpp
struct PCG_API UE_DEPRECATED(5.6, "Use PCGPointOctree::FPointRef instead") FPCGPointRef
UE_DEPRECATED(5.6, "Use GetPointOctree instead")
```

**Do not allocate `UPCGPointData` directly.** Which concrete class is correct is decided by a
CVar, so the engine provides a factory (`PCGContext.h`):

```cpp
static UPCGBasePointData* NewPointData_AnyThread(FPCGContext* Context);
```

### Writing points on 5.8

`UPCGBasePointData` (`PCGBasePointData.h`) works in value ranges, not per-point structs:

```cpp
virtual void SetNumPoints(int32 InNumPoints, bool bInitializeValues = true);
virtual void AllocateProperties(EPCGPointNativeProperties Properties);
FPCGPointTransform::ValueRange GetTransformValueRange(bool bAllocate = true);
template<EPCGPointNativeProperties Property> TPCGValueRange<T> GetValueRange(...);
```

Ordering is load-bearing, and the header says so explicitly:

> Calling SetNumPoints/AllocateProperties/FreeProperties can and will probably invalidate ranges
> so make sure that you do those operations first or that you get a new range after you do.

So: **size first, allocate second, fetch ranges third, write last.** A range fetched before a
resize is a dangling view.

### Per-point native properties

From `PCGPoint.h`, the fields a point carries:

| Field | Type | Default |
|---|---|---|
| `Transform` | `FTransform` | identity |
| `Density` | `float` | `1.0f` |
| `BoundsMin` | `FVector` | `-FVector::One()` |
| `BoundsMax` | `FVector` | `FVector::One()` |
| `Color` | `FVector4` | `FVector4::One()` |
| `Steepness` | `float` | `0.5f` |
| `Seed` | `int32` | `0` |
| `MetadataEntry` | `int64` | `-1` |

`Seed` being per-point matters for us: it is where a deterministic per-node seed belongs, so
downstream nodes randomise reproducibly instead of drawing from a global stream whose order
depends on evaluation scheduling.

---

## 2. Custom attributes

Anything beyond the native fields goes in metadata (`Metadata/PCGMetadata.h`):

```cpp
template<typename T>
FPCGMetadataAttribute<T>* CreateAttribute(
    FPCGAttributeIdentifier AttributeName, const T& DefaultValue,
    bool bAllowsInterpolation, bool bOverrideParent);
```

The header warns that `CreateAttribute` raises a warning if the attribute already exists, and
directs you to `FindOrCreateAttribute` when that can happen. For our bridge it can — a rebuild
reuses data — so **`FindOrCreateAttribute` is the correct call**, not `CreateAttribute`.

`bAllowsInterpolation` should be **false** for our attributes. Building height, road class and
subtype are categorical or discrete; interpolating them across points would invent values that
were never in the OSM data.

---

## 3. Data types available

From `Data/`, the ones relevant to a city graph:

| Type | Use for |
|---|---|
| `UPCGBasePointData` / `UPCGPointArrayData` | buildings, amenities, any per-instance placement |
| `UPCGSplineData` | roads, waterways, railways — anything linear |
| `UPCGPolyLineData` | base class for linear data |
| `UPCGPolygon2DData` | area footprints |
| `UPCGPolygon2DInteriorData` | filling an area — landuse, vegetation |
| `UPCGLandscapeData` | sampling the landscape surface |
| `UPCGDifferenceData` | exclusion — subtracting roads/water from a fill region |

The mapping from our graph is close to one-to-one:

| Graph node | PCG data |
|---|---|
| `Building` | point (transform at centroid) + polygon for footprint |
| `RoadSegment` | spline |
| `Waterway`, `Railway`, `Barrier`, `PowerLine` | spline |
| `WaterBody`, `VegetationArea`, `LanduseZone`, `LeisureArea` | polygon 2D |
| `Junction` | point |
| `TerrainTile` | bounds only; sampling goes through `UPCGLandscapeData` once a landscape exists |

---

## 4. Project constraints that apply to us

From `Plugins/pcg-skill.md` in this project, which is a standards document rather than an API
reference, but states constraints worth honouring:

- **Exclusion zones are mandatory.** PCG graphs must explicitly exclude roads, paths, water and
  hand-placed structures. Our graph already has the relationships needed to build them:
  `Contains` gives zone membership, and road/water nodes give the geometry to subtract.
- **Runtime generation only under 1 km².** Larger areas use pre-baked output for streaming
  compatibility. Our region cap is 25 km², so anything above ~1 km² must bake.
- **Nanite where eligible**, since PCG instance counts pass the threshold easily.
- Foliage tool is for hero placement only; large-scale population goes through PCG.

---

## 5. Consequences for the bridge (Phase 4)

1. **Target `UPCGBasePointData`, never `UPCGPointData`.** The concrete class is a CVar decision.
2. **Order operations**: `SetNumPoints` → `AllocateProperties` → get ranges → write.
3. **`FindOrCreateAttribute`**, with `bAllowsInterpolation = false`.
4. **Write a per-point `Seed`** derived from the node's stable id and the rule seed, so selection
   is reproducible and independent of evaluation order.
5. **Splines for linear features, polygons for areas.** Do not flatten a road to a point cloud —
   the continuity is the whole reason corridors exist as a grouping.
6. **Exclusions come from the graph**, not from a hand-drawn volume.

---

## 6. What is deliberately not here

No PCG *graph assets* are authored by this plugin, and no runtime PCG component is configured.
Phase 4 produces the **data** and a **dry run** describing what would be generated; wiring that
into a PCG graph is Phase 5. Documenting an API we have not exercised yet would be recollection
dressed as reference, which is the thing this file exists to avoid.

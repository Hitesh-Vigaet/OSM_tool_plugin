# City Asset Brief — OSM World Generator

Assets required for procedural city generation from OpenStreetMap data. Target: Unreal Engine 5.8.

Quantities and priorities are **measured** from two real 1 km² Bangalore regions (UB City,
Indiranagar): 1,850 tagged buildings, 1,379 highway elements, 587 amenities, 382 natural features,
251 barriers. They are not estimates.

---

## Read this first: what we are NOT asking for

**Do not model whole buildings.** Footprints in the data range from **19.5 m² to 7,814 m²** — a
401× spread — with aspect ratios up to 6.1. No library of building meshes survives that range;
scaling one to fit produces huts and apartment blocks made of the same stretched geometry, with
windows scaled to nonsense.

Instead:

| | |
|---|---|
| **Buildings are generated** | The engine extrudes each building from its own footprint to its own height, then applies a *style recipe*. Walls subdivide into fixed-width bays, so window spacing stays constant at any size. |
| **You supply the parts** | Tiling **materials** for flat surfaces, and small **meshes** that slot onto generated walls — windows, doors, balconies, rooftop clutter — plus everything that stands on the ground. |

**The dividing line:** if it has real depth you would notice from the pavement, it is a **mesh**.
If it is flat detail a normal map and parallax can carry, it is a **material**. A window *frame* is
a mesh; the *brick around it* is a material.

---

## Why these priorities — what the data contains

| Feature | Count | Share |
|---|---:|---|
| Buildings, untyped (`building=yes`) | 1,393 | 75.3% of buildings |
| Footways | 370 | 26.8% of highway |
| Trees, individually mapped | 333 | 87.2% of natural |
| Street lamps | 297 | 21.5% of highway |
| Restaurants / cafés / fast food | 190 | 32.4% of amenity |
| Commercial buildings | 132 | 7.1% of buildings |
| Kerbs | 119 | 47.4% of barrier |
| Gates | 99 | 39.4% of barrier |
| Houses | 95 | 5.1% of buildings |
| Apartments | 79 | 4.3% of buildings |
| Benches | 68 | 11.6% of amenity |
| Traffic signals | 29 | 2.1% of highway |
| Bus stops | 27 | 2.0% of highway |

**Two findings that shape this brief:**

1. **75% of buildings carry no type at all.** The generic "urban Indian building" archetype set
   matters more than any specialty set. Do not over-invest in hospital or station variants.
2. **Only 21% record their floor count.** Most heights will be inferred, so facades must read
   correctly at a height nobody authored.

---

## Deliverable 1 — Materials

Seamless, tileable, real-world scale. Each needs base colour, normal, roughness, ambient occlusion.

### Facade & wall

| Material | Variants | Priority | Notes |
|---|---|---|---|
| Painted plaster / render | 10–14 | **P0** | The dominant Indian facade. White, cream, ochre, terracotta, teal, pink, faded pastels. Clean and weathered pairs. |
| Concrete, fair-faced | 3–4 | **P0** | Board-formed and smooth. Staining and water streaks matter more than pristine. |
| Exposed brick | 3 | **P0** | Red clay, wire-cut, painted-over-brick. |
| Ceramic / vitrified tile cladding | 4–6 | **P0** | Very common on Indian mid-rise frontages. Glossy and matte. |
| Glass curtain wall | 4 | **P0** | Blue-green, grey, bronze tints; mullion spacing baked into the normal. |
| Granite / stone cladding | 3 | P1 | Commercial plinths and entrance surrounds. |
| Corrugated metal sheet | 2–3 | P1 | Industrial, temporary structures, rear extensions. |
| Grime / weathering overlay | 4 | P1 | Blendable: monsoon streaking, algae at the base, dust. Shader-applied over any facade. |

### Roof

| Material | Variants | Priority | Notes |
|---|---|---|---|
| RCC flat roof | 3 | **P0** | Weathering-course tiles, bare concrete, tar-coated. The default here. |
| Clay tile, pitched | 2–3 | P1 | Mangalore tile pattern, houses and older stock. |
| Metal sheet roof | 2 | P1 | Corrugated and standing-seam. |

### Road & ground

One style per road class — variety here is noise, not realism.

| Material | Variants | Priority | Notes |
|---|---|---|---|
| Asphalt | 3 | **P0** | New, worn, patched-and-repaired. Patched is the most useful. |
| Paver block | 2–3 | **P0** | Interlocking — near-universal on Indian footpaths and service roads. |
| Cement footpath tile | 2 | **P0** | 370 footways in the sample. Include tactile paving. |
| Road markings (decal set) | 1 set | **P0** | Lane lines, centre lines, zebra, stop line, arrows, yellow box, parking bays. 162 crossings mapped. |
| Concrete road | 2 | P1 | Panelled, with expansion joints. |
| Gravel / unpaved | 2 | P1 | Service tracks, construction access. |
| Bare earth / dirt | 2 | P1 | Verges, unfinished plots. |

### Terrain & landscape

| Material | Variants | Priority | Notes |
|---|---|---|---|
| Grass, maintained | 2 | **P0** | Parks, gardens, pitches. |
| Grass, dry / patchy | 2 | **P0** | Bangalore's default outside monsoon. |
| Soil / bare ground | 2 | P1 | Blend layer under vegetation scatter. |
| Scrub ground | 1–2 | P2 | Wasteland, undeveloped plots. |
| Sports surface | 2 | P2 | 24 pitches mapped — synthetic turf, red clay. |

### Water

| Material | Variants | Priority | Notes |
|---|---|---|---|
| Still water, lake | 1 | P1 | Green-brown, low clarity. Not tropical blue. |
| Drain / stormwater channel | 1 | P1 | All 15 waterways in the sample are drains. Murky, with debris. |
| Swimming pool | 1 | P2 | 9 mapped. Tiled bottom, clean. |

### Shared / prop materials

Applied across many small meshes — author once, reuse everywhere.

| Material | Variants | Priority | Notes |
|---|---|---|---|
| Painted metal | 4 | **P0** | Railings, gates, poles, shutters. Include chipped and rusted. |
| Galvanised / bare metal | 2 | **P0** | Ducting, tanks, brackets. |
| Glass, clear & tinted | 3 | **P0** | Windows, shopfronts, vehicle glazing. |
| Printed signage atlas | 1 set | **P0** | Shop boards and hoardings, Kannada and English. Highly characteristic — do not skip. |
| Plastic | 3 | P1 | Water tanks, bins, chairs, signage. |
| Wood | 3 | P1 | Doors, benches, stall framing. |
| Fabric / tarpaulin | 3 | P1 | Awnings, stall covers, sheeting. Blue tarp is characteristic. |

---

## Deliverable 2 — Meshes

Modular parts that snap onto generated walls, plus props that stand on the ground. All instanced —
keep them tight and reusable.

### Facade kit — placed per bay

A bay is a fixed slice of wall, nominally **3 m** wide. Author to the bay, not to a building.

| Mesh | Variants | Priority | Notes |
|---|---|---|---|
| Window, casement | 5–6 | **P0** | With frame depth and reveal. Square through to tall proportions. |
| Window, sliding | 4 | **P0** | Aluminium and UPVC. Most common on newer Indian construction. |
| Window grille / security bars | 6–8 | **P0** | Near-universal here and a huge character cue. Plain bars to decorative ironwork. |
| **Chajja (concrete sunshade)** | 3 | **P0** | Projecting slab over windows. Defining local feature — its shadow line is what makes a facade read as Indian. |
| Shopfront, glazed | 4 | **P0** | Ground floor only. Full-height glass with signage band above. |
| Rolling shutter | 3 | **P0** | Open, half-open, closed. Essential — most shops show one. |
| Door, residential | 4 | **P0** | Single and double leaf, with frame. |
| Balcony, cantilever | 5–6 | **P0** | Slab plus railing: MS railing, glass panel, masonry parapet. |
| Parapet / roof edge | 3 | **P0** | Caps every flat roof. Plain, railed, perforated block. |
| Ventilator / louvre | 3 | P1 | Small high-set openings — bathrooms, stairwells. |
| Door, glass entrance | 2 | P1 | Commercial and office lobbies. |
| Plinth & entrance steps | 3 | P1 | Buildings sit above pavement level; this is the join. |
| Drainpipe & downspout | 2 | P1 | Full height. Cheap, and its absence is noticeable. |
| Electrical meter box & conduit | 3 | P1 | Exterior-mounted, ground floor. Surface cabling is normal here. |
| Corner & column trim | 2–3 | P2 | Pilasters and quoins for older stock. |

### Rooftop clutter

Flat roofs are the default, and an empty one reads as unfinished from any aerial view.

| Mesh | Variants | Priority | Notes |
|---|---|---|---|
| **Overhead water tank** | 4–5 | **P0** | Black plastic tank on a stand. The single most recognisable Indian rooftop object. |
| Staircase headroom box | 3 | **P0** | The structure over the stairwell. On nearly every flat roof. |
| Split AC outdoor unit | 3 | **P0** | Also wall-mounted on facades, not just roofs. |
| Solar water heater | 2 | P1 | Panel plus cylinder. Very common in Bangalore. |
| Satellite dish | 2 | P1 | Wall and roof mounted. |
| Lift machine room | 2 | P1 | Taller buildings only. |
| Clothesline & laundry | 3 | P1 | Adds life. Cloth can be simple cards. |
| Vent / exhaust cowl | 3 | P2 | Small scatter dressing. |
| Rooftop HVAC plant | 3 | P2 | Commercial and office only. |

### Street furniture & infrastructure

Counts are mapped instances — these are placed from real data, not scattered.

| Mesh | Variants | Priority | Notes |
|---|---|---|---|
| Street lamp | 4–5 | **P0** | 297 mapped. Single-arm, double-arm, highway mast, decorative park type. |
| Kerb profile | 3 | **P0** | 119 mapped. Standard, dropped, painted. Runs along every road edge. |
| Gate | 6–8 | **P0** | 99 mapped. Sliding, swing, ornamental ironwork, lift-gate barriers. |
| Compound wall | 4 | **P0** | Modular runs. Plain plaster, brick, railing-topped, glass shards on top. |
| Bench | 3 | **P0** | 68 mapped. |
| Bus shelter | 2–3 | **P0** | 27 mapped. Includes ad panel. |
| Traffic signal | 3 | **P0** | 29 mapped. Pole, mast arm, pedestrian unit. |
| **Utility pole & cabling** | 4 | **P0** | Overhead cable runs with visible sag. Defining feature of Indian streets. |
| Hoarding / billboard | 4 | **P0** | Large format. Extremely prominent in Bangalore. |
| Shop signboard | 6–8 | **P0** | Above shopfronts. Pairs with the signage atlas. |
| Waste bin | 3 | P1 | 29 mapped. Municipal and commercial. |
| Road sign set | 8–10 | P1 | Regulatory, directional, street name boards. |
| Bollard | 3 | P1 | Lane separators, pavement protection. |
| Distribution transformer | 2 | P1 | Pole-mounted and ground-mounted with cage. |
| Electrical junction box | 3 | P1 | Pavement-mounted. |
| Manhole & drain cover | 3 | P1 | Can be decals where flush. |
| Awning / canopy | 4 | P1 | Fabric and metal, over shopfronts. |
| ATM booth | 2 | P2 | 20 mapped. |
| Metro entrance | 2 | P2 | 8 mapped. Canopy and stair opening. |

### Vegetation

333 trees are individually mapped with real positions — placed, not scattered.

| Mesh | Variants | Priority | Notes |
|---|---|---|---|
| Street tree, broadleaf | 6–8 | **P0** | Rain tree, gulmohar, neem, peepal, tabebuia. Two or three sizes each. |
| Shrub & hedge | 5 | **P0** | Scattered inside vegetation and garden areas. |
| Palm | 3 | P1 | Coconut and areca. |
| Sapling / young tree | 3 | P1 | 26 mapped explicitly. |
| Grass clump | 4 | P1 | Density scatter over ground materials. |
| Potted plant | 4 | P2 | Balconies, entrances, rooftops. |

### Street life

Not in the OSM data, but scattered along roads and in parking areas. This is what separates a
model of a city from a city.

| Mesh | Variants | Priority | Notes |
|---|---|---|---|
| Car, parked | 6–8 | **P0** | Hatchback-heavy mix, matching the local fleet. |
| Two-wheeler | 5–6 | **P0** | Scooters and motorcycles, parked in rows. Extremely common. |
| Auto-rickshaw | 2–3 | **P0** | Yellow-green. Instantly reads as Indian. |
| Street vendor cart | 4 | P1 | Fruit cart, tea stall, snack cart. |
| Temporary stall | 3 | P1 | Tarpaulin and pole framing. |
| Plastic chair & table | 3 | P1 | Outside cafés and tea stalls. 190 food amenities mapped. |
| Rubble & debris pile | 4 | P1 | Construction spill, sand heaps. 14 construction sites mapped. |
| Construction barricade | 3 | P2 | Metal sheet and striped barriers. |
| Playground equipment | 4 | P2 | 3 playgrounds mapped. |
| Sports goal & net | 3 | P2 | 24 pitches mapped. |

---

## Technical requirements (non-negotiable)

Assets that miss these will not slot into generated geometry.

| Requirement | Specification |
|---|---|
| **Units** | Centimetres. `1 uu = 1 cm`. Real-world scale, no exceptions — the generator sizes from measured footprints. |
| **Up axis** | Z-up, X-forward on export. |
| **Pivot — facade parts** | Bottom-centre of the bay face, on the wall plane, so parts sit flush when placed. |
| **Pivot — ground props** | Base centre, resting on `Z = 0`. |
| **Bay width** | Facade parts author to a `3 m` nominal bay, and must tolerate ±15% squash without visible distortion. |
| **Floor height** | `3.2 m` nominal. Windows and chajjas must sit correctly within it. |
| **Texel density** | `512 px/m` for facade parts and hero props; `256 px/m` for background and scatter. |
| **Tiling materials** | Seamless both axes, authored at `2×2 m` or `4×4 m` real-world coverage. State coverage in the filename. |
| **UVs** | UV0 unwrapped, no overlap for opaque. UV1 lightmap only where the asset is not Nanite. |
| **Nanite** | Enable wherever eligible. Instance counts run to tens of thousands, well past the threshold. |
| **LODs** | Only for non-Nanite assets — foliage and anything with translucency. Three levels. |
| **Material slots** | One per distinct material, kept to a minimum. Instanced props should ideally use a single slot. |
| **Collision** | Simple primitives on ground props. None on facade parts — the building shell carries it. |

**Naming:** `SM_[Category]_[Item]_[Variant]` for meshes, `M_[Category]_[Item]_[Variant]` for
materials, `T_[Name]_[BC|N|ORM]` for textures.
Example: `SM_Facade_WindowGrille_04`, `M_Wall_PlasterPainted_Ochre`.

**Variants of one item must be interchangeable** — same pivot, same footprint, same slot count —
because the generator swaps between them by ratio without inspecting the mesh.

---

## Explicitly out of scope

Time spent on these is wasted — the generator produces them, or does not use them.

| Do not build | Why |
|---|---|
| **Whole buildings** | Generated from footprint and height. A modelled building cannot fit a footprint it was not modelled for. |
| **Road meshes** | Swept along the real spline at the real width. Supply the surface material only. |
| **Terrain meshes** | Built from elevation data. Supply ground materials only. |
| **Pre-assembled facades** | Assembled per bay at generation time. Supply the individual parts. |
| **Site-specific landmarks** | Not yet — the pipeline places nothing by name. Revisit once hero buildings are on the roadmap. |
| **Interiors** | Out of scope entirely. Windows can be opaque with an interior-parallax material. |

---

*Variant counts are recommendations. The generator accepts any number per category and selects by
configurable ratio, so the list can be delivered in waves — P0 first.*

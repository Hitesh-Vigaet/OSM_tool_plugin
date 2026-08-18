#!/usr/bin/env python3
"""Render a plan view of the geometry the world builder actually emitted.

Reads the JSON Lines dump written by FOSMGeometryDump and produces a top-down PNG plus a
correctness report. The point is to make generation defects visible in seconds without opening
the editor: coverage holes, footprints that are not closed, surfaces facing the wrong way,
buildings sitting in roads.

    python3 Tools/plan_view.py <dump.jsonl> -o plan.png

Reads the FINAL geometry, not the source OSM. A renderer fed from the .osm file would only
confirm the .osm file is fine, which is not where the defects have been.
"""

import argparse
import json
import math
import sys
from collections import defaultdict

from PIL import Image, ImageDraw

# Matches the grey-box palette in FOSMWorldBuilder.cpp, so the plan view and the viewport agree.
COLOURS = {
    "terrain":    ( 97,  86,  68),
    "landuse":    ( 74,  66,  86),
    "vegetation": ( 35,  81,  37),
    "leisure":    ( 53,  97,  61),
    "paved":      ( 58,  55,  55),
    "civic":      ( 62,  70,  74),
    "water":      ( 16,  52, 108),
    "rail":       ( 53,  45,  40),
    "road":       ( 35,  35,  40),
    "building":   (114, 111, 106),
}

# Painted back to front, matching the lift order in FOSMWorldBuilder.
ORDER = ["terrain", "landuse", "vegetation", "leisure", "civic", "paved", "water", "rail", "road", "building"]

BUILDING_OUTLINE = (70, 66, 62)


def load(path):
    header, meshes = None, []
    with open(path, "r") as handle:
        for line_no, line in enumerate(handle, 1):
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError as exc:
                # A truncated final line is expected if the build crashed; that dump is still
                # worth rendering, so warn rather than fail.
                print(f"warning: line {line_no} is not valid JSON ({exc}); stopping here",
                      file=sys.stderr)
                break
            if obj.get("kind") == "header":
                header = obj
            else:
                meshes.append(obj)
    return header, meshes


def world_vertices(mesh):
    """Local vertices plus the actor location, in cm."""
    lx, ly, lz = mesh["loc"]
    flat = mesh["v"]
    return [(flat[i] + lx, flat[i + 1] + ly, flat[i + 2] + lz) for i in range(0, len(flat), 3)]


def check_closure(mesh, verts):
    """Is this mesh a closed solid?

    A watertight surface has every undirected edge shared by exactly two triangles. A box whose
    walls are missing, doubled or inside-out fails this, which is precisely the "buildings look
    like 2 or 3 loose panels" symptom — stated as a property that can be measured rather than
    a matter of opinion.

    Vertices are welded by position first: the mesher emits each wall quad with its own corner
    vertices, so adjacent walls share an edge geometrically but not by index.
    """
    tris = mesh["t"]
    if not tris:
        return None

    weld, key_of = {}, []
    for v in verts:
        key = (round(v[0], 1), round(v[1], 1), round(v[2], 1))
        key_of.append(weld.setdefault(key, len(weld)))

    edges = defaultdict(int)
    for i in range(0, len(tris), 3):
        a, b, c = (key_of[tris[i]], key_of[tris[i + 1]], key_of[tris[i + 2]])
        for u, v in ((a, b), (b, c), (c, a)):
            edges[(min(u, v), max(u, v))] += 1

    boundary = sum(1 for n in edges.values() if n == 1)
    nonmanifold = sum(1 for n in edges.values() if n > 2)
    return {"edges": len(edges), "boundary": boundary, "nonmanifold": nonmanifold}


def normal_stats(mesh, verts):
    """Sign of each triangle's geometric normal, for spotting inconsistent winding.

    Reports how many horizontal faces point up versus down. A ground surface with a mix of both
    is wound inconsistently, whatever the engine's convention turns out to be.
    """
    tris = mesh["t"]
    up = down = 0
    for i in range(0, len(tris), 3):
        p0, p1, p2 = verts[tris[i]], verts[tris[i + 1]], verts[tris[i + 2]]
        ux, uy, uz = (p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2])
        vx, vy, vz = (p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2])
        nz = ux * vy - uy * vx
        nx = uy * vz - uz * vy
        ny = uz * vx - ux * vz
        # Only near-horizontal faces carry a meaningful up/down answer.
        if abs(nz) > 0.7 * math.sqrt(nx * nx + ny * ny + nz * nz + 1e-12):
            if nz > 0:
                up += 1
            else:
                down += 1
    return up, down


def render(header, meshes, out_path, size, report):
    # World bounds from the geometry itself, not the region: geometry escaping the region is one
    # of the things worth seeing.
    minx = miny = float("inf")
    maxx = maxy = float("-inf")

    prepared = []
    for mesh in meshes:
        verts = world_vertices(mesh)
        if not verts:
            continue
        prepared.append((mesh, verts))
        for x, y, _ in verts:
            minx, maxx = min(minx, x), max(maxx, x)
            miny, maxy = min(miny, y), max(maxy, y)

    if not prepared:
        print("nothing to draw: the dump contains no geometry", file=sys.stderr)
        return None

    pad = 0.02 * max(maxx - minx, maxy - miny, 1.0)
    minx, maxx = minx - pad, maxx + pad
    miny, maxy = miny - pad, maxy + pad

    # Unreal X is north and Y is east, so east goes across the image and north up it — a map
    # orientation, which is how the result will be compared against a real one.
    span = max(maxx - minx, maxy - miny, 1.0)
    scale = size / span

    def project(x, y):
        return ((y - miny) * scale, size - (x - minx) * scale)

    image = Image.new("RGB", (size, size), (26, 25, 24))
    draw = ImageDraw.Draw(image, "RGBA")

    by_kind = defaultdict(list)
    for mesh, verts in prepared:
        by_kind[mesh["kind"]].append((mesh, verts))

    for kind in ORDER + sorted(k for k in by_kind if k not in ORDER):
        colour = COLOURS.get(kind, (200, 60, 200))   # magenta flags an unexpected kind
        for mesh, verts in by_kind.get(kind, []):
            tris = mesh["t"]
            for i in range(0, len(tris), 3):
                poly = [project(verts[tris[i + k]][0], verts[tris[i + k]][1]) for k in range(3)]
                draw.polygon(poly, fill=colour)

        # Building outlines drawn after the fills, so individual footprints stay separable where
        # they abut — a terrace otherwise reads as one undifferentiated mass.
        #
        # Only the silhouette: edges shared by two triangles are interior to the roof's
        # triangulation and mean nothing on a plan. Drawing every edge covers each building in
        # spokes radiating from whichever vertex the triangulator fanned from, which looks like a
        # defect and hides the real one.
        if kind == "building":
            for mesh, verts in by_kind.get(kind, []):
                tris = mesh["t"]
                seen = defaultdict(int)
                for i in range(0, len(tris), 3):
                    p0, p1, p2 = verts[tris[i]], verts[tris[i + 1]], verts[tris[i + 2]]
                    ux, uy, uz = (p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2])
                    vx, vy, vz = (p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2])
                    if (ux * vy - uy * vx) <= 0:
                        continue   # roof triangles only; walls and floor add nothing to a plan
                    for u, v in ((tris[i], tris[i + 1]), (tris[i + 1], tris[i + 2]),
                                 (tris[i + 2], tris[i])):
                        pu = (round(verts[u][0], 1), round(verts[u][1], 1))
                        pv = (round(verts[v][0], 1), round(verts[v][1], 1))
                        if pu != pv:
                            seen[(min(pu, pv), max(pu, pv))] += 1
                # An edge on exactly one roof triangle is on the footprint boundary; anything
                # shared is interior to the triangulation and is not part of the outline.
                for (pu, pv), count in seen.items():
                    if count == 1:
                        draw.line([project(*pu), project(*pv)], fill=BUILDING_OUTLINE + (150,), width=1)

    image.save(out_path)

    report["bounds_m"] = {
        "north_south": round((maxx - minx) / 100.0, 1),
        "east_west": round((maxy - miny) / 100.0, 1),
    }
    return out_path


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dump")
    parser.add_argument("-o", "--out", default="plan.png")
    parser.add_argument("-s", "--size", type=int, default=2000, help="image edge in pixels")
    args = parser.parse_args()

    header, meshes = load(args.dump)
    if not meshes:
        print("dump contains no meshes", file=sys.stderr)
        return 1

    counts = defaultdict(int)
    for mesh in meshes:
        counts[mesh["kind"]] += 1

    report = {"counts": dict(counts)}

    # ---- correctness checks ----
    open_buildings, worst = 0, []
    for mesh in meshes:
        if mesh["kind"] != "building":
            continue
        verts = world_vertices(mesh)
        result = check_closure(mesh, verts)
        if result and (result["boundary"] or result["nonmanifold"]):
            open_buildings += 1
            worst.append((result["boundary"], mesh["label"], result))

    flip = defaultdict(lambda: [0, 0])
    for mesh in meshes:
        if mesh["kind"] == "building":
            continue
        up, down = normal_stats(mesh, world_vertices(mesh))
        flip[mesh["kind"]][0] += up
        flip[mesh["kind"]][1] += down

    render(header, meshes, args.out, args.size, report)

    print("=" * 62)
    if header:
        print(f"region  {header['minLat']:.5f},{header['minLon']:.5f} -> "
              f"{header['maxLat']:.5f},{header['maxLon']:.5f}   [{header.get('notes','')}]")
    print(f"extent  {report['bounds_m']['east_west']:.0f} m E-W x "
          f"{report['bounds_m']['north_south']:.0f} m N-S")
    print("-" * 62)
    for kind in ORDER + sorted(k for k in counts if k not in ORDER):
        if counts.get(kind):
            print(f"  {kind:<12} {counts[kind]:>6}")
    print("-" * 62)

    total_buildings = counts.get("building", 0)
    if total_buildings:
        print(f"closure   {total_buildings - open_buildings}/{total_buildings} buildings are "
              f"closed solids")
        for boundary, label, result in sorted(worst, reverse=True)[:5]:
            print(f"    {label:<22} {boundary} open edges, "
                  f"{result['nonmanifold']} non-manifold, {result['edges']} total")

    # Unreal draws a face when its geometric normal points AWAY from the camera, so a ground
    # surface must be wound for a -Z normal to be visible from above. Anything counted as "+Z" is
    # a surface that will be silently culled.
    print("winding   horizontal ground faces (must be -Z to be visible from above):")
    for kind, (up, down) in sorted(flip.items()):
        verdict = "ok" if up == 0 else f"{up} FACE THE WRONG WAY and will not draw"
        print(f"    {kind:<12} {down:>7} visible  {up:>7} culled   {verdict}")

    print("=" * 62)
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

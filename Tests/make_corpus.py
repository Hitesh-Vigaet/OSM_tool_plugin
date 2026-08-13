#!/usr/bin/env python3
"""
Generates the Phase 1.5 regression corpus in Tests/Data/.

Every file here reproduces a failure that actually occurred during development, so the
validation gates are proven against data that genuinely broke something rather than against
synthetic cases someone imagined. Where a real file existed it is trimmed rather than
invented; where the original was lost (the cached DEMs live under Saved/, which is not
version-controlled) the TIFF is written byte-by-byte to reproduce the exact structure that
failed.

Run from the plugin root:  python3 Tests/make_corpus.py [path-to-RegionCache]

Regenerating is safe and deterministic: the same inputs always produce the same bytes.
"""

import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "Data")

# Region the OSM fixtures are built around (Bangalore / Jakkur, where the failures occurred).
REGION = dict(min_lat=12.9700, max_lat=12.9800, min_lon=77.6020, max_lon=77.6120)


# ---------------------------------------------------------------------------
# OSM fixtures
# ---------------------------------------------------------------------------

def read_elements(path, max_ways, bounds=None):
    """
    Pull a bounded subset of ways, plus exactly the nodes they reference.

    Ways are selected first and nodes follow from them. Capping nodes first — the obvious
    order — leaves every way referencing at least one node that did not make the cut, so the
    "clean" fixture would carry dangling references and be indistinguishable from the fixture
    that is supposed to be the only one with them.
    """
    all_nodes = {}
    all_ways = []

    with open(path, encoding="utf-8", errors="replace") as handle:
        current_way = None
        for line in handle:
            node_match = re.search(r'<node id="(\d+)"[^>]*lat="([-\d.]+)"[^>]*lon="([-\d.]+)"', line)
            if node_match:
                all_nodes[int(node_match.group(1))] = (
                    float(node_match.group(2)), float(node_match.group(3)))
                continue

            if "<way " in line:
                way_match = re.search(r'<way id="(\d+)"', line)
                current_way = [int(way_match.group(1)), [], {}] if way_match else None
            elif current_way is not None and "<nd ref=" in line:
                current_way[1].append(int(re.search(r'ref="(\d+)"', line).group(1)))
            elif current_way is not None and "<tag " in line:
                tag_match = re.search(r'<tag k="([^"]*)" v="([^"]*)"', line)
                if tag_match:
                    current_way[2][tag_match.group(1)] = tag_match.group(2)
            elif current_way is not None and "</way>" in line:
                if len(current_way[1]) >= 2:
                    all_ways.append(tuple(current_way))
                current_way = None

    # Balanced across categories rather than "the first N ways".
    #
    # Taking the first N produced an all-roads fixture, which meant the FrontsOnto and Contains
    # tests ran against a graph containing no buildings and no zones — they passed without
    # exercising anything. A fixture has to contain the things the tests claim to check.
    quota_keys = ("building", "highway", "landuse", "leisure", "natural", "waterway", "amenity")
    per_category = max(1, max_ways // len(quota_keys))
    taken = {key: 0 for key in quota_keys}
    taken["other"] = 0

    def category_of(tags):
        for key in quota_keys:
            if key in tags:
                return key
        return "other"

    ways, used_ids = [], set()
    for way_id, refs, tags in all_ways:
        if len(ways) >= max_ways:
            break
        if not all(ref in all_nodes for ref in refs):
            continue
        # Both axes. Filtering latitude alone let ways through that sat outside the region's
        # longitude, so the import clipped them away and the fixture silently lost most of its
        # categories — the graph ended up with no buildings at all.
        if bounds and not all(
                bounds["min_lat"] <= all_nodes[ref][0] <= bounds["max_lat"]
                and bounds["min_lon"] <= all_nodes[ref][1] <= bounds["max_lon"]
                for ref in refs):
            continue

        category = category_of(tags)
        if category == "other" or taken[category] >= per_category:
            continue

        taken[category] += 1
        ways.append((way_id, refs, tags))
        used_ids.update(refs)

    nodes = [(node_id, all_nodes[node_id][0], all_nodes[node_id][1]) for node_id in sorted(used_ids)]
    return nodes, ways


def read_all_nodes(path):
    """Every node in a file, unfiltered — the extremes are the point, not a sample of them."""
    nodes = []
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = re.search(r'<node id="(\d+)"[^>]*lat="([-\d.]+)"[^>]*lon="([-\d.]+)"', line)
            if match:
                nodes.append((int(match.group(1)), float(match.group(2)), float(match.group(3))))
    return nodes


def write_osm(path, nodes, ways):
    """Ways are (id, node_refs, tags); tags are written verbatim so classification is real."""
    parts = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<osm version="0.6" generator="OSMWorldGen test corpus">',
    ]
    for node_id, lat, lon in nodes:
        parts.append(f'  <node id="{node_id}" lat="{lat:.7f}" lon="{lon:.7f}"/>')
    for way_id, refs, tags in ways:
        parts.append(f'  <way id="{way_id}">')
        parts.extend(f'    <nd ref="{ref}"/>' for ref in refs)
        for key, value in sorted(tags.items()):
            parts.append(f'    <tag k="{key}" v="{value}"/>')
        parts.append("  </way>")
    parts.append("</osm>")

    body = "\n".join(parts) + "\n"
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(body)
    return body


def build_osm_fixtures(cache_dir):
    small_source = os.path.join(cache_dir, "578f3a412446852a7f3e6a8af28663a9", "region.osm")
    wide_source = os.path.join(cache_dir, "59bf65474deb2a062e9411ca6e910a6d", "region.osm")

    # 1. Baseline: a clean, in-region file that must be accepted.
    nodes, ways = read_elements(small_source, max_ways=40, bounds=REGION)
    valid_body = write_osm(os.path.join(DATA, "valid_small.osm"), nodes, ways)

    # 2. The 40 km way, reproducing the file that produced a 500 km landscape.
    #
    #    The fixture must be the in-region data PLUS one escaping way, not the distant geometry
    #    on its own: a file located entirely elsewhere is correctly rejected as disjoint, which
    #    is a different defect from the one being tested. What broke generation was a file that
    #    legitimately covers the region and *also* contains one way running far outside it —
    #    Overpass returns the full geometry of any way merely overlapping the query box.
    #    Read every node rather than a category-quota'd subset: the fixture's whole purpose is
    #    the extreme span, and sampling would quietly shrink it.
    far_nodes = sorted(read_all_nodes(wide_source), key=lambda n: n[1])  # by latitude
    escaping = [far_nodes[0], far_nodes[len(far_nodes) // 2], far_nodes[-1]]

    oversized_nodes = list(nodes) + escaping
    oversized_ways = list(ways) + [(999999002, [n[0] for n in escaping], {"highway": "trunk"})]
    write_osm(os.path.join(DATA, "oversized_way.osm"), oversized_nodes, oversized_ways)

    span_km = (escaping[-1][1] - escaping[0][1]) * 111.32
    print(f"  oversized_way.osm     escaping way spans {span_km:.1f} km")

    # 3. UTF-16 body with a UTF-8 declaration. Parsers that trust the prolog produce garbage
    #    instead of failing, which is why this is detected from the bytes.
    with open(os.path.join(DATA, "utf16_mislabeled.osm"), "wb") as handle:
        handle.write(b"\xff\xfe" + valid_body.encode("utf-16-le"))

    # 4. Interrupted download: valid prefix, no closing tag.
    with open(os.path.join(DATA, "truncated.osm"), "w", encoding="utf-8", newline="\n") as handle:
        handle.write(valid_body[: len(valid_body) // 2])

    # 5. A way pointing at a node that is not in the file. Legitimate at a region boundary,
    #    so this must warn rather than reject.
    dangling_ways = list(ways[:-1]) + [(999999001, [nodes[0][0], 888888888], {"highway": "residential"})]
    write_osm(os.path.join(DATA, "dangling_refs.osm"), nodes, dangling_ways)

    # 6/7. Degenerate inputs that must fail cleanly rather than crash.
    open(os.path.join(DATA, "empty.osm"), "w").close()
    with open(os.path.join(DATA, "not_osm.osm"), "w", encoding="utf-8", newline="\n") as handle:
        handle.write('<?xml version="1.0" encoding="UTF-8"?>\n<html><body>Not OSM data at all.</body></html>\n')

    from collections import Counter
    mix = Counter(
        next((k for k in ("building", "highway", "landuse", "leisure", "natural", "waterway", "amenity")
              if k in tags), "other")
        for _, _, tags in ways)
    print(f"  valid_small.osm       {len(nodes)} nodes, {len(ways)} ways  {dict(mix)}")


# ---------------------------------------------------------------------------
# TIFF fixtures
#
# Written by hand rather than via a library so each fixture reproduces one exact structural
# defect. Layout: 8-byte header, IFD, then values and raster appended in a known order.
# ---------------------------------------------------------------------------

TAG_WIDTH, TAG_LENGTH, TAG_BITS = 256, 257, 258
TAG_COMPRESSION, TAG_STRIP_OFFSETS = 259, 273
TAG_SAMPLES_PER_PIXEL, TAG_ROWS_PER_STRIP, TAG_STRIP_BYTE_COUNTS = 277, 278, 279
TAG_TILE_WIDTH, TAG_TILE_LENGTH, TAG_TILE_OFFSETS, TAG_TILE_BYTE_COUNTS = 322, 323, 324, 325
TAG_SAMPLE_FORMAT = 339
TAG_PIXEL_SCALE, TAG_TIEPOINT = 33550, 33922

TYPE_SHORT, TYPE_LONG, TYPE_DOUBLE = 3, 4, 12


def build_tiff(path, width, height, *, compression=1, geotags=True,
               pixel_scale=None, tiled=False, tile_size=None, truncate_to=None,
               origin=None, elevation_base=900):
    """Assemble a minimal single-band 16-bit TIFF with the requested structure."""
    if pixel_scale is None:
        pixel_scale = (0.000277777777778, 0.000277777777778)  # 1 arc-second
    if origin is None:
        origin = (REGION["min_lon"] - 0.002, REGION["max_lat"] + 0.002)

    raster = bytearray()
    for row in range(height):
        for col in range(width):
            raster += struct.pack("<h", elevation_base + ((row * width + col) % 40))

    entries = [
        (TAG_WIDTH, TYPE_LONG, 1, width),
        (TAG_LENGTH, TYPE_LONG, 1, height),
        (TAG_BITS, TYPE_SHORT, 1, 16),
        (TAG_COMPRESSION, TYPE_SHORT, 1, compression),
        (TAG_SAMPLES_PER_PIXEL, TYPE_SHORT, 1, 1),
        (TAG_SAMPLE_FORMAT, TYPE_SHORT, 1, 2),
    ]

    # Values too large for the 4-byte inline field live in a trailing block.
    extra = bytearray()
    extra_base = 0  # patched once the header size is known

    def defer(payload):
        offset = len(extra)
        extra.extend(payload)
        return offset

    pixel_scale_off = defer(struct.pack("<3d", pixel_scale[0], pixel_scale[1], 0.0)) if geotags else None
    tiepoint_off = defer(struct.pack("<6d", 0.0, 0.0, 0.0, origin[0], origin[1], 0.0)) if geotags else None

    if tiled:
        tw, th = tile_size
        entries += [
            (TAG_TILE_WIDTH, TYPE_LONG, 1, tw),
            (TAG_TILE_LENGTH, TYPE_LONG, 1, th),
            (TAG_TILE_OFFSETS, TYPE_LONG, 1, 0),      # patched below
            (TAG_TILE_BYTE_COUNTS, TYPE_LONG, 1, len(raster)),
        ]
    else:
        entries += [
            (TAG_STRIP_OFFSETS, TYPE_LONG, 1, 0),     # patched below
            (TAG_ROWS_PER_STRIP, TYPE_LONG, 1, height),
            (TAG_STRIP_BYTE_COUNTS, TYPE_LONG, 1, len(raster)),
        ]

    if geotags:
        entries += [
            (TAG_PIXEL_SCALE, TYPE_DOUBLE, 3, 0),     # patched below
            (TAG_TIEPOINT, TYPE_DOUBLE, 6, 0),        # patched below
        ]

    entries.sort(key=lambda e: e[0])

    header_size = 8
    ifd_size = 2 + 12 * len(entries) + 4
    extra_base = header_size + ifd_size
    raster_offset = extra_base + len(extra)

    resolved = []
    for tag, typ, count, value in entries:
        if tag in (TAG_STRIP_OFFSETS, TAG_TILE_OFFSETS):
            value = raster_offset
        elif tag == TAG_PIXEL_SCALE:
            value = extra_base + pixel_scale_off
        elif tag == TAG_TIEPOINT:
            value = extra_base + tiepoint_off
        resolved.append((tag, typ, count, value))

    out = bytearray()
    out += b"II" + struct.pack("<HI", 42, header_size)
    out += struct.pack("<H", len(resolved))
    for tag, typ, count, value in resolved:
        if typ == TYPE_SHORT and count == 1:
            out += struct.pack("<HHI", tag, typ, count) + struct.pack("<HH", value, 0)
        else:
            out += struct.pack("<HHII", tag, typ, count, value)
    out += struct.pack("<I", 0)
    out += extra
    out += raster

    if truncate_to is not None:
        out = out[:truncate_to]

    with open(path, "wb") as handle:
        handle.write(bytes(out))
    return len(out)


def build_tiff_fixtures():
    # Covers the region with a margin, as a padded fetch would.
    build_tiff(os.path.join(DATA, "valid_striped.tif"), 48, 48)

    # Same raster, but too small to cover the region — exercises the cross-file coverage gate.
    build_tiff(os.path.join(DATA, "partial_coverage.tif"), 8, 8,
               origin=(REGION["min_lon"] + 0.004, REGION["max_lat"] - 0.004))

    # No georeferencing: the reader would otherwise guess an origin and place terrain
    # in the wrong part of the world.
    build_tiff(os.path.join(DATA, "no_geotags.tif"), 16, 16, geotags=False)

    # Compression the decoder cannot handle. 7 = JPEG.
    build_tiff(os.path.join(DATA, "jpeg_compressed.tif"), 16, 16, compression=7)

    # Pixel scale in metres — a projected CRS, which is out of scope and must be refused
    # rather than silently misplaced.
    build_tiff(os.path.join(DATA, "projected_crs.tif"), 16, 16, pixel_scale=(30.0, 30.0))

    # Multi-tile: the decoder reads only the first tile, so accepting this would yield
    # elevation for one corner and garbage elsewhere.
    build_tiff(os.path.join(DATA, "multitile.tif"), 64, 64, tiled=True, tile_size=(16, 16))

    # Interrupted download: header intact, raster missing.
    build_tiff(os.path.join(DATA, "truncated.tif"), 48, 48, truncate_to=200)

    # Not a TIFF at all.
    with open(os.path.join(DATA, "not_a_tiff.tif"), "wb") as handle:
        handle.write(b"This is plainly not a TIFF file.\n")


def main():
    cache_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser(
        "~/Desktop/Test_3d_project/Saved/OSMWorldGen/RegionCache")

    os.makedirs(DATA, exist_ok=True)
    print(f"Generating corpus in {DATA}")

    if os.path.isdir(cache_dir):
        build_osm_fixtures(cache_dir)
    else:
        print(f"  WARNING: no RegionCache at {cache_dir}; skipping OSM fixtures", file=sys.stderr)

    build_tiff_fixtures()

    for name in sorted(os.listdir(DATA)):
        print(f"  {name:26s} {os.path.getsize(os.path.join(DATA, name)):>8d} bytes")


if __name__ == "__main__":
    main()

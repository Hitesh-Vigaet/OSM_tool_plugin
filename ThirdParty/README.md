# Third-Party Dependencies

This plugin uses open-source libraries to handle OpenStreetMap binary (.osm.pbf) files and advanced GIS transformations.

## Libraries

| Library | Version | License | Required for | Included by default |
|---------|---------|---------|--------------|----------------------|
| **libosmium** | v2.20+ | Boost 1.0 | `.osm.pbf` binary parsing | Run `download_thirdparty.sh` |
| **protozero** | v1.7+ | BSD 2-Clause | libosmium PBF dependency | Run `download_thirdparty.sh` |
| **zlib** | Bundled | zlib license | PBF decompression | Included via UE Engine zlib |
| **expat** | Bundled | MIT | `.osm` XML parsing | Built-in UE FXmlFile fallback |

## How to Enable PBF Parsing (.osm.pbf)

libosmium and protozero are header-only libraries. To enable binary `.osm.pbf` file parsing:

### macOS / Linux:
Run the download script from this folder:
```bash
chmod +x download_thirdparty.sh
./download_thirdparty.sh
```

### Windows (PowerShell):
```powershell
git clone --depth 1 https://github.com/osmcode/libosmium.git libosmium
git clone --depth 1 https://github.com/mapbox/protozero.git protozero
```

Once the `libosmium/include` and `protozero/include` directories exist in `ThirdParty/`, Unreal Build Tool will automatically define `OSM_WITH_LIBOSMIUM=1` on the next build!

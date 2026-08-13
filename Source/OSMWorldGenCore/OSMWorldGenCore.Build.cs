// Copyright InviMind. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class OSMWorldGenCore : ModuleRules
{
    public OSMWorldGenCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        bEnableExceptions = true; // Required for libosmium parsing exceptions


        // ------------------------------------------------------------------
        // UE module dependencies
        // ------------------------------------------------------------------
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "GeometryCore",   // PolygonTriangulation, for building roofs from footprints
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Projects", // for IPluginManager
            "XmlParser", // for FXmlFile in FOSMXMLParser
        });

        // ------------------------------------------------------------------
        // ThirdParty: libosmium (header-only)
        // ------------------------------------------------------------------
        string ThirdPartyDir = Path.Combine(PluginDirectory, "ThirdParty");

        string OsmiumInclude = Path.Combine(ThirdPartyDir, "libosmium", "include");
        if (Directory.Exists(OsmiumInclude))
        {
            PublicIncludePaths.Add(OsmiumInclude);
            PublicDefinitions.Add("OSM_WITH_LIBOSMIUM=1");
        }
        else
        {
            PublicDefinitions.Add("OSM_WITH_LIBOSMIUM=0");
        }

        // ------------------------------------------------------------------
        // ThirdParty: protozero (header-only, libosmium PBF dependency)
        // ------------------------------------------------------------------
        string ProtozeroInclude = Path.Combine(ThirdPartyDir, "protozero", "include");
        if (Directory.Exists(ProtozeroInclude))
        {
            PublicIncludePaths.Add(ProtozeroInclude);
        }

        // ------------------------------------------------------------------
        // zlib: use UE's bundled version
        // ------------------------------------------------------------------
        AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");

        // ------------------------------------------------------------------
        // GDAL: optional, for advanced DEM/CRS support
        // ------------------------------------------------------------------
        string GDALInclude = Path.Combine(ThirdPartyDir, "gdal", "include");
        if (Directory.Exists(GDALInclude))
        {
            PublicIncludePaths.Add(GDALInclude);
            PublicDefinitions.Add("OSM_WITH_GDAL=1");

            // Platform-specific lib linking would go here
            if (Target.Platform == UnrealTargetPlatform.Win64)
            {
                string GDALLibDir = Path.Combine(ThirdPartyDir, "gdal", "lib", "Win64");
                PublicAdditionalLibraries.Add(Path.Combine(GDALLibDir, "gdal.lib"));
            }
            else if (Target.Platform == UnrealTargetPlatform.Mac)
            {
                string GDALLibDir = Path.Combine(ThirdPartyDir, "gdal", "lib", "Mac");
                PublicAdditionalLibraries.Add(Path.Combine(GDALLibDir, "libgdal.a"));
            }
        }
        else
        {
            PublicDefinitions.Add("OSM_WITH_GDAL=0");
        }
    }
}

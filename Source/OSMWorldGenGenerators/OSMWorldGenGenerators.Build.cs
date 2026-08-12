// Copyright InviMind. All Rights Reserved.

using UnrealBuildTool;

public class OSMWorldGenGenerators : ModuleRules
{
    public OSMWorldGenGenerators(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "OSMWorldGenCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UnrealEd",
            "Landscape",
            "LandscapeEditor",
            "PCG",
            "ProceduralMeshComponent",
            "GeometryScriptingCore",
            "GeometryFramework",
            "GeometryCore",
            "MeshDescription",
            "StaticMeshDescription",
            "InterchangeCore",
            "InterchangeEngine",
            "RenderCore",
        });
    }
}

// Copyright InviMind. All Rights Reserved.

using UnrealBuildTool;

public class OSMWorldGenEditor : ModuleRules
{
    public OSMWorldGenEditor(ReadOnlyTargetRules Target) : base(Target)
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
            "Slate",
            "SlateCore",
            "EditorFramework",
            "InputCore",
            "PropertyEditor",
            "ToolMenus",
            "Projects",
            "EditorStyle",
            "HTTP",
            "Json",
            "DeveloperSettings",
            "ApplicationCore",
            "ProceduralMeshComponent",  // grey-box city geometry
            "GeometryCore",             // PolygonTriangulation for area surfaces
        });
    }
}

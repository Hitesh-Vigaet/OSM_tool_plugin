// Copyright InviMind. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Lists the vector parameters engine materials expose, and what they are wired to.
 *
 * The grey-box pass tints one engine material per surface colour. Setting a vector parameter that
 * the material does not define, or that it defines but does not connect to base colour, fails
 * silently: the city renders in a single flat grey and nothing reports a problem. This is how that
 * gets checked against the actual install instead of assumed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMMaterialProbeTest,
    "OSMWorldGen.Diagnostics.MaterialProbe",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMMaterialProbeTest::RunTest(const FString& Parameters)
{
    const TCHAR* Candidates[] = {
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"),
        TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"),
        TEXT("/Engine/EngineDebugMaterials/DebugMeshMaterial.DebugMeshMaterial"),
        TEXT("/Engine/EngineDebugMaterials/LevelColorationUnlitMaterial.LevelColorationUnlitMaterial"),
        TEXT("/Engine/EngineDebugMaterials/M_SimpleOpaque.M_SimpleOpaque"),
    };

    for (const TCHAR* Path : Candidates)
    {
        UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, Path);
        if (!Material)
        {
            AddInfo(FString::Printf(TEXT("%s -> NOT FOUND"), Path));
            continue;
        }

        TArray<FMaterialParameterInfo> Infos;
        TArray<FGuid> Guids;
        Material->GetAllVectorParameterInfo(Infos, Guids);

        TArray<FString> Names;
        for (const FMaterialParameterInfo& Info : Infos)
        {
            Names.Add(Info.Name.ToString());
        }

        TArray<FMaterialParameterInfo> ScalarInfos;
        TArray<FGuid> ScalarGuids;
        Material->GetAllScalarParameterInfo(ScalarInfos, ScalarGuids);

        TArray<FString> ScalarNames;
        for (const FMaterialParameterInfo& Info : ScalarInfos)
        {
            ScalarNames.Add(Info.Name.ToString());
        }

        const UMaterial* Base = Material->GetMaterial();

        AddInfo(FString::Printf(TEXT("%s -> shading=%d  vectors=[%s]  scalars=[%s]"),
            *Material->GetName(),
            Base ? static_cast<int32>(Base->GetShadingModels().GetFirstShadingModel()) : -1,
            *FString::Join(Names, TEXT(", ")),
            *FString::Join(ScalarNames, TEXT(", "))));
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

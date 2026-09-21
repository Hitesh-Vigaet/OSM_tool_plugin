// Copyright InviMind. All Rights Reserved.

#include "Materials/FOSMThermalMaterialBuilder.h"
#include "Factories/MaterialFactoryNew.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/EOSMSurfaceCategory.h"
#include "Sensors/UOSMThermalMPC.h"

const FString& FOSMThermalMaterialBuilder::GetDefaultMaterialAssetPath()
{
    static const FString DefaultPath = TEXT("/OSMWorldGen/Materials/M_IRPostProcess.M_IRPostProcess");
    return DefaultPath;
}

UMaterialInterface* FOSMThermalMaterialBuilder::GetOrCreateThermalPostProcessMaterial(bool bForceRebuild)
{
    const FString& AssetPath = GetDefaultMaterialAssetPath();

    // 1. Try to load existing material asset if not forcing rebuild
    if (!bForceRebuild)
    {
        if (UMaterialInterface* ExistingMat = LoadObject<UMaterialInterface>(nullptr, *AssetPath))
        {
            return ExistingMat;
        }
    }

    // 2. Programmatically construct M_IRPostProcess
    const FString PackageName = TEXT("/OSMWorldGen/Materials/M_IRPostProcess");
    UPackage* Package = CreatePackage(*PackageName);
    if (!Package) return nullptr;

    Package->FullyLoad();

    UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
    UMaterial* Material = Cast<UMaterial>(Factory->FactoryCreateNew(
        UMaterial::StaticClass(), Package, FName(TEXT("M_IRPostProcess")),
        RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));

    if (!Material) return nullptr;

    // Clear any existing nodes in case we are rebuilding an existing asset
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Empty();
    Material->GetEditorOnlyData()->EmissiveColor.Expression = nullptr;

    Material->MaterialDomain = MD_PostProcess;
    Material->BlendableLocation = EBlendableLocation::BL_SceneColorAfterDOF;

    // Node 1: SceneTexture for Scene Color (PPI_PostProcessInput0) - provides lighting & 3D contours
    UMaterialExpressionSceneTexture* SceneColorNode = NewObject<UMaterialExpressionSceneTexture>(Material);
    SceneColorNode->SceneTextureId = ESceneTextureId::PPI_PostProcessInput0;
    SceneColorNode->MaterialExpressionEditorX = -600;
    SceneColorNode->MaterialExpressionEditorY = -200;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(SceneColorNode);

    // Node 2: SceneTexture for Custom Stencil (PPI_CustomStencil) - provides material surface categories
    UMaterialExpressionSceneTexture* StencilNode = NewObject<UMaterialExpressionSceneTexture>(Material);
    StencilNode->SceneTextureId = ESceneTextureId::PPI_CustomStencil;
    StencilNode->MaterialExpressionEditorX = -600;
    StencilNode->MaterialExpressionEditorY = 0;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(StencilNode);

    // Node 3: PaletteMode parameter (0=Ironbow, 1=WhiteHot, 2=BlackHot)
    UMaterialExpressionScalarParameter* PaletteParam = NewObject<UMaterialExpressionScalarParameter>(Material);
    PaletteParam->ParameterName = FName(TEXT("PaletteMode"));
    PaletteParam->DefaultValue = 0.0f; // Default Ironbow
    PaletteParam->MaterialExpressionEditorX = -600;
    PaletteParam->MaterialExpressionEditorY = 200;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(PaletteParam);

    // Node 4: AmbientTempK parameter (default 293.15K)
    UMaterialExpressionScalarParameter* AmbientParam = NewObject<UMaterialExpressionScalarParameter>(Material);
    AmbientParam->ParameterName = FName(TEXT("AmbientTempK"));
    AmbientParam->DefaultValue = 293.15f;
    AmbientParam->MaterialExpressionEditorX = -600;
    AmbientParam->MaterialExpressionEditorY = 350;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(AmbientParam);

    // Node 6: SceneTexture for WorldNormal (PPI_WorldNormal)
    UMaterialExpressionSceneTexture* NormalNode = NewObject<UMaterialExpressionSceneTexture>(Material);
    NormalNode->SceneTextureId = ESceneTextureId::PPI_WorldNormal;
    NormalNode->MaterialExpressionEditorX = -600;
    NormalNode->MaterialExpressionEditorY = 100;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(NormalNode);

    // Node 7: HeatSensitivity scalar parameter
    UMaterialExpressionScalarParameter* SensParam = NewObject<UMaterialExpressionScalarParameter>(Material);
    SensParam->ParameterName = FName(TEXT("HeatSensitivity"));
    SensParam->DefaultValue = 1.0f;
    SensParam->MaterialExpressionEditorX = -600;
    SensParam->MaterialExpressionEditorY = 500;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(SensParam);

    // Node 8: Ensure MPC_ThermalState exists as a saved asset package with valid Parameter IDs
    const FString MPCPath = TEXT("/OSMWorldGen/Materials/MPC_ThermalState");
    UMaterialParameterCollection* MPC = LoadObject<UMaterialParameterCollection>(nullptr, *(MPCPath + TEXT(".MPC_ThermalState")));
    if (!MPC)
    {
        UPackage* MPCPackage = CreatePackage(*MPCPath);
        if (MPCPackage)
        {
            MPCPackage->FullyLoad();
            MPC = NewObject<UMaterialParameterCollection>(MPCPackage, FName(TEXT("MPC_ThermalState")), RF_Public | RF_Standalone | RF_Transactional);
        }
    }

    if (MPC)
    {
        bool bMPCModified = false;
        // Ensure all 16 scalar parameters exist with valid IDs
        for (int i = 0; i < 16; ++i)
        {
            FName ParamName = UOSMThermalMPC::GetParameterNameForCategory(static_cast<EOSMSurfaceCategory>(i));
            FCollectionScalarParameter* Found = nullptr;
            for (FCollectionScalarParameter& P : MPC->ScalarParameters)
            {
                if (P.ParameterName == ParamName)
                {
                    Found = &P;
                    break;
                }
            }
            if (!Found)
            {
                FCollectionScalarParameter NewParam;
                NewParam.Id = FGuid::NewGuid();
                NewParam.ParameterName = ParamName;
                NewParam.DefaultValue = 293.15f;
                MPC->ScalarParameters.Add(NewParam);
                bMPCModified = true;
            }
            else if (!Found->Id.IsValid())
            {
                Found->Id = FGuid::NewGuid();
                bMPCModified = true;
            }
        }

        // Ensure SunDirection exists with valid ID
        FCollectionVectorParameter* FoundSun = nullptr;
        for (FCollectionVectorParameter& VP : MPC->VectorParameters)
        {
            if (VP.ParameterName == FName(TEXT("SunDirection")))
            {
                FoundSun = &VP;
                break;
            }
        }
        if (!FoundSun)
        {
            FCollectionVectorParameter NewSun;
            NewSun.Id = FGuid::NewGuid();
            NewSun.ParameterName = FName(TEXT("SunDirection"));
            NewSun.DefaultValue = FLinearColor(0.0f, -0.5f, -0.866f, 0.0f);
            MPC->VectorParameters.Add(NewSun);
            bMPCModified = true;
        }
        else if (!FoundSun->Id.IsValid())
        {
            FoundSun->Id = FGuid::NewGuid();
            bMPCModified = true;
        }

        if (bMPCModified)
        {
            MPC->PreEditChange(nullptr);
            MPC->PostEditChange();
            if (UPackage* MPCPackage = MPC->GetOutermost())
            {
                MPCPackage->MarkPackageDirty();
                FAssetRegistryModule::AssetCreated(MPC);
                const FString MPCFileName = FPackageName::LongPackageNameToFilename(MPCPath, FPackageName::GetAssetPackageExtension());
                FSavePackageArgs SaveArgs;
                SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
                UPackage::SavePackage(MPCPackage, MPC, *MPCFileName, SaveArgs);
            }
        }
    }

    UMaterialExpressionCustom* CustomNode = NewObject<UMaterialExpressionCustom>(Material);
    CustomNode->OutputType = ECustomMaterialOutputType::CMOT_Float3;
    CustomNode->Description = TEXT("Radiometric Thermal Shading & False-Color");
    CustomNode->MaterialExpressionEditorX = 200;
    CustomNode->MaterialExpressionEditorY = 0;

    FCustomInput ColorInput;
    ColorInput.InputName = FName(TEXT("SceneColor"));
    ColorInput.Input.Expression = SceneColorNode;
    CustomNode->Inputs.Add(ColorInput);

    FCustomInput StencilInput;
    StencilInput.InputName = FName(TEXT("StencilVal"));
    StencilInput.Input.Expression = StencilNode;
    CustomNode->Inputs.Add(StencilInput);

    FCustomInput PalInput;
    PalInput.InputName = FName(TEXT("PaletteMode"));
    PalInput.Input.Expression = PaletteParam;
    CustomNode->Inputs.Add(PalInput);

    FCustomInput AmbInput;
    AmbInput.InputName = FName(TEXT("AmbientTempK"));
    AmbInput.Input.Expression = AmbientParam;
    CustomNode->Inputs.Add(AmbInput);

    FCustomInput NormalInput;
    NormalInput.InputName = FName(TEXT("WorldNormal"));
    NormalInput.Input.Expression = NormalNode;
    CustomNode->Inputs.Add(NormalInput);

    FCustomInput SensInput;
    SensInput.InputName = FName(TEXT("HeatSensitivity"));
    SensInput.Input.Expression = SensParam;
    CustomNode->Inputs.Add(SensInput);

    // Inject SunDirection
    {
        UMaterialExpression* SunSourceNode = nullptr;
        if (MPC)
        {
            UMaterialExpressionCollectionParameter* SunNode = NewObject<UMaterialExpressionCollectionParameter>(Material);
            SunNode->Collection = MPC;
            SunNode->ParameterName = FName(TEXT("SunDirection"));
            for (const FCollectionVectorParameter& VP : MPC->VectorParameters)
            {
                if (VP.ParameterName == SunNode->ParameterName)
                {
                    SunNode->ParameterId = VP.Id;
                    break;
                }
            }
            SunNode->MaterialExpressionEditorX = -600;
            SunNode->MaterialExpressionEditorY = 650;
            Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(SunNode);
            SunSourceNode = SunNode;
        }
        else
        {
            UMaterialExpressionVectorParameter* FallbackSun = NewObject<UMaterialExpressionVectorParameter>(Material);
            FallbackSun->ParameterName = FName(TEXT("SunDirection"));
            FallbackSun->DefaultValue = FLinearColor(0.0f, -0.5f, -0.866f, 0.0f);
            FallbackSun->MaterialExpressionEditorX = -600;
            FallbackSun->MaterialExpressionEditorY = 650;
            Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(FallbackSun);
            SunSourceNode = FallbackSun;
        }

        FCustomInput SunInput;
        SunInput.InputName = FName(TEXT("SunDirection"));
        SunInput.Input.Expression = SunSourceNode;
        CustomNode->Inputs.Add(SunInput);
    }

    // Inject 16 Category Temperatures
    for (int i = 0; i < 16; ++i)
    {
        UMaterialExpression* CatSourceNode = nullptr;
        FName ParamName = UOSMThermalMPC::GetParameterNameForCategory(static_cast<EOSMSurfaceCategory>(i));
        if (MPC)
        {
            UMaterialExpressionCollectionParameter* CatNode = NewObject<UMaterialExpressionCollectionParameter>(Material);
            CatNode->Collection = MPC;
            CatNode->ParameterName = ParamName;
            for (const FCollectionScalarParameter& SP : MPC->ScalarParameters)
            {
                if (SP.ParameterName == ParamName)
                {
                    CatNode->ParameterId = SP.Id;
                    break;
                }
            }
            CatNode->MaterialExpressionEditorX = -600;
            CatNode->MaterialExpressionEditorY = 800 + i * 150;
            Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(CatNode);
            CatSourceNode = CatNode;
        }
        else
        {
            UMaterialExpressionScalarParameter* FallbackCat = NewObject<UMaterialExpressionScalarParameter>(Material);
            FallbackCat->ParameterName = ParamName;
            FallbackCat->DefaultValue = 293.15f;
            FallbackCat->MaterialExpressionEditorX = -600;
            FallbackCat->MaterialExpressionEditorY = 800 + i * 150;
            Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(FallbackCat);
            CatSourceNode = FallbackCat;
        }

        FCustomInput CatInput;
        CatInput.InputName = FName(*FString::Printf(TEXT("Cat%d"), i));
        CatInput.Input.Expression = CatSourceNode;
        CustomNode->Inputs.Add(CatInput);
    }

    CustomNode->Code = TEXT(
        "int Category = (int)round(StencilVal.r);\n"
        "float SceneLuma = dot(SceneColor.rgb, float3(0.299, 0.587, 0.114));\n\n"
        "// Dynamic Temperatures from MPC (in Kelvin)\n"
        "float MatTemps[16] = {\n"
        "    Cat0, Cat1, Cat2, Cat3, Cat4, Cat5, Cat6, Cat7,\n"
        "    Cat8, Cat9, Cat10, Cat11, Cat12, Cat13, Cat14, Cat15\n"
        "};\n\n"
        "float MatTempK = AmbientTempK;\n"
        "if (Category > 0 && Category < 16)\n"
        "{\n"
        "    MatTempK = MatTemps[Category];\n"
        "}\n"
        "else\n"
        "{\n"
        "    // Unclassified geometry or background: derive thermodynamic temperature from scene luminosity\n"
        "    MatTempK = AmbientTempK + (SceneLuma - 0.45f) * 25.0f;\n"
        "}\n\n"
        "// Approximate local solar heating differential using dot product with SunDirection\n"
        "float3 Normal = normalize(WorldNormal.rgb * 2.0f - 1.0f); // Decode normal\n"
        "float CosTheta = saturate(dot(Normal, -normalize(SunDirection.xyz)));\n"
        "float LocalSolarBoost = CosTheta * 15.0f; // Max 15K diff for direct sun\n"
        "MatTempK += LocalSolarBoost;\n\n"
        "// High Dynamic Range normalization using HeatSensitivity\n"
        "// Base temperature shifts dynamically with ambient conditions (Auto-Exposure FLIR behavior)\n"
        "float RangeK = 50.0f / max(0.1f, HeatSensitivity);\n"
        "float BaseK = AmbientTempK - (RangeK * 0.35f); // 35% of range reserved for cold sky/water/air\n"
        "float NormT = saturate((MatTempK - BaseK) / RangeK);\n\n"
        "// Retain subtle structural geometric detail from scene luminance\n"
        "float StructuralDetail = SceneLuma * 0.20f;\n"
        "NormT = saturate(NormT * 0.80f + StructuralDetail);\n\n"
        "float3 OutColor = float3(0, 0, 0);\n\n"
        "// Palette 0: Authentic Military Ironbow\n"
        "if (PaletteMode < 0.5f)\n"
        "{\n"
        "    float r = 0.0;\n"
        "    float g = 0.0;\n"
        "    float b = 0.0;\n"
        "    if (NormT < 0.25f)\n"
        "    {\n"
        "        // Deep Purple to Navy Blue (Coldest)\n"
        "        float seg = NormT / 0.25f;\n"
        "        r = seg * 0.35f;\n"
        "        g = 0.0f;\n"
        "        b = 0.40f + seg * 0.55f;\n"
        "    }\n"
        "    else if (NormT < 0.55f)\n"
        "    {\n"
        "        // Rich Magenta to Crimson Fire Red (Warm ambient, walls)\n"
        "        float seg = (NormT - 0.25f) / 0.30f;\n"
        "        r = 0.35f + seg * 0.65f;\n"
        "        g = seg * 0.20f;\n"
        "        b = (1.0f - seg) * 0.90f;\n"
        "    }\n"
        "    else if (NormT < 0.85f)\n"
        "    {\n"
        "        // Radiant Orange to Golden Amber Yellow (Sunlit surfaces, roads)\n"
        "        float seg = (NormT - 0.55f) / 0.30f;\n"
        "        r = 1.0f;\n"
        "        g = 0.20f + seg * 0.75f;\n"
        "        b = 0.0f;\n"
        "    }\n"
        "    else\n"
        "    {\n"
        "        // Blazing Yellow to Glowing Pure White (Hottest hotspots)\n"
        "        float seg = (NormT - 0.85f) / 0.15f;\n"
        "        r = 1.0f;\n"
        "        g = 0.95f + seg * 0.05f;\n"
        "        b = seg * 1.0f;\n"
        "    }\n"
        "    OutColor = float3(r, g, b);\n"
        "}\n"
        "// Palette 1: High Dynamic Range White-Hot FLIR\n"
        "else if (PaletteMode < 1.5f)\n"
        "{\n"
        "    float WH = pow(max(0.0001f, NormT), 1.25f);\n"
        "    OutColor = float3(WH, WH, WH);\n"
        "}\n"
        "// Palette 2: Inverted Black-Hot FLIR\n"
        "else\n"
        "{\n"
        "    float BH = 1.0f - pow(max(0.0001f, NormT), 1.25f);\n"
        "    OutColor = float3(BH, BH, BH);\n"
        "}\n\n"
        "return OutColor;"
    );

    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(CustomNode);
    Material->GetEditorOnlyData()->EmissiveColor.Expression = CustomNode;

    Material->PreEditChange(nullptr);
    Material->PostEditChange();
    Package->MarkPackageDirty();

    // Register with asset registry and save package
    FAssetRegistryModule::AssetCreated(Material);

    const FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    UPackage::SavePackage(Package, Material, *PackageFileName, SaveArgs);

    return Material;
}

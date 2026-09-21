// Copyright InviMind. All Rights Reserved.

#include "Materials/FOSMThermalMaterialBuilderV2.h"
#include "Factories/MaterialFactoryNew.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/FOSMMaterialPaletteFactory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

const FString& FOSMThermalMaterialBuilderV2::GetDefaultMaterialAssetPath()
{
    static const FString DefaultPath = TEXT("/OSMWorldGen/Materials/M_IRPostProcessV2.M_IRPostProcessV2");
    return DefaultPath;
}

UMaterialInterface* FOSMThermalMaterialBuilderV2::GetOrCreateThermalPostProcessMaterial(bool bForceRebuild)
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

    // 2. Programmatically construct M_IRPostProcessV2
    const FString PackageName = TEXT("/OSMWorldGen/Materials/M_IRPostProcessV2");
    UPackage* Package = CreatePackage(*PackageName);
    if (!Package) return nullptr;

    Package->FullyLoad();

    UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
    UMaterial* Material = Cast<UMaterial>(Factory->FactoryCreateNew(
        UMaterial::StaticClass(), Package, FName(TEXT("M_IRPostProcessV2")),
        RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));

    if (!Material) return nullptr;

    // Clear any existing nodes in case we are rebuilding an existing asset
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Empty();
    Material->GetEditorOnlyData()->EmissiveColor.Expression = nullptr;

    Material->MaterialDomain = MD_PostProcess;
    // Set to BL_SceneColorBeforeDOF so that Unreal's Depth of Field, Bloom, and
    // Temporal Anti-Aliasing (TAA/TSR) run on the thermal colors, creating the
    // soft optical lens blur and thermal bloom characteristic of real FLIR optics.
    Material->BlendableLocation = EBlendableLocation::BL_SceneColorBeforeDOF;

    // =========================================================================
    // All inputs are regular ScalarParameter / VectorParameter nodes.
    // They are driven at runtime via UMaterialInstanceDynamic::SetScalarParameterValue
    // from AOSMInfraredCamera::UpdateMaterialParameters().
    //
    // This avoids MaterialParameterCollection (MaterialCollection0) entirely,
    // which causes "use of undeclared identifier 'MaterialCollection0'" on
    // Metal/SM5 when used inside PostProcess materials.
    // =========================================================================

    // Node 1: SceneTexture for Scene Color (PPI_PostProcessInput0)
    UMaterialExpressionSceneTexture* SceneColorNode = NewObject<UMaterialExpressionSceneTexture>(Material);
    SceneColorNode->SceneTextureId = ESceneTextureId::PPI_PostProcessInput0;
    SceneColorNode->MaterialExpressionEditorX = -600;
    SceneColorNode->MaterialExpressionEditorY = -200;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(SceneColorNode);

    // Node 2: SceneTexture for Custom Stencil (PPI_CustomStencil)
    UMaterialExpressionSceneTexture* StencilNode = NewObject<UMaterialExpressionSceneTexture>(Material);
    StencilNode->SceneTextureId = ESceneTextureId::PPI_CustomStencil;
    StencilNode->MaterialExpressionEditorX = -600;
    StencilNode->MaterialExpressionEditorY = 0;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(StencilNode);

    // Node 2b: SceneTexture for Scene Depth (PPI_SceneDepth) to distinguish sky from geometry
    UMaterialExpressionSceneTexture* DepthNode = NewObject<UMaterialExpressionSceneTexture>(Material);
    DepthNode->SceneTextureId = ESceneTextureId::PPI_SceneDepth;
    DepthNode->MaterialExpressionEditorX = -600;
    DepthNode->MaterialExpressionEditorY = -100;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(DepthNode);

    // Node 3: PaletteMode parameter (0=Ironbow, 1=WhiteHot, 2=BlackHot)
    UMaterialExpressionScalarParameter* PaletteParam = NewObject<UMaterialExpressionScalarParameter>(Material);
    PaletteParam->ParameterName = FName(TEXT("PaletteMode"));
    PaletteParam->DefaultValue = 0.0f;
    PaletteParam->MaterialExpressionEditorX = -600;
    PaletteParam->MaterialExpressionEditorY = 200;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(PaletteParam);

    // Node 4: AmbientTempK parameter
    UMaterialExpressionScalarParameter* AmbientParam = NewObject<UMaterialExpressionScalarParameter>(Material);
    AmbientParam->ParameterName = FName(TEXT("AmbientTempK"));
    AmbientParam->DefaultValue = 293.15f;
    AmbientParam->MaterialExpressionEditorX = -600;
    AmbientParam->MaterialExpressionEditorY = 350;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(AmbientParam);

    // Node 5: SceneTexture for WorldNormal
    UMaterialExpressionSceneTexture* NormalNode = NewObject<UMaterialExpressionSceneTexture>(Material);
    NormalNode->SceneTextureId = ESceneTextureId::PPI_WorldNormal;
    NormalNode->MaterialExpressionEditorX = -600;
    NormalNode->MaterialExpressionEditorY = 100;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(NormalNode);

    // Node 6: HeatSensitivity scalar parameter
    UMaterialExpressionScalarParameter* SensParam = NewObject<UMaterialExpressionScalarParameter>(Material);
    SensParam->ParameterName = FName(TEXT("HeatSensitivity"));
    SensParam->DefaultValue = 1.0f;
    SensParam->MaterialExpressionEditorX = -600;
    SensParam->MaterialExpressionEditorY = 500;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(SensParam);

    // Node 7: SunDirection vector parameter (driven at runtime)
    UMaterialExpressionVectorParameter* SunDirParam = NewObject<UMaterialExpressionVectorParameter>(Material);
    SunDirParam->ParameterName = FName(TEXT("SunDirection"));
    SunDirParam->DefaultValue = FLinearColor(0.0f, -0.5f, -0.866f, 0.0f);
    SunDirParam->MaterialExpressionEditorX = -600;
    SunDirParam->MaterialExpressionEditorY = 650;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(SunDirParam);

    // Nodes 8-23: 16 Category Temperature scalar parameters (Cat0..Cat15)
    UMaterialExpressionScalarParameter* CatParams[16];
    for (int32 i = 0; i < 16; ++i)
    {
        CatParams[i] = NewObject<UMaterialExpressionScalarParameter>(Material);
        CatParams[i]->ParameterName = FName(*FString::Printf(TEXT("CatTemp%d"), i));
        CatParams[i]->DefaultValue = FOSMMaterialPaletteFactory::GetStandardBaseTemperatureK(static_cast<EOSMSurfaceCategory>(i));
        CatParams[i]->MaterialExpressionEditorX = -600;
        CatParams[i]->MaterialExpressionEditorY = 800 + i * 100;
        Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(CatParams[i]);
    }

    // Node 24: AbsoluteWorldPosition (for vertical wall gradients + deterministic spatial noise)
    UMaterialExpressionWorldPosition* WorldPosNode = NewObject<UMaterialExpressionWorldPosition>(Material);
    WorldPosNode->WorldPositionShaderOffset = EWorldPositionIncludedOffsets::WPT_Default;
    WorldPosNode->MaterialExpressionEditorX = -600;
    WorldPosNode->MaterialExpressionEditorY = 2450;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(WorldPosNode);

    // Node 25: ScreenPosition (for FLIR sensor artifacts: grain, scanlines, vignette)
    UMaterialExpressionScreenPosition* ScreenPosNode = NewObject<UMaterialExpressionScreenPosition>(Material);
    ScreenPosNode->MaterialExpressionEditorX = -600;
    ScreenPosNode->MaterialExpressionEditorY = 2600;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(ScreenPosNode);

    // Node 26: GroundLevelZ (Z position of ground in cm)
    UMaterialExpressionScalarParameter* GroundZParam = NewObject<UMaterialExpressionScalarParameter>(Material);
    GroundZParam->ParameterName = FName(TEXT("GroundLevelZ"));
    GroundZParam->DefaultValue = 0.0f;
    GroundZParam->MaterialExpressionEditorX = -600;
    GroundZParam->MaterialExpressionEditorY = 2750;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(GroundZParam);

    // Node 27: MaxHeightCm (maximum expected building height in cm)
    UMaterialExpressionScalarParameter* MaxHeightParam = NewObject<UMaterialExpressionScalarParameter>(Material);
    MaxHeightParam->ParameterName = FName(TEXT("MaxHeightCm"));
    MaxHeightParam->DefaultValue = 5000.0f;
    MaxHeightParam->MaterialExpressionEditorX = -600;
    MaxHeightParam->MaterialExpressionEditorY = 2900;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(MaxHeightParam);

    // Node 28: FrameCounter (incremented each tick for animated grain noise)
    UMaterialExpressionScalarParameter* FrameCounterParam = NewObject<UMaterialExpressionScalarParameter>(Material);
    FrameCounterParam->ParameterName = FName(TEXT("FrameCounter"));
    FrameCounterParam->DefaultValue = 0.0f;
    FrameCounterParam->MaterialExpressionEditorX = -600;
    FrameCounterParam->MaterialExpressionEditorY = 3050;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(FrameCounterParam);

    // Node 29: GrainIntensity (sensor grain noise strength)
    UMaterialExpressionScalarParameter* GrainParam = NewObject<UMaterialExpressionScalarParameter>(Material);
    GrainParam->ParameterName = FName(TEXT("GrainIntensity"));
    GrainParam->DefaultValue = 0.06f;
    GrainParam->MaterialExpressionEditorX = -600;
    GrainParam->MaterialExpressionEditorY = 3200;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(GrainParam);

    // Node 30: ScanlineOpacity (scanline overlay strength)
    UMaterialExpressionScalarParameter* ScanlineParam = NewObject<UMaterialExpressionScalarParameter>(Material);
    ScanlineParam->ParameterName = FName(TEXT("ScanlineOpacity"));
    ScanlineParam->DefaultValue = 0.04f;
    ScanlineParam->MaterialExpressionEditorX = -600;
    ScanlineParam->MaterialExpressionEditorY = 3350;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(ScanlineParam);

    // Node 31: VignetteStrength (corner darkening intensity)
    UMaterialExpressionScalarParameter* VignetteParam = NewObject<UMaterialExpressionScalarParameter>(Material);
    VignetteParam->ParameterName = FName(TEXT("VignetteStrength"));
    VignetteParam->DefaultValue = 0.8f;
    VignetteParam->MaterialExpressionEditorX = -600;
    VignetteParam->MaterialExpressionEditorY = 3500;
    Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(VignetteParam);

    // Build Custom HLSL Node with all inputs wired
    UMaterialExpressionCustom* CustomNode = NewObject<UMaterialExpressionCustom>(Material);
    CustomNode->OutputType = ECustomMaterialOutputType::CMOT_Float3;
    CustomNode->Description = TEXT("Photorealistic IR/FLIR Thermal Shading & False-Color");
    CustomNode->MaterialExpressionEditorX = 200;
    CustomNode->MaterialExpressionEditorY = 0;

    // Wire SceneColor
    FCustomInput ColorInput;
    ColorInput.InputName = FName(TEXT("SceneColor"));
    ColorInput.Input.Expression = SceneColorNode;
    CustomNode->Inputs.Add(ColorInput);

    // Wire Stencil
    FCustomInput StencilInput;
    StencilInput.InputName = FName(TEXT("StencilVal"));
    StencilInput.Input.Expression = StencilNode;
    CustomNode->Inputs.Add(StencilInput);

    // Wire SceneDepth
    FCustomInput DepthInput;
    DepthInput.InputName = FName(TEXT("SceneDepthVal"));
    DepthInput.Input.Expression = DepthNode;
    CustomNode->Inputs.Add(DepthInput);

    // Wire PaletteMode
    FCustomInput PalInput;
    PalInput.InputName = FName(TEXT("PaletteMode"));
    PalInput.Input.Expression = PaletteParam;
    CustomNode->Inputs.Add(PalInput);

    // Wire AmbientTempK
    FCustomInput AmbInput;
    AmbInput.InputName = FName(TEXT("AmbientTempK"));
    AmbInput.Input.Expression = AmbientParam;
    CustomNode->Inputs.Add(AmbInput);

    // Wire WorldNormal
    FCustomInput NormalInput;
    NormalInput.InputName = FName(TEXT("WorldNormal"));
    NormalInput.Input.Expression = NormalNode;
    CustomNode->Inputs.Add(NormalInput);

    // Wire HeatSensitivity
    FCustomInput SensInput;
    SensInput.InputName = FName(TEXT("HeatSensitivity"));
    SensInput.Input.Expression = SensParam;
    CustomNode->Inputs.Add(SensInput);

    // Wire SunDirection
    FCustomInput SunInput;
    SunInput.InputName = FName(TEXT("SunDirection"));
    SunInput.Input.Expression = SunDirParam;
    CustomNode->Inputs.Add(SunInput);

    // Wire 16 Category Temperatures
    for (int32 i = 0; i < 16; ++i)
    {
        FCustomInput CatInput;
        CatInput.InputName = FName(*FString::Printf(TEXT("Cat%d"), i));
        CatInput.Input.Expression = CatParams[i];
        CustomNode->Inputs.Add(CatInput);
    }

    // Wire WorldPosition
    FCustomInput WorldPosInput;
    WorldPosInput.InputName = FName(TEXT("WorldPos"));
    WorldPosInput.Input.Expression = WorldPosNode;
    CustomNode->Inputs.Add(WorldPosInput);

    // Wire ScreenPosition
    FCustomInput ScreenPosInput;
    ScreenPosInput.InputName = FName(TEXT("ScreenUV"));
    ScreenPosInput.Input.Expression = ScreenPosNode;
    CustomNode->Inputs.Add(ScreenPosInput);

    // Wire GroundLevelZ
    FCustomInput GroundZInput;
    GroundZInput.InputName = FName(TEXT("GroundLevelZ"));
    GroundZInput.Input.Expression = GroundZParam;
    CustomNode->Inputs.Add(GroundZInput);

    // Wire MaxHeightCm
    FCustomInput MaxHInput;
    MaxHInput.InputName = FName(TEXT("MaxHeightCm"));
    MaxHInput.Input.Expression = MaxHeightParam;
    CustomNode->Inputs.Add(MaxHInput);

    // Wire FrameCounter
    FCustomInput FrameInput;
    FrameInput.InputName = FName(TEXT("FrameCounter"));
    FrameInput.Input.Expression = FrameCounterParam;
    CustomNode->Inputs.Add(FrameInput);

    // Wire GrainIntensity
    FCustomInput GrainInput;
    GrainInput.InputName = FName(TEXT("GrainIntensity"));
    GrainInput.Input.Expression = GrainParam;
    CustomNode->Inputs.Add(GrainInput);

    // Wire ScanlineOp
    FCustomInput ScanInput;
    ScanInput.InputName = FName(TEXT("ScanlineOp"));
    ScanInput.Input.Expression = ScanlineParam;
    CustomNode->Inputs.Add(ScanInput);

    // Wire VignetteStr
    FCustomInput VigInput;
    VigInput.InputName = FName(TEXT("VignetteStr"));
    VigInput.Input.Expression = VignetteParam;
    CustomNode->Inputs.Add(VigInput);

    CustomNode->Code = TEXT(R"hlsl(
// ============================================================
// Photorealistic IR/FLIR Thermal Shader V7
// Stochastic Non-Uniform Multi-Octave Thermal Model
// Real-world calibrated FLIR drone imagery:
//   - 13-tap 2-tier Gaussian optical thermal blur (Germanium lens PSF)
//   - Every surface has mixed, non-uniform, organic temperatures
//   - Roofs: Corners/edges yellower -> center fades to Crimson/Scarlet
//   - Roofs: Organic FBM splotches across the entire surface (Zero flat color)
//   - Roofs strictly clamped to [313.8K, 322.2K] (Red <-> Orange <-> Yellow, NO purple)
//   - Walls: Base is deep dark purple -> flows up into Crimson/Scarlet at top
//   - Walls: Vertical FBM micro-patches break horizontal banding
//   - Roads: Warm Amber-Orange (314K) with tire/asphalt splotchy variation
//   - City drift is minute decimal values (+-0.12K, zero building jumping)
//   - Quintic Hermite value noise (zero sine checkerboards, zero boxes)
// ============================================================

struct FLIRCore
{
    // Deterministic, algebraically stable 2D hash (no trigonometric artifacts on Metal/SM5)
    static float Hash2D(float2 p)
    {
        float3 p3 = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
        p3 += dot(p3, p3.yzx + 33.33f);
        return frac((p3.x + p3.y) * p3.z);
    }

    // C2 continuous 2D Value Noise with Quintic Hermite interpolation
    // Eliminates all linear/bilinear grid line artifacts and box patterns
    static float ValueNoise(float2 p)
    {
        float2 i = floor(p);
        float2 f = frac(p);
        // Quintic Hermite curve: 6t^5 - 15t^4 + 10t^3
        float2 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);

        float a = Hash2D(i + float2(0.0f, 0.0f));
        float b = Hash2D(i + float2(1.0f, 0.0f));
        float c = Hash2D(i + float2(0.0f, 1.0f));
        float d = Hash2D(i + float2(1.0f, 1.0f));

        return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
    }

    // 2-Octave Fractal Brownian Motion (FBM)
    static float FBM2(float2 p)
    {
        return ValueNoise(p) * 0.65f + ValueNoise(p * 2.13f + float2(1.7f, 9.2f)) * 0.35f;
    }

    // 3-Octave FBM for rich multi-scale rooftop thermal variations
    static float FBM3(float2 p)
    {
        float v = ValueNoise(p) * 0.52f;
        v += ValueNoise(p * 2.07f + float2(3.1f, 7.4f)) * 0.31f;
        v += ValueNoise(p * 4.35f + float2(8.3f, 2.8f)) * 0.17f;
        return v;
    }

    // Authentic FLIR Ironbow False-Color Palette
    static float3 PaletteIronbow(float NormT)
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;

        if (NormT < 0.08f)
        {
            // Pure Black -> Deep Navy (Sky, deep shadows, cold water)
            float s = NormT / 0.08f;
            r = s * 0.04f;
            g = 0.0f;
            b = s * 0.14f;
        }
        else if (NormT < 0.20f)
        {
            // Deep Navy -> Deep Indigo/Violet (Vegetation, park lawn)
            float s = (NormT - 0.08f) / 0.12f;
            r = 0.04f + s * 0.08f;
            g = 0.0f;
            b = 0.14f + s * 0.14f;
        }
        else if (NormT < 0.34f)
        {
            // Deep Violet -> Rich Dark Purple (Wall bases, cool terrain)
            // Low-luminance realistic FLIR dark purple (no neon light pink)
            float s = (NormT - 0.20f) / 0.14f;
            r = 0.12f + s * 0.14f;
            g = 0.0f;
            b = 0.28f + s * 0.12f;
        }
        else if (NormT < 0.46f)
        {
            // Rich Dark Purple -> Deep Plum/Dark Violet-Red (Mid walls)
            float s = (NormT - 0.34f) / 0.12f;
            r = 0.26f + s * 0.22f;
            g = 0.0f;
            b = 0.40f - s * 0.16f;
        }
        else if (NormT < 0.58f)
        {
            // Deep Plum -> RICH CRIMSON RED (Flow-down on upper walls)
            float s = (NormT - 0.46f) / 0.12f;
            r = 0.48f + s * 0.40f;
            g = 0.0f;
            b = 0.24f * (1.0f - s);
        }
        else if (NormT < 0.70f)
        {
            // CRIMSON RED -> VIVID SCARLET (Parapets, roof centers)
            float s = (NormT - 0.58f) / 0.12f;
            r = 0.88f + s * 0.12f;
            g = s * 0.18f;
            b = 0.0f;
        }
        else if (NormT < 0.82f)
        {
            // Scarlet -> WARM AMBER-ORANGE (Rooftops mid-region)
            float s = (NormT - 0.70f) / 0.12f;
            r = 1.0f;
            g = 0.18f + s * 0.30f;
            b = 0.0f;
        }
        else if (NormT < 0.93f)
        {
            // Warm Orange -> Deep Golden Amber (Hot rooftop regions)
            float s = (NormT - 0.82f) / 0.11f;
            r = 1.0f;
            g = 0.48f + s * 0.30f;
            b = 0.0f;
        }
        else if (NormT < 0.975f)
        {
            // Golden Amber -> BRIGHT YELLOW (Corners & peak hotspots only!)
            float s = (NormT - 0.93f) / 0.045f;
            r = 1.0f;
            g = 0.78f + s * 0.18f;
            b = s * 0.10f;
        }
        else
        {
            // Yellow -> WHITE-HOT (Extreme heat: HVAC exhausts only)
            float s = (NormT - 0.975f) / 0.025f;
            r = 1.0f;
            g = 0.96f + s * 0.04f;
            b = 0.10f + s * 0.90f;
        }

        return float3(r, g, b);
    }

    static float3 EvaluatePixel(
        float2 TapUV,
        float CenterDepth,
        float CenterStencil,
        float3 CenterNormal,
        float3 CenterWPos,
        float3 SunDir,
        float AmbientTemp,
        float HeatSens,
        float GroundZ,
        float MaxH,
        float PalMode,
        float MatTempsArray[16],
        float ScreenEdge)
    {
        // 1. Sample scene textures at tap offset
        float TapDepth = SceneTextureLookup(TapUV, 1, false).r;
        bool bIsSky = (TapDepth > 500000.0f);
        if (bIsSky)
        {
            if (PalMode < 0.5f) return float3(0.0f, 0.0f, 0.0f);
            if (PalMode < 1.5f) return float3(0.0f, 0.0f, 0.0f);
            return float3(1.0f, 1.0f, 1.0f);
        }

        float TapStencil = SceneTextureLookup(TapUV, 25, false).r;
        int Cat = (int)round(TapStencil);

        float3 TapColor = SceneTextureLookup(TapUV, 14, true).rgb;
        float Luma = dot(TapColor, float3(0.299, 0.587, 0.114));

        // Sample normal at tap UV for precise boundary convolution
        float3 TapNormal = SceneTextureLookup(TapUV, 8, false).rgb;
        float NormLen = length(TapNormal);
        if (NormLen > 0.1f) TapNormal /= NormLen;
        else TapNormal = CenterNormal;

        // 2. Identify surface type
        bool bIsGround = (Cat == 10 || Cat == 11 || Cat == 12 || Cat == 13);
        bool bIsRoad = (Cat == 9);
        bool bIsExplicitRoof = (Cat == 15);
        bool bIsElevated = ((CenterWPos.z - GroundZ) > 180.0f); // Elevated >1.8m above ground

        // Surface is a roof if explicitly tagged RoofTile OR if it's an elevated upward surface
        bool bIsRoof = (bIsExplicitRoof || (TapNormal.z > 0.50f && bIsElevated && !bIsGround && !bIsRoad));
        bool bIsVertical = (abs(TapNormal.z) < 0.55f && bIsElevated);

        // 3. Compute temperature with stochastic non-uniform distribution
        float T = AmbientTemp;

        if (bIsRoof)
        {
            // ROOFTOP THERMAL MODEL:
            // - Corners & edges are yellower (hot solar traps, parapet absorption)
            // - Center fades into warm amber and crimson red
            // - Spread of colors is organic, random, and non-uniform (FBM noise)
            // - ZERO flat single-color surfaces
            // - Strictly clamped to [313.8K, 322.2K] (Red <-> Orange <-> Yellow, NO purple)

            float2 WorldM = CenterWPos.xy * 0.01f; // cm -> meters

            // World-space building cell representation (~13.5m typical building footprint)
            float2 CellFrac = frac(WorldM / 13.5f);
            float2 CellDist2D = abs(CellFrac - 0.5f) * 2.0f; // 0 at center, 1 at edge
            float WorldEdge = max(CellDist2D.x, CellDist2D.y);
            float CornerBoost = CellDist2D.x * CellDist2D.y; // High only at corners where both X & Y near 1

            // Multi-octave FBM noise field (~1.8m scale): produces natural splotchy patches
            float RoofNoise = FBM3(WorldM * 0.55f);

            // Randomize the edge boundary so it is NOT a straight mathematical line
            float OrganicEdge = saturate(WorldEdge * 0.65f + ScreenEdge * 0.55f + (RoofNoise - 0.5f) * 0.50f);

            // Center is Crimson Red/Scarlet (~314.8K), transitioning towards Golden Amber at edges (~319.8K)
            float BaseRoofT = lerp(314.8f, 319.8f, pow(OrganicEdge, 1.35f));

            // Corners get extra solar concentration (+1.8K) modulated by noise
            float CornerHeat = CornerBoost * (0.7f + 0.7f * RoofNoise) * 1.8f;

            // Organic splotchy variations across the roof surface (+-1.5K)
            float SurfaceVariation = (RoofNoise - 0.5f) * 3.0f;

            // Micro architectural detail from texture luma
            float LumaDetail = (Luma - 0.40f) * 1.2f;

            T = BaseRoofT + CornerHeat + SurfaceVariation + LumaDetail;

            // Strict safety clamp: Guaranteed Red <-> Orange <-> Yellow ONLY
            // 313.8K -> NormT 0.63 (Crimson Red)
            // 322.2K -> NormT 0.968 (Bright Yellow)
            T = clamp(T, 313.8f, 322.2f);
        }
        else if (bIsRoad)
        {
            // ROAD THERMAL MODEL:
            // Warm Amber-Orange (314.0K), NOT bright yellow!
            // Splotchy tire/asphalt wear variation (~2.5m scale)
            float2 RoadCoord = CenterWPos.xy * 0.01f;
            float RoadNoise = FBM2(RoadCoord * 0.45f);
            float RoadOffset = (RoadNoise - 0.5f) * 2.2f; // +-1.1K
            T = 314.0f + RoadOffset + (Luma - 0.4f) * 1.4f;
            T = clamp(T, 311.5f, 316.8f); // Stays in Crimson -> Amber
        }
        else if (bIsGround)
        {
            // GROUND & VEGETATION MODEL:
            // Soil (10), Grass (11), Trees (12), Water (13)
            // Deep cool tones: 301.0K to 304.5K (Indigo to Dark Purple)
            if (Cat >= 0 && Cat < 16) T = MatTempsArray[Cat];
            else T = 302.5f;

            float2 GroundCoord = CenterWPos.xy * 0.01f;
            float GroundNoise = FBM2(GroundCoord * 0.35f);
            float GroundOffset = (GroundNoise - 0.5f) * 1.6f; // +-0.8K
            T += GroundOffset + (Luma - 0.35f) * 1.0f;
            T = clamp(T, 299.0f, 305.5f);
        }
        else if (bIsVertical)
        {
            // BUILDING WALL THERMAL MODEL:
            // - Street level is realistic deep dark purple (~305.5K)
            // - Conduction flow-down from hot roof: rises to Crimson Red at upper wall
            // - Top border / parapet touches Scarlet (~315.2K)
            // - Splotchy vertical FBM patches break horizontal uniformity (+-0.9K)
            // - Walls NEVER reach yellow!
            float RelH = saturate((CenterWPos.z - GroundZ) / max(1.0f, MaxH));
            float FlowDown = lerp(-3.0f, 5.0f, pow(RelH, 1.8f));
            float TopBorder = (RelH > 0.90f) ? ((RelH - 0.90f) / 0.10f) * 1.8f : 0.0f;

            float2 WallCoord = float2(CenterWPos.x + CenterWPos.y * 0.7f, CenterWPos.z) * 0.01f;
            float WallNoise = FBM2(WallCoord * 0.75f);
            float WallOffset = (WallNoise - 0.5f) * 1.8f; // +-0.9K

            T = 308.5f + FlowDown + TopBorder + WallOffset + (Luma - 0.35f) * 1.2f;
            T = clamp(T, 303.5f, 316.2f);
        }
        else
        {
            // Unclassified geometry
            if (Cat > 0 && Cat < 16) T = MatTempsArray[Cat];
            else T = AmbientTemp + (Luma - 0.4f) * 1.8f;
        }

        // Gentle solar exposure on sun-facing non-roof surfaces
        if (!bIsRoof)
        {
            float CosTheta = saturate(dot(TapNormal, -normalize(SunDir)));
            T += CosTheta * saturate(Luma * 1.2f) * 1.4f;
        }

        // Ultra-subtle city-wide temperature drift (~+-0.12K)
        // Smooth continuous low-frequency noise (period ~100m)
        // Neighboring buildings differ by less than 0.02K (ZERO visible color jumping)
        float CityDrift = (ValueNoise(CenterWPos.xy * 0.00001f) - 0.5f) * 0.24f;
        T += CityDrift;

        // 4. Normalize to [0, 1]
        // 298.0K (25°C) to 323.0K (50°C) -> 25K span
        float BaseK = 298.0f;
        float RangeK = 25.0f / max(0.1f, HeatSens);
        float NormT = saturate((T - BaseK) / RangeK);

        // 5. False-color palette evaluation
        if (PalMode < 0.5f)
        {
            return PaletteIronbow(NormT);
        }
        else if (PalMode < 1.5f)
        {
            float WH = pow(max(0.0001f, NormT), 1.2f);
            return float3(WH, WH, WH);
        }
        else
        {
            float BH = 1.0f - pow(max(0.0001f, NormT), 1.2f);
            return float3(BH, BH, BH);
        }
    }
};

float2 UV = ScreenUV.rg;
float3 Normal = normalize(WorldNormal.rgb);
float3 WPos = WorldPos.rgb;

float MatTemps[16] = {
    Cat0, Cat1, Cat2, Cat3, Cat4, Cat5, Cat6, Cat7,
    Cat8, Cat9, Cat10, Cat11, Cat12, Cat13, Cat14, Cat15
};

// Detect geometric roof boundaries using normal variation and depth discontinuity
float NormZGrad = abs(ddx(Normal.z)) + abs(ddy(Normal.z));
float DepthGrad = (abs(ddx(SceneDepthVal.r)) + abs(ddy(SceneDepthVal.r))) / max(50.0f, SceneDepthVal.r * 0.02f);
float ScreenEdge = saturate(NormZGrad * 4.0f + DepthGrad * 1.5f);

// ============================================================
// 13-TAP 2-TIER GAUSSIAN OPTICAL THERMAL BLUR
// Simulates FLIR Germanium lens point-spread function (PSF).
// Convolves the false-color thermal output across 4.0 screen pixels.
// Blurs all edges, borders, roof outlines, and heat transitions
// for authentic soft FLIR thermal camera optics (no jagged aliasing).
// ============================================================

float2 Texel = float2(
    max(0.0002f, abs(ddx(UV.x))),
    max(0.0003f, abs(ddy(UV.y)))
);

// Optical blur radius: 4.0 screen pixels
float2 R = Texel * 4.0f;

// 13-tap 2-tier Gaussian kernel offsets & weights (sum = 1.000)
float2 TapOffsets[13] = {
    float2(0.0f, 0.0f),                                   // Center (0.18)
    float2(0.0f, -0.42f * R.y),                           // Inner Up (0.09)
    float2(0.0f,  0.42f * R.y),                           // Inner Down (0.09)
    float2(-0.42f * R.x, 0.0f),                           // Inner Left (0.09)
    float2( 0.42f * R.x, 0.0f),                           // Inner Right (0.09)
    float2(-0.70f * R.x, -0.70f * R.y),                   // Diag Top-Left (0.06)
    float2( 0.70f * R.x, -0.70f * R.y),                   // Diag Top-Right (0.06)
    float2(-0.70f * R.x,  0.70f * R.y),                   // Diag Bot-Left (0.06)
    float2( 0.70f * R.x,  0.70f * R.y),                   // Diag Bot-Right (0.06)
    float2(0.0f, -R.y),                                   // Outer Up (0.05)
    float2(0.0f,  R.y),                                   // Outer Down (0.05)
    float2(-R.x, 0.0f),                                   // Outer Left (0.05)
    float2( R.x, 0.0f)                                    // Outer Right (0.05)
};

float TapWeights[13] = {
    0.18f,
    0.09f, 0.09f, 0.09f, 0.09f,
    0.06f, 0.06f, 0.06f, 0.06f,
    0.05f, 0.05f, 0.05f, 0.05f
};

float3 BlurredThermalColor = float3(0.0f, 0.0f, 0.0f);
float TotalWeight = 0.0f;

for (int k = 0; k < 13; ++k)
{
    float2 SampleUV = UV + TapOffsets[k];
    float3 TapCol = FLIRCore::EvaluatePixel(
        SampleUV,
        SceneDepthVal.r,
        StencilVal.r,
        Normal,
        WPos,
        SunDirection.xyz,
        AmbientTempK,
        HeatSensitivity,
        GroundLevelZ,
        MaxHeightCm,
        PaletteMode,
        MatTemps,
        ScreenEdge
    );
    BlurredThermalColor += TapCol * TapWeights[k];
    TotalWeight += TapWeights[k];
}

BlurredThermalColor /= TotalWeight;

// Lens corner vignette darkening
if (VignetteStr > 0.001f)
{
    float2 VigUV = UV - 0.5f;
    float VigDist = dot(VigUV, VigUV);
    float Vig = 1.0f - VigDist * VignetteStr;
    BlurredThermalColor *= saturate(Vig);
}

return saturate(BlurredThermalColor);
)hlsl");

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

// Copyright InviMind. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Generation/FOSMWorldBuilder.h"
#include "Graph/UOSMCityGraph.h"
#include "Region/FOSMRegion.h"
#include "Scene/FOSMSceneSetup.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    /** One camera position, so a run produces a comparable set rather than a single lucky angle. */
    struct FOSMSnapshotView
    {
        const TCHAR* Name;
        /** Pitch downward in degrees: 90 is straight down. */
        double PitchDegrees;
        /** Compass yaw in degrees. */
        double YawDegrees;
        /** Camera distance as a multiple of the region's diagonal. */
        double DistanceScale;
    };

    const FOSMSnapshotView Views[] = {
        { TEXT("top"),     89.0,   0.0, 0.62 },
        { TEXT("oblique"), 35.0, -45.0, 0.85 },
        { TEXT("street"),   6.0,  20.0, 0.16 },
    };
}

/**
 * Renders the generated city to PNGs, headless.
 *
 * A SceneCaptureComponent2D into a render target rather than a viewport screenshot: there is no
 * viewport in a commandline editor, and this needs none. It does need a real RHI, so run WITHOUT
 * -NullRHI and WITH -RenderOffScreen.
 *
 * The plan view in Tools/plan_view.py answers "is it in the right place"; this answers "does it
 * look right", which no amount of vertex arithmetic can. Both matter: the roofs-facing-downward
 * defect was invisible on a plan and obvious the moment anything was rendered.
 *
 *   UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests OSMWorldGen.Diagnostics.Snapshot; Quit"
 *                    -OSMDump -RenderOffScreen -unattended -nopause -nosplash
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMSnapshotTest,
    "OSMWorldGen.Diagnostics.Snapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMSnapshotTest::RunTest(const FString& Parameters)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("OSMDump")))
    {
        AddInfo(TEXT("Skipped: pass -OSMDump to render snapshots."));
        return true;
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        AddError(TEXT("No editor world available."));
        return false;
    }

    if (!FApp::CanEverRender())
    {
        AddWarning(TEXT("This process cannot render: run without -NullRHI and with -RenderOffScreen."));
        return true;
    }

    // ---- the city ----
    FString GraphPath;
    FParse::Value(FCommandLine::Get(), TEXT("-OSMGraph="), GraphPath);

    UOSMCityGraph* Graph = nullptr;
    if (!GraphPath.IsEmpty())
    {
        Graph = LoadObject<UOSMCityGraph>(nullptr, *GraphPath);
    }
    else
    {
        FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        Registry.Get().SearchAllAssets(true);

        TArray<FAssetData> Assets;
        Registry.Get().GetAssetsByClass(UOSMCityGraph::StaticClass()->GetClassPathName(), Assets);
        for (const FAssetData& Asset : Assets)
        {
            UOSMCityGraph* Candidate = Cast<UOSMCityGraph>(Asset.GetAsset());
            if (Candidate && (!Graph || Candidate->Nodes.Num() > Graph->Nodes.Num()))
            {
                Graph = Candidate;
            }
        }
    }

    if (!Graph)
    {
        AddError(TEXT("No city graph to render."));
        return false;
    }

    FOSMRegion Region;
    FString Error;
    if (!FOSMRegion::FromBoundingBox(Graph->RegionMinLat, Graph->RegionMinLon,
                                     Graph->RegionMaxLat, Graph->RegionMaxLon, Region, Error))
    {
        AddError(Error);
        return false;
    }

    // Light before geometry: an unlit capture is a black rectangle, and debugging that wastes a
    // cycle every time.
    const FOSMSceneSetup::FResult SceneResult = FOSMSceneSetup::SetUpScene(Region);
    if (!SceneResult.bSucceeded)
    {
        AddWarning(FString::Printf(TEXT("Scene setup: %s"), *SceneResult.Error));
    }

    // Layers can be rendered in isolation, which is the only reliable way to tell "this surface is
    // black" from "this surface is absent" — the two look identical against an unlit background.
    FString Layers = TEXT("terrain,buildings,roads,areas");
    FParse::Value(FCommandLine::Get(), TEXT("-OSMLayers="), Layers);

    FOSMWorldBuilder::FOptions Options;
    Options.bTerrain   = Layers.Contains(TEXT("terrain"));
    Options.bBuildings = Layers.Contains(TEXT("buildings"));
    Options.bRoads     = Layers.Contains(TEXT("roads"));
    Options.bAreas     = Layers.Contains(TEXT("areas"));
    AddInfo(FString::Printf(TEXT("Layers: %s"), *Layers));

    const FOSMWorldBuilder::FResult BuildResult = FOSMWorldBuilder::Build(*Graph, Region, Options);
    AddInfo(BuildResult.ToString());

    // ---- framing ----
    FBox Bounds(ForceInit);
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->Tags.Contains(FOSMWorldBuilder::GetGeneratedActorTag()))
        {
            FVector Origin, Extent;
            It->GetActorBounds(false, Origin, Extent);
            if (!Extent.IsNearlyZero())
            {
                Bounds += FBox(Origin - Extent, Origin + Extent);
            }
        }
    }

    if (!Bounds.IsValid)
    {
        AddError(TEXT("Nothing was built, so there is nothing to photograph."));
        return false;
    }

    const FVector Centre = Bounds.GetCenter();
    const double Diagonal = Bounds.GetSize().Size2D();

    AddInfo(FString::Printf(TEXT("City bounds %.0f x %.0f m, centre (%.0f, %.0f, %.0f)"),
        Bounds.GetSize().X / 100.0, Bounds.GetSize().Y / 100.0, Centre.X, Centre.Y, Centre.Z));

    // ---- capture ----
    int32 Resolution = 1600;
    FParse::Value(FCommandLine::Get(), TEXT("-OSMShotSize="), Resolution);
    Resolution = FMath::Clamp(Resolution, 256, 4096);

    UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>();
    Target->RenderTargetFormat = RTF_RGBA8_SRGB;
    Target->InitAutoFormat(Resolution, Resolution);
    Target->UpdateResourceImmediate(true);

    AActor* CameraActor = World->SpawnActor<AActor>();
    USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(CameraActor);
    CameraActor->SetRootComponent(Capture);
    Capture->RegisterComponent();
    Capture->TextureTarget = Target;
    // The final image. Safe to use despite there being no converged lighting in a commandline
    // editor, because the grey-box material is unlit — the colours come out exactly as assigned.
    Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->FOVAngle = 60.0f;
    Capture->ShowFlags.SetAntiAliasing(true);

    const FString OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OSMWorldGen"), TEXT("Snapshots"));

    int32 Written = 0;
    for (const FOSMSnapshotView& View : Views)
    {
        const double Distance = Diagonal * View.DistanceScale;
        const double Pitch = FMath::DegreesToRadians(View.PitchDegrees);
        const double Yaw = FMath::DegreesToRadians(View.YawDegrees);

        const FVector Offset(
            -FMath::Cos(Pitch) * FMath::Cos(Yaw) * Distance,
            -FMath::Cos(Pitch) * FMath::Sin(Yaw) * Distance,
             FMath::Sin(Pitch) * Distance);

        // Street level looks at something a person would stand near, not at the city's centroid
        // in the sky.
        const FVector LookAt = FString(View.Name) == TEXT("street")
            ? FVector(Centre.X, Centre.Y, Bounds.Min.Z + 1000.0)
            : Centre;

        CameraActor->SetActorLocation(LookAt + Offset);
        Capture->SetWorldRotation((LookAt - (LookAt + Offset)).Rotation());

        Capture->CaptureScene();

        const FString Path = FPaths::Combine(OutDir, FString::Printf(TEXT("%s.png"), View.Name));

        TArray64<uint8> Png;
        FMemoryWriter64 Writer(Png);

        if (FImageUtils::ExportRenderTarget2DAsPNG(Target, Writer)
            && FFileHelper::SaveArrayToFile(Png, *Path))
        {
            AddInfo(FString::Printf(TEXT("Snapshot: %s"), *Path));
            ++Written;
        }
        else
        {
            AddWarning(FString::Printf(TEXT("Could not write %s"), *Path));
        }
    }

    TestTrue(TEXT("At least one snapshot was rendered"), Written > 0);

    World->DestroyActor(CameraActor);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

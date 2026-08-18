// Copyright InviMind. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Diagnostics/FOSMGeometryDump.h"
#include "Generation/FOSMWorldBuilder.h"
#include "Graph/UOSMCityGraph.h"
#include "Region/FOSMRegion.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Builds a saved city graph and dumps the resulting geometry for offline inspection.
 *
 * Not really a test — it asserts almost nothing. It exists so the build can be driven from the
 * command line, producing a file that Tools/plan_view.py renders into a plan view. That loop takes
 * seconds and needs no editor, which is what makes iterating on the geometry practical: the
 * alternative is a human opening the editor and describing what they see, once per change.
 *
 * Skipped unless -OSMDump is passed, because it spawns a city's worth of actors and has no place
 * running as part of an ordinary suite.
 *
 *   UnrealEditor-Cmd <project> -run=Automation -ExecCmds="Automation RunTests OSMWorldGen.Diagnostics.PlanViewDump"
 *                    -OSMDump -OSMGraph=/Game/OSMWorldGen/Graphs/CityGraph_12p9750_77p6070
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMPlanViewDumpTest,
    "OSMWorldGen.Diagnostics.PlanViewDump",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMPlanViewDumpTest::RunTest(const FString& Parameters)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("OSMDump")))
    {
        AddInfo(TEXT("Skipped: pass -OSMDump to run the plan-view dump."));
        return true;
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        AddError(TEXT("No editor world available."));
        return false;
    }

    // Named graph if one was given, otherwise whichever saved graph is largest — the biggest
    // region is the one whose defects are worth looking at.
    FString GraphPath;
    FParse::Value(FCommandLine::Get(), TEXT("-OSMGraph="), GraphPath);

    UOSMCityGraph* Graph = nullptr;

    if (!GraphPath.IsEmpty())
    {
        Graph = LoadObject<UOSMCityGraph>(nullptr, *GraphPath);
        if (!Graph)
        {
            AddError(FString::Printf(TEXT("Could not load graph '%s'."), *GraphPath));
            return false;
        }
    }
    else
    {
        FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        Registry.Get().SearchAllAssets(/*bSynchronousSearch*/ true);

        TArray<FAssetData> Assets;
        Registry.Get().GetAssetsByClass(UOSMCityGraph::StaticClass()->GetClassPathName(), Assets);

        for (const FAssetData& Asset : Assets)
        {
            UOSMCityGraph* Candidate = Cast<UOSMCityGraph>(Asset.GetAsset());
            if (Candidate && (!Graph || Candidate->Nodes.Num() > Graph->Nodes.Num()))
            {
                Graph = Candidate;
                GraphPath = Asset.GetObjectPathString();
            }
        }

        if (!Graph)
        {
            AddError(TEXT("No saved city graph found under /Game/OSMWorldGen/Graphs."));
            return false;
        }
    }

    AddInfo(FString::Printf(TEXT("Graph '%s': %d nodes."), *GraphPath, Graph->Nodes.Num()));

    FOSMRegion Region;
    FString Error;
    if (!FOSMRegion::FromBoundingBox(Graph->RegionMinLat, Graph->RegionMinLon,
                                     Graph->RegionMaxLat, Graph->RegionMaxLon, Region, Error))
    {
        AddError(FString::Printf(TEXT("Graph region is unusable: %s"), *Error));
        return false;
    }

    FString Stem = TEXT("plan");
    FParse::Value(FCommandLine::Get(), TEXT("-OSMDumpName="), Stem);

    FOSMWorldBuilder::FOptions Options;
    Options.DebugDumpPath = FOSMGeometryDump::DefaultPath(Stem);

    const FOSMWorldBuilder::FResult BuildResult = FOSMWorldBuilder::Build(*Graph, Region, Options);

    AddInfo(BuildResult.ToString());
    for (const FString& Problem : BuildResult.Problems)
    {
        AddInfo(Problem);
    }

    TestTrue(TEXT("The dump was written"), !BuildResult.DumpPath.IsEmpty());
    AddInfo(FString::Printf(TEXT("Dump: %s"), *Options.DebugDumpPath));

    // The level is left as it was found; this is a diagnostic, not a build.
    FOSMWorldBuilder::Clear();

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

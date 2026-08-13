// Copyright InviMind. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Generation/FOSMAssetSelector.h"
#include "Generation/FOSMDryRunReport.h"
#include "Graph/UOSMCityGraph.h"
#include "Region/FOSMRegion.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Phase 4 tests: deterministic selection, the fallback chain, and the dry run.
 *
 * The properties under test are the ones that make the dry run trustworthy. A dry run that
 * predicts something other than what generation will do is worse than no dry run, because it
 * gets believed.
 */
namespace OSMGenTest
{
    UOSMCityGraph* MakeRoadGraph(int32 SegmentCount, int32 CorridorCount)
    {
        UOSMCityGraph* Graph = NewObject<UOSMCityGraph>(GetTransientPackage());
        Graph->RegionMinLat = 12.97;  Graph->RegionMaxLat = 12.98;
        Graph->RegionMinLon = 77.60;  Graph->RegionMaxLon = 77.61;

        for (int32 Index = 0; Index < SegmentCount; ++Index)
        {
            FOSMGraphNode Road;
            Road.Id = Graph->Nodes.Num();
            Road.Type = EOSMNodeType::RoadSegment;
            Road.SubType = TEXT("residential");
            Road.Name = FString::Printf(TEXT("Street %d"), Index % FMath::Max(1, CorridorCount));
            Road.Geometry = Graph->Geometry.AddPolyline({
                FVector(12.975 + Index * 1e-5, 77.605, 0.0),
                FVector(12.975 + Index * 1e-5, 77.606, 0.0) });
            Graph->Nodes.Add(Road);
        }

        // Corridor groups, mirroring what the graph builder produces for same-named streets.
        for (int32 Corridor = 0; Corridor < CorridorCount; ++Corridor)
        {
            FOSMNodeGroup Group;
            Group.Kind = EOSMGroupKind::Corridor;
            Group.NodeType = EOSMNodeType::RoadSegment;
            Group.Name = FString::Printf(TEXT("Street %d"), Corridor);

            for (const FOSMGraphNode& Node : Graph->Nodes)
            {
                if (Node.Name == Group.Name) Group.NodeIds.Add(Node.Id);
            }
            Graph->Groups.Add(Group);
        }

        return Graph;
    }

    void AddTwoChoiceRule(UOSMCityGraph& Graph, EOSMNodeType Type, float WeightA, float WeightB, bool bPerCorridor)
    {
        FOSMAssetRule& Rule = Graph.Config.FindOrAddRule(Type, FString());
        Rule.Seed = 1234;
        Rule.bSelectPerCorridor = bPerCorridor;
        Rule.Choices.Add({ FSoftObjectPath(TEXT("/Game/Fake/A.A")), TEXT("Asphalt"), WeightA });
        Rule.Choices.Add({ FSoftObjectPath(TEXT("/Game/Fake/B.B")), TEXT("Gravel"),  WeightB });
    }
}

// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMSelectionDeterminismTest,
    "OSMWorldGen.Generation.SelectionDeterminism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMSelectionDeterminismTest::RunTest(const FString& Parameters)
{
    UOSMCityGraph* Graph = OSMGenTest::MakeRoadGraph(/*Segments*/ 200, /*Corridors*/ 200);
    OSMGenTest::AddTwoChoiceRule(*Graph, EOSMNodeType::RoadSegment, 3.0f, 1.0f, /*bPerCorridor*/ false);

    const TArray<FOSMAssetSelection> First = FOSMAssetSelector::SelectForGraph(*Graph);
    const TArray<FOSMAssetSelection> Second = FOSMAssetSelector::SelectForGraph(*Graph);

    // The property the dry run depends on: run it twice, get the same answer, or the report
    // describes a generation that will not happen.
    TestEqual(TEXT("Same graph gives the same number of selections"), First.Num(), Second.Num());

    bool bIdentical = true;
    for (int32 Index = 0; Index < FMath::Min(First.Num(), Second.Num()); ++Index)
    {
        if (First[Index].ChoiceIndex != Second[Index].ChoiceIndex) bIdentical = false;
    }
    TestTrue(TEXT("Selection is reproducible"), bIdentical);

    // Weights must actually bias the outcome. 3:1 over 200 nodes should land near 75%; the
    // window is wide because this is a random draw, not a quota.
    int32 CountA = 0, Selected = 0;
    for (const FOSMAssetSelection& Selection : First)
    {
        if (Selection.IsFallback()) continue;
        ++Selected;
        if (Selection.Label == TEXT("Asphalt")) ++CountA;
    }

    TestEqual(TEXT("Every road selected something"), Selected, 200);
    const double Ratio = Selected > 0 ? static_cast<double>(CountA) / Selected : 0.0;
    TestTrue(FString::Printf(TEXT("3:1 weighting lands near 75%% (got %.1f%%)"), Ratio * 100.0),
        Ratio > 0.62 && Ratio < 0.88);

    // Changing the seed must change the assignment, or the seed is decorative.
    Graph->Config.FindOrAddRule(EOSMNodeType::RoadSegment, FString()).Seed = 9999;
    const TArray<FOSMAssetSelection> Reseeded = FOSMAssetSelector::SelectForGraph(*Graph);

    int32 Differences = 0;
    for (int32 Index = 0; Index < FMath::Min(First.Num(), Reseeded.Num()); ++Index)
    {
        if (First[Index].ChoiceIndex != Reseeded[Index].ChoiceIndex) ++Differences;
    }
    TestTrue(TEXT("A new seed reshuffles the assignment"), Differences > 0);

    return true;
}

// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMCorridorContinuityTest,
    "OSMWorldGen.Generation.CorridorContinuity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMCorridorContinuityTest::RunTest(const FString& Parameters)
{
    // 60 segments across 3 streets: 20 segments each.
    UOSMCityGraph* Graph = OSMGenTest::MakeRoadGraph(/*Segments*/ 60, /*Corridors*/ 3);
    OSMGenTest::AddTwoChoiceRule(*Graph, EOSMNodeType::RoadSegment, 1.0f, 1.0f, /*bPerCorridor*/ true);

    const TArray<FOSMAssetSelection> Selections = FOSMAssetSelector::SelectForGraph(*Graph);

    // The road-continuity rule: one street, one surface. Without this a road alternates asphalt
    // and gravel every ten metres, which is the specific defect the corridor grouping exists to
    // prevent.
    TMap<FString, int32> ChoicePerCorridor;
    bool bConsistent = true;

    for (const FOSMAssetSelection& Selection : Selections)
    {
        if (Selection.CorridorName.IsEmpty()) continue;

        if (const int32* Existing = ChoicePerCorridor.Find(Selection.CorridorName))
        {
            if (*Existing != Selection.ChoiceIndex) bConsistent = false;
        }
        else
        {
            ChoicePerCorridor.Add(Selection.CorridorName, Selection.ChoiceIndex);
        }
    }

    TestEqual(TEXT("All three corridors resolved"), ChoicePerCorridor.Num(), 3);
    TestTrue(TEXT("Every segment of a street shares one choice"), bConsistent);

    // Per-node selection must genuinely differ, or the corridor test proves nothing.
    Graph->Config.FindOrAddRule(EOSMNodeType::RoadSegment, FString()).bSelectPerCorridor = false;
    const TArray<FOSMAssetSelection> PerNode = FOSMAssetSelector::SelectForGraph(*Graph);

    TMap<FString, TSet<int32>> PerNodeChoices;
    for (const FOSMAssetSelection& Selection : PerNode)
    {
        PerNodeChoices.FindOrAdd(Selection.SubType).Add(Selection.ChoiceIndex);
    }
    TestTrue(TEXT("Per-node selection varies within a street"),
        PerNodeChoices.Num() > 0 && PerNodeChoices.CreateIterator().Value().Num() > 1);

    return true;
}

// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMFallbackChainTest,
    "OSMWorldGen.Generation.FallbackChain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMFallbackChainTest::RunTest(const FString& Parameters)
{
    UOSMCityGraph* Graph = OSMGenTest::MakeRoadGraph(/*Segments*/ 10, /*Corridors*/ 1);

    // ---- No rule at all ----
    {
        const TArray<FOSMAssetSelection> Selections = FOSMAssetSelector::SelectForGraph(*Graph);

        TestEqual(TEXT("Every node is still accounted for"), Selections.Num(), 10);

        int32 Fallbacks = 0;
        for (const FOSMAssetSelection& Selection : Selections)
        {
            if (Selection.Outcome == EOSMSelectionOutcome::FallbackNoRule) ++Fallbacks;
        }

        // The rule this enforces: a node with no asset produces a REPORTED fallback, never a
        // silent omission. Silence is how "where did my water go?" happens.
        TestEqual(TEXT("Unconfigured nodes report a no-rule fallback"), Fallbacks, 10);
    }

    // ---- Rule exists but every choice is empty ----
    {
        FOSMAssetRule& Rule = Graph->Config.FindOrAddRule(EOSMNodeType::RoadSegment, FString());
        Rule.Choices.Add({ FSoftObjectPath(), TEXT("Unassigned slot"), 1.0f });

        const TArray<FOSMAssetSelection> Selections = FOSMAssetSelector::SelectForGraph(*Graph);

        int32 NoAsset = 0;
        for (const FOSMAssetSelection& Selection : Selections)
        {
            if (Selection.Outcome == EOSMSelectionOutcome::FallbackNoAsset) ++NoAsset;
        }

        // An empty slot is a slot the user has not filled in, not a valid outcome. Treating it
        // as selectable would place nothing while reporting success.
        TestEqual(TEXT("Empty asset slots report a no-asset fallback"), NoAsset, 10);
    }

    // ---- Zero weights mean no preference, not "never pick" ----
    {
        FOSMAssetRule& Rule = Graph->Config.FindOrAddRule(EOSMNodeType::RoadSegment, FString());
        Rule.Choices.Reset();
        Rule.Choices.Add({ FSoftObjectPath(TEXT("/Game/Fake/A.A")), TEXT("A"), 0.0f });
        Rule.Choices.Add({ FSoftObjectPath(TEXT("/Game/Fake/B.B")), TEXT("B"), 0.0f });

        const TArray<FOSMAssetSelection> Selections = FOSMAssetSelector::SelectForGraph(*Graph);

        int32 Selected = 0;
        for (const FOSMAssetSelection& Selection : Selections)
        {
            if (!Selection.IsFallback()) ++Selected;
        }
        TestEqual(TEXT("All-zero weights still select something"), Selected, 10);
    }

    return true;
}

// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMDryRunTest,
    "OSMWorldGen.Generation.DryRun",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMDryRunTest::RunTest(const FString& Parameters)
{
    UOSMCityGraph* Graph = OSMGenTest::MakeRoadGraph(/*Segments*/ 40, /*Corridors*/ 4);
    OSMGenTest::AddTwoChoiceRule(*Graph, EOSMNodeType::RoadSegment, 3.0f, 1.0f, /*bPerCorridor*/ true);

    const FOSMDryRunReport Report = FOSMDryRun::Run(*Graph);

    TestEqual(TEXT("Every road is counted"), Report.TotalNodes, 40);
    TestEqual(TEXT("Every road would be placed"), Report.TotalSelected, 40);
    TestEqual(TEXT("Nothing falls back"), Report.TotalFallbacks, 0);
    TestFalse(TEXT("The run is not empty"), Report.IsEmpty());

    TestEqual(TEXT("One category reported"), Report.Categories.Num(), 1);
    if (Report.Categories.Num() == 1)
    {
        const FOSMDryRunCategory& Category = Report.Categories[0];
        TestEqual(TEXT("Category is roads"), Category.NodeType, EOSMNodeType::RoadSegment);

        // Roads must map to splines. Flattening a road into points would discard the continuity
        // that corridors exist to preserve — see docs/pcg-reference.md section 3.
        TestTrue(TEXT("Roads map to spline data"), Category.PCGDataType.Contains(TEXT("Spline")));

        int32 Total = 0;
        for (const TPair<FString, int32>& Pair : Category.AssetCounts) Total += Pair.Value;
        TestEqual(TEXT("Asset counts sum to the placed total"), Total, 40);
    }

    TestEqual(TEXT("All four corridors reported"), Report.CorridorSizes.Num(), 4);

    const FString Text = Report.ToDisplayString();
    TestTrue(TEXT("Report renders"), Text.Len() > 0);
    TestTrue(TEXT("Report states that nothing was created"), Text.Contains(TEXT("Nothing has been created")));

    // ---- Empty case says so plainly ----
    {
        UOSMCityGraph* Bare = OSMGenTest::MakeRoadGraph(/*Segments*/ 5, /*Corridors*/ 1);
        const FOSMDryRunReport EmptyRun = FOSMDryRun::Run(*Bare);

        TestTrue(TEXT("A graph with no assets reports an empty run"), EmptyRun.IsEmpty());
        TestEqual(TEXT("Its nodes are still counted"), EmptyRun.TotalNodes, 5);
        TestEqual(TEXT("And all of them are fallbacks"), EmptyRun.TotalFallbacks, 5);
        TestTrue(TEXT("The empty case explains itself"),
            EmptyRun.ToDisplayString().Contains(TEXT("Nothing would be generated")));
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

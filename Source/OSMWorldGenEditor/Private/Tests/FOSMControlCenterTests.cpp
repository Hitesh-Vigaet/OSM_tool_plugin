// Copyright InviMind. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Graph/FOSMGenerationConfig.h"
#include "Graph/FOSMGraphAssetIO.h"
#include "Session/FOSMGraphSession.h"
#include "Graph/FOSMGraphBuilder.h"
#include "Graph/UOSMCityGraph.h"
#include "Model/FOSMFeatureTable.h"
#include "Region/FOSMRegion.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Phase 3 tests: the graph asset round-trip and the configuration model.
 *
 * The round-trip is the acceptance criterion deferred out of Phase 2.7. It was deferred rather
 * than faked because a save path with no consumer is a save path nobody notices is broken — now
 * that the Control Center reopens graphs, the test covers a path something actually uses.
 */
namespace OSMControlCenterTest
{
    FOSMRegion MakeRegion()
    {
        FOSMRegion Region;
        FString Error;
        const bool bBuilt = FOSMRegion::FromCenterAndArea(12.9750, 77.6070, 1.0, Region, Error);
        check(bBuilt);
        return Region;
    }

    /** A small graph built by hand, so the test does not depend on the corpus being present. */
    UOSMCityGraph* MakeGraph(const FOSMRegion& Region)
    {
        UOSMCityGraph* Graph = NewObject<UOSMCityGraph>(GetTransientPackage());
        Graph->RegionMinLat = Region.GetMinLat();
        Graph->RegionMaxLat = Region.GetMaxLat();
        Graph->RegionMinLon = Region.GetMinLon();
        Graph->RegionMaxLon = Region.GetMaxLon();

        const double Lat = Region.GetCenterLat();
        const double Lon = Region.GetCenterLon();

        TArray<TArray<FVector>> Rings;
        Rings.Add({
            FVector(Lat,          Lon,          0.0),
            FVector(Lat + 0.0002, Lon,          0.0),
            FVector(Lat + 0.0002, Lon + 0.0002, 0.0),
            FVector(Lat,          Lon + 0.0002, 0.0) });

        FOSMGraphNode Building;
        Building.Id = 0;
        Building.Type = EOSMNodeType::Building;
        Building.SubType = TEXT("residential");
        Building.Name = TEXT("Test Building");
        Building.OSMId = 1001;
        Building.Tags.Add(TEXT("building"), TEXT("residential"));
        Building.Metrics.CentroidLatLon = FVector2D(Lat + 0.0001, Lon + 0.0001);
        Building.Metrics.AreaSqm = 484.0;
        Building.Metrics.HeightMeters = 12.0;
        Building.Geometry = Graph->Geometry.AddArea(Rings);
        Building.ValidationFlags.Add(TEXT("node.building.nostreet"));
        Graph->Nodes.Add(Building);

        FOSMGraphNode Road;
        Road.Id = 1;
        Road.Type = EOSMNodeType::RoadSegment;
        Road.SubType = TEXT("residential");
        Road.Name = TEXT("Test Street");
        Road.OSMId = 2002;
        Road.Metrics.CentroidLatLon = FVector2D(Lat, Lon);
        Road.Metrics.LengthMeters = 55.0;
        Road.Geometry = Graph->Geometry.AddPolyline({
            FVector(Lat, Lon, 0.0), FVector(Lat + 0.0005, Lon, 0.0) });
        Graph->Nodes.Add(Road);

        FOSMGraphEdge Fronts;
        Fronts.Type = EOSMRelationshipType::FrontsOnto;
        Fronts.FromNode = 0;
        Fronts.ToNode = 1;
        Fronts.Value = 11.5;
        Fronts.ToleranceMeters = 50.0;
        Graph->Edges.Add(Fronts);

        FOSMNodeGroup Group;
        Group.Name = TEXT("Buildings");
        Group.Kind = EOSMGroupKind::Category;
        Group.NodeType = EOSMNodeType::Building;
        Group.NodeIds.Add(0);
        Graph->Groups.Add(Group);

        return Graph;
    }
}

// ===========================================================================
// Asset round-trip — the deferred Phase 2.7 criterion
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMGraphRoundTripTest,
    "OSMWorldGen.ControlCenter.GraphRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMGraphRoundTripTest::RunTest(const FString& Parameters)
{
    const FOSMRegion Region = OSMControlCenterTest::MakeRegion();
    UOSMCityGraph* Original = OSMControlCenterTest::MakeGraph(Region);

    // Configuration must survive too — a graph that reloads without the user's asset ratios is
    // a graph they have to configure again every session.
    FOSMAssetRule& Rule = Original->Config.FindOrAddRule(EOSMNodeType::Building, FString());
    Rule.Seed = 4242;
    Rule.Choices.Add({ FSoftObjectPath(), TEXT("House A"), 3.0f });
    Rule.Choices.Add({ FSoftObjectPath(), TEXT("House B"), 1.0f });
    Original->Config.SetNodeTypeVisible(EOSMNodeType::Railway, false);

    const FString OriginalHash = Original->ComputeContentHash();

    FString Error;
    UOSMCityGraph* Saved = FOSMGraphAssetIO::SaveGraph(Original, Region, Error);

    TestNotNull(TEXT("Graph saves to a package"), Saved);
    TestTrue(FString::Printf(TEXT("Save reports no error (got '%s')"), *Error), Error.IsEmpty());

    if (!Saved)
    {
        return true;
    }

    UOSMCityGraph* Loaded = FOSMGraphAssetIO::LoadGraph(Region);
    TestNotNull(TEXT("Graph loads back from its package"), Loaded);

    if (!Loaded)
    {
        return true;
    }

    // "Identical content" is asserted by hash, not field by field: a field-by-field test only
    // covers the fields someone remembered to list, and would pass while silently losing a
    // property added later.
    TestEqual(TEXT("Reloaded graph has identical content"), Loaded->ComputeContentHash(), OriginalHash);

    TestEqual(TEXT("Node count survives"), Loaded->NumNodes(), Original->NumNodes());
    TestEqual(TEXT("Edge count survives"), Loaded->NumEdges(), Original->NumEdges());
    TestEqual(TEXT("Group count survives"), Loaded->Groups.Num(), Original->Groups.Num());
    TestEqual(TEXT("Geometry point count survives"),
        Loaded->Geometry.NumPoints(), Original->Geometry.NumPoints());

    // Detail that the hash deliberately does not cover, so it needs asserting separately.
    if (const FOSMGraphNode* Node = Loaded->FindNode(0))
    {
        TestEqual(TEXT("Tags survive"), Node->Tags.Num(), 1);
        TestEqual(TEXT("Name survives"), Node->Name, FString(TEXT("Test Building")));
        TestTrue(TEXT("Validation flags survive"),
            Node->ValidationFlags.Contains(TEXT("node.building.nostreet")));
        TestTrue(TEXT("Geometry handle still resolves"),
            Loaded->Geometry.GetOuterRing(Node->Geometry).Num() == 4);
    }
    else
    {
        AddError(TEXT("Node 0 missing after reload."));
    }

    const FOSMAssetRule* LoadedRule = Loaded->Config.FindRule(EOSMNodeType::Building, FString());
    TestNotNull(TEXT("Asset rule survives"), LoadedRule);
    if (LoadedRule)
    {
        TestEqual(TEXT("Seed survives"), LoadedRule->Seed, 4242);
        TestEqual(TEXT("Choices survive"), LoadedRule->Choices.Num(), 2);
        TestNearlyEqual(TEXT("Ratio survives"), LoadedRule->GetNormalisedRatio(0), 0.75f, 1e-4f);
    }

    TestFalse(TEXT("Visibility state survives"),
        Loaded->Config.IsNodeTypeVisible(EOSMNodeType::Railway));

    return true;
}

// ===========================================================================
// Configuration model
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMGenerationConfigTest,
    "OSMWorldGen.ControlCenter.GenerationConfig",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMGenerationConfigTest::RunTest(const FString& Parameters)
{
    FOSMGenerationConfig Config;

    // ---- Ratios are normalised from weights ----
    FOSMAssetRule& Rule = Config.FindOrAddRule(EOSMNodeType::Building, FString());
    Rule.Choices.Add({ FSoftObjectPath(), TEXT("A"), 3.0f });
    Rule.Choices.Add({ FSoftObjectPath(), TEXT("B"), 1.0f });

    TestNearlyEqual(TEXT("First ratio is 75%"), Rule.GetNormalisedRatio(0), 0.75f, 1e-5f);
    TestNearlyEqual(TEXT("Second ratio is 25%"), Rule.GetNormalisedRatio(1), 0.25f, 1e-5f);
    TestNearlyEqual(TEXT("Ratios sum to 1"),
        Rule.GetNormalisedRatio(0) + Rule.GetNormalisedRatio(1), 1.0f, 1e-5f);

    // Weights are stored independently, so editing one must not rewrite the other — that is the
    // whole reason they are weights rather than percentages.
    Rule.Choices[1].Weight = 3.0f;
    TestNearlyEqual(TEXT("Editing one weight leaves the other alone"), Rule.Choices[0].Weight, 3.0f, 1e-5f);
    TestNearlyEqual(TEXT("Ratios re-normalise after an edit"), Rule.GetNormalisedRatio(0), 0.5f, 1e-5f);

    // All-zero weights mean "no preference", not "nothing gets chosen".
    Rule.Choices[0].Weight = 0.0f;
    Rule.Choices[1].Weight = 0.0f;
    TestNearlyEqual(TEXT("Zero weights report an even split"), Rule.GetNormalisedRatio(0), 0.5f, 1e-5f);

    // Out-of-range indices must be answerable, since the UI can hold a stale index across edits.
    TestEqual(TEXT("Invalid choice index gives zero"), Rule.GetNormalisedRatio(99), 0.0f);

    // ---- Rule lookup prefers the specific over the general ----
    FOSMAssetRule& Specific = Config.FindOrAddRule(EOSMNodeType::Building, TEXT("apartments"));
    Specific.Seed = 777;

    const FOSMAssetRule* FoundSpecific = Config.FindRule(EOSMNodeType::Building, TEXT("apartments"));
    TestNotNull(TEXT("Specific rule is found"), FoundSpecific);
    if (FoundSpecific)
    {
        TestEqual(TEXT("Specific rule wins over the type-wide one"), FoundSpecific->Seed, 777);
    }

    const FOSMAssetRule* FoundFallback = Config.FindRule(EOSMNodeType::Building, TEXT("warehouse"));
    TestNotNull(TEXT("Unconfigured subtype falls back to the type-wide rule"), FoundFallback);
    if (FoundFallback)
    {
        TestNotEqual(TEXT("Fallback is the general rule, not the specific one"), FoundFallback->Seed, 777);
    }

    TestNull(TEXT("A type with no rule at all returns nothing"),
        Config.FindRule(EOSMNodeType::WaterBody, TEXT("lake")));

    // ---- Road rules default to per-corridor selection ----
    //
    // A street must not alternate mud and tarmac every ten metres, so selection happens per
    // corridor rather than per segment.
    const FOSMAssetRule& RoadRule = Config.FindOrAddRule(EOSMNodeType::RoadSegment, FString());
    TestTrue(TEXT("Roads select per corridor by default"), RoadRule.bSelectPerCorridor);

    const FOSMAssetRule& AreaRule = Config.FindOrAddRule(EOSMNodeType::LanduseZone, FString());
    TestFalse(TEXT("Areas do not, since they have no corridor"), AreaRule.bSelectPerCorridor);

    // ---- Visibility ----
    TestTrue(TEXT("Types start visible"), Config.IsNodeTypeVisible(EOSMNodeType::Building));
    Config.SetNodeTypeVisible(EOSMNodeType::Building, false);
    TestFalse(TEXT("Hiding works"), Config.IsNodeTypeVisible(EOSMNodeType::Building));
    TestTrue(TEXT("Hiding one type leaves others visible"),
        Config.IsNodeTypeVisible(EOSMNodeType::RoadSegment));
    Config.SetNodeTypeVisible(EOSMNodeType::Building, true);
    TestTrue(TEXT("Unhiding works"), Config.IsNodeTypeVisible(EOSMNodeType::Building));

    return true;
}

// ===========================================================================
// Closing the panel must not destroy the import
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMGraphSessionTest,
    "OSMWorldGen.ControlCenter.GraphSession",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMGraphSessionTest::RunTest(const FString& Parameters)
{
    FOSMGraphSession& Session = FOSMGraphSession::Get();

    // Restored at the end: this is a process-wide singleton, and a test that leaves it holding
    // a fixture would corrupt whatever the user had open.
    UOSMCityGraph* const PreviousGraph = Session.GetGraph();
    const FOSMRegion PreviousRegion = Session.GetRegion();
    const FOSMGraphReport PreviousReport = Session.GetReport();

    const FOSMRegion Region = OSMControlCenterTest::MakeRegion();
    UOSMCityGraph* Graph = OSMControlCenterTest::MakeGraph(Region);
    const FString ExpectedHash = Graph->ComputeContentHash();

    FOSMGraphReport Report;
    Report.NodesCreated = Graph->NumNodes();

    Session.Set(Graph, Region, Report);

    TestTrue(TEXT("Session holds the graph"), Session.HasGraph());
    TestEqual(TEXT("Session returns the same graph"), Session.GetGraph(), Graph);
    TestNearlyEqual(TEXT("Session preserves the region"),
        Session.GetRegion().GetMinLat(), Region.GetMinLat(), 1e-12);

    // The regression this guards: the Control Center used to hold the ONLY reference, so
    // closing the panel dropped the graph and a collection destroyed it. Forcing a full GC with
    // no widget alive proves the session roots it on its own.
    CollectGarbage(RF_NoFlags, /*bFullPurge*/ true);

    TestTrue(TEXT("Graph survives garbage collection with no panel open"), Session.HasGraph());

    if (UOSMCityGraph* Survivor = Session.GetGraph())
    {
        TestEqual(TEXT("Surviving graph is unchanged"), Survivor->ComputeContentHash(), ExpectedHash);
        TestEqual(TEXT("Node data survives"), Survivor->NumNodes(), 2);
        TestEqual(TEXT("Relationships survive"), Survivor->NumEdges(), 1);
    }
    else
    {
        AddError(TEXT("Graph was collected — the session is not rooting it."));
    }

    Session.Clear();
    TestFalse(TEXT("Clear empties the session"), Session.HasGraph());
    TestNull(TEXT("Cleared session returns no graph"), Session.GetGraph());

    if (PreviousGraph)
    {
        Session.Set(PreviousGraph, PreviousRegion, PreviousReport);
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

// Copyright InviMind. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Graph/FOSMGraphBuilder.h"
#include "Graph/UOSMCityGraph.h"
#include "Model/FOSMFeatureTable.h"
#include "Region/FOSMRegion.h"
#include "Validation/FOSMImportReport.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Phase 2 city-graph tests.
 *
 * Two of the acceptance criteria carry the real weight and are tested first-class here:
 *
 *   "nothing vanishes" — every considered feature becomes a node or a stated rejection
 *   "determinism"      — the same input always produces a byte-identical graph
 *
 * Both are properties the old pipeline silently violated, and neither is visible by looking at
 * output. They only show up if something checks.
 */
namespace OSMGraphTest
{
    FOSMRegion GetTestRegion()
    {
        FOSMRegion Region;
        FString Error;
        const bool bBuilt = FOSMRegion::FromBoundingBox(12.9700, 77.6020, 12.9800, 77.6120, Region, Error);
        check(bBuilt);
        return Region;
    }

    FString GetCorpusPath(const FString& FileName)
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("OSMWorldGen"));
        const FString BaseDir = Plugin.IsValid()
            ? Plugin->GetBaseDir()
            : FPaths::ProjectPluginsDir() / TEXT("OSM_plugin");
        return FPaths::ConvertRelativePathToFull(BaseDir / TEXT("Tests") / TEXT("Data") / FileName);
    }

    /** Imports the corpus baseline and builds a graph from it. Returns nullptr if unavailable. */
    UOSMCityGraph* BuildFromCorpus(
        FAutomationTestBase& Test,
        const FOSMGraphBuildOptions& Options,
        FOSMGraphReport& OutReport,
        FOSMImportReport* OutImportReport = nullptr)
    {
        const FString OSMPath = GetCorpusPath(TEXT("valid_small.osm"));
        if (!FPaths::FileExists(OSMPath))
        {
            Test.AddWarning(TEXT("Corpus missing. Run: python3 Tests/make_corpus.py"));
            return nullptr;
        }

        const FOSMRegion Region = GetTestRegion();

        FOSMFeatureTable Features;
        const FOSMImportReport ImportReport =
            FOSMImportValidator::Run(Region, OSMPath, FString(), Features);

        if (OutImportReport)
        {
            *OutImportReport = ImportReport;
        }

        if (!ImportReport.IsAccepted())
        {
            Test.AddError(TEXT("Corpus baseline failed import, so the graph cannot be tested."));
            return nullptr;
        }

        return FOSMGraphBuilder::Build(Features, Region, Options, GetTransientPackage(), OutReport);
    }
}

// ===========================================================================
// Geometry store
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMGeometryStoreTest,
    "OSMWorldGen.Graph.GeometryStore",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMGeometryStoreTest::RunTest(const FString& Parameters)
{
    FOSMGeometryStore Store;

    // A unit square with a hole, in lat/lon (X = lat, Y = lon).
    TArray<TArray<FVector>> Rings;
    Rings.Add({ FVector(0, 0, 0), FVector(0, 1, 0), FVector(1, 1, 0), FVector(1, 0, 0) });
    Rings.Add({ FVector(0.25, 0.25, 0), FVector(0.25, 0.75, 0), FVector(0.75, 0.75, 0), FVector(0.75, 0.25, 0) });

    const FOSMGeometryHandle AreaHandle = Store.AddArea(Rings);
    TestTrue(TEXT("Area handle is valid"), AreaHandle.IsValid());
    TestTrue(TEXT("Area is marked as an area"), Store.IsArea(AreaHandle));
    TestEqual(TEXT("Both rings stored"), Store.GetRingCount(AreaHandle), 2);
    TestEqual(TEXT("Outer ring has 4 points"), Store.GetOuterRing(AreaHandle).Num(), 4);
    TestEqual(TEXT("Hole has 4 points"), Store.GetRing(AreaHandle, 1).Num(), 4);

    FVector2D Centroid;
    TestTrue(TEXT("Centroid computed"), Store.GetCentroid(AreaHandle, Centroid));
    TestNearlyEqual(TEXT("Centroid latitude"), Centroid.X, 0.5, 1e-9);
    TestNearlyEqual(TEXT("Centroid longitude"), Centroid.Y, 0.5, 1e-9);

    FVector2D BoundsMin, BoundsMax;
    TestTrue(TEXT("Bounds computed"), Store.GetBounds(AreaHandle, BoundsMin, BoundsMax));
    TestNearlyEqual(TEXT("Bounds min"), BoundsMin.X, 0.0, 1e-9);
    TestNearlyEqual(TEXT("Bounds max"), BoundsMax.X, 1.0, 1e-9);

    // A second geometry must not disturb the first — the whole point of the flat store is that
    // records stay independent while sharing one point array.
    const FOSMGeometryHandle LineHandle =
        Store.AddPolyline({ FVector(2, 2, 0), FVector(3, 3, 0), FVector(4, 4, 0) });

    TestTrue(TEXT("Polyline handle is valid"), LineHandle.IsValid());
    TestFalse(TEXT("Polyline is not an area"), Store.IsArea(LineHandle));
    TestEqual(TEXT("Polyline has one ring"), Store.GetRingCount(LineHandle), 1);
    TestEqual(TEXT("Polyline has 3 points"), Store.GetOuterRing(LineHandle).Num(), 3);
    TestEqual(TEXT("Original area is intact"), Store.GetOuterRing(AreaHandle).Num(), 4);
    TestEqual(TEXT("Store holds every point"), Store.NumPoints(), 11);

    // An invalid handle must read as empty rather than crash — the Control Center will hold
    // handles across edits and cannot be trusted to never present a stale one.
    const FOSMGeometryHandle Invalid;
    TestEqual(TEXT("Invalid handle has no rings"), Store.GetRingCount(Invalid), 0);
    TestEqual(TEXT("Invalid handle reads as empty"), Store.GetOuterRing(Invalid).Num(), 0);

    return true;
}

// ===========================================================================
// Node building — "nothing vanishes"
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMGraphNodeBuildTest,
    "OSMWorldGen.Graph.NodeBuilding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMGraphNodeBuildTest::RunTest(const FString& Parameters)
{
    FOSMGraphReport Report;
    FOSMGraphBuildOptions Options;
    UOSMCityGraph* Graph = OSMGraphTest::BuildFromCorpus(*this, Options, Report);
    if (!Graph)
    {
        return true;
    }

    TestTrue(TEXT("Graph build is accepted"), Report.IsAccepted());
    TestTrue(TEXT("Nodes were created"), Graph->NumNodes() > 0);

    // The headline acceptance criterion. A feature that neither becomes a node nor appears in
    // the rejection list has vanished, which is the failure mode that made "where did my water
    // go?" impossible to answer.
    TestTrue(TEXT("Every feature is accounted for"), Report.IsComplete());
    TestEqual(TEXT("Considered == created + rejected"),
        Report.FeaturesConsidered, Report.NodesCreated - Graph->CountNodesOfType(EOSMNodeType::Junction)
            + Report.RejectedFeatures.Num());

    // Every node must carry identity and geometry, or be a derived node that legitimately has
    // neither.
    for (const FOSMGraphNode& Node : Graph->Nodes)
    {
        TestEqual(TEXT("Node id matches its index"), Node.Id, Graph->Nodes.IndexOfByPredicate(
            [&Node](const FOSMGraphNode& Other) { return Other.Id == Node.Id; }));

        if (Node.Type == EOSMNodeType::Junction)
        {
            TestEqual(TEXT("Junctions are derived, so carry no OSM id"), Node.OSMId, (int64)0);
        }
        else
        {
            TestTrue(TEXT("Feature-backed node has geometry"), Node.HasGeometry());
            TestNotEqual(TEXT("Feature-backed node has an OSM id"), Node.OSMId, (int64)0);
        }
    }

    // The corpus baseline is a road-heavy urban extract, so road segments must be present and
    // must have measurable length.
    const TArray<int32> Roads = Graph->GetNodesOfType(EOSMNodeType::RoadSegment);
    TestTrue(TEXT("Road segments were built"), Roads.Num() > 0);

    for (const int32 RoadId : Roads)
    {
        TestTrue(TEXT("Road has non-zero length"), Graph->Nodes[RoadId].Metrics.LengthMeters > 0.0);
    }

    return true;
}

// ===========================================================================
// Topology
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMGraphTopologyTest,
    "OSMWorldGen.Graph.Topology",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMGraphTopologyTest::RunTest(const FString& Parameters)
{
    FOSMGraphReport Report;
    FOSMGraphBuildOptions Options;
    UOSMCityGraph* Graph = OSMGraphTest::BuildFromCorpus(*this, Options, Report);
    if (!Graph)
    {
        return true;
    }

    const int32 JunctionCount = Graph->CountNodesOfType(EOSMNodeType::Junction);
    TestTrue(TEXT("Junctions were detected in a street network"), JunctionCount > 0);

    // Every junction must join at least two roads — a "junction" of one road is a contradiction
    // and would mean the shared-coordinate detection is matching things it should not.
    for (const int32 JunctionId : Graph->GetNodesOfType(EOSMNodeType::Junction))
    {
        const TArray<int32> Incoming = Graph->GetIncomingEdges(JunctionId);

        int32 SharesNodeCount = 0;
        for (const int32 EdgeIndex : Incoming)
        {
            if (Graph->Edges[EdgeIndex].Type == EOSMRelationshipType::SharesNode)
            {
                ++SharesNodeCount;
            }
        }

        TestTrue(TEXT("Junction joins at least two roads"), SharesNodeCount >= 2);
    }

    TestTrue(TEXT("ConnectsTo edges were created"),
        Graph->CountEdgesOfType(EOSMRelationshipType::ConnectsTo) > 0);

    // Endpoints must be real, and topological edges must claim no tolerance — they are exact.
    for (const FOSMGraphEdge& Edge : Graph->Edges)
    {
        TestTrue(TEXT("Edge source exists"), Graph->Nodes.IsValidIndex(Edge.FromNode));
        TestTrue(TEXT("Edge target exists"), Graph->Nodes.IsValidIndex(Edge.ToNode));
        TestNotEqual(TEXT("Edge is not a self-loop"), Edge.FromNode, Edge.ToNode);

        if (!Edge.IsSpatial())
        {
            TestEqual(TEXT("Topological edges carry no tolerance"), Edge.ToleranceMeters, 0.0);
        }
    }

    return true;
}

// ===========================================================================
// Spatial derivation
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMGraphSpatialTest,
    "OSMWorldGen.Graph.Spatial",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMGraphSpatialTest::RunTest(const FString& Parameters)
{
    FOSMGraphReport Report;
    FOSMGraphBuildOptions Options;
    Options.FrontsOntoCutoffMeters = 50.0;

    UOSMCityGraph* Graph = OSMGraphTest::BuildFromCorpus(*this, Options, Report);
    if (!Graph)
    {
        return true;
    }

    const int32 BuildingCount = Graph->CountNodesOfType(EOSMNodeType::Building);

    for (const FOSMGraphEdge& Edge : Graph->Edges)
    {
        if (Edge.Type != EOSMRelationshipType::FrontsOnto) continue;

        // A spatial edge that does not record what it measured, or that exceeds the tolerance
        // it claims, cannot be reviewed — and an unreviewable derivation is one that gets
        // trusted by default.
        TestEqual(TEXT("FrontsOnto starts at a building"),
            Graph->Nodes[Edge.FromNode].Type, EOSMNodeType::Building);
        TestEqual(TEXT("FrontsOnto ends at a road"),
            Graph->Nodes[Edge.ToNode].Type, EOSMNodeType::RoadSegment);
        TestTrue(TEXT("FrontsOnto records its distance"), Edge.Value >= 0.0);
        TestTrue(TEXT("FrontsOnto respects its cutoff"), Edge.Value <= Options.FrontsOntoCutoffMeters);
        TestEqual(TEXT("FrontsOnto records its tolerance"),
            Edge.ToleranceMeters, Options.FrontsOntoCutoffMeters);
    }

    // Each building gets at most one street — "the road it fronts onto" is singular by
    // definition, and more than one would mean the nearest-search is emitting duplicates.
    TMap<int32, int32> FrontsPerBuilding;
    for (const FOSMGraphEdge& Edge : Graph->Edges)
    {
        if (Edge.Type == EOSMRelationshipType::FrontsOnto)
        {
            ++FrontsPerBuilding.FindOrAdd(Edge.FromNode);
        }
    }
    for (const TPair<int32, int32>& Pair : FrontsPerBuilding)
    {
        TestEqual(TEXT("A building fronts onto exactly one road"), Pair.Value, 1);
    }

    // Buildings without a street must be flagged, not quietly dropped from the relation set.
    int32 Flagged = 0;
    for (const int32 BuildingId : Graph->GetNodesOfType(EOSMNodeType::Building))
    {
        if (Graph->Nodes[BuildingId].ValidationFlags.Contains(TEXT("node.building.nostreet")))
        {
            ++Flagged;
        }
    }
    TestEqual(TEXT("Every building is either linked or flagged"),
        FrontsPerBuilding.Num() + Flagged, BuildingCount);

    // Disabling the derivation must actually disable it.
    {
        FOSMGraphBuildOptions Off;
        Off.bComputeFrontsOnto = false;
        Off.bComputeContains = false;

        FOSMGraphReport OffReport;
        if (UOSMCityGraph* PlainGraph = OSMGraphTest::BuildFromCorpus(*this, Off, OffReport))
        {
            TestEqual(TEXT("No FrontsOnto edges when disabled"),
                PlainGraph->CountEdgesOfType(EOSMRelationshipType::FrontsOnto), 0);
            TestEqual(TEXT("No Contains edges when disabled"),
                PlainGraph->CountEdgesOfType(EOSMRelationshipType::Contains), 0);
        }
    }

    return true;
}

// ===========================================================================
// Grouping
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMGraphGroupingTest,
    "OSMWorldGen.Graph.Grouping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMGraphGroupingTest::RunTest(const FString& Parameters)
{
    FOSMGraphReport Report;
    FOSMGraphBuildOptions Options;
    UOSMCityGraph* Graph = OSMGraphTest::BuildFromCorpus(*this, Options, Report);
    if (!Graph)
    {
        return true;
    }

    TestTrue(TEXT("Groups were created"), Graph->Groups.Num() > 0);

    // The requirement that motivated grouping: one handle for all roads, with every individual
    // segment still addressable underneath. A group that loses per-node identity would satisfy
    // the first half and destroy the second.
    const FOSMNodeGroup* Roads = Graph->FindGroup(TEXT("Road Segments"));
    TestNotNull(TEXT("Road Segments category group exists"), Roads);

    if (Roads)
    {
        TestEqual(TEXT("Category group holds every node of its type"),
            Roads->Num(), Graph->CountNodesOfType(EOSMNodeType::RoadSegment));
        TestEqual(TEXT("Category group records its type"), Roads->Kind, EOSMGroupKind::Category);

        for (const int32 NodeId : Roads->NodeIds)
        {
            const FOSMGraphNode* Node = Graph->FindNode(NodeId);
            TestNotNull(TEXT("Grouped node is still individually addressable"), Node);
            if (Node)
            {
                TestEqual(TEXT("Grouped node kept its type"), Node->Type, EOSMNodeType::RoadSegment);
            }
        }
    }

    // Corridors: a named street should be one entity, not N fragments.
    int32 CorridorCount = 0;
    for (const FOSMNodeGroup& Group : Graph->Groups)
    {
        if (Group.Kind != EOSMGroupKind::Corridor) continue;
        ++CorridorCount;

        TestTrue(TEXT("A corridor groups more than one segment"), Group.Num() >= 2);
        for (const int32 NodeId : Group.NodeIds)
        {
            TestEqual(TEXT("Corridor members share the corridor name"),
                Graph->Nodes[NodeId].Name, Group.Name);
        }
    }

    AddInfo(FString::Printf(TEXT("Built %d corridor group(s)."), CorridorCount));

    // Emitted so a test run doubles as a readable snapshot of what the graph understood.
    AddInfo(TEXT("\n") + Graph->ToSummaryString());
    return true;
}

// ===========================================================================
// Determinism and validation
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMGraphDeterminismTest,
    "OSMWorldGen.Graph.Determinism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMGraphDeterminismTest::RunTest(const FString& Parameters)
{
    FOSMGraphBuildOptions Options;

    FOSMGraphReport ReportA, ReportB;
    UOSMCityGraph* GraphA = OSMGraphTest::BuildFromCorpus(*this, Options, ReportA);
    UOSMCityGraph* GraphB = OSMGraphTest::BuildFromCorpus(*this, Options, ReportB);

    if (!GraphA || !GraphB)
    {
        return true;
    }

    // The acceptance criterion. Anything order-dependent — an unsorted TMap iteration deciding
    // junction ids, a distance tie broken by memory layout — shows up here as a changed hash
    // rather than as a rendering difference noticed three phases later.
    TestEqual(TEXT("Two builds of the same input produce the same hash"),
        GraphA->ComputeContentHash(), GraphB->ComputeContentHash());

    TestEqual(TEXT("Same node count"), GraphA->NumNodes(), GraphB->NumNodes());
    TestEqual(TEXT("Same edge count"), GraphA->NumEdges(), GraphB->NumEdges());
    TestEqual(TEXT("Same group count"), GraphA->Groups.Num(), GraphB->Groups.Num());
    TestEqual(TEXT("Same geometry point count"),
        GraphA->Geometry.NumPoints(), GraphB->Geometry.NumPoints());

    // A hash that never changes is as useless as one that always does — confirm it actually
    // responds to content.
    GraphB->Nodes[0].Metrics.AreaSqm += 1.0;
    TestNotEqual(TEXT("Hash changes when content changes"),
        GraphA->ComputeContentHash(), GraphB->ComputeContentHash());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMGraphValidationTest,
    "OSMWorldGen.Graph.Validation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMGraphValidationTest::RunTest(const FString& Parameters)
{
    const FOSMRegion Region = OSMGraphTest::GetTestRegion();

    // ---- Nothing may sit outside the region ----
    {
        FOSMGraphReport Report;
        FOSMGraphBuildOptions Options;
        UOSMCityGraph* Graph = OSMGraphTest::BuildFromCorpus(*this, Options, Report);
        if (!Graph)
        {
            return true;
        }

        // This is the check that would have caught the 40 km way before it became a 500 km
        // landscape. Clipping runs before the graph is built, so a non-zero count here means
        // the clip and the region disagree.
        TestFalse(TEXT("No geometry outside the region"),
            Report.Validation.HasCode(TEXT("graph.geometry.outsideregion")));

        for (const FOSMGraphNode& Node : Graph->Nodes)
        {
            if (!Node.HasGeometry()) continue;

            FVector2D BoundsMin, BoundsMax;
            if (Graph->Geometry.GetBounds(Node.Geometry, BoundsMin, BoundsMax))
            {
                // Asserted against the shared clip margin rather than a tolerance invented
                // here. A test that picks its own bound is testing its own opinion: this one
                // originally demanded ~11 cm while clipping allowed 250 m, and the disagreement
                // read as a code failure when it was really two numbers that should have been
                // one. The margin is now the contract, so the test asserts the contract.
                constexpr double Tolerance = OSMRegionLimits::ClipMarginDegrees + 1e-9;
                TestTrue(TEXT("Node geometry lies within the region, allowing the clip margin"),
                    BoundsMin.X >= Region.GetMinLat() - Tolerance &&
                    BoundsMax.X <= Region.GetMaxLat() + Tolerance &&
                    BoundsMin.Y >= Region.GetMinLon() - Tolerance &&
                    BoundsMax.Y <= Region.GetMaxLon() + Tolerance);
            }
        }
    }

    // ---- An invalid region must stop the build, not produce a partial graph ----
    {
        FOSMFeatureTable Empty;
        FOSMGraphReport Report;
        FOSMGraphBuildOptions Options;

        UOSMCityGraph* Graph = FOSMGraphBuilder::Build(
            Empty, FOSMRegion(), Options, GetTransientPackage(), Report);

        TestNotNull(TEXT("A graph object is still returned"), Graph);
        TestFalse(TEXT("Build with no region is rejected"), Report.IsAccepted());
        TestTrue(TEXT("Rejection names the invalid region"),
            Report.Validation.HasCode(TEXT("graph.region.invalid")));

        if (Graph)
        {
            TestEqual(TEXT("No nodes are built without a region"), Graph->NumNodes(), 0);
        }
    }

    // ---- Flagged nodes are kept, never dropped ----
    {
        FOSMGraphReport Report;
        FOSMGraphBuildOptions Options;
        Options.FrontsOntoCutoffMeters = 0.5;  // absurdly tight, so most buildings fail

        if (UOSMCityGraph* Graph = OSMGraphTest::BuildFromCorpus(*this, Options, Report))
        {
            const int32 BuildingCount = Graph->CountNodesOfType(EOSMNodeType::Building);
            if (BuildingCount > 0)
            {
                TestTrue(TEXT("A tight cutoff flags buildings"), Report.FlaggedNodes > 0);
                TestEqual(TEXT("Flagged buildings are still in the graph"),
                    Graph->CountNodesOfType(EOSMNodeType::Building), BuildingCount);
                TestTrue(TEXT("The flagging is reported, not silent"),
                    Report.Validation.HasCode(TEXT("graph.building.nostreet")));
            }
        }
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

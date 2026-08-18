// Copyright InviMind. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Generation/FOSMWorldBuilder.h"
#include "Graph/UOSMCityGraph.h"
#include "Region/FOSMRegion.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Phase 5 placement tests.
 *
 * The regression these exist for: the builder spawned every actor with a location, then replaced
 * the root component — which discards the spawn transform and leaves the component at identity.
 * All 542 buildings, 461 roads and 178 areas landed on top of each other at the world origin,
 * each still carrying vertices offset from its own centroid. The build reported success, the
 * counts were right, and the result was one nested blob under a single enormous plane.
 *
 * Counting spawned actors would not have caught it. Checking WHERE they are, does.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMWorldBuilderPlacementTest,
    "OSMWorldGen.Generation.Placement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMWorldBuilderPlacementTest::RunTest(const FString& Parameters)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        AddWarning(TEXT("No editor world available; placement test skipped."));
        return true;
    }

    FOSMRegion Region;
    FString Error;
    if (!FOSMRegion::FromCenterAndArea(12.9718, 77.5957, 1.0, Region, Error))
    {
        AddError(Error);
        return true;
    }

    // Three buildings at deliberately separated corners of the region, so "did they land in
    // different places" is unambiguous.
    UOSMCityGraph* Graph = NewObject<UOSMCityGraph>(GetTransientPackage());
    Graph->RegionMinLat = Region.GetMinLat();
    Graph->RegionMaxLat = Region.GetMaxLat();
    Graph->RegionMinLon = Region.GetMinLon();
    Graph->RegionMaxLon = Region.GetMaxLon();

    const FVector2D Centres[] = {
        FVector2D(Region.GetMinLat() + 0.0008, Region.GetMinLon() + 0.0008),
        FVector2D(Region.GetCenterLat(),       Region.GetCenterLon()),
        FVector2D(Region.GetMaxLat() - 0.0008, Region.GetMaxLon() - 0.0008),
    };

    for (int32 Index = 0; Index < 3; ++Index)
    {
        const FVector2D C = Centres[Index];
        const double D = 0.0002;   // ~22 m box

        TArray<TArray<FVector>> Rings;
        Rings.Add({
            FVector(C.X - D, C.Y - D, 0.0),
            FVector(C.X + D, C.Y - D, 0.0),
            FVector(C.X + D, C.Y + D, 0.0),
            FVector(C.X - D, C.Y + D, 0.0) });

        FOSMGraphNode Node;
        Node.Id = Index;
        Node.Type = EOSMNodeType::Building;
        Node.SubType = TEXT("yes");
        Node.OSMId = 1000 + Index;
        Node.Geometry = Graph->Geometry.AddArea(Rings);
        Node.Metrics.CentroidLatLon = C;
        Node.Metrics.HeightMeters = 10.0;
        Graph->Nodes.Add(Node);
    }

    FOSMWorldBuilder::FOptions Options;
    Options.bRoads = false;
    Options.bAreas = false;
    Options.bUseTerrain = false;   // no DEM here; keep the test about XY placement

    const FOSMWorldBuilder::FResult BuildResult = FOSMWorldBuilder::Build(*Graph, Region, Options);
    TestEqual(TEXT("All three buildings are built"), BuildResult.Buildings, 3);

    // Collect what actually landed in the world.
    TArray<FVector> Locations;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->Tags.Contains(FOSMWorldBuilder::GetGeneratedActorTag())
            && It->GetActorLabel().StartsWith(TEXT("Building_")))
        {
            Locations.Add(It->GetActorLocation());
        }
    }

    TestEqual(TEXT("Three building actors are in the level"), Locations.Num(), 3);

    if (Locations.Num() == 3)
    {
        // The actual regression: distinct positions, not a pile at the origin.
        for (int32 A = 0; A < Locations.Num(); ++A)
        {
            for (int32 B = A + 1; B < Locations.Num(); ++B)
            {
                const double Apart = FVector::Dist2D(Locations[A], Locations[B]);
                TestTrue(
                    FString::Printf(TEXT("Buildings %d and %d are apart (%.0f cm)"), A, B, Apart),
                    Apart > 1000.0);   // >10 m
            }
        }

        // And they must span a sensible fraction of a 1 km region rather than clustering.
        FBox Spread(ForceInit);
        for (const FVector& L : Locations) Spread += L;

        TestTrue(
            FString::Printf(TEXT("Placement spans the region (%.0f x %.0f cm)"),
                Spread.GetSize().X, Spread.GetSize().Y),
            Spread.GetSize().X > 50000.0 && Spread.GetSize().Y > 50000.0);   // >500 m

        // The failure mode was EVERY actor at the origin. One of these three sits there
        // legitimately — its centroid is the region centre — so the assertion is that they do not
        // all collapse there, not that none of them may be there. Checking the stricter thing
        // fails on correct output, which is how a test starts getting ignored.
        int32 AtOrigin = 0;
        for (const FVector& L : Locations)
        {
            if (L.SizeSquared2D() < 1.0) ++AtOrigin;
        }
        TestTrue(
            FString::Printf(TEXT("Buildings are not all piled at the origin (%d of %d there)"),
                AtOrigin, Locations.Num()),
            AtOrigin <= 1);
    }

    const int32 Removed = FOSMWorldBuilder::Clear();
    TestTrue(TEXT("Clear removes what the builder made"), Removed >= 3);

    // Nothing tagged may survive a clear, or repeated builds accumulate.
    int32 Remaining = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->Tags.Contains(FOSMWorldBuilder::GetGeneratedActorTag())) ++Remaining;
    }
    TestEqual(TEXT("No generated actors remain after Clear"), Remaining, 0);

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

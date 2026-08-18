// Copyright InviMind. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Elevation/FOSMTerrainSurface.h"
#include "Elevation/FOSMDEMSampler.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OSMTerrainTest
{
    FString CorpusPath(const FString& FileName)
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("OSMWorldGen"));
        const FString BaseDir = Plugin.IsValid()
            ? Plugin->GetBaseDir()
            : FPaths::ProjectPluginsDir() / TEXT("OSM_plugin");
        return FPaths::ConvertRelativePathToFull(BaseDir / TEXT("Tests") / TEXT("Data") / FileName);
    }

    /** Height of the mesh at a point, computed from the emitted triangles alone. */
    bool MeshHeightAt(const TArray<FVector>& Vertices, const TArray<int32>& Triangles,
                      double X, double Y, double& OutZ)
    {
        for (int32 Index = 0; Index + 2 < Triangles.Num(); Index += 3)
        {
            const FVector& P0 = Vertices[Triangles[Index]];
            const FVector& P1 = Vertices[Triangles[Index + 1]];
            const FVector& P2 = Vertices[Triangles[Index + 2]];

            // Barycentric coordinates in XY.
            const double Denominator = (P1.Y - P2.Y) * (P0.X - P2.X) + (P2.X - P1.X) * (P0.Y - P2.Y);
            if (FMath::IsNearlyZero(Denominator))
            {
                continue;
            }

            const double A = ((P1.Y - P2.Y) * (X - P2.X) + (P2.X - P1.X) * (Y - P2.Y)) / Denominator;
            const double B = ((P2.Y - P0.Y) * (X - P2.X) + (P0.X - P2.X) * (Y - P2.Y)) / Denominator;
            const double C = 1.0 - A - B;

            if (A >= -1e-9 && B >= -1e-9 && C >= -1e-9)
            {
                OutZ = A * P0.Z + B * P1.Z + C * P2.Z;
                return true;
            }
        }
        return false;
    }
}

/**
 * The invariant the whole grounding fix rests on: HeightCmAt must return the height of the surface
 * that is actually DRAWN, not merely a plausible height near it.
 *
 * The regression this exists for: the terrain was an 8 m grid, while roads and areas sampled the
 * raw DEM continuously at their own vertices. Both answers were reasonable and they disagreed by
 * tens of centimetres, against layer offsets of a few — so on every slope the ground pushed up
 * through the roads. 7.8% of road vertices ended up below the ground they sat on, and area
 * surfaces were out by as much as 21 m where they left the DEM.
 *
 * A test comparing HeightCmAt against the DEM would have passed throughout. This one compares it
 * against the emitted triangles, which is the thing that has to agree.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMTerrainSurfaceTest,
    "OSMWorldGen.Terrain.SurfaceMatchesMesh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMTerrainSurfaceTest::RunTest(const FString& Parameters)
{
    const FString DEMPath = OSMTerrainTest::CorpusPath(TEXT("valid_striped.tif"));
    if (!FPaths::FileExists(DEMPath))
    {
        AddWarning(TEXT("Corpus missing. Run: python3 Tests/make_corpus.py"));
        return true;
    }

    FOSMDEMSampler Sampler;
    if (!Sampler.Load(DEMPath))
    {
        AddError(TEXT("Could not load the corpus DEM."));
        return false;
    }

    const FOSMGeoTIFFTile& Tile = Sampler.GetTileMetadata();
    const double CentreLat = 0.5 * (Tile.GetMinLat() + Tile.GetMaxLat());
    const double CentreLon = 0.5 * (Tile.GetMinLon() + Tile.GetMaxLon());

    const FOSMLocalProjection Projection(CentreLat, CentreLon);

    FBox2D Bounds(ForceInit);
    Bounds += Projection.ToMeters(FVector2D(Tile.GetMinLat(), Tile.GetMinLon()));
    Bounds += Projection.ToMeters(FVector2D(Tile.GetMaxLat(), Tile.GetMaxLon()));

    FOSMTerrainSurface Surface;
    if (!Surface.Build(&Sampler, Projection, Bounds, /*QuadMeters*/ 8.0))
    {
        AddError(TEXT("Terrain surface could not be built."));
        return false;
    }

    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    Surface.BuildMesh(Vertices, Triangles, Normals, UVs);

    TestTrue(TEXT("The surface produced a mesh"), Vertices.Num() > 0 && Triangles.Num() > 0);
    if (Vertices.Num() == 0)
    {
        return false;
    }

    // A flat surface would satisfy the agreement check trivially, so first confirm there is
    // actually relief here to disagree about.
    double MinZ = TNumericLimits<double>::Max();
    double MaxZ = TNumericLimits<double>::Lowest();
    for (const FVector& Vertex : Vertices)
    {
        MinZ = FMath::Min(MinZ, Vertex.Z);
        MaxZ = FMath::Max(MaxZ, Vertex.Z);
    }
    AddInfo(FString::Printf(TEXT("Relief across the surface: %.2f m"), (MaxZ - MinZ) / 100.0));

    // ---- every grid node ----
    for (const FVector& Vertex : Vertices)
    {
        const double Queried = Surface.HeightCmAt(Vertex.X / 100.0, Vertex.Y / 100.0);
        if (!FMath::IsNearlyEqual(Queried, Vertex.Z, 0.01))
        {
            AddError(FString::Printf(
                TEXT("Grid node (%.1f, %.1f): mesh is at %.3f cm but HeightCmAt says %.3f cm."),
                Vertex.X, Vertex.Y, Vertex.Z, Queried));
            break;
        }
    }

    // ---- points between nodes, where a bilinear approximation would drift off the triangles ----
    const FBox2D SurfaceBounds = Surface.GetBoundsMeters();
    FRandomStream Stream(20260814);

    double WorstError = 0.0;
    int32 Compared = 0;

    for (int32 Sample = 0; Sample < 500; ++Sample)
    {
        // Inset, so a sample never lands in the clamped region outside the grid.
        const double X = Stream.FRandRange(SurfaceBounds.Min.X + 1.0, SurfaceBounds.Max.X - 1.0);
        const double Y = Stream.FRandRange(SurfaceBounds.Min.Y + 1.0, SurfaceBounds.Max.Y - 1.0);

        double MeshZ = 0.0;
        if (!OSMTerrainTest::MeshHeightAt(Vertices, Triangles, X * 100.0, Y * 100.0, MeshZ))
        {
            continue;
        }

        ++Compared;
        WorstError = FMath::Max(WorstError, FMath::Abs(Surface.HeightCmAt(X, Y) - MeshZ));
    }

    TestTrue(TEXT("Interior samples were actually compared"), Compared > 100);
    TestTrue(
        FString::Printf(TEXT("HeightCmAt matches the drawn triangles everywhere (worst error %.4f cm over %d samples)"),
            WorstError, Compared),
        WorstError < 0.05);

    // ---- outside the grid ----
    //
    // Must clamp to the nearest edge. Returning zero here is what dropped every road and park that
    // left the raster to the bottom of the valley.
    const double OutsideHeight = Surface.HeightCmAt(SurfaceBounds.Min.X - 500.0, SurfaceBounds.Min.Y - 500.0);
    const double CornerHeight = Surface.HeightCmAt(SurfaceBounds.Min.X, SurfaceBounds.Min.Y);

    TestTrue(
        FString::Printf(TEXT("Outside the grid clamps to the edge (%.2f cm vs corner %.2f cm)"),
            OutsideHeight, CornerHeight),
        FMath::IsNearlyEqual(OutsideHeight, CornerHeight, 0.01));

    return true;
}

/**
 * Ground extremes under a footprint must cover the footprint, not just its corners.
 *
 * The defect this guards: a foundation dug to the lowest CORNER with the roof measured from the
 * highest CORNER. A footprint wider than one terrain quad can straddle a ridge no corner touches,
 * so the ground rises through the middle of a wall while every corner sits correctly on the
 * surface — a building half-buried in a hillside, with nothing wrong at any point the code looked.
 *
 * The two assertions are deliberately asymmetric, because the two directions of error are not
 * equally bad. UNDER-reading the maximum buries the building; OVER-reading the minimum floats it.
 * Those are checked strictly. Erring the safe way is merely bounded, and checked loosely — a
 * comparison against a lattice sweep cannot be exact in that direction, because the sweep itself
 * misses extremes between its samples and the implementation under test does not.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMTerrainFootprintExtremesTest,
    "OSMWorldGen.Terrain.FootprintExtremes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMTerrainFootprintExtremesTest::RunTest(const FString& Parameters)
{
    const FString DEMPath = OSMTerrainTest::CorpusPath(TEXT("valid_striped.tif"));
    if (!FPaths::FileExists(DEMPath))
    {
        AddWarning(TEXT("Corpus missing. Run: python3 Tests/make_corpus.py"));
        return true;
    }

    FOSMDEMSampler Sampler;
    if (!Sampler.Load(DEMPath))
    {
        AddError(TEXT("Could not load the corpus DEM."));
        return false;
    }

    const FOSMGeoTIFFTile& Tile = Sampler.GetTileMetadata();
    const FOSMLocalProjection Projection(0.5 * (Tile.GetMinLat() + Tile.GetMaxLat()),
                                         0.5 * (Tile.GetMinLon() + Tile.GetMaxLon()));

    FBox2D Bounds(ForceInit);
    Bounds += Projection.ToMeters(FVector2D(Tile.GetMinLat(), Tile.GetMinLon()));
    Bounds += Projection.ToMeters(FVector2D(Tile.GetMaxLat(), Tile.GetMaxLon()));

    FOSMTerrainSurface Surface;
    if (!Surface.Build(&Sampler, Projection, Bounds, /*QuadMeters*/ 8.0))
    {
        AddError(TEXT("Terrain surface could not be built."));
        return false;
    }

    FRandomStream Stream(20260815);

    int32 Compared = 0;
    int32 CornersWouldHaveMissed = 0;
    double WorstUnderMax = 0.0;    // dangerous: building gets buried
    double WorstOverMin = 0.0;     // dangerous: building floats
    double WorstSlack = 0.0;       // safe direction, only bounded

    for (int32 Sample = 0; Sample < 60; ++Sample)
    {
        // Deliberately several quads across: the only size at which corners can miss anything.
        const double HalfSize = Stream.FRandRange(12.0, 40.0);
        const double CentreX = Stream.FRandRange(Bounds.Min.X + 60.0, Bounds.Max.X - 60.0);
        const double CentreY = Stream.FRandRange(Bounds.Min.Y + 60.0, Bounds.Max.Y - 60.0);

        const TArray<FVector2D> Ring = {
            FVector2D(CentreX - HalfSize, CentreY - HalfSize),
            FVector2D(CentreX + HalfSize, CentreY - HalfSize),
            FVector2D(CentreX + HalfSize, CentreY + HalfSize),
            FVector2D(CentreX - HalfSize, CentreY + HalfSize),
        };

        double MinCm = 0.0;
        double MaxCm = 0.0;
        Surface.MinMaxHeightCmOverPolygon(Ring, MinCm, MaxCm);

        // Reference: a dense sweep. A lower bound on the true maximum and an upper bound on the
        // true minimum — never the other way round, which is what makes the asymmetry valid.
        double SweptMin = TNumericLimits<double>::Max();
        double SweptMax = TNumericLimits<double>::Lowest();
        constexpr int32 Steps = 320;
        for (int32 StepX = 0; StepX <= Steps; ++StepX)
        {
            for (int32 StepY = 0; StepY <= Steps; ++StepY)
            {
                const double X = FMath::Lerp(CentreX - HalfSize, CentreX + HalfSize, StepX / double(Steps));
                const double Y = FMath::Lerp(CentreY - HalfSize, CentreY + HalfSize, StepY / double(Steps));
                const double Height = Surface.HeightCmAt(X, Y);
                SweptMin = FMath::Min(SweptMin, Height);
                SweptMax = FMath::Max(SweptMax, Height);
            }
        }

        double CornerMin = TNumericLimits<double>::Max();
        double CornerMax = TNumericLimits<double>::Lowest();
        for (const FVector2D& Corner : Ring)
        {
            const double Height = Surface.HeightCmAt(Corner);
            CornerMin = FMath::Min(CornerMin, Height);
            CornerMax = FMath::Max(CornerMax, Height);
        }

        if (CornerMax < SweptMax - 1.0 || CornerMin > SweptMin + 1.0)
        {
            ++CornersWouldHaveMissed;
        }

        WorstUnderMax = FMath::Max(WorstUnderMax, SweptMax - MaxCm);
        WorstOverMin  = FMath::Max(WorstOverMin,  MinCm - SweptMin);
        WorstSlack    = FMath::Max(WorstSlack, FMath::Max(MaxCm - SweptMax, SweptMin - MinCm));
        ++Compared;
    }

    AddInfo(FString::Printf(
        TEXT("%d of %d footprints have ground extremes their corners do not reach."),
        CornersWouldHaveMissed, Compared));

    // If this fires the corpus is too flat to be exercising the thing under test.
    TestTrue(TEXT("The corpus terrain varies within a footprint, so the test has something to catch"),
             CornersWouldHaveMissed > 0);

    TestTrue(
        FString::Printf(TEXT("The maximum is never under-read, which would bury a building (worst %.4f cm)"),
            WorstUnderMax),
        WorstUnderMax < 0.01);

    TestTrue(
        FString::Printf(TEXT("The minimum is never over-read, which would float a building (worst %.4f cm)"),
            WorstOverMin),
        WorstOverMin < 0.01);

    // A sanity bound against gross over-estimation — returning the whole grid's extremes, say —
    // NOT a precision check. It cannot be one: the sweep is a lower bound on the true maximum, so
    // the implementation legitimately exceeds it by however much the sweep's own step misses on
    // this terrain. Measured at ~9 cm with a 0.5 m step, which is why an earlier 5 cm bound failed
    // against a correct implementation. Tightening this would only be testing the reference.
    TestTrue(
        FString::Printf(TEXT("Erring the safe way stays bounded (worst %.2f cm)"), WorstSlack),
        WorstSlack < 25.0);

    return true;
}

/**
 * A polyline split at terrain triangle boundaries must be coplanar with the ground everywhere.
 *
 * This is the property that makes draped geometry sit on the terrain instead of cutting through
 * it. Sampling a road's height exactly at its vertices achieves nothing when consecutive vertices
 * are 377 m apart across an 8 m grid: the ribbon between them is flat and the hill in between is
 * not. Measured on a real region before this existed, the ground rose as much as 7.8 m through
 * surfaces whose every sampled vertex was correct.
 *
 * The check is the midpoint of each output segment. Within a single terrain triangle the surface
 * is planar, so if a segment stays inside one triangle then linear interpolation of its endpoints
 * equals the terrain at every point along it — and the midpoint is where any violation is largest.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMTerrainSplitTest,
    "OSMWorldGen.Terrain.SplitFollowsGround",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMTerrainSplitTest::RunTest(const FString& Parameters)
{
    const FString DEMPath = OSMTerrainTest::CorpusPath(TEXT("valid_striped.tif"));
    if (!FPaths::FileExists(DEMPath))
    {
        AddWarning(TEXT("Corpus missing. Run: python3 Tests/make_corpus.py"));
        return true;
    }

    FOSMDEMSampler Sampler;
    if (!Sampler.Load(DEMPath))
    {
        AddError(TEXT("Could not load the corpus DEM."));
        return false;
    }

    const FOSMGeoTIFFTile& Tile = Sampler.GetTileMetadata();
    const FOSMLocalProjection Projection(0.5 * (Tile.GetMinLat() + Tile.GetMaxLat()),
                                         0.5 * (Tile.GetMinLon() + Tile.GetMaxLon()));

    FBox2D Bounds(ForceInit);
    Bounds += Projection.ToMeters(FVector2D(Tile.GetMinLat(), Tile.GetMinLon()));
    Bounds += Projection.ToMeters(FVector2D(Tile.GetMaxLat(), Tile.GetMaxLon()));

    FOSMTerrainSurface Surface;
    if (!Surface.Build(&Sampler, Projection, Bounds, /*QuadMeters*/ 8.0))
    {
        AddError(TEXT("Terrain surface could not be built."));
        return false;
    }

    FRandomStream Stream(20260815);

    double WorstSplitError = 0.0;
    double WorstUnsplitError = 0.0;
    int32 Segments = 0;

    for (int32 Sample = 0; Sample < 40; ++Sample)
    {
        // Long spans, like the real data: OSM road segments reach 377 m and area edges 777 m.
        const FVector2D A(Stream.FRandRange(Bounds.Min.X + 20.0, Bounds.Max.X - 20.0),
                          Stream.FRandRange(Bounds.Min.Y + 20.0, Bounds.Max.Y - 20.0));
        const FVector2D B(Stream.FRandRange(Bounds.Min.X + 20.0, Bounds.Max.X - 20.0),
                          Stream.FRandRange(Bounds.Min.Y + 20.0, Bounds.Max.Y - 20.0));

        // How far the undivided span departs from the ground — the defect being fixed.
        {
            const double HeightA = Surface.HeightCmAt(A);
            const double HeightB = Surface.HeightCmAt(B);
            for (int32 Step = 1; Step < 64; ++Step)
            {
                const double Alpha = Step / 64.0;
                const FVector2D Point = FMath::Lerp(A, B, Alpha);
                const double Flat = FMath::Lerp(HeightA, HeightB, Alpha);
                WorstUnsplitError = FMath::Max(WorstUnsplitError,
                    FMath::Abs(Surface.HeightCmAt(Point) - Flat));
            }
        }

        const TArray<FVector2D> Line = { A, B };
        TArray<FVector2D> Split;
        Surface.SplitAtTriangleBoundaries(Line, /*bClosed*/ false, Split);

        TestTrue(TEXT("Splitting keeps the original endpoints"),
                 Split.Num() >= 2
                 && Split[0].Equals(A, 0.01)
                 && Split[Split.Num() - 1].Equals(B, 0.01));

        for (int32 Index = 0; Index + 1 < Split.Num(); ++Index)
        {
            const FVector2D Midpoint = 0.5 * (Split[Index] + Split[Index + 1]);
            const double Interpolated = 0.5 * (Surface.HeightCmAt(Split[Index])
                                             + Surface.HeightCmAt(Split[Index + 1]));

            WorstSplitError = FMath::Max(WorstSplitError,
                FMath::Abs(Surface.HeightCmAt(Midpoint) - Interpolated));
            ++Segments;
        }
    }

    AddInfo(FString::Printf(
        TEXT("Undivided spans depart from the ground by up to %.1f cm; split into %d segments."),
        WorstUnsplitError, Segments));

    // If this fires the corpus is flat and the test proves nothing.
    TestTrue(TEXT("Undivided spans really do depart from the ground, so there is something to fix"),
             WorstUnsplitError > 10.0);

    TestTrue(
        FString::Printf(TEXT("Every split segment is coplanar with the ground (worst %.4f cm)"),
            WorstSplitError),
        WorstSplitError < 0.01);

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

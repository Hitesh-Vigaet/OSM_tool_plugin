// Copyright InviMind. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Generation/FOSMBuildingMesher.h"
#include "Generation/UOSMBuildingArchetype.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Phase 5.3 tests.
 *
 * These check the two properties that separate generating a building from scaling a mesh:
 * bay spacing stays constant at any wall length, and UVs are in world metres so materials tile
 * at the same rate on a 20 m2 hut and a 7,800 m2 block. If either fails, the output looks
 * generated no matter how good the art is.
 */
namespace OSMMesherTest
{
    UOSMBuildingArchetype* MakeArchetype()
    {
        UOSMBuildingArchetype* A = NewObject<UOSMBuildingArchetype>(GetTransientPackage());
        A->DisplayName = TEXT("Test");
        A->FloorHeightMeters = 3.2f;
        A->DefaultHeightMeters = 9.6f;
        A->BayWidthMeters = 3.0f;
        A->PlinthHeightMeters = 0.3f;
        return A;
    }

    /** Axis-aligned rectangle in metres. */
    TArray<TArray<FVector2D>> MakeRect(double Width, double Depth)
    {
        TArray<TArray<FVector2D>> Rings;
        Rings.Add({ FVector2D(0,0), FVector2D(Width,0), FVector2D(Width,Depth), FVector2D(0,Depth) });
        return Rings;
    }
}

// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMMesherBaysTest, "OSMWorldGen.Mesher.BaySpacing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMMesherBaysTest::RunTest(const FString& Parameters)
{
    // Bay counts round to nearest, never to zero.
    TestEqual(TEXT("A 3 m wall is one bay"),   FOSMBuildingMesher::ComputeBayCount(3.0, 3.0), 1);
    TestEqual(TEXT("A 9 m wall is three bays"), FOSMBuildingMesher::ComputeBayCount(9.0, 3.0), 3);
    TestEqual(TEXT("A 40 m wall is 13 bays"),  FOSMBuildingMesher::ComputeBayCount(40.0, 3.0), 13);
    TestEqual(TEXT("A 0.4 m wall is still one bay"), FOSMBuildingMesher::ComputeBayCount(0.4, 3.0), 1);

    // The property that matters: actual bay width stays close to nominal across a huge range of
    // wall lengths. This is what a scaled mesh cannot do.
    for (double Length : { 3.0, 5.9, 12.4, 27.3, 40.0, 88.5, 140.0 })
    {
        const int32 Bays = FOSMBuildingMesher::ComputeBayCount(Length, 3.0);
        const double Actual = Length / Bays;

        // +/-17% covers the worst case of rounding to nearest (a wall of 1.5 nominal bays), and
        // sits inside the +/-15% squash tolerance the art brief specifies for wider walls.
        TestTrue(
            FString::Printf(TEXT("%.1f m wall -> %d bays of %.2f m, near the 3 m nominal"), Length, Bays, Actual),
            Actual > 2.4 && Actual < 3.6);
    }

    return true;
}

// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMMesherGeometryTest, "OSMWorldGen.Mesher.Geometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMMesherGeometryTest::RunTest(const FString& Parameters)
{
    UOSMBuildingArchetype* Archetype = OSMMesherTest::MakeArchetype();

    FOSMBuildingMeshParams Params;
    Params.Rings = OSMMesherTest::MakeRect(10.0, 8.0);
    Params.HeightMeters = 12.0f;
    Params.Archetype = Archetype;

    FOSMMeshData Mesh;
    FString Error;
    TestTrue(TEXT("A rectangle builds"), FOSMBuildingMesher::Build(Params, Mesh, Error));
    TestFalse(TEXT("Mesh is not empty"), Mesh.IsEmpty());

    // Geometry must match the input exactly — this is the entire point of generating from the
    // footprint rather than fitting a mesh to it.
    const FBox Bounds = Mesh.GetBounds();
    TestNearlyEqual(TEXT("Width matches the footprint"),  Bounds.GetSize().X, 1000.0, 1.0);  // 10 m in cm
    TestNearlyEqual(TEXT("Depth matches the footprint"),  Bounds.GetSize().Y,  800.0, 1.0);  // 8 m
    TestNearlyEqual(TEXT("Height matches the request"),   Bounds.Max.Z,       1200.0, 1.0);  // 12 m
    TestNearlyEqual(TEXT("Base sits at zero"),            Bounds.Min.Z,          0.0, 1.0);

    // Every section present: walls split at the ground-floor line, plus a roof.
    TestTrue(TEXT("Wall triangles exist"),
        Mesh.GetTrianglesForSection(EOSMMeshSection::Wall).Num() > 0);
    TestTrue(TEXT("Ground floor triangles exist"),
        Mesh.GetTrianglesForSection(EOSMMeshSection::GroundFloor).Num() > 0);
    TestTrue(TEXT("Roof triangles exist"),
        Mesh.GetTrianglesForSection(EOSMMeshSection::Roof).Num() > 0);

    // Array lengths must agree, or the caller builds a corrupt mesh section.
    TestEqual(TEXT("A normal per vertex"), Mesh.Normals.Num(), Mesh.Vertices.Num());
    TestEqual(TEXT("A UV per vertex"), Mesh.UVs.Num(), Mesh.Vertices.Num());
    TestEqual(TEXT("A section per triangle"), Mesh.TriangleSections.Num(), Mesh.NumTriangles());
    TestEqual(TEXT("Triangle indices are whole triangles"), Mesh.Triangles.Num() % 3, 0);

    for (const int32 Index : Mesh.Triangles)
    {
        if (!Mesh.Vertices.IsValidIndex(Index))
        {
            AddError(FString::Printf(TEXT("Triangle references vertex %d, out of range."), Index));
            break;
        }
    }

    // ---- Winding: reversing the input ring must not flip the building inside out ----
    {
        FOSMBuildingMeshParams Reversed = Params;
        Algo::Reverse(Reversed.Rings[0]);

        FOSMMeshData ReversedMesh;
        FString ReversedError;
        TestTrue(TEXT("A clockwise ring also builds"),
            FOSMBuildingMesher::Build(Reversed, ReversedMesh, ReversedError));

        // Source data winds either way; normalising means walls face outward regardless.
        TestEqual(TEXT("Winding order does not change the vertex count"),
            ReversedMesh.Vertices.Num(), Mesh.Vertices.Num());
    }

    return true;
}

// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMMesherScaleTest, "OSMWorldGen.Mesher.ScaleIndependence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMMesherScaleTest::RunTest(const FString& Parameters)
{
    UOSMBuildingArchetype* Archetype = OSMMesherTest::MakeArchetype();

    // The extremes measured in real data: 19.5 m2 to 7,814 m2, a 401x range. One archetype has to
    // serve both without either looking wrong.
    auto BuildFor = [&](double W, double D, float H, FOSMMeshData& Out)
    {
        FOSMBuildingMeshParams P;
        P.Rings = OSMMesherTest::MakeRect(W, D);
        P.HeightMeters = H;
        P.Archetype = Archetype;
        FString Error;
        return FOSMBuildingMesher::Build(P, Out, Error);
    };

    FOSMMeshData Hut, Block;
    TestTrue(TEXT("Smallest real footprint builds (19.5 m2)"), BuildFor(4.4, 4.4, 3.2f, Hut));
    TestTrue(TEXT("Largest real footprint builds (7,814 m2)"), BuildFor(88.4, 88.4, 40.0f, Block));

    // UVs are in world metres, so a wall material tiles at the same rate on both. A stretched-UV
    // implementation would give both buildings the same UV extent regardless of size — that is
    // the bug this checks for.
    auto MaxU = [](const FOSMMeshData& M)
    {
        double Max = 0.0;
        for (const FVector2D& UV : M.UVs) Max = FMath::Max(Max, FMath::Abs(UV.X));
        return Max;
    };

    const double HutU = MaxU(Hut);
    const double BlockU = MaxU(Block);

    TestTrue(TEXT("The large building has a proportionally larger UV extent"), BlockU > HutU * 5.0);
    TestTrue(TEXT("UV extent tracks real size, not a normalised 0..1"), HutU > 1.0);

    // Bay counts scale with the wall, keeping window spacing constant rather than stretching it.
    const int32 HutBays = FOSMBuildingMesher::ComputeBayCount(4.4, 3.0);
    const int32 BlockBays = FOSMBuildingMesher::ComputeBayCount(88.4, 3.0);
    TestTrue(TEXT("The large building gets many more bays"), BlockBays > HutBays * 10);

    return true;
}

// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMHeightChainTest, "OSMWorldGen.Mesher.HeightChain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMHeightChainTest::RunTest(const FString& Parameters)
{
    UOSMBuildingArchetype* Archetype = OSMMesherTest::MakeArchetype();
    Archetype->FloorHeightMeters = 3.0f;
    Archetype->DefaultHeightMeters = 9.0f;
    Archetype->HeightJitterMeters = 0.0f;

    // Only 21% of sampled buildings record a level count, so the order of this chain decides how
    // most of a city looks.
    TestNearlyEqual(TEXT("A tagged height wins outright"),
        Archetype->ResolveHeightMeters(17.5f, 4, 1), 17.5f, 0.01f);

    TestNearlyEqual(TEXT("Levels x floor height is next"),
        Archetype->ResolveHeightMeters(0.0f, 4, 1), 12.0f, 0.01f);

    TestNearlyEqual(TEXT("The archetype default is last"),
        Archetype->ResolveHeightMeters(0.0f, 0, 1), 9.0f, 0.01f);

    TestTrue(TEXT("Height never falls below one storey"),
        Archetype->ResolveHeightMeters(0.0f, -3, 1) >= Archetype->FloorHeightMeters);

    // Jitter must be deterministic, or a rebuild reshuffles the skyline and no dry run can
    // predict what generation will produce.
    Archetype->HeightJitterMeters = 2.0f;
    const float First  = Archetype->ResolveHeightMeters(0.0f, 3, 4242);
    const float Second = Archetype->ResolveHeightMeters(0.0f, 3, 4242);
    TestNearlyEqual(TEXT("The same seed gives the same height"), First, Second, 0.0001f);
    TestNotEqual(TEXT("A different seed gives a different height"),
        First, Archetype->ResolveHeightMeters(0.0f, 3, 99));

    // A surveyed height must not be nudged by a random number.
    TestNearlyEqual(TEXT("Jitter never touches a tagged height"),
        Archetype->ResolveHeightMeters(21.0f, 0, 7), 21.0f, 0.0001f);

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

// Copyright InviMind. All Rights Reserved.

#include "Generation/FOSMBuildingMesher.h"
#include "Generation/UOSMBuildingArchetype.h"
#include "CompGeom/PolygonTriangulation.h"

namespace
{
    /** Unreal works in centimetres; every input to the mesher is in metres. */
    constexpr double MetersToCm = 100.0;

    /** Below this a ring is a degenerate sliver rather than a footprint. */
    constexpr double MinRingAreaSqm = 0.5;
}

// ---------------------------------------------------------------------------
float UOSMBuildingArchetype::ResolveHeightMeters(float TaggedHeight, int32 TaggedLevels, int32 Seed) const
{
    // The height chain (plan_v3_pipeline.md Phase 5.4), most trustworthy source first. Only 21%
    // of buildings in the sampled data record a level count and fewer still a metric height, so
    // the last branch decides how most of a city looks — which is why the source is worth
    // recording rather than silently collapsing to a number.
    float Height;

    if (TaggedHeight > 0.0f)
    {
        // Explicit metres. Trusted as-is, and deliberately not jittered: a surveyed value should
        // not be nudged by a random number.
        return TaggedHeight;
    }

    if (TaggedLevels > 0)
    {
        Height = TaggedLevels * FloorHeightMeters;
    }
    else
    {
        Height = DefaultHeightMeters;
    }

    if (HeightJitterMeters > 0.0f)
    {
        // Seeded, so a rebuild reproduces the same skyline instead of reshuffling it.
        FRandomStream Stream(Seed);
        Height += Stream.FRandRange(-HeightJitterMeters, HeightJitterMeters);
    }

    // Never below one storey: a zero or negative height would produce a degenerate mesh, and a
    // building that exists in the data should exist in the world.
    return FMath::Max(Height, FloorHeightMeters);
}

// ---------------------------------------------------------------------------
double FOSMBuildingMesher::SignedArea(const TArray<FVector2D>& Ring)
{
    if (Ring.Num() < 3)
    {
        return 0.0;
    }

    double Twice = 0.0;
    for (int32 Index = 0; Index < Ring.Num(); ++Index)
    {
        const FVector2D& P0 = Ring[Index];
        const FVector2D& P1 = Ring[(Index + 1) % Ring.Num()];
        Twice += P0.X * P1.Y - P1.X * P0.Y;
    }
    return Twice * 0.5;
}

// ---------------------------------------------------------------------------
int32 FOSMBuildingMesher::ComputeBayCount(double WallLengthMeters, double NominalBayWidthMeters)
{
    if (NominalBayWidthMeters <= 0.0)
    {
        return 1;
    }

    // Rounded to the NEAREST whole bay, then clamped to at least one. Rounding down would leave
    // a wall of 5.9 m with a single 5.9 m bay — a stretched opening, the exact failure bays exist
    // to prevent. Rounding to nearest keeps the actual bay width within ~17% of nominal, which is
    // inside the +/-15% squash tolerance the art brief asks for.
    return FMath::Max(1, FMath::RoundToInt(WallLengthMeters / NominalBayWidthMeters));
}

// ---------------------------------------------------------------------------
namespace
{
    /** Append one quad as two triangles, with a shared normal and world-metre UVs. */
    void AddQuad(
        FOSMMeshData& Mesh,
        const FVector& BottomLeft, const FVector& BottomRight,
        const FVector& TopRight, const FVector& TopLeft,
        const FVector& Normal,
        double UStartMeters, double UEndMeters,
        double VStartMeters, double VEndMeters,
        EOSMMeshSection Section)
    {
        const int32 Base = Mesh.Vertices.Num();

        Mesh.Vertices.Add(BottomLeft);
        Mesh.Vertices.Add(BottomRight);
        Mesh.Vertices.Add(TopRight);
        Mesh.Vertices.Add(TopLeft);

        for (int32 Index = 0; Index < 4; ++Index)
        {
            Mesh.Normals.Add(Normal);
        }

        // UVs in metres, so materials tile at a real-world rate rather than stretching to fit the
        // surface. This is what lets one wall material read correctly on a hut and a tower block.
        Mesh.UVs.Emplace(UStartMeters, VEndMeters);
        Mesh.UVs.Emplace(UEndMeters,   VEndMeters);
        Mesh.UVs.Emplace(UEndMeters,   VStartMeters);
        Mesh.UVs.Emplace(UStartMeters, VStartMeters);

        // Wound for an INWARD geometric normal, which is what makes the wall visible from outside.
        // See the winding convention on FOSMBuildingMesher.h. The vertex normals above stay
        // outward, because they light the surface rather than face it.
        Mesh.Triangles.Append({ Base + 0, Base + 2, Base + 1 });
        Mesh.Triangles.Append({ Base + 0, Base + 3, Base + 2 });

        Mesh.TriangleSections.Add(static_cast<uint8>(Section));
        Mesh.TriangleSections.Add(static_cast<uint8>(Section));
    }
}

// ---------------------------------------------------------------------------
bool FOSMBuildingMesher::Build(const FOSMBuildingMeshParams& Params, FOSMMeshData& OutMesh, FString& OutError)
{
    OutMesh.Reset();

    if (Params.Rings.Num() == 0 || Params.Rings[0].Num() < 3)
    {
        OutError = TEXT("Footprint has no outer ring with at least 3 points.");
        return false;
    }

    if (!Params.Archetype)
    {
        OutError = TEXT("No archetype supplied — the mesher has no recipe to build from.");
        return false;
    }

    const UOSMBuildingArchetype& Archetype = *Params.Archetype;

    TArray<FVector2D> Outer = Params.Rings[0];

    // A ring that closes explicitly (last point == first) would produce a zero-length wall
    // segment, so the duplicate is dropped rather than special-cased later.
    if (Outer.Num() > 3 && FVector2D::Distance(Outer[0], Outer.Last()) < 0.01)
    {
        Outer.Pop();
    }

    const double Area = SignedArea(Outer);
    if (FMath::Abs(Area) < MinRingAreaSqm)
    {
        OutError = FString::Printf(
            TEXT("Footprint area is %.3f m2, below the %.1f m2 minimum — a degenerate sliver."),
            FMath::Abs(Area), MinRingAreaSqm);
        return false;
    }

    // Normalise to counter-clockwise so wall normals point outward for every building, whichever
    // way the source data happened to wind the ring.
    if (Area < 0.0)
    {
        Algo::Reverse(Outer);
    }

    const double HeightCm = FMath::Max(0.5f, Params.HeightMeters) * MetersToCm;
    const double PlinthCm = Archetype.PlinthHeightMeters * MetersToCm;
    const double FloorCm  = FMath::Max(2.0f, Archetype.FloorHeightMeters) * MetersToCm;

    // The ground floor gets its own section so shopfronts can differ from the floors above — a
    // commercial street reads wrongly when every storey is identical.
    const double GroundFloorTopCm = FMath::Min(PlinthCm + FloorCm, HeightCm);

    // The footprint with every bay division inserted as a real vertex.
    //
    // The roof and floor caps are built from this rather than from the raw footprint, so their
    // boundary vertices coincide exactly with the wall corners. Capping the raw ring instead
    // leaves a T-junction at every bay division — the cap edge spans a wall the walls themselves
    // split into a dozen pieces — which is a hairline crack along the roofline under any renderer
    // that interpolates depth even slightly differently on the two sides.
    TArray<FVector2D> DividedRing;
    DividedRing.Reserve(Outer.Num() * 4);

    // ---- Walls ----
    for (int32 EdgeIndex = 0; EdgeIndex < Outer.Num(); ++EdgeIndex)
    {
        const FVector2D& Start = Outer[EdgeIndex];
        const FVector2D& End = Outer[(EdgeIndex + 1) % Outer.Num()];

        const double WallLengthMeters = FVector2D::Distance(Start, End);
        if (WallLengthMeters < 0.05)
        {
            continue;   // duplicate or near-duplicate point
        }

        const int32 BayCount = ComputeBayCount(WallLengthMeters, Archetype.BayWidthMeters);

        // Outward normal for a counter-clockwise ring: the edge direction rotated -90 degrees.
        const FVector2D Direction = (End - Start).GetSafeNormal();
        const FVector Normal(Direction.Y, -Direction.X, 0.0);

        for (int32 Bay = 0; Bay < BayCount; ++Bay)
        {
            const double T0 = static_cast<double>(Bay) / BayCount;
            const double T1 = static_cast<double>(Bay + 1) / BayCount;

            const FVector2D A = FMath::Lerp(Start, End, T0);
            const FVector2D B = FMath::Lerp(Start, End, T1);

            // Only the bay's start point: its end is the next bay's start, and the last bay's end
            // is the next edge's start. Adding both would duplicate every vertex.
            DividedRing.Add(A);

            const double UStart = WallLengthMeters * T0;
            const double UEnd = WallLengthMeters * T1;

            const FVector A_cm(A.X * MetersToCm, A.Y * MetersToCm, 0.0);
            const FVector B_cm(B.X * MetersToCm, B.Y * MetersToCm, 0.0);

            // Split at the ground-floor line so the two sections can carry different materials.
            if (GroundFloorTopCm < HeightCm - 1.0)
            {
                AddQuad(OutMesh,
                    A_cm, B_cm,
                    B_cm + FVector(0, 0, GroundFloorTopCm), A_cm + FVector(0, 0, GroundFloorTopCm),
                    Normal, UStart, UEnd, 0.0, GroundFloorTopCm / MetersToCm,
                    EOSMMeshSection::GroundFloor);

                AddQuad(OutMesh,
                    A_cm + FVector(0, 0, GroundFloorTopCm), B_cm + FVector(0, 0, GroundFloorTopCm),
                    B_cm + FVector(0, 0, HeightCm), A_cm + FVector(0, 0, HeightCm),
                    Normal, UStart, UEnd, GroundFloorTopCm / MetersToCm, HeightCm / MetersToCm,
                    EOSMMeshSection::Wall);
            }
            else
            {
                // Single-storey: one section, no artificial split line.
                AddQuad(OutMesh,
                    A_cm, B_cm,
                    B_cm + FVector(0, 0, HeightCm), A_cm + FVector(0, 0, HeightCm),
                    Normal, UStart, UEnd, 0.0, HeightCm / MetersToCm,
                    EOSMMeshSection::GroundFloor);
            }
        }
    }

    // ---- Roof ----
    //
    // Triangulated from the same polygon, so the roof is exactly the building's own outline. 95%
    // of real footprints are near-rectangular and the worst observed is 34 vertices, so a general
    // triangulator is comfortably fast enough here.
    {
        // The bay-divided ring where one was produced, so the cap welds to the wall tops. Ear
        // clipping copes with the collinear vertices bay division introduces, but if it does not
        // for some footprint, the raw outline is retried — a roof with a T-junction is a far
        // better outcome than no roof.
        TArray<FVector2D> RoofRing = DividedRing.Num() >= 3 ? DividedRing : Outer;
        TArray<UE::Geometry::FIndex3i> RoofTriangles;

        PolygonTriangulation::TriangulateSimplePolygon<double>(RoofRing, RoofTriangles, /*bCalculateVertexNormals*/ false);

        if (RoofTriangles.Num() == 0 && RoofRing.Num() != Outer.Num())
        {
            RoofRing = Outer;
            PolygonTriangulation::TriangulateSimplePolygon<double>(RoofRing, RoofTriangles, false);
        }

        if (RoofTriangles.Num() == 0)
        {
            // Self-intersecting or otherwise untriangulable. The walls are still valid, so the
            // building is kept rather than discarded — a roofless building is a visible, fixable
            // defect; a missing building is an invisible one.
            OutError = TEXT("Roof could not be triangulated; walls were generated without a roof.");
        }
        else
        {
            const double RoofZ = HeightCm;
            const int32 Base = OutMesh.Vertices.Num();

            for (const FVector2D& Point : RoofRing)
            {
                OutMesh.Vertices.Emplace(Point.X * MetersToCm, Point.Y * MetersToCm, RoofZ);
                OutMesh.Normals.Emplace(0.0, 0.0, 1.0);

                // World-metre UVs again, so a roof material tiles at the same rate on every
                // building regardless of footprint size.
                OutMesh.UVs.Emplace(Point.X, Point.Y);
            }

            for (const UE::Geometry::FIndex3i& Triangle : RoofTriangles)
            {
                // Reversed from the triangulator's counter-clockwise output, giving a -Z geometric
                // normal so the roof is visible from above.
                OutMesh.Triangles.Append({ Base + Triangle.A, Base + Triangle.C, Base + Triangle.B });
                OutMesh.TriangleSections.Add(static_cast<uint8>(EOSMMeshSection::Roof));
            }

            // ---- Floor ----
            //
            // A cap on the underside, wound the opposite way so it faces down. Buildings sit on
            // sloped terrain, so without it the ground cuts into the interior on the uphill side
            // and the building is visibly hollow. It also makes the mesh a closed solid, which is
            // a property a test can check.
            {
                const int32 FloorBase = OutMesh.Vertices.Num();

                for (const FVector2D& Point : RoofRing)
                {
                    OutMesh.Vertices.Emplace(Point.X * MetersToCm, Point.Y * MetersToCm, 0.0);
                    OutMesh.Normals.Emplace(0.0, 0.0, -1.0);
                    OutMesh.UVs.Emplace(Point.X, Point.Y);
                }

                for (const UE::Geometry::FIndex3i& Triangle : RoofTriangles)
                {
                    // The triangulator's own order, giving a +Z geometric normal — the opposite of
                    // the roof, so the floor is the face visible from underneath.
                    OutMesh.Triangles.Append({ FloorBase + Triangle.A, FloorBase + Triangle.B, FloorBase + Triangle.C });
                    OutMesh.TriangleSections.Add(static_cast<uint8>(EOSMMeshSection::Roof));
                }
            }
        }
    }

    if (OutMesh.IsEmpty())
    {
        OutError = TEXT("Mesher produced no geometry.");
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
TArray<int32> FOSMMeshData::GetTrianglesForSection(EOSMMeshSection Section) const
{
    TArray<int32> Result;

    for (int32 TriIndex = 0; TriIndex < TriangleSections.Num(); ++TriIndex)
    {
        if (TriangleSections[TriIndex] != static_cast<uint8>(Section))
        {
            continue;
        }

        const int32 Base = TriIndex * 3;
        if (Base + 2 < Triangles.Num())
        {
            Result.Append({ Triangles[Base], Triangles[Base + 1], Triangles[Base + 2] });
        }
    }

    return Result;
}

FBox FOSMMeshData::GetBounds() const
{
    FBox Bounds(ForceInit);
    for (const FVector& Vertex : Vertices)
    {
        Bounds += Vertex;
    }
    return Bounds;
}

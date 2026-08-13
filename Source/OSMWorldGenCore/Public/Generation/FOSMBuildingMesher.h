// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UOSMBuildingArchetype;

/** Which material a triangle belongs to. Kept small — one section per slot at build time. */
enum class EOSMMeshSection : uint8
{
    Wall = 0,
    GroundFloor,
    Roof,
    MAX
};

/**
 * Raw mesh output, engine-agnostic.
 *
 * Deliberately plain arrays rather than a UStaticMesh or a procedural component: the mesher is a
 * pure function, so it can be unit-tested without an editor, a world, or an actor. Turning this
 * into renderable geometry is the caller's job.
 */
struct OSMWORLDGENCORE_API FOSMMeshData
{
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;

    /** Section index per triangle (Triangles.Num()/3 entries). */
    TArray<uint8> TriangleSections;

    int32 NumTriangles() const { return Triangles.Num() / 3; }

    bool IsEmpty() const { return Vertices.Num() == 0 || Triangles.Num() == 0; }

    /** Triangle indices belonging to one section, for building per-material sections. */
    TArray<int32> GetTrianglesForSection(EOSMMeshSection Section) const;

    /** Axis-aligned bounds of the generated geometry. */
    FBox GetBounds() const;

    void Reset()
    {
        Vertices.Reset();
        Triangles.Reset();
        Normals.Reset();
        UVs.Reset();
        TriangleSections.Reset();
    }
};

/** Everything the mesher needs about one building. */
struct OSMWORLDGENCORE_API FOSMBuildingMeshParams
{
    /**
     * Footprint rings in LOCAL METRES, not lat/lon. Ring 0 is the outer boundary; further rings
     * are holes. The caller projects, so the mesher never has to know about a CRS.
     */
    TArray<TArray<FVector2D>> Rings;

    /** Wall height in metres, already resolved through the height chain. */
    float HeightMeters = 9.6f;

    /** Deterministic per-building seed, so variation reproduces across rebuilds. */
    int32 Seed = 0;

    const UOSMBuildingArchetype* Archetype = nullptr;
};

/**
 * Builds a building mesh from its own footprint (plan_v3_pipeline.md Phase 5.3).
 *
 * The core of the approach: geometry comes from the data, character from the archetype. Two
 * details do the heavy lifting and are worth stating plainly, because getting either wrong is
 * what makes generated cities look generated:
 *
 *   **Walls are divided into whole bays.** A 40 m wall gets 13 bays of ~3.08 m rather than one
 *   stretched span, so window spacing stays constant whatever the wall length.
 *
 *   **UVs are in world metres.** A brick material tiles identically on a 20 m^2 hut and a
 *   7,800 m^2 block, instead of stretching with the surface.
 *
 * A pure function over its inputs — no world, no actor, no engine state — so it is unit-testable.
 */
class OSMWORLDGENCORE_API FOSMBuildingMesher
{
public:
    /** Build walls and roof. Returns false with OutError populated when the footprint is unusable. */
    static bool Build(const FOSMBuildingMeshParams& Params, FOSMMeshData& OutMesh, FString& OutError);

    /**
     * Bay count for a wall: at least one, otherwise the nearest whole number of nominal bays.
     *
     * Exposed for testing, because "windows stay evenly spaced at any wall length" is the
     * property that distinguishes this from scaling a mesh, and it should be checkable directly.
     */
    static int32 ComputeBayCount(double WallLengthMeters, double NominalBayWidthMeters);

    /**
     * Signed area of a ring in m^2. Positive is counter-clockwise.
     * Used to normalise winding so every generated face points outward.
     */
    static double SignedArea(const TArray<FVector2D>& Ring);
};

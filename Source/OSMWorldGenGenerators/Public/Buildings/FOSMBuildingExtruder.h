// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDynamicMesh;
class UStaticMesh;
class UMaterialInterface;

/**
 * Procedurally extrudes a building footprint into a 3D mesh via Geometry Scripting.
 *
 * Supports:
 *  - Outer polygon ring
 *  - Interior hole rings (courtyards)
 *  - Flat roof cap generation (v1)
 *  - Separate Material IDs for walls vs roof
 *  - LOD mesh baking
 */
class OSMWORLDGENGENERATORS_API FOSMBuildingExtruder
{
public:
    /** Input descriptor for a single extrusion job */
    struct FExtrudeInput
    {
        /** Outer polygon ring, winding order must be counter-clockwise when viewed from above */
        TArray<FVector2D> OuterRing;

        /** Interior hole rings (each wound clockwise) */
        TArray<TArray<FVector2D>> InnerRings;

        /** Building height in centimetres (UE units) */
        float HeightCm = 900.0f;

        /** Z coordinate of the ground base in world space (cm) */
        float GroundZ = 0.0f;

        /** Material slot index assigned to wall faces */
        int32 WallMaterialSlot = 0;

        /** Material slot index assigned to the roof cap face */
        int32 RoofMaterialSlot = 1;
    };

    /** Result of a successful extrusion */
    struct FExtrudeResult
    {
        /** The dynamic mesh ready for baking or further editing */
        UDynamicMesh* DynamicMesh = nullptr;

        /** World-space centroid of the footprint (used for actor placement) */
        FVector Centroid = FVector::ZeroVector;

        /** World-space bounding box */
        FBox Bounds = FBox(EForceInit::ForceInit);

        bool IsValid() const { return DynamicMesh != nullptr; }
    };

    /**
     * Extrude the building footprint into a UDynamicMesh.
     *
     * @param Input     Extrusion parameters
     * @param Outer     UObject outer for new objects (e.g. a transient package)
     * @return          Result with a populated DynamicMesh, or invalid if geometry failed
     */
    static FExtrudeResult Extrude(const FExtrudeInput& Input, UObject* Outer);

#if WITH_EDITOR
    /**
     * Bake a UDynamicMesh to a new UStaticMesh asset and embed LODs.
     *
     * @param DynMesh       Source dynamic mesh
     * @param AssetPath     Full content-browser path (e.g. "/Game/OSMWorldGen/GeneratedMeshes/Building_123")
     * @param WallMaterial  Material for wall faces (slot 0)
     * @param RoofMaterial  Material for roof faces (slot 1)
     * @param LODDistances  Distance thresholds for LOD1/LOD2 (empty = no LODs)
     * @return              The newly created (or updated) UStaticMesh, or nullptr on failure
     */
    static UStaticMesh* BakeToStaticMesh(
        UDynamicMesh* DynMesh,
        const FString& AssetPath,
        UMaterialInterface* WallMaterial,
        UMaterialInterface* RoofMaterial,
        const TArray<float>& LODDistances);
#endif

private:
    /** Triangulate a polygon with holes using fan-triangulation (ear-clip for convex, layered for non-convex) */
    static bool TriangulateFootprint(
        const TArray<FVector2D>& Outer,
        const TArray<TArray<FVector2D>>& Holes,
        TArray<FVector>& OutVerts,
        TArray<int32>& OutTris,
        TArray<FVector2D>& OutUVs);

    /** Compute the 2D centroid of a polygon ring */
    static FVector2D ComputeCentroid(const TArray<FVector2D>& Ring);
};

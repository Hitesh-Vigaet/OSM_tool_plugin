// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

class USplineComponent;
class UProceduralMeshComponent;
class UOSMRoadTypeDataAsset;
class FOSMDEMSampler;

/**
 * Procedurally generates 3D road meshes along a spline.
 * Uses UProceduralMeshComponent to extrude cross-sections.
 */
class OSMWORLDGENGENERATORS_API FOSMRoadMeshBuilder
{
public:
    /**
     * Build the procedural mesh for a road along a spline.
     * 
     * @param Spline        The spline defining the road path
     * @param ProcMesh      The procedural mesh component to populate
     * @param RoadAsset     Data asset defining width, materials, and sidewalks
     * @param CustomWidth   Width override from OSM tags (0.0 if not overridden)
     * @param SectionIndex  The procedural mesh section index to use (usually 0)
     */
    static void BuildRoadMesh(
        const USplineComponent* Spline,
        UProceduralMeshComponent* ProcMesh,
        const UOSMRoadTypeDataAsset* RoadAsset,
        float CustomWidth = 0.0f,
        int32 SectionIndex = 0);

    /**
     * Snaps the spline points to the terrain elevation using the DEM Sampler.
     * 
     * @param Spline        The spline to snap
     * @param DEMSampler    Loaded DEM sampler (must be valid)
     * @param OffsetZ       Optional vertical offset to add after snapping (e.g., to prevent z-fighting)
     */
    static void SnapSplineToTerrain(
        USplineComponent* Spline,
        const class UOSMCRSTransformer* Transformer,
        const FOSMDEMSampler& DEMSampler,
        float OffsetZ = 5.0f);

private:
    /** Generate vertices, triangles, and UVs for a single extruded quad-strip */
    static void ExtrudeCrossSection(
        const USplineComponent* Spline,
        TArray<FVector>& Vertices,
        TArray<int32>& Triangles,
        TArray<FVector>& Normals,
        TArray<FVector2D>& UV0,
        TArray<FProcMeshTangent>& Tangents,
        float WidthMeters,
        float UVTilingPerMeter);
};

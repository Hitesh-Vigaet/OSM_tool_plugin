// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UProceduralMeshComponent;
class USplineComponent;
struct FOSMFeature;

/**
 * Handles detection and procedural mesh generation for road intersections.
 */
class OSMWORLDGENGENERATORS_API FOSMIntersectionBuilder
{
public:
    /**
     * Data representing an incoming road to a junction
     */
    struct FRoadJunctionLeg
    {
        int64 WayId;
        USplineComponent* Spline;
        float WidthMeters;
        int32 IntersectionPriority;
        
        /** The direction vector pointing INTO the intersection */
        FVector InwardDirection;
        
        /** The spline point index that connects to the intersection */
        int32 ConnectedPointIndex;
    };

    /**
     * Builds intersection patches for a set of connected road legs at a shared node.
     * 
     * @param IntersectionCenter  The world location of the shared node
     * @param ConnectedLegs       Array of roads connecting to this point
     * @param ProcMesh            Procedural mesh component to add the junction geometry to
     * @param SectionIndex        Mesh section index
     * @param SurfaceMaterial     Material for the intersection surface
     */
    static void BuildIntersection(
        const FVector& IntersectionCenter,
        const TArray<FRoadJunctionLeg>& ConnectedLegs,
        UProceduralMeshComponent* ProcMesh,
        int32 SectionIndex,
        class UMaterialInterface* SurfaceMaterial);

private:
    /** Generate a polygonal patch covering the junction area */
    static void GeneratePatchGeometry(
        const FVector& Center,
        const TArray<FRoadJunctionLeg>& SortedLegs,
        UProceduralMeshComponent* ProcMesh,
        int32 SectionIndex,
        float MaxWidthMeters);
};

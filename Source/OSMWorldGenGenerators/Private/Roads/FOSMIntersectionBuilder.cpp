// Copyright InviMind. All Rights Reserved.

#include "Roads/FOSMIntersectionBuilder.h"
#include "Components/SplineComponent.h"
#include "ProceduralMeshComponent.h"

// ---------------------------------------------------------------------------
void FOSMIntersectionBuilder::BuildIntersection(
    const FVector& IntersectionCenter,
    const TArray<FRoadJunctionLeg>& ConnectedLegs,
    UProceduralMeshComponent* ProcMesh,
    int32 SectionIndex,
    UMaterialInterface* SurfaceMaterial)
{
    if (ConnectedLegs.Num() < 3 || !ProcMesh) return;

    // Find the maximum road width involved to size the intersection patch
    float MaxWidth = 0.0f;
    for (const FRoadJunctionLeg& Leg : ConnectedLegs)
    {
        MaxWidth = FMath::Max(MaxWidth, Leg.WidthMeters);
    }

    // Sort legs by angle around the center so we can build a clean polygon
    TArray<FRoadJunctionLeg> SortedLegs = ConnectedLegs;
    SortedLegs.Sort([&](const FRoadJunctionLeg& A, const FRoadJunctionLeg& B)
    {
        const float AngleA = FMath::Atan2(A.InwardDirection.Y, A.InwardDirection.X);
        const float AngleB = FMath::Atan2(B.InwardDirection.Y, B.InwardDirection.X);
        return AngleA < AngleB;
    });

    GeneratePatchGeometry(IntersectionCenter, SortedLegs, ProcMesh, SectionIndex, MaxWidth);

    if (SurfaceMaterial)
    {
        ProcMesh->SetMaterial(SectionIndex, SurfaceMaterial);
    }
}

// ---------------------------------------------------------------------------
void FOSMIntersectionBuilder::GeneratePatchGeometry(
    const FVector& Center,
    const TArray<FRoadJunctionLeg>& SortedLegs,
    UProceduralMeshComponent* ProcMesh,
    int32 SectionIndex,
    float MaxWidthMeters)
{
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UV0;
    TArray<FProcMeshTangent> Tangents;

    const float RadiusCm = (MaxWidthMeters * 100.0f) * 0.75f; // Generous radius to cover corners
    const int32 NumLegs = SortedLegs.Num();
    
    // Add center vertex
    Vertices.Add(FVector::ZeroVector); // Local to the junction (which we'll place at Center)
    UV0.Add(FVector2D(0.5f, 0.5f));
    Normals.Add(FVector::UpVector);
    Tangents.Add(FProcMeshTangent(FVector::ForwardVector, false));

    // Add perimeter vertices
    for (int32 i = 0; i < NumLegs; ++i)
    {
        // Direction from center pointing OUT towards the road leg
        const FVector OutDir = -SortedLegs[i].InwardDirection;
        const FVector PerimVert = OutDir * RadiusCm;

        Vertices.Add(PerimVert);
        
        // Simple planar UV mapping for junctions
        UV0.Add(FVector2D(0.5f + (OutDir.X * 0.5f), 0.5f + (OutDir.Y * 0.5f)));
        Normals.Add(FVector::UpVector);
        Tangents.Add(FProcMeshTangent(FVector::ForwardVector, false));

        // Build triangle (Center, Current, Next)
        if (i > 0)
        {
            Triangles.Add(0);
            Triangles.Add(i);
            Triangles.Add((i == NumLegs - 1) ? 1 : i + 1);
        }
    }
    
    // Final triangle to close the loop
    Triangles.Add(0);
    Triangles.Add(NumLegs);
    Triangles.Add(1);

    // Transform vertices to world space if ProcMesh is at origin, 
    // or keep them local if we add the mesh at 'Center'
    const FTransform ProcTransform = ProcMesh->GetComponentTransform();
    for (FVector& Vert : Vertices)
    {
        // Vert is currently local to 'Center'. Shift it to world space, 
        // then local to the ProcMesh.
        FVector WorldVert = Center + Vert;
        Vert = ProcTransform.InverseTransformPosition(WorldVert);
    }

    ProcMesh->CreateMeshSection(SectionIndex, Vertices, Triangles, Normals, UV0, TArray<FColor>(), Tangents, true);
}

// Copyright InviMind. All Rights Reserved.

#include "Roads/FOSMRoadMeshBuilder.h"
#include "Roads/UOSMRoadTypeDataAsset.h"
#include "Components/SplineComponent.h"
#include "ProceduralMeshComponent.h"
#include "Terrain/FOSMDEMSampler.h"
#include "CRS/FOSMCRSTransformer.h"

// ---------------------------------------------------------------------------
void FOSMRoadMeshBuilder::BuildRoadMesh(
    const USplineComponent* Spline,
    UProceduralMeshComponent* ProcMesh,
    const UOSMRoadTypeDataAsset* RoadAsset,
    float CustomWidth,
    int32 SectionIndex)
{
    if (!Spline || !ProcMesh || !RoadAsset) return;

    const float WidthMeters = (CustomWidth > 0.0f && !RoadAsset->bForceAssetWidth) 
        ? CustomWidth 
        : RoadAsset->DefaultWidthMeters;

    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UV0;
    TArray<FProcMeshTangent> Tangents;

    ExtrudeCrossSection(Spline, Vertices, Triangles, Normals, UV0, Tangents, WidthMeters, RoadAsset->UVTilingPerMeter);

    ProcMesh->CreateMeshSection(SectionIndex, Vertices, Triangles, Normals, UV0, TArray<FColor>(), Tangents, true);
    
    if (RoadAsset->SurfaceMaterial)
    {
        ProcMesh->SetMaterial(SectionIndex, RoadAsset->SurfaceMaterial);
    }
}

// ---------------------------------------------------------------------------
void FOSMRoadMeshBuilder::ExtrudeCrossSection(
    const USplineComponent* Spline,
    TArray<FVector>& Vertices,
    TArray<int32>& Triangles,
    TArray<FVector>& Normals,
    TArray<FVector2D>& UV0,
    TArray<FProcMeshTangent>& Tangents,
    float WidthMeters,
    float UVTilingPerMeter)
{
    // Unreal uses cm, OSM uses meters
    const float HalfWidthCm = (WidthMeters * 100.0f) * 0.5f;
    const float SplineLength = Spline->GetSplineLength();
    
    // Extrude resolution
    const float StepSizeCm = 500.0f; // Cross-section every 5 meters
    const int32 NumSteps = FMath::Max(2, FMath::CeilToInt(SplineLength / StepSizeCm) + 1);

    Vertices.Reserve(NumSteps * 2);
    UV0.Reserve(NumSteps * 2);
    Normals.Reserve(NumSteps * 2);
    Tangents.Reserve(NumSteps * 2);
    Triangles.Reserve((NumSteps - 1) * 6);

    for (int32 i = 0; i < NumSteps; ++i)
    {
        const float Distance = (i == NumSteps - 1) ? SplineLength : static_cast<float>(i) * StepSizeCm;
        
        // World transforms
        const FVector Center = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
        const FVector Right  = Spline->GetRightVectorAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
        const FVector Up     = Spline->GetUpVectorAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
        const FVector Fwd    = Spline->GetDirectionAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);

        const FVector LeftVert = Center - (Right * HalfWidthCm);
        const FVector RightVert = Center + (Right * HalfWidthCm);

        // Transform to local space relative to ProcMesh component (assuming ProcMesh is at spline origin)
        const FTransform SplineTransform = Spline->GetComponentTransform();
        Vertices.Add(SplineTransform.InverseTransformPosition(LeftVert));
        Vertices.Add(SplineTransform.InverseTransformPosition(RightVert));

        // Normals and Tangents (Local space)
        const FVector LocalUp = SplineTransform.InverseTransformVectorNoScale(Up).GetSafeNormal();
        const FVector LocalFwd = SplineTransform.InverseTransformVectorNoScale(Fwd).GetSafeNormal();
        
        Normals.Add(LocalUp);
        Normals.Add(LocalUp);

        FProcMeshTangent Tangent(LocalFwd, false);
        Tangents.Add(Tangent);
        Tangents.Add(Tangent);

        // UVs
        // U goes across the road (0 to 1)
        // V goes along the road, tiled based on distance
        const float VCoord = (Distance / 100.0f) * UVTilingPerMeter;
        UV0.Add(FVector2D(0.0f, VCoord));
        UV0.Add(FVector2D(1.0f, VCoord));

        // Triangles
        //
        // Winding must be clockwise viewed from the front for Unreal to treat the face as
        // front-facing (verified against the engine's GenerateBoxMesh top face, whose
        // up-facing +Z quad has a negative 2D signed cross product). The previous winding
        // here produced a positive cross, i.e. a downward-facing road surface that was
        // backface-culled when viewed from above.
        if (i < NumSteps - 1)
        {
            const int32 CurrIdx = i * 2;      // Left  vertex at this cross-section
            const int32 NextIdx = (i + 1) * 2; // Left  vertex at the next cross-section
                                               // (+1 on either is the matching Right vertex)

            // Triangle 1 (Left, Right, Next Left)
            Triangles.Add(CurrIdx);
            Triangles.Add(CurrIdx + 1);
            Triangles.Add(NextIdx);

            // Triangle 2 (Right, Next Right, Next Left)
            Triangles.Add(CurrIdx + 1);
            Triangles.Add(NextIdx + 1);
            Triangles.Add(NextIdx);
        }
    }
}

// ---------------------------------------------------------------------------
void FOSMRoadMeshBuilder::SnapSplineToTerrain(
    USplineComponent* Spline,
    const UOSMCRSTransformer* Transformer,
    const FOSMDEMSampler& DEMSampler,
    float OffsetZ)
{
    if (!Spline || !Transformer || !DEMSampler.IsLoaded()) return;

    const int32 NumPoints = Spline->GetNumberOfSplinePoints();
    for (int32 i = 0; i < NumPoints; ++i)
    {
        // Get UE world position
        FVector WorldPos = Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);

        // Inverse transform to WGS84
        double Lat, Lon, Elev;
        Transformer->TransformToWGS84(WorldPos, Lat, Lon, Elev);

        // Sample DEM
        const double DEMElev = DEMSampler.SampleElevationSafe(Lat, Lon, Elev);
        
        // Re-transform to UE World
        FVector SnappedPos = Transformer->TransformToUnreal(Lat, Lon, DEMElev);
        
        // Apply vertical offset (e.g. lift slightly off terrain)
        SnappedPos.Z += OffsetZ;

        Spline->SetLocationAtSplinePoint(i, SnappedPos, ESplineCoordinateSpace::World, true);
    }

    Spline->UpdateSpline();
}

// Copyright InviMind. All Rights Reserved.

#include "Buildings/FOSMBuildingExtruder.h"

// Geometry Scripting
#include "GeometryScript/MeshBasicEditFunctions.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "GeometryScript/MeshMaterialFunctions.h"
#include "GeometryScript/MeshSimplifyFunctions.h"
#include "GeometryScript/MeshQueryFunctions.h"
#include "UDynamicMesh.h"

// Proper ear-clipping triangulation (handles concave, non-self-intersecting polygons
// correctly — unlike naive centroid-fan triangulation, which produces degenerate,
// near-zero-area triangles for any non-convex footprint). Already linked via the
// GeometryCore module dependency.
#include "CompGeom/PolygonTriangulation.h"
#include "Algo/Reverse.h"

// Core & Assets
#if WITH_EDITOR
#include "GeometryScript/MeshAssetFunctions.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshAttributes.h"
#include "MeshDescription.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "PackageTools.h"
#include "UObject/SavePackage.h"
#endif

namespace
{
    // OSM closed ways repeat the first node as the last node, and often contain
    // consecutive near-duplicate points. Both produce zero-area geometry downstream
    // (degenerate wall quads and degenerate cap triangles), which is what generates the
    // "has some nearly zero normals / bi-normals" warnings on import. Strip them once,
    // up front, so both the roof cap and the walls consume clean input.
    TArray<FVector2D> CleanRing(const TArray<FVector2D>& In)
    {
        constexpr double DupToleranceCm = 1.0; // 1 cm — well below real building detail

        TArray<FVector2D> Out;
        Out.Reserve(In.Num());

        for (const FVector2D& P : In)
        {
            if (Out.Num() == 0 || !P.Equals(Out.Last(), DupToleranceCm))
            {
                Out.Add(P);
            }
        }

        // Drop the repeated closing vertex if present
        while (Out.Num() >= 2 && Out.Last().Equals(Out[0], DupToleranceCm))
        {
            Out.Pop();
        }

        return Out;
    }

    // Signed area in the XY plane. Positive == counter-clockwise viewed from +Z.
    double SignedArea2D(const TArray<FVector2D>& Ring)
    {
        double Area = 0.0;
        const int32 N = Ring.Num();
        for (int32 i = 0; i < N; ++i)
        {
            const FVector2D& A = Ring[i];
            const FVector2D& B = Ring[(i + 1) % N];
            Area += (A.X * B.Y) - (B.X * A.Y);
        }
        return Area * 0.5;
    }
}

// ---------------------------------------------------------------------------
FVector2D FOSMBuildingExtruder::ComputeCentroid(const TArray<FVector2D>& Ring)
{
    FVector2D Sum = FVector2D::ZeroVector;
    for (const FVector2D& P : Ring)
    {
        Sum += P;
    }
    return Ring.Num() > 0 ? Sum / static_cast<float>(Ring.Num()) : FVector2D::ZeroVector;
}

// ---------------------------------------------------------------------------
bool FOSMBuildingExtruder::TriangulateFootprint(
    const TArray<FVector2D>& Outer,
    const TArray<TArray<FVector2D>>& Holes,
    TArray<FVector>& OutVerts,
    TArray<int32>& OutTris,
    TArray<FVector2D>& OutUVs)
{
    if (Outer.Num() < 3) return false;

    // Compute bounding box for UV normalisation
    FVector2D BoundsMin(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
    FVector2D BoundsMax(TNumericLimits<float>::Lowest(), TNumericLimits<float>::Lowest());
    for (const FVector2D& P : Outer)
    {
        BoundsMin = FVector2D(FMath::Min(BoundsMin.X, P.X), FMath::Min(BoundsMin.Y, P.Y));
        BoundsMax = FVector2D(FMath::Max(BoundsMax.X, P.X), FMath::Max(BoundsMax.Y, P.Y));
    }
    const FVector2D BoundsExtent = BoundsMax - BoundsMin;

    // Emit one vertex per ring point (no artificial centroid vertex — a centroid fan is
    // only valid for convex polygons and produces inverted/degenerate triangles on the
    // L-shaped and U-shaped footprints that are common in real OSM building data).
    for (int32 i = 0; i < Outer.Num(); ++i)
    {
        OutVerts.Add(FVector(Outer[i].X, Outer[i].Y, 0.0f));

        const FVector2D NormUV = (BoundsExtent.GetMax() > 0.0f)
            ? (Outer[i] - BoundsMin) / BoundsExtent
            : FVector2D(0.5f, 0.5f);
        OutUVs.Add(NormUV);
    }

    // Ear-clipping handles arbitrary simple (non-self-intersecting) polygons, concave included.
    TArray<UE::Geometry::FIndex3i> EarTris;
    PolygonTriangulation::TriangulateSimplePolygon<double>(Outer, EarTris, /*bOrientAsHoleFill=*/false);

    if (EarTris.Num() == 0)
    {
        return false;
    }

    // Force every cap triangle to face upward. Unreal front-faces are wound clockwise
    // when viewed from the front, which for an up-facing (+Z) surface means a NEGATIVE
    // 2D signed cross product — verified against the engine's own GenerateBoxMesh top
    // face. Ear-clipping's orientation follows the input ring, which OSM doesn't guarantee.
    for (const UE::Geometry::FIndex3i& Tri : EarTris)
    {
        const FVector2D& A = Outer[Tri.A];
        const FVector2D& B = Outer[Tri.B];
        const FVector2D& C = Outer[Tri.C];

        const double Cross = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);

        OutTris.Add(Tri.A);
        if (Cross <= 0.0)
        {
            OutTris.Add(Tri.B);
            OutTris.Add(Tri.C);
        }
        else
        {
            OutTris.Add(Tri.C);
            OutTris.Add(Tri.B);
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
FOSMBuildingExtruder::FExtrudeResult FOSMBuildingExtruder::Extrude(
    const FExtrudeInput& Input,
    UObject* Outer)
{
    FExtrudeResult Result;

    if (Input.OuterRing.Num() < 3) return Result;

    // Strip the repeated closing vertex and any consecutive duplicates before building
    // any geometry — a duplicated point produces a zero-area wall quad with an
    // undefined normal, which is the source of the "nearly zero normals" import warnings.
    TArray<FVector2D> OuterRing = CleanRing(Input.OuterRing);
    if (OuterRing.Num() < 3) return Result;

    // Degenerate footprints (all points collinear, or sub-square-meter area) cannot
    // produce valid geometry — skip rather than emitting a broken mesh.
    const double RingArea = SignedArea2D(OuterRing);
    if (FMath::Abs(RingArea) < 100.0) // 100 cm² = 0.01 m²
    {
        return Result;
    }

    // OSM does not guarantee ring winding direction. Normalize to counter-clockwise so
    // the wall-normal cross product below always yields outward-facing normals rather
    // than flipping to inward for clockwise-wound footprints.
    if (RingArea < 0.0)
    {
        Algo::Reverse(OuterRing);
    }

    // Compute centroid for actor placement
    const FVector2D Centroid2D = ComputeCentroid(OuterRing);
    Result.Centroid = FVector(Centroid2D.X, Centroid2D.Y, Input.GroundZ);

    // Create target dynamic mesh
    UDynamicMesh* DynMesh = NewObject<UDynamicMesh>(Outer);
    Result.DynamicMesh = DynMesh;

    // ---- Build vertex / triangle lists ----

    // Roof cap geometry (floor at GroundZ, same ring at GroundZ+HeightCm)
    TArray<FVector> BaseVerts, RoofVerts;
    TArray<FVector2D> FloorUVs, RoofUVs;
    TArray<int32> CapTris;

    if (!TriangulateFootprint(OuterRing, Input.InnerRings, BaseVerts, CapTris, FloorUVs))
    {
        Result.DynamicMesh = nullptr;
        return Result;
    }

    // Offset roof verts upward
    RoofVerts = BaseVerts;
    for (FVector& V : RoofVerts) { V.Z += Input.HeightCm; }

    RoofUVs = FloorUVs;

    // ---- Assemble combined vertex buffer ----
    // Layout: [base floor verts (unused, just for reference)] [roof verts] [wall verts]
    // For simplicity we build the mesh using GeometryScript's AppendTriangleList

    FGeometryScriptSimpleMeshBuffers Buffers;

    // ----- Roof face -----
    const int32 RoofVertOffset = 0;
    for (int32 i = 0; i < RoofVerts.Num(); ++i)
    {
        Buffers.Vertices.Add(RoofVerts[i]);
        Buffers.Normals.Add(FVector::UpVector);
        Buffers.UV0.Add(RoofUVs[i]);
        Buffers.VertexColors.Add(FLinearColor::White);
    }
    // Roof triangles (same winding as cap, already CCW when viewed from above)
    for (int32 i = 0; i < CapTris.Num(); i += 3)
    {
        Buffers.Triangles.Add(FIntVector(
            RoofVertOffset + CapTris[i],
            RoofVertOffset + CapTris[i + 1],
            RoofVertOffset + CapTris[i + 2]));
        Buffers.TriGroupIDs.Add(Input.RoofMaterialSlot);
    }

    // ----- Wall quads -----
    const int32 WallVertOffset = Buffers.Vertices.Num();
    const int32 NumOuter = OuterRing.Num();

    for (int32 i = 0; i < NumOuter; ++i)
    {
        const int32 j = (i + 1) % NumOuter;

        const FVector BottomL(OuterRing[i].X, OuterRing[i].Y, Input.GroundZ);
        const FVector BottomR(OuterRing[j].X, OuterRing[j].Y, Input.GroundZ);
        const FVector TopL(OuterRing[i].X, OuterRing[i].Y, Input.GroundZ + Input.HeightCm);
        const FVector TopR(OuterRing[j].X, OuterRing[j].Y, Input.GroundZ + Input.HeightCm);

        // Wall normal: outward perpendicular of edge (i→j)
        const FVector Edge = (BottomR - BottomL).GetSafeNormal();
        const FVector Normal = FVector::CrossProduct(FVector::UpVector, Edge).GetSafeNormal();

        // Segment horizontal distance for UV
        const float SegLenCm = (BottomR - BottomL).Size();
        const float URight = SegLenCm / 100.0f; // 1 UV unit per meter
        const float VTop = Input.HeightCm / 100.0f;

        const int32 Base = WallVertOffset + i * 4;

        Buffers.Vertices.Add(BottomL);
        Buffers.Vertices.Add(BottomR);
        Buffers.Vertices.Add(TopL);
        Buffers.Vertices.Add(TopR);

        Buffers.Normals.Add(Normal);
        Buffers.Normals.Add(Normal);
        Buffers.Normals.Add(Normal);
        Buffers.Normals.Add(Normal);

        Buffers.UV0.Add(FVector2D(0.0f, 0.0f));
        Buffers.UV0.Add(FVector2D(URight, 0.0f));
        Buffers.UV0.Add(FVector2D(0.0f, VTop));
        Buffers.UV0.Add(FVector2D(URight, VTop));

        for (int32 k = 0; k < 4; ++k) Buffers.VertexColors.Add(FLinearColor::White);

        // Two triangles per wall quad
        Buffers.Triangles.Add(FIntVector(Base + 0, Base + 2, Base + 1)); // BL → TL → BR
        Buffers.Triangles.Add(FIntVector(Base + 1, Base + 2, Base + 3)); // BR → TL → TR
        Buffers.TriGroupIDs.Add(Input.WallMaterialSlot);
        Buffers.TriGroupIDs.Add(Input.WallMaterialSlot);
    }

    // Feed into dynamic mesh
    FGeometryScriptIndexList NewTriIndices;
    UGeometryScriptLibrary_MeshBasicEditFunctions::AppendBuffersToMesh(
        DynMesh, Buffers, NewTriIndices);

    // Recompute smooth normals
    FGeometryScriptCalculateNormalsOptions NormOpts;
    NormOpts.bAngleWeighted = true;
    UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(DynMesh, NormOpts);

    // Compute bounds (GetMeshBoundingBox now lives in MeshQueryFunctions and returns FBox directly)
    Result.Bounds = UGeometryScriptLibrary_MeshQueryFunctions::GetMeshBoundingBox(DynMesh);

    return Result;
}

// ---------------------------------------------------------------------------
#if WITH_EDITOR
UStaticMesh* FOSMBuildingExtruder::BakeToStaticMesh(
    UDynamicMesh* DynMesh,
    const FString& AssetPath,
    UMaterialInterface* WallMaterial,
    UMaterialInterface* RoofMaterial,
    const TArray<float>& LODDistances)
{
    if (!DynMesh) return nullptr;

    // Create / find the package
    FString PackageName = AssetPath;
    UPackage* Package = CreatePackage(*PackageName);
    if (!Package) return nullptr;

    Package->FullyLoad();

    // Create a new UStaticMesh inside the package
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
    UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone);
    if (!StaticMesh) return nullptr;

    // Convert DynamicMesh → StaticMesh via Geometry Scripting
    FGeometryScriptCopyMeshToAssetOptions CopyOptions;
    CopyOptions.bEnableRecomputeNormals = true;
    CopyOptions.bEnableRecomputeTangents = true;

    FGeometryScriptMeshWriteLOD TargetLOD;
    TargetLOD.LODIndex = 0;
    TargetLOD.bWriteHiResSource = false;

    EGeometryScriptOutcomePins Outcome;
    UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshToStaticMesh(
        DynMesh, StaticMesh, CopyOptions, TargetLOD, Outcome);

    if (Outcome != EGeometryScriptOutcomePins::Success)
    {
        UE_LOG(LogTemp, Warning, TEXT("FOSMBuildingExtruder: Failed to bake to StaticMesh at %s"), *AssetPath);
        return nullptr;
    }

    // Assign materials
    StaticMesh->GetStaticMaterials().Reset();
    StaticMesh->GetStaticMaterials().Add(FStaticMaterial(WallMaterial, TEXT("Wall")));
    StaticMesh->GetStaticMaterials().Add(FStaticMaterial(RoofMaterial, TEXT("Roof")));

    // LOD screen sizes (from distances in cm → rough screen-size approx)
    if (LODDistances.Num() >= 1)
    {
        FMeshReductionSettings LOD1Settings;
        LOD1Settings.PercentTriangles = 0.5f;
        LOD1Settings.MaxDeviation = 0.0f;
        LOD1Settings.WeldingThreshold = 10.0f;

        FMeshReductionSettings LOD2Settings;
        LOD2Settings.PercentTriangles = 0.1f;
        LOD2Settings.MaxDeviation = 0.0f;
        LOD2Settings.WeldingThreshold = 50.0f;

        StaticMesh->SetAutoComputeLODScreenSize(false);

        if (StaticMesh->GetNumSourceModels() < 3)
        {
            StaticMesh->SetNumSourceModels(3);
        }
        StaticMesh->GetSourceModel(0).ScreenSize.Default = 1.0f;
        StaticMesh->GetSourceModel(1).ScreenSize.Default = (LODDistances.Num() > 0) ? (1.0f / (LODDistances[0] / 5000.0f)) : 0.3f;
        StaticMesh->GetSourceModel(2).ScreenSize.Default = (LODDistances.Num() > 1) ? (1.0f / (LODDistances[1] / 5000.0f)) : 0.05f;
        StaticMesh->GetSourceModel(1).ReductionSettings = LOD1Settings;
        StaticMesh->GetSourceModel(2).ReductionSettings = LOD2Settings;
    }

    // Build and save
    StaticMesh->Build(false);
    StaticMesh->MarkPackageDirty();

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.Error = GError;

    const FString FileName = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());
    UPackage::SavePackage(Package, StaticMesh, *FileName, SaveArgs);

    FAssetRegistryModule::AssetCreated(StaticMesh);

    return StaticMesh;
}
#endif

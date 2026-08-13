// Copyright InviMind. All Rights Reserved.

#include "Generation/FOSMWorldBuilder.h"
#include "Elevation/FOSMDEMSampler.h"
#include "Elevation/FOSMGeoTIFFTile.h"
#include "Generation/FOSMBuildingMesher.h"
#include "Generation/UOSMBuildingArchetype.h"
#include "Graph/UOSMCityGraph.h"
#include "CompGeom/PolygonTriangulation.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

const FName FOSMWorldBuilder::GetGeneratedActorTag()
{
    return FName(TEXT("OSMWorldGen.Generated"));
}

namespace
{
    constexpr double MetersToCm = 100.0;
    constexpr double MetersPerDegLat = 111320.0;

    /** Grey-box palette. Deliberately flat and unlit-looking: this pass is about shape. */
    const FLinearColor ColourBuilding(0.62f, 0.60f, 0.58f);   // neutral grey
    const FLinearColor ColourRoof    (0.48f, 0.46f, 0.45f);   // slightly darker, to read the cap
    const FLinearColor ColourRoad    (0.24f, 0.24f, 0.26f);   // asphalt
    const FLinearColor ColourVeg     (0.22f, 0.55f, 0.22f);   // green
    const FLinearColor ColourWater   (0.12f, 0.35f, 0.70f);   // blue
    const FLinearColor ColourLanduse (0.45f, 0.40f, 0.52f);   // muted violet
    const FLinearColor ColourLeisure (0.35f, 0.62f, 0.38f);

    UWorld* GetEditorWorld()
    {
        return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    }

    /**
     * A tintable material.
     *
     * BasicShapeMaterial is used because it is the one engine material verified to expose a
     * "Color" vector parameter — checked against this install rather than assumed, since a
     * material without the parameter fails silently and leaves everything default grey.
     */
    UMaterialInterface* GetBaseMaterial()
    {
        static UMaterialInterface* Base = LoadObject<UMaterialInterface>(
            nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
        return Base;
    }

    UMaterialInstanceDynamic* MakeColouredMaterial(UObject* Outer, const FLinearColor& Colour)
    {
        UMaterialInterface* Base = GetBaseMaterial();
        if (!Base) return nullptr;

        UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Base, Outer);
        if (Instance)
        {
            Instance->SetVectorParameterValue(TEXT("Color"), Colour);
        }
        return Instance;
    }

    /** Projects lat/lon to local metres about the region centre, matching the overlay's frame. */
    struct FProjector
    {
        double CentreLat = 0.0;
        double CentreLon = 0.0;
        double LonScale = MetersPerDegLat;

        explicit FProjector(const FOSMRegion& Region)
            : CentreLat(Region.GetCenterLat())
            , CentreLon(Region.GetCenterLon())
            , LonScale(MetersPerDegLat * FMath::Cos(FMath::DegreesToRadians(Region.GetCenterLat())))
        {
        }

        FVector2D ToMeters(const FVector2D& LatLon) const
        {
            return FVector2D((LatLon.X - CentreLat) * MetersPerDegLat,
                             (LatLon.Y - CentreLon) * LonScale);
        }
    };

    /** Ground height in cm at a coordinate, relative to the DEM's lowest point. */
    struct FGround
    {
        const FOSMDEMSampler* Sampler = nullptr;
        double BaseElevation = 0.0;

        double HeightCm(const FVector2D& LatLon) const
        {
            if (!Sampler) return 0.0;

            const double Elevation = Sampler->SampleElevation(LatLon.X, LatLon.Y);
            if (FMath::IsNaN(Elevation)) return 0.0;

            return (Elevation - BaseElevation) * MetersToCm;
        }
    };

    /** Spawn one actor carrying a procedural mesh, tagged so Clear() can find it again. */
    AActor* SpawnMeshActor(UWorld* World, AActor* Root, const FString& Label, const FVector& Location,
                           UProceduralMeshComponent*& OutMesh)
    {
        FActorSpawnParameters Params;
        Params.ObjectFlags = RF_Transactional;

        AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator, Params);
        if (!Actor) { OutMesh = nullptr; return nullptr; }

        Actor->SetActorLabel(Label);
        Actor->Tags.AddUnique(FOSMWorldBuilder::GetGeneratedActorTag());

        OutMesh = NewObject<UProceduralMeshComponent>(Actor);
        Actor->SetRootComponent(OutMesh);
        OutMesh->RegisterComponent();
        OutMesh->SetMobility(EComponentMobility::Movable);

        if (Root)
        {
            Actor->AttachToActor(Root, FAttachmentTransformRules::KeepWorldTransform);
        }

        return Actor;
    }
}

// ---------------------------------------------------------------------------
int32 FOSMWorldBuilder::Clear()
{
    UWorld* World = GetEditorWorld();
    if (!World) return 0;

    // Collected first, then destroyed: mutating the level while iterating it is how an iterator
    // ends up pointing at a destroyed actor.
    TArray<AActor*> ToRemove;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->Tags.Contains(GetGeneratedActorTag()))
        {
            ToRemove.Add(*It);
        }
    }

    for (AActor* Actor : ToRemove)
    {
        World->DestroyActor(Actor);
    }

    return ToRemove.Num();
}

// ---------------------------------------------------------------------------
FOSMWorldBuilder::FResult FOSMWorldBuilder::Build(
    const UOSMCityGraph& Graph, const FOSMRegion& Region, const FOptions& Options)
{
    const double StartTime = FPlatformTime::Seconds();
    FResult Result;

    UWorld* World = GetEditorWorld();
    if (!World)
    {
        Result.Problems.Add(TEXT("No editor world is open. Open a level first."));
        return Result;
    }

    if (!Region.IsValid())
    {
        Result.Problems.Add(TEXT("The region is invalid, so nothing can be placed."));
        return Result;
    }

    // A rebuild replaces, never accumulates.
    Clear();

    const FProjector Projector(Region);

    // Terrain, so geometry sits on the ground rather than through it.
    FOSMDEMSampler Sampler;
    FGround Ground;
    if (Options.bUseTerrain && !Graph.SourceDEMFile.IsEmpty() && Sampler.Load(Graph.SourceDEMFile))
    {
        Ground.Sampler = &Sampler;
        Ground.BaseElevation = Sampler.GetTileMetadata().MinElevation;
    }

    // One root, so the whole city is a single thing to select, move or delete.
    FActorSpawnParameters RootParams;
    RootParams.ObjectFlags = RF_Transactional;
    AActor* Root = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, RootParams);
    if (Root)
    {
        Root->SetActorLabel(TEXT("OSM_City"));
        Root->Tags.AddUnique(GetGeneratedActorTag());
        USceneComponent* RootComp = NewObject<USceneComponent>(Root);
        Root->SetRootComponent(RootComp);
        RootComp->RegisterComponent();
    }

    // ---- Buildings ----
    if (Options.bBuildings)
    {
        // A default archetype so the grey-box pass works before any archetype assets exist.
        // Without it the whole point of this pass — seeing shape and placement — would wait on
        // art that has not been authored yet.
        UOSMBuildingArchetype* Fallback = NewObject<UOSMBuildingArchetype>(GetTransientPackage());
        Fallback->DisplayName = TEXT("Grey box");

        for (const int32 NodeId : Graph.GetNodesOfType(EOSMNodeType::Building))
        {
            const FOSMGraphNode& Node = Graph.Nodes[NodeId];
            if (!Node.HasGeometry()) { ++Result.Skipped; continue; }

            const TArrayView<const FVector2D> Ring = Graph.Geometry.GetOuterRing(Node.Geometry);
            if (Ring.Num() < 3) { ++Result.Skipped; continue; }

            // Footprint projected to metres, relative to the building's own centroid so the mesh
            // is built around its origin and the actor carries the world position. Keeping mesh
            // vertices near zero avoids the float precision loss that wrecked the old pipeline.
            const FVector2D CentroidM = Projector.ToMeters(Node.Metrics.CentroidLatLon);

            FOSMBuildingMeshParams MeshParams;
            MeshParams.Rings.AddDefaulted(1);
            MeshParams.Rings[0].Reserve(Ring.Num());
            for (const FVector2D& Point : Ring)
            {
                MeshParams.Rings[0].Add(Projector.ToMeters(Point) - CentroidM);
            }

            const int32 Levels = Node.Tags.Contains(TEXT("building:levels"))
                ? FCString::Atoi(*Node.Tags[TEXT("building:levels")]) : 0;

            MeshParams.HeightMeters = Fallback->ResolveHeightMeters(
                static_cast<float>(Node.Metrics.HeightMeters), Levels, NodeId);
            MeshParams.Seed = NodeId;
            MeshParams.Archetype = Fallback;

            FOSMMeshData Mesh;
            FString Error;
            if (!FOSMBuildingMesher::Build(MeshParams, Mesh, Error))
            {
                ++Result.Skipped;
                if (Result.Problems.Num() < 12)
                {
                    Result.Problems.Add(FString::Printf(TEXT("Building #%d: %s"), NodeId, *Error));
                }
                continue;
            }

            const FVector Location(
                CentroidM.X * MetersToCm,
                CentroidM.Y * MetersToCm,
                Ground.HeightCm(Node.Metrics.CentroidLatLon));

            UProceduralMeshComponent* MeshComp = nullptr;
            if (!SpawnMeshActor(World, Root, FString::Printf(TEXT("Building_%d"), NodeId), Location, MeshComp))
            {
                ++Result.Skipped;
                continue;
            }

            // Walls and roof as separate sections so the roof reads as a distinct plane — which
            // is what makes height differences legible in a flat-grey pass.
            const TArray<FVector> EmptyTangents;
            const TArray<FLinearColor> EmptyColours;

            TArray<int32> WallTris = Mesh.GetTrianglesForSection(EOSMMeshSection::Wall);
            WallTris.Append(Mesh.GetTrianglesForSection(EOSMMeshSection::GroundFloor));

            MeshComp->CreateMeshSection_LinearColor(0, Mesh.Vertices, WallTris, Mesh.Normals,
                Mesh.UVs, EmptyColours, TArray<FProcMeshTangent>(), /*bCreateCollision*/ false);

            MeshComp->CreateMeshSection_LinearColor(1, Mesh.Vertices,
                Mesh.GetTrianglesForSection(EOSMMeshSection::Roof), Mesh.Normals,
                Mesh.UVs, EmptyColours, TArray<FProcMeshTangent>(), /*bCreateCollision*/ false);

            MeshComp->SetMaterial(0, MakeColouredMaterial(MeshComp, ColourBuilding));
            MeshComp->SetMaterial(1, MakeColouredMaterial(MeshComp, ColourRoof));

            ++Result.Buildings;
        }
    }

    // ---- Roads ----
    if (Options.bRoads)
    {
        for (const int32 NodeId : Graph.GetNodesOfType(EOSMNodeType::RoadSegment))
        {
            const FOSMGraphNode& Node = Graph.Nodes[NodeId];
            if (!Node.HasGeometry()) { ++Result.Skipped; continue; }

            const TArrayView<const FVector2D> Line = Graph.Geometry.GetOuterRing(Node.Geometry);
            if (Line.Num() < 2) { ++Result.Skipped; continue; }

            // Width from the data where tagged, else a sane default by nothing more than "a road
            // is wider than a footpath".
            const double HalfWidthM = 0.5 * (Node.Metrics.WidthMeters > 0.5
                ? Node.Metrics.WidthMeters
                : (Node.SubType == TEXT("footway") || Node.SubType == TEXT("path") ? 2.0 : 6.0));

            const FVector2D OriginM = Projector.ToMeters(Line[0]);

            TArray<FVector> Vertices;
            TArray<int32> Triangles;
            TArray<FVector> Normals;
            TArray<FVector2D> UVs;

            double RunningLength = 0.0;

            for (int32 Index = 0; Index < Line.Num(); ++Index)
            {
                const FVector2D Here = Projector.ToMeters(Line[Index]);

                // Direction averaged across the joint, so consecutive quads meet without a gap on
                // the outside of a bend.
                FVector2D Direction;
                if (Index == 0)
                {
                    Direction = (Projector.ToMeters(Line[1]) - Here).GetSafeNormal();
                }
                else if (Index == Line.Num() - 1)
                {
                    Direction = (Here - Projector.ToMeters(Line[Index - 1])).GetSafeNormal();
                    RunningLength += FVector2D::Distance(Here, Projector.ToMeters(Line[Index - 1]));
                }
                else
                {
                    const FVector2D Prev = Projector.ToMeters(Line[Index - 1]);
                    const FVector2D Next = Projector.ToMeters(Line[Index + 1]);
                    Direction = ((Here - Prev).GetSafeNormal() + (Next - Here).GetSafeNormal()).GetSafeNormal();
                    RunningLength += FVector2D::Distance(Here, Prev);
                }

                const FVector2D Side(-Direction.Y, Direction.X);
                const FVector2D Left = Here + Side * HalfWidthM;
                const FVector2D Right = Here - Side * HalfWidthM;

                // Each edge sampled against the terrain independently, so a road crossing a slope
                // banks with the ground instead of floating at one end.
                const double GroundCm = Ground.HeightCm(Line[Index]);

                // Raised slightly so a road never z-fights the ground plane beneath it.
                const double LiftCm = 6.0;

                Vertices.Emplace((Left.X - OriginM.X) * MetersToCm, (Left.Y - OriginM.Y) * MetersToCm, GroundCm + LiftCm);
                Vertices.Emplace((Right.X - OriginM.X) * MetersToCm, (Right.Y - OriginM.Y) * MetersToCm, GroundCm + LiftCm);

                Normals.Emplace(0, 0, 1);
                Normals.Emplace(0, 0, 1);

                UVs.Emplace(0.0, RunningLength);
                UVs.Emplace(1.0, RunningLength);

                if (Index > 0)
                {
                    const int32 Base = (Index - 1) * 2;
                    Triangles.Append({ Base, Base + 1, Base + 3 });
                    Triangles.Append({ Base, Base + 3, Base + 2 });
                }
            }

            if (Triangles.Num() == 0) { ++Result.Skipped; continue; }

            UProceduralMeshComponent* MeshComp = nullptr;
            const FVector Location(OriginM.X * MetersToCm, OriginM.Y * MetersToCm, 0.0);

            if (!SpawnMeshActor(World, Root, FString::Printf(TEXT("Road_%d"), NodeId), Location, MeshComp))
            {
                ++Result.Skipped;
                continue;
            }

            MeshComp->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs,
                TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
            MeshComp->SetMaterial(0, MakeColouredMaterial(MeshComp, ColourRoad));

            ++Result.Roads;
        }
    }

    // ---- Areas: water, vegetation, landuse, leisure ----
    if (Options.bAreas)
    {
        struct FAreaKind { EOSMNodeType Type; FLinearColor Colour; double LiftCm; const TCHAR* Prefix; };

        // Lift values separate overlapping areas: a park inside a landuse zone inside the ground
        // would otherwise z-fight into a flickering mess.
        const FAreaKind Kinds[] = {
            { EOSMNodeType::LanduseZone,    ColourLanduse, 1.0,  TEXT("Landuse") },
            { EOSMNodeType::VegetationArea, ColourVeg,     3.0,  TEXT("Vegetation") },
            { EOSMNodeType::LeisureArea,    ColourLeisure, 4.0,  TEXT("Leisure") },
            { EOSMNodeType::WaterBody,      ColourWater,   5.0,  TEXT("Water") },
        };

        for (const FAreaKind& Kind : Kinds)
        {
            for (const int32 NodeId : Graph.GetNodesOfType(Kind.Type))
            {
                const FOSMGraphNode& Node = Graph.Nodes[NodeId];
                if (!Node.HasGeometry() || !Graph.Geometry.IsArea(Node.Geometry)) { ++Result.Skipped; continue; }

                const TArrayView<const FVector2D> Ring = Graph.Geometry.GetOuterRing(Node.Geometry);
                if (Ring.Num() < 3) { ++Result.Skipped; continue; }

                const FVector2D CentroidM = Projector.ToMeters(Node.Metrics.CentroidLatLon);

                TArray<FVector2D> Flat;
                Flat.Reserve(Ring.Num());
                for (const FVector2D& Point : Ring)
                {
                    Flat.Add(Projector.ToMeters(Point) - CentroidM);
                }

                TArray<UE::Geometry::FIndex3i> Tris;
                PolygonTriangulation::TriangulateSimplePolygon<double>(Flat, Tris, false);
                if (Tris.Num() == 0)
                {
                    ++Result.Skipped;
                    if (Result.Problems.Num() < 12)
                    {
                        Result.Problems.Add(FString::Printf(
                            TEXT("%s #%d: outline could not be triangulated (likely self-intersecting)."),
                            Kind.Prefix, NodeId));
                    }
                    continue;
                }

                TArray<FVector> Vertices;
                TArray<FVector> Normals;
                TArray<FVector2D> UVs;
                Vertices.Reserve(Flat.Num());

                for (int32 Index = 0; Index < Flat.Num(); ++Index)
                {
                    // Sampled per vertex, so an area follows the slope it sits on.
                    const double GroundCm = Ground.HeightCm(Ring[Index]);
                    Vertices.Emplace(Flat[Index].X * MetersToCm, Flat[Index].Y * MetersToCm, GroundCm + Kind.LiftCm);
                    Normals.Emplace(0, 0, 1);
                    UVs.Emplace(Flat[Index].X, Flat[Index].Y);
                }

                TArray<int32> Triangles;
                Triangles.Reserve(Tris.Num() * 3);
                for (const UE::Geometry::FIndex3i& Tri : Tris)
                {
                    // Reversed for an up-facing surface: Unreal's front faces are clockwise seen
                    // from the front, and the triangulator emits counter-clockwise.
                    Triangles.Append({ Tri.A, Tri.C, Tri.B });
                }

                UProceduralMeshComponent* MeshComp = nullptr;
                const FVector Location(CentroidM.X * MetersToCm, CentroidM.Y * MetersToCm, 0.0);

                if (!SpawnMeshActor(World, Root, FString::Printf(TEXT("%s_%d"), Kind.Prefix, NodeId), Location, MeshComp))
                {
                    ++Result.Skipped;
                    continue;
                }

                MeshComp->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs,
                    TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
                MeshComp->SetMaterial(0, MakeColouredMaterial(MeshComp, Kind.Colour));

                ++Result.Areas;
            }
        }
    }

    Result.DurationSeconds = FPlatformTime::Seconds() - StartTime;
    return Result;
}

// ---------------------------------------------------------------------------
FString FOSMWorldBuilder::FResult::ToString() const
{
    TArray<FString> Lines;

    Lines.Add(FString::Printf(TEXT("Built %d buildings, %d roads, %d areas in %.2f s."),
        Buildings, Roads, Areas, DurationSeconds));

    if (Skipped > 0)
    {
        Lines.Add(FString::Printf(TEXT("%d node(s) skipped — see below."), Skipped));
    }

    for (const FString& Problem : Problems)
    {
        Lines.Add(FString::Printf(TEXT("  %s"), *Problem));
    }

    return FString::Join(Lines, TEXT("\n"));
}

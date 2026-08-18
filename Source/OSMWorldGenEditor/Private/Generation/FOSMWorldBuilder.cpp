// Copyright InviMind. All Rights Reserved.

#include "Generation/FOSMWorldBuilder.h"
#include "Diagnostics/FOSMGeometryDump.h"
#include "Elevation/FOSMDEMSampler.h"
#include "Elevation/FOSMTerrainSurface.h"
#include "Elevation/FOSMGeoTIFFTile.h"
#include "Generation/FOSMBuildingMesher.h"
#include "Generation/UOSMBuildingArchetype.h"
#include "Graph/UOSMCityGraph.h"
#include "CompGeom/PolygonTriangulation.h"
#include "CompGeom/Delaunay2.h"
#include "Algo/Reverse.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
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

    /**
     * Grey-box palette. Flat and unlit-looking: this pass is about shape.
     *
     * Deliberately darker and less saturated than the first pass. The pale version washed out
     * under the editor's exposure until terrain, landuse and pavement were nearly the same tone,
     * and the differences between categories — which is the entire information content of a
     * grey-box — stopped reading at any distance.
     */
    const FLinearColor ColourTerrain (0.26f, 0.23f, 0.18f);   // earth, so every overlay reads on it
    const FLinearColor ColourBuilding(0.44f, 0.43f, 0.41f);   // neutral grey
    const FLinearColor ColourRoof    (0.31f, 0.30f, 0.29f);   // darker, to read the cap
    const FLinearColor ColourRoad    (0.13f, 0.13f, 0.15f);   // asphalt
    const FLinearColor ColourRail    (0.20f, 0.17f, 0.15f);   // ballast: warmer and lighter than tarmac
    const FLinearColor ColourPaved   (0.22f, 0.21f, 0.21f);   // parking, plazas, platforms
    const FLinearColor ColourCivic   (0.24f, 0.27f, 0.29f);   // school, hospital, worship: grounds, not tarmac

    /**
     * Which surface an amenity area is.
     *
     * "amenity" is a catch-all in the tag rules, so treating the whole class as pavement paints
     * every school, hospital and place of worship as a car park. That is not a subtle error: in a
     * sampled Bangalore region 21 of 28 amenity areas are institutional grounds and only 3 are
     * parking, while in Prague 137 of 152 are parking. Whichever single colour were chosen would
     * be badly wrong for one of those cities.
     */
    bool IsPavedAmenity(const FString& SubType)
    {
        static const TSet<FString> Paved = {
            TEXT("parking"), TEXT("parking_space"), TEXT("parking_entrance"), TEXT("bicycle_parking"),
            TEXT("motorcycle_parking"), TEXT("taxi"), TEXT("fuel"), TEXT("charging_station"),
            TEXT("bus_station"), TEXT("trolley_bay"), TEXT("marketplace"), TEXT("recycling"),
        };
        return Paved.Contains(SubType);
    }
    const FLinearColor ColourVeg     (0.13f, 0.31f, 0.14f);   // green
    const FLinearColor ColourWater   (0.06f, 0.20f, 0.42f);   // blue
    const FLinearColor ColourLanduse (0.28f, 0.25f, 0.33f);   // muted violet
    const FLinearColor ColourLeisure (0.20f, 0.37f, 0.23f);

    UWorld* GetEditorWorld()
    {
        return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    }

    /**
     * A tintable material for the grey-box pass.
     *
     * BasicShapeMaterial exposes a "Color" vector parameter, verified against this install by
     * OSMWorldGen.Diagnostics.MaterialProbe, and it is what produced visible grey buildings, green
     * vegetation and blue water in the editor viewport.
     *
     * KNOWN GAP: in the headless snapshot (OSMWorldGen.Diagnostics.Snapshot) every surface renders
     * as one flat grey regardless of this parameter. That was chased a long way — four other
     * engine materials and a purpose-built unlit vertex-colour material all produced the identical
     * flat grey, with the material confirmed applied and the parameter confirmed set. The common
     * factor is a shader fallback in the offscreen render path, not the geometry. So the snapshot
     * is currently trustworthy for SHAPE and COVERAGE but not for COLOUR; the plan view is the
     * authority on colour, and it does not depend on shaders at all.
     */
    UMaterialInterface* GetBaseMaterial()
    {
        static UMaterialInterface* Base = []() -> UMaterialInterface*
        {
            // Overridable so candidates can be compared by rendering them rather than by reading
            // the asset and hoping.
            FString Path = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
            FParse::Value(FCommandLine::Get(), TEXT("-OSMMaterial="), Path);
            return LoadObject<UMaterialInterface>(nullptr, *Path);
        }();
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

    /**
     * Append one triangle, wound so its geometric normal points down and it is visible from above.
     *
     * Degenerate triangles are dropped rather than emitted with an arbitrary winding: a zero-area
     * face has no meaningful direction, contributes nothing, and would show up in the winding
     * audit as an unexplained failure.
     */
    void AppendFacingDown(const TArray<FVector>& Vertices, TArray<int32>& Triangles,
                          int32 A, int32 B, int32 C)
    {
        const FVector& P0 = Vertices[A];
        const FVector& P1 = Vertices[B];
        const FVector& P2 = Vertices[C];

        const double NormalZ = (P1.X - P0.X) * (P2.Y - P0.Y) - (P1.Y - P0.Y) * (P2.X - P0.X);

        if (FMath::IsNearlyZero(NormalZ, 1.0))
        {
            return;
        }

        if (NormalZ < 0.0)
        {
            Triangles.Append({ A, B, C });
        }
        else
        {
            Triangles.Append({ A, C, B });
        }
    }

    /** One colour repeated per vertex, for surfaces that are a single flat colour. */
    TArray<FLinearColor> UniformColours(int32 Count, const FLinearColor& Colour)
    {
        TArray<FLinearColor> Colours;
        Colours.Init(Colour, Count);
        return Colours;
    }

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

        // Re-apply the location AFTER swapping in the new root.
        //
        // SpawnActor applies the spawn transform to the root component that existed at the time.
        // Replacing that root discards it, leaving the component at identity — which put every
        // building, road and area on top of each other at the world origin, each still carrying
        // vertices offset from its own centroid. The result looked like one nested blob and a
        // single enormous ground plane, rather than a city.
        Actor->SetActorLocation(Location);

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

    FOSMGeometryDump Dump(Options.DebugDumpPath);
    Dump.WriteHeader(Region.GetMinLat(), Region.GetMaxLat(), Region.GetMinLon(), Region.GetMaxLon(),
                     Options.bGreyBox ? TEXT("greybox") : TEXT("materials"));

    const FOSMLocalProjection Projector(Region.GetCenterLat(), Region.GetCenterLon());

    // ---- The ground ----
    //
    // Built before anything else and used by everything else. Its extent is the region UNIONED
    // with the actual extent of the data, because Overpass returns whole ways and roughly 13% of
    // the geometry legitimately crosses the region boundary. Sizing the ground to the region alone
    // left that geometry hanging over nothing, and the DEM fallback dropped it to the valley
    // floor — the largest grounding error in the scene at up to 21 m.
    FOSMDEMSampler Sampler;
    const bool bHaveDEM = Options.bUseTerrain
        && !Graph.SourceDEMFile.IsEmpty()
        && Sampler.Load(Graph.SourceDEMFile);

    FBox2D GroundBounds(ForceInit);
    GroundBounds += Projector.ToMeters(FVector2D(Region.GetMinLat(), Region.GetMinLon()));
    GroundBounds += Projector.ToMeters(FVector2D(Region.GetMaxLat(), Region.GetMaxLon()));

    for (const FOSMGraphNode& Node : Graph.Nodes)
    {
        if (!Node.HasGeometry())
        {
            continue;
        }
        for (const FVector2D& Point : Graph.Geometry.GetOuterRing(Node.Geometry))
        {
            GroundBounds += Projector.ToMeters(Point);
        }
    }

    // A margin so nothing sits exactly on the edge, where the surface stops being interpolated
    // and starts being clamped.
    GroundBounds = GroundBounds.ExpandBy(Options.TerrainMarginMeters);

    FOSMTerrainSurface Ground;
    Ground.Build(bHaveDEM ? &Sampler : nullptr, Projector, GroundBounds, Options.TerrainQuadMeters);

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

    // ---- Terrain ----
    //
    // Built first and drawn beneath everything else, because it is the surface the city sits on:
    // without it, roads and areas float over a void and there is no way to tell whether anything
    // is actually on the ground. A DEM-sampled grid rather than a flat plane, so the relief the
    // elevation data describes — 34 m across the sampled region — is visible.
    if (Options.bTerrain)
    {
        TArray<FVector> Vertices;
        TArray<FVector> Normals;
        TArray<FVector2D> UVs;
        TArray<int32> Triangles;

        // Emitted by the surface itself, so the mesh drawn and the heights everything else was
        // grounded against are by construction the same surface.
        Ground.BuildMesh(Vertices, Triangles, Normals, UVs);

        UProceduralMeshComponent* MeshComp = nullptr;
        if (SpawnMeshActor(World, Root, TEXT("Terrain"), FVector::ZeroVector, MeshComp))
        {
            MeshComp->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs,
                UniformColours(Vertices.Num(), ColourTerrain), TArray<FProcMeshTangent>(),
                /*bCreateCollision*/ false);
            MeshComp->SetMaterial(0, MakeColouredMaterial(MeshComp, ColourTerrain));

            Dump.Add(TEXT("terrain"), TEXT("Terrain"), FVector::ZeroVector, Vertices, Triangles);

            Result.Terrain = 1;
        }
        else
        {
            Result.Problems.Add(TEXT("Terrain surface could not be spawned."));
        }
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

            const double TaggedHeightM = Fallback->ResolveHeightMeters(
                static_cast<float>(Node.Metrics.HeightMeters), Levels, NodeId);

            // ---- Foundation ----
            //
            // The ground under a footprint is sampled at every vertex, not once at the centroid.
            // A flat-bottomed box on sloping ground has to either float on the downhill side or
            // sink on the uphill one, and with a single centroid height it did both: 463 of 1317
            // buildings had a corner more than half a metre in the air, 436 had one more than half
            // a metre underground, worst case 4 m.
            //
            // So the base goes to the LOWEST ground under the footprint, less a skirt, and the
            // height is measured up from the HIGHEST. The building is buried into the slope rather
            // than balanced on it, and stands its full height above the ground you can see.
            // Over the whole footprint, not just its corners: a building spanning more than one
            // terrain quad can straddle a ridge none of its corners touch, and the ground would
            // then rise through the middle of a wall with every corner sitting correctly.
            TArray<FVector2D> FootprintMeters;
            FootprintMeters.Reserve(Ring.Num());
            for (const FVector2D& Point : Ring)
            {
                FootprintMeters.Add(Projector.ToMeters(Point));
            }

            double LowestGroundCm = 0.0;
            double HighestGroundCm = 0.0;
            Ground.MinMaxHeightCmOverPolygon(FootprintMeters, LowestGroundCm, HighestGroundCm);

            const double SkirtCm = FMath::Max(Options.FoundationSkirtMeters, 0.0) * MetersToCm;
            const double BaseCm = LowestGroundCm - SkirtCm;

            MeshParams.HeightMeters = static_cast<float>(
                TaggedHeightM + (HighestGroundCm - BaseCm) / MetersToCm);
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

            const FVector Location(CentroidM.X * MetersToCm, CentroidM.Y * MetersToCm, BaseCm);

            UProceduralMeshComponent* MeshComp = nullptr;
            if (!SpawnMeshActor(World, Root, FString::Printf(TEXT("Building_%d"), NodeId), Location, MeshComp))
            {
                ++Result.Skipped;
                continue;
            }

            // Walls and roof as separate sections so the roof reads as a distinct plane — which
            // is what makes height differences legible in a flat-grey pass.
            TArray<int32> WallTris = Mesh.GetTrianglesForSection(EOSMMeshSection::Wall);
            WallTris.Append(Mesh.GetTrianglesForSection(EOSMMeshSection::GroundFloor));

            const TArray<int32> RoofTris = Mesh.GetTrianglesForSection(EOSMMeshSection::Roof);

            // Walls and roof share one vertex array, so the colour is chosen per vertex by which
            // section uses it. Roof last, because a vertex shared with the wall top belongs
            // visually to the cap.
            TArray<FLinearColor> Colours = UniformColours(Mesh.Vertices.Num(), ColourBuilding);
            for (const int32 Index : RoofTris)
            {
                if (Colours.IsValidIndex(Index)) Colours[Index] = ColourRoof;
            }

            MeshComp->CreateMeshSection_LinearColor(0, Mesh.Vertices, WallTris, Mesh.Normals,
                Mesh.UVs, Colours, TArray<FProcMeshTangent>(), /*bCreateCollision*/ false);

            MeshComp->CreateMeshSection_LinearColor(1, Mesh.Vertices, RoofTris, Mesh.Normals,
                Mesh.UVs, Colours, TArray<FProcMeshTangent>(), /*bCreateCollision*/ false);

            MeshComp->SetMaterial(0, MakeColouredMaterial(MeshComp, ColourBuilding));
            MeshComp->SetMaterial(1, MakeColouredMaterial(MeshComp, ColourRoof));

            // The complete mesh, walls and roof together — the closure check needs every triangle
            // of the solid, not one section of it.
            Dump.Add(TEXT("building"), FString::Printf(TEXT("Building_%d"), NodeId), Location,
                     Mesh.Vertices, Mesh.Triangles);

            ++Result.Buildings;
        }
    }

    // ---- Linear features: roads, railways, waterways ----
    //
    // Railways and waterways go through the same ribbon builder as roads. They were classified all
    // along and simply never drawn: in a sampled Prague region 403 railway ways were dropped on the
    // floor, which in a city built around its rail corridors left large stretches of bare ground
    // that read as missing data rather than as an unhandled category.
    if (Options.bRoads)
    {
        struct FLinearKind
        {
            EOSMNodeType Type;
            FLinearColor Colour;
            double LiftCm;
            double DefaultWidthMeters;   // 0 defers to the road heuristic
            const TCHAR* Prefix;
            const TCHAR* DumpKind;
        };

        const FLinearKind LinearKinds[] = {
            { EOSMNodeType::Waterway,    ColourWater, 25.0,  6.0, TEXT("Waterway"), TEXT("water") },
            { EOSMNodeType::Railway,     ColourRail,  30.0,  4.0, TEXT("Railway"),  TEXT("rail")  },
            { EOSMNodeType::RoadSegment, ColourRoad,  35.0,  0.0, TEXT("Road"),     TEXT("road")  },
        };

        for (const FLinearKind& Kind : LinearKinds)
        {
        for (const int32 NodeId : Graph.GetNodesOfType(Kind.Type))
        {
            const FOSMGraphNode& Node = Graph.Nodes[NodeId];
            if (!Node.HasGeometry()) { ++Result.Skipped; continue; }

            // A closed railway or waterway way is a yard or a basin, not a ribbon; those belong to
            // the area pass.
            if (Graph.Geometry.IsArea(Node.Geometry)) { continue; }

            const TArrayView<const FVector2D> Line = Graph.Geometry.GetOuterRing(Node.Geometry);
            if (Line.Num() < 2) { ++Result.Skipped; continue; }

            // Width from the data where tagged, else a sane default by nothing more than "a road
            // is wider than a footpath".
            const double HalfWidthM = 0.5 * (Node.Metrics.WidthMeters > 0.5
                ? Node.Metrics.WidthMeters
                : (Kind.DefaultWidthMeters > 0.0
                    ? Kind.DefaultWidthMeters
                    : (Node.SubType == TEXT("footway") || Node.SubType == TEXT("path") ? 2.0 : 6.0)));

            // ---- follow the ground ----
            //
            // A cross-section is placed wherever the centreline crosses from one terrain triangle
            // into the next, as well as at the way's own vertices. Without this, the ribbon between
            // two sampled points is flat while the ground beneath it is not, and no amount of care
            // at the endpoints helps: measured on real data, road segments run up to 377 m across
            // an 8 m terrain grid, and a straight span departs from the ground by up to 29.9 m.
            //
            // Split at triangle boundaries rather than at a fixed interval, because within one
            // triangle the terrain is planar — so a segment that stays inside one is exactly
            // coplanar with the ground under it. A fixed interval would only shrink the error.
            TArray<FVector2D> Centreline;
            Centreline.Reserve(Line.Num());
            for (const FVector2D& Point : Line)
            {
                Centreline.Add(Projector.ToMeters(Point));
            }

            TArray<FVector2D> Draped;
            Ground.SplitAtTriangleBoundaries(Centreline, /*bClosed*/ false, Draped);
            if (Draped.Num() < 2) { ++Result.Skipped; continue; }

            const FVector2D OriginM = Draped[0];

            TArray<FVector> Vertices;
            TArray<int32> Triangles;
            TArray<FVector> Normals;
            TArray<FVector2D> UVs;

            double RunningLength = 0.0;

            for (int32 Index = 0; Index < Draped.Num(); ++Index)
            {
                const FVector2D Here = Draped[Index];

                // Direction averaged across the joint, so consecutive quads meet without a gap on
                // the outside of a bend.
                FVector2D Direction;
                if (Index == 0)
                {
                    Direction = (Draped[1] - Here).GetSafeNormal();
                }
                else if (Index == Draped.Num() - 1)
                {
                    Direction = (Here - Draped[Index - 1]).GetSafeNormal();
                    RunningLength += FVector2D::Distance(Here, Draped[Index - 1]);
                }
                else
                {
                    const FVector2D Prev = Draped[Index - 1];
                    const FVector2D Next = Draped[Index + 1];
                    Direction = ((Here - Prev).GetSafeNormal() + (Next - Here).GetSafeNormal()).GetSafeNormal();
                    RunningLength += FVector2D::Distance(Here, Prev);
                }

                const FVector2D Side(-Direction.Y, Direction.X);
                const FVector2D Left = Here + Side * HalfWidthM;
                const FVector2D Right = Here - Side * HalfWidthM;

                const double LiftCm = Kind.LiftCm;

                // Each kerb sampled where it actually is, not at the centreline, so a road across
                // a cross-slope banks with the ground instead of cutting into the uphill side.
                const double LeftGroundCm = Ground.HeightCmAt(Left);
                const double RightGroundCm = Ground.HeightCmAt(Right);

                Vertices.Emplace((Left.X - OriginM.X) * MetersToCm, (Left.Y - OriginM.Y) * MetersToCm, LeftGroundCm + LiftCm);
                Vertices.Emplace((Right.X - OriginM.X) * MetersToCm, (Right.Y - OriginM.Y) * MetersToCm, RightGroundCm + LiftCm);

                Normals.Emplace(0, 0, 1);
                Normals.Emplace(0, 0, 1);

                UVs.Emplace(0.0, RunningLength);
                UVs.Emplace(1.0, RunningLength);

                if (Index > 0)
                {
                    const int32 Base = (Index - 1) * 2;

                    // Wound for a -Z geometric normal, so the surface is visible from above.
                    //
                    // Decided per TRIANGLE by measuring the sign, not assumed and not decided per
                    // quad. Where a way doubles back on itself the averaged joint direction flips,
                    // which swaps the left and right kerbs; the resulting quad is folded, and its
                    // two triangles can face opposite ways — so even choosing one winding for the
                    // quad leaves one of them culled. Measuring each triangle needs no enumeration
                    // of the cases that cause the fold.
                    AppendFacingDown(Vertices, Triangles, Base, Base + 3, Base + 1);
                    AppendFacingDown(Vertices, Triangles, Base, Base + 2, Base + 3);
                }
            }

            if (Triangles.Num() == 0) { ++Result.Skipped; continue; }

            UProceduralMeshComponent* MeshComp = nullptr;
            const FVector Location(OriginM.X * MetersToCm, OriginM.Y * MetersToCm, 0.0);

            const FString Label = FString::Printf(TEXT("%s_%d"), Kind.Prefix, NodeId);

            if (!SpawnMeshActor(World, Root, Label, Location, MeshComp))
            {
                ++Result.Skipped;
                continue;
            }

            MeshComp->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs,
                UniformColours(Vertices.Num(), Kind.Colour), TArray<FProcMeshTangent>(), false);
            MeshComp->SetMaterial(0, MakeColouredMaterial(MeshComp, Kind.Colour));

            Dump.Add(Kind.DumpKind, Label, Location, Vertices, Triangles);

            ++Result.Roads;
        }
        }
    }

    // ---- Areas: water, vegetation, landuse, leisure ----
    if (Options.bAreas)
    {
        struct FAreaKind { EOSMNodeType Type; FLinearColor Colour; double LiftCm; const TCHAR* Prefix; };

        // Lift values order the layers: a park inside a landuse zone on top of terrain would
        // otherwise z-fight into a flickering mess.
        //
        // Spaced 5 cm apart rather than 4, and the ordering carried up to 35 cm for roads. Depth
        // precision falls off with viewing distance, so a gap that separates two surfaces cleanly
        // at street level stops separating them at all across a whole city — which is why roads
        // still flickered under landuse when the camera pulled back. Wider gaps push that failure
        // further away but cannot remove it: the real fix is to stop stacking coplanar surfaces
        // and partition the ground into one non-overlapping mesh, which is the next task.
        const FAreaKind Kinds[] = {
            { EOSMNodeType::LanduseZone,    ColourLanduse,  5.0,  TEXT("Landuse") },
            { EOSMNodeType::VegetationArea, ColourVeg,     10.0,  TEXT("Vegetation") },
            { EOSMNodeType::LeisureArea,    ColourLeisure, 15.0,  TEXT("Leisure") },
            // Parking, plazas, platforms and explicitly-mapped road surfaces. 137 parking areas
            // in the sampled Prague region were classified and never drawn.
            { EOSMNodeType::Amenity,        ColourPaved,   20.0,  TEXT("Paved") },
            { EOSMNodeType::WaterBody,      ColourWater,   25.0,  TEXT("Water") },
        };

        // How many areas ended up with a surface that spans the ground instead of following it.
        // Reported rather than inferred: the difference is invisible in the counts and obvious in
        // the viewport, which is the worst combination to debug from.
        int32 FellBackToSpanning = 0;

        for (const FAreaKind& Kind : Kinds)
        {
            for (const int32 NodeId : Graph.GetNodesOfType(Kind.Type))
            {
                const FOSMGraphNode& Node = Graph.Nodes[NodeId];
                if (!Node.HasGeometry() || !Graph.Geometry.IsArea(Node.Geometry)) { ++Result.Skipped; continue; }

                const TArrayView<const FVector2D> Ring = Graph.Geometry.GetOuterRing(Node.Geometry);
                if (Ring.Num() < 3) { ++Result.Skipped; continue; }

                const FVector2D CentroidM = Projector.ToMeters(Node.Metrics.CentroidLatLon);

                TArray<FVector2D> RingMeters;
                RingMeters.Reserve(Ring.Num());
                for (const FVector2D& Point : Ring)
                {
                    RingMeters.Add(Projector.ToMeters(Point));
                }

                // The boundary follows the ground for the same reason a road does — area edges run
                // even longer, up to 777 m in the sampled region.
                //
                // This drapes the OUTLINE only. The interior is still spanned by the triangulator,
                // so a large polygon over a hill can still bulge away from the ground in the middle
                // even with a perfect edge. Fixing that properly means triangulating against the
                // terrain grid rather than draping onto it, which is the planar-partition work.
                TArray<FVector2D> DrapedRing;
                Ground.SplitAtTriangleBoundaries(RingMeters, /*bClosed*/ true, DrapedRing);
                if (DrapedRing.Num() < 3) { ++Result.Skipped; continue; }

                TArray<FVector2D> Flat;
                Flat.Reserve(DrapedRing.Num());
                for (const FVector2D& Point : DrapedRing)
                {
                    Flat.Add(Point - CentroidM);
                }

                // Normalised counter-clockwise before triangulating.
                //
                // OSM winds area rings either way, and the triangulator preserves the input
                // winding — so without this the emitted normal followed the source data and
                // roughly half of every ground surface faced downward and was culled. Measured on
                // a real region: 276 landuse faces up, 317 down. The ground looked eaten away in
                // patches, which is not a defect anyone would attribute to ring winding.
                if (FOSMBuildingMesher::SignedArea(Flat) < 0.0)
                {
                    Algo::Reverse(Flat);
                }

                // ---- triangulate against the terrain, not just across the outline ----
                //
                // The terrain's own grid nodes inside the polygon are fed in as interior points, so
                // the surface bends with the ground rather than spanning it. Draping the outline
                // alone is not enough: the triangulator joins far-apart boundary vertices straight
                // across the middle, and a large area over a hill still bulged up to 7.9 m away
                // from the ground with a perfectly draped edge.
                const int32 RingCount = Flat.Num();

                TArray<FVector2D> InteriorNodes;
                Ground.GetGridNodesInsidePolygon(DrapedRing, InteriorNodes);

                TArray<FVector2D> Points = Flat;
                Points.Reserve(RingCount + InteriorNodes.Num());
                for (const FVector2D& GridNode : InteriorNodes)
                {
                    Points.Add(GridNode - CentroidM);
                }

                TArray<UE::Geometry::FIndex2i> Constraints;
                Constraints.Reserve(RingCount);
                for (int32 Index = 0; Index < RingCount; ++Index)
                {
                    Constraints.Emplace(Index, (Index + 1) % RingCount);
                }

                TArray<UE::Geometry::FIndex3i> Tris;
                {
                    UE::Geometry::FDelaunay2 Delaunay;

                    // OSM rings repeat their closing point, and draping can land a triangle
                    // crossing exactly on an existing vertex. Either produces a duplicate, which
                    // silently invalidates the constrained edges that reference it — and the whole
                    // triangulation then fails, falling back to a surface that spans the ground
                    // instead of following it. 184 of 231 areas were failing this way.
                    Delaunay.bAutomaticallyFixEdgesToDuplicateVertices = true;

                    // Real OSM outlines self-intersect often enough that refusing to produce
                    // anything when a constraint cannot be honoured is the worse trade: a slightly
                    // wrong boundary beats a polygon that spans a hill.
                    Delaunay.bValidateEdges = false;

                    if (Delaunay.Triangulate(Points, Constraints))
                    {
                        // Solid fill rather than a winding rule: it fills the outline whichever way
                        // the source data wound it, and every triangle's facing is corrected
                        // individually below anyway.
                        Delaunay.GetFilledTriangles(Tris, Constraints, UE::Geometry::FDelaunay2::EFillMode::Solid);
                    }
                }

                if (Tris.Num() == 0)
                {
                    ++FellBackToSpanning;
                    // Self-intersecting rings defeat a constrained triangulation. Ear clipping on
                    // the outline alone still produces a usable surface — one that spans the ground
                    // rather than following it, which is a visible flaw and far better than a hole.
                    Points.SetNum(RingCount);
                    PolygonTriangulation::TriangulateSimplePolygon<double>(Flat, Tris, false);
                }

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
                Vertices.Reserve(Points.Num());

                for (int32 Index = 0; Index < Points.Num(); ++Index)
                {
                    // Sampled from the vertex's OWN position, so an area follows the slope it sits
                    // on. Previously this read Ring[Index] while placing Flat[Index] — which stopped
                    // being the same point the moment the ring was reversed to normalise its
                    // winding, silently giving each vertex some other vertex's height.
                    const double GroundCm = Ground.HeightCmAt(Points[Index] + CentroidM);
                    Vertices.Emplace(Points[Index].X * MetersToCm, Points[Index].Y * MetersToCm,
                                     GroundCm + Kind.LiftCm);
                    Normals.Emplace(0, 0, 1);
                    UVs.Emplace(Points[Index].X, Points[Index].Y);
                }

                TArray<int32> Triangles;
                Triangles.Reserve(Tris.Num() * 3);
                for (const UE::Geometry::FIndex3i& Tri : Tris)
                {
                    // Facing decided per triangle by measuring it, rather than by assuming what the
                    // triangulator produced — a Delaunay fill makes no promise about winding, and
                    // guessing wrong culls the surface silently.
                    AppendFacingDown(Vertices, Triangles, Tri.A, Tri.B, Tri.C);
                }

                // Amenity splits by subtype: a car park is tarmac, a school is grounds.
                const bool bCivic = Kind.Type == EOSMNodeType::Amenity && !IsPavedAmenity(Node.SubType);
                const FLinearColor Colour = bCivic ? ColourCivic : Kind.Colour;
                const TCHAR* Prefix = bCivic ? TEXT("Civic") : Kind.Prefix;

                UProceduralMeshComponent* MeshComp = nullptr;
                const FVector Location(CentroidM.X * MetersToCm, CentroidM.Y * MetersToCm, 0.0);
                const FString Label = FString::Printf(TEXT("%s_%d"), Prefix, NodeId);

                if (!SpawnMeshActor(World, Root, Label, Location, MeshComp))
                {
                    ++Result.Skipped;
                    continue;
                }

                MeshComp->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs,
                    UniformColours(Vertices.Num(), Colour), TArray<FProcMeshTangent>(), false);
                MeshComp->SetMaterial(0, MakeColouredMaterial(MeshComp, Colour));

                Dump.Add(*FString(Prefix).ToLower(), Label, Location, Vertices, Triangles);

                ++Result.Areas;
            }
        }

        if (FellBackToSpanning > 0)
        {
            Result.Problems.Add(FString::Printf(
                TEXT("%d of %d areas could not be triangulated against the terrain; their surfaces "
                     "span the ground rather than following it."),
                FellBackToSpanning, Result.Areas));
        }
    }

    if (Dump.IsEnabled())
    {
        FString DumpError;
        if (Dump.Write(DumpError))
        {
            Result.DumpPath = Options.DebugDumpPath;
        }
        else
        {
            Result.Problems.Add(DumpError);
        }
    }

    Result.DurationSeconds = FPlatformTime::Seconds() - StartTime;
    return Result;
}

// ---------------------------------------------------------------------------
FString FOSMWorldBuilder::FResult::ToString() const
{
    TArray<FString> Lines;

    Lines.Add(FString::Printf(TEXT("Built %s, %d buildings, %d roads, %d areas in %.2f s."),
        Terrain ? TEXT("terrain") : TEXT("no terrain"), Buildings, Roads, Areas, DurationSeconds));

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

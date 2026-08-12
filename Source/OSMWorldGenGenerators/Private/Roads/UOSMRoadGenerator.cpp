// Copyright InviMind. All Rights Reserved.

#include "Roads/UOSMRoadGenerator.h"
#include "Roads/UOSMRoadTypeDataAsset.h"
#include "Roads/FOSMRoadMeshBuilder.h"
#include "Roads/FOSMIntersectionBuilder.h"
#include "Terrain/FOSMDEMSampler.h"
#include "CRS/FOSMCRSTransformer.h"
#include "Components/UOSMMetadataComponent.h"
#include "Components/SplineComponent.h"
#include "ProceduralMeshComponent.h"
#include "OSMWorldGenGenerators.h"
#include "Engine/World.h"
#include "EngineUtils.h"

// ---------------------------------------------------------------------------
const UOSMRoadTypeDataAsset* UOSMRoadGenerator::GetRoadAssetForSubtype(const FString& SubType) const
{
    if (const TObjectPtr<UOSMRoadTypeDataAsset>* Found = RoadTypeOverrides.Find(SubType))
    {
        return *Found;
    }
    if (DefaultRoadType)
    {
        return DefaultRoadType;
    }

    // Dynamic fallback so roads generate out-of-the-box, cached per highway subtype.
    //
    // A single 8 m width for every road was making service alleys and footpaths as wide as
    // arterial roads, and since a residential street's real carriageway is ~5 m, the extra
    // width spilled over neighbouring building footprints — which is why buildings appeared
    // to be sitting on the roads. Widths below are typical carriageway widths, not
    // right-of-way, so they stay inside the gap OSM leaves between a road and its buildings.
    static TMap<FString, UOSMRoadTypeDataAsset*> FallbackAssets;

    if (UOSMRoadTypeDataAsset** Cached = FallbackAssets.Find(SubType))
    {
        return *Cached;
    }

    float WidthMeters = 5.0f;
    if (SubType == TEXT("motorway") || SubType == TEXT("trunk"))                 WidthMeters = 14.0f;
    else if (SubType == TEXT("primary"))                                         WidthMeters = 10.0f;
    else if (SubType == TEXT("secondary"))                                       WidthMeters = 8.0f;
    else if (SubType == TEXT("tertiary"))                                        WidthMeters = 7.0f;
    else if (SubType == TEXT("residential") || SubType == TEXT("unclassified"))  WidthMeters = 5.5f;
    else if (SubType == TEXT("service"))                                         WidthMeters = 3.5f;
    else if (SubType == TEXT("track"))                                           WidthMeters = 3.0f;
    else if (SubType == TEXT("footway") || SubType == TEXT("path")
          || SubType == TEXT("cycleway") || SubType == TEXT("pedestrian")
          || SubType == TEXT("steps"))                                           WidthMeters = 1.8f;

    UOSMRoadTypeDataAsset* Asset = NewObject<UOSMRoadTypeDataAsset>(GetTransientPackage());
    Asset->DefaultWidthMeters = WidthMeters;
    Asset->SurfaceMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
    Asset->AddToRoot(); // transient-package objects are otherwise GC'd between runs

    FallbackAssets.Add(SubType, Asset);
    return Asset;
}

// ---------------------------------------------------------------------------
AActor* UOSMRoadGenerator::CreateRoadNetworkActor(const FOSMGenerationContext& Context)
{
    UWorld* World = Context.TargetWorld.Get();
    if (!World) return nullptr;

    // Re-running road generation should replace the previous network, not stack a second
    // one on top of it. Actor labels aren't unique keys, so match on the label we set below.
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetActorLabel() == TEXT("OSM_RoadNetwork"))
        {
            World->DestroyActor(*It);
        }
    }

    AActor* NetworkActor = World->SpawnActor<AActor>();
    if (!NetworkActor) return nullptr;

    NetworkActor->SetActorLabel(TEXT("OSM_RoadNetwork"));
    
    // Create root component
    USceneComponent* RootComp = NewObject<USceneComponent>(NetworkActor);
    RootComp->SetMobility(EComponentMobility::Static);
    NetworkActor->SetRootComponent(RootComp);
    NetworkActor->AddInstanceComponent(RootComp);
    RootComp->RegisterComponent();

    return NetworkActor;
}

// ---------------------------------------------------------------------------
bool UOSMRoadGenerator::Generate(
    const FOSMGenerationContext& Context,
    const TArray<const FOSMFeature*>& Features,
    TArray<AActor*>& OutActors)
{
    if (!Context.CRSTransformer || !Context.TargetWorld) return false;

    // Fast exit if no highways
    if (Features.Num() == 0) return true;

    AActor* NetworkActor = CreateRoadNetworkActor(Context);
    if (!NetworkActor) return false;
    OutActors.Add(NetworkActor);

    // Initialize ProcMesh component to hold all road geometry
    UProceduralMeshComponent* ProcMesh = NewObject<UProceduralMeshComponent>(NetworkActor);
    ProcMesh->SetMobility(EComponentMobility::Static);
    ProcMesh->SetupAttachment(NetworkActor->GetRootComponent());
    ProcMesh->bUseAsyncCooking = true;
    NetworkActor->AddInstanceComponent(ProcMesh);
    ProcMesh->RegisterComponent();

    // Ground elevation source, shared with the terrain and building generators via the
    // context so all three snap to the same surface. This used to be a default-constructed
    // (never loaded) sampler, which always returned elevation 0 and left roads sitting at
    // Z=0 while the landscape sat at the region's real elevation — ~890 m apart in Bangalore.
    // Unloaded is still a valid state: SampleElevationSafe then returns the supplied default,
    // which is the intended flat-ground fallback when no DEM is available.
    FOSMDEMSampler GroundSampler;
    if (!Context.DEMFilePath.IsEmpty())
    {
        GroundSampler.Load(Context.DEMFilePath);
    }

    int32 SectionIndex = 0;

    // Track endpoints for intersections (NodeID -> array of road legs)
    TMap<int64, TArray<FOSMIntersectionBuilder::FRoadJunctionLeg>> IntersectionMap;

    for (const FOSMFeature* Feature : Features)
    {
        if (Context.bCancelRequested && *Context.bCancelRequested) return false;

        if (Feature->Type != EOSMFeatureType::Highway || Feature->Polyline.Num() < 2)
            continue;

        const UOSMRoadTypeDataAsset* RoadAsset = GetRoadAssetForSubtype(Feature->SubType);
        if (!RoadAsset)
        {
            // Skip if we don't have an asset and no default is set
            continue;
        }

        // Create Spline Component
        USplineComponent* Spline = NewObject<USplineComponent>(NetworkActor);
        Spline->SetMobility(EComponentMobility::Static);
        Spline->SetupAttachment(NetworkActor->GetRootComponent());
        NetworkActor->AddInstanceComponent(Spline);

        // Populate Spline points
        Spline->ClearSplinePoints(false);
        for (int32 i = 0; i < Feature->Polyline.Num(); ++i)
        {
            const FVector& Pt = Feature->Polyline[i];
            // Polyline points are already transformed to UE coords by Stage 3 (FOSMFeature prep)
            // But we should double check if they are WGS84 or UE. 
            // In FOSMTagClassifier they were created as (Lat, Lon, 0).
            // We must transform them here!
            FVector UECoords = Context.CRSTransformer->TransformToUnreal(Pt.X, Pt.Y, 0.0);
            Spline->AddSplinePoint(UECoords, ESplineCoordinateSpace::Local, false);
        }

        // Smooth tangents
        for (int32 i = 0; i < Spline->GetNumberOfSplinePoints(); ++i)
        {
            Spline->SetSplinePointType(i, ESplinePointType::Curve, false);
        }
        Spline->UpdateSpline();

        // Must be registered before BuildRoadMesh: the mesh builder reads
        // GetComponentTransform() to convert cross-section vertices into local space, and
        // an unregistered component has no valid component-to-world transform.
        Spline->RegisterComponent();

        // Snapping
        if (bSnapToTerrain && GroundSampler.IsLoaded())
        {
            FOSMRoadMeshBuilder::SnapSplineToTerrain(Spline, Context.CRSTransformer.Get(), GroundSampler, SnappingOffsetZ);
        }
        else if (bSnapToTerrain)
        {
            // Flat snapping: just set Z to offset
            for (int32 i = 0; i < Spline->GetNumberOfSplinePoints(); ++i)
            {
                FVector Pos = Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
                Pos.Z = SnappingOffsetZ;
                Spline->SetLocationAtSplinePoint(i, Pos, ESplineCoordinateSpace::Local, false);
            }
            Spline->UpdateSpline();
        }

        // Extrude Mesh
        FOSMRoadMeshBuilder::BuildRoadMesh(Spline, ProcMesh, RoadAsset, Feature->Computed.WidthMeters, SectionIndex++);

        // Add Metadata
        UOSMMetadataComponent* MetaComp = NewObject<UOSMMetadataComponent>(NetworkActor);
        MetaComp->FeatureType = Feature->Type;
        MetaComp->SubType = Feature->SubType;
        MetaComp->Tags = Feature->Tags;
        NetworkActor->AddInstanceComponent(MetaComp);

        // Register for intersections
        if (bGenerateIntersections)
        {
            // For real OSM data, we'd use the original Node IDs from the Way.
            // Since FOSMFeature only stores the points, we can use the exact FVector coordinates 
            // as a hash key for intersections, or ideally, preserve the NodeRefs in FOSMFeature.
            // For now, this is a placeholder for intersection registration.
        }

        MetaComp->RegisterComponent();
    }

    // Build Intersections
    if (bGenerateIntersections)
    {
        for (auto& Pair : IntersectionMap)
        {
            const TArray<FOSMIntersectionBuilder::FRoadJunctionLeg>& Legs = Pair.Value;
            if (Legs.Num() >= 3)
            {
                // FVector Center = ... (derive from node ID)
                // FOSMIntersectionBuilder::BuildIntersection(Center, Legs, ProcMesh, SectionIndex++, DefaultRoadType->SurfaceMaterial);
            }
        }
    }

    return true;
}

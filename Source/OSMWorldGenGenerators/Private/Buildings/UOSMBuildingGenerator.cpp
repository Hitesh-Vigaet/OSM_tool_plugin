// Copyright InviMind. All Rights Reserved.

#include "Buildings/UOSMBuildingGenerator.h"
#include "Buildings/FOSMBuildingExtruder.h"
#include "Buildings/FOSMBuildingHeightResolver.h"
#include "Terrain/FOSMDEMSampler.h"
#include "CRS/FOSMCRSTransformer.h"
#include "Components/UOSMMetadataComponent.h"
#include "Model/FOSMFeature.h"
#include "Model/EOSMFeatureType.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/DateTime.h"
#include "Math/RandomStream.h"

namespace
{
    // Recognized by name on whichever material is resolved (user-assigned or the
    // engine grid fallback). Setting a parameter that doesn't exist on the underlying
    // material is a safe no-op (UMaterialInstanceDynamic ignores unknown names).
    const FName GVariedColorParamNames[] = { TEXT("BaseColor"), TEXT("Color"), TEXT("TintColor"), TEXT("Albedo") };
}

// ---------------------------------------------------------------------------
FString UOSMBuildingGenerator::GetTimestampedSavePath() const
{
    if (!CachedSavePath.IsEmpty()) return CachedSavePath;

    const FString Timestamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"));
    CachedSavePath = FString::Printf(TEXT("%s/%s"), *MeshSavePath, *Timestamp);
    return CachedSavePath;
}

// ---------------------------------------------------------------------------
FString UOSMBuildingGenerator::MakeAssetName(int64 FeatureId, int32 FallbackIndex) const
{
    if (FeatureId > 0)
    {
        return FString::Printf(TEXT("Building_%lld"), FeatureId);
    }
    return FString::Printf(TEXT("Building_%04d"), FallbackIndex);
}

// ---------------------------------------------------------------------------
FLinearColor UOSMBuildingGenerator::MakeVariedColor(int64 OSMId, float SatMin, float SatMax, float ValMin, float ValMax)
{
    FRandomStream Stream(static_cast<int32>(GetTypeHash(OSMId)));
    const uint8 Hue8 = static_cast<uint8>(Stream.FRandRange(0.0f, 255.0f));
    const uint8 Sat8 = static_cast<uint8>(FMath::Clamp(Stream.FRandRange(SatMin, SatMax), 0.0f, 1.0f) * 255.0f);
    const uint8 Val8 = static_cast<uint8>(FMath::Clamp(Stream.FRandRange(ValMin, ValMax), 0.0f, 1.0f) * 255.0f);
    return FLinearColor::MakeFromHSV8(Hue8, Sat8, Val8);
}

// ---------------------------------------------------------------------------
bool UOSMBuildingGenerator::Generate(
    const FOSMGenerationContext& Context,
    const TArray<const FOSMFeature*>& Features,
    TArray<AActor*>& OutActors)
{
    if (!Context.CRSTransformer || !Context.TargetWorld) return false;
    if (Features.Num() == 0) return true;

    UWorld* World = Context.TargetWorld.Get();
    const FString SavePath = GetTimestampedSavePath();

    // Ground elevation source, shared with the terrain and road generators via the context so
    // all three snap to the same surface. This used to be a default-constructed (never loaded)
    // sampler, so every building sat at Z=0 while the landscape sat at the region's real
    // elevation — ~890 m apart in Bangalore. Leaving it unloaded is still the correct flat-
    // ground fallback when no DEM is available.
    FOSMDEMSampler GroundSampler;
    if (!Context.DEMFilePath.IsEmpty())
    {
        GroundSampler.Load(Context.DEMFilePath);
    }

    int32 MeshIndex = 0;

    for (const FOSMFeature* Feature : Features)
    {
        if (Context.bCancelRequested && *Context.bCancelRequested) return false;

        if (Feature->Type != EOSMFeatureType::Building || !Feature->HasPolygon())
            continue;

        // ── 1. Resolve Height ──────────────────────────────────────────────
        const FOSMBuildingHeightResolver::FResolutionResult HeightResult =
            FOSMBuildingHeightResolver::Resolve(*Feature, DefaultFloorHeight, DefaultBuildingHeight);
        const float HeightCm = HeightResult.HeightMeters * 100.0f;

        // ── 2. Convert Footprint to UE 2D coords ───────────────────────────
        // GetOuterRing() returns Polygons[0] — the main footprint
        // Points are stored as (Lat, Lon, 0) and get CRS-transformed below
        const TArray<FVector>& OuterLatLon = Feature->GetOuterRing();
        TArray<FVector2D> OuterRing;
        OuterRing.Reserve(OuterLatLon.Num());
        float GroundZ = 0.0f;
        bool bGroundZSet = false;

        for (const FVector& LatLon : OuterLatLon)
        {
            FVector UEPos = Context.CRSTransformer->TransformToUnreal(LatLon.X, LatLon.Y, 0.0);
            OuterRing.Add(FVector2D(UEPos.X, UEPos.Y));

            // ── 3. Terrain Snapping (best-effort via DEM sampler) ──────────
            if (!bGroundZSet)
            {
                const double ElevM = GroundSampler.IsLoaded()
                    ? GroundSampler.SampleElevationSafe(LatLon.X, LatLon.Y, 0.0)
                    : 0.0;
                GroundZ = static_cast<float>(ElevM * 100.0);
                bGroundZSet = true;
            }
        }

        if (OuterRing.Num() < 3) continue;

        // ── 4. Extrude into DynamicMesh ───────────────────────────────────
        FOSMBuildingExtruder::FExtrudeInput ExtrudeInput;
        ExtrudeInput.OuterRing = OuterRing;
        // Inner rings would come from relation "inner" members (Phase 4 v1.1)
        ExtrudeInput.HeightCm = HeightCm;
        ExtrudeInput.GroundZ = GroundZ;
        ExtrudeInput.WallMaterialSlot = 0;
        ExtrudeInput.RoofMaterialSlot = 1;

        FOSMBuildingExtruder::FExtrudeResult ExtrudeResult =
            FOSMBuildingExtruder::Extrude(ExtrudeInput, GetTransientPackage());

        if (!ExtrudeResult.IsValid()) continue;

#if WITH_EDITOR
        // ── 5. Bake to UStaticMesh ─────────────────────────────────────────
        const FString AssetPath = FString::Printf(TEXT("%s/%s"),
            *SavePath, *MakeAssetName(Feature->OSMId, MeshIndex));

        UStaticMesh* BakedMesh = FOSMBuildingExtruder::BakeToStaticMesh(
            ExtrudeResult.DynamicMesh,
            AssetPath,
            DefaultWallMaterial,
            DefaultRoofMaterial,
            bGenerateLODs ? LODDistances : TArray<float>());

        if (!BakedMesh) continue;

        // ── 6. Spawn Actor ─────────────────────────────────────────────────
        // Actor names are derived purely from the OSM ID (no per-run suffix), so
        // re-running generation on a level that already has this building would collide
        // with the previous run's actor of the same name. Destroy any such stale actor
        // first so re-running replaces it (matches the "independently re-runnable stage"
        // workflow in plan_v2_workflow.md §12) instead of colliding.
        const FName DesiredName(*MakeAssetName(Feature->OSMId, MeshIndex));
        if (AActor* StaleActor = FindObject<AActor>(World->PersistentLevel, *DesiredName.ToString()))
        {
            World->DestroyActor(StaleActor);
        }

        FActorSpawnParameters SpawnParams;
        SpawnParams.Name = DesiredName;
        // Defensive backstop: if the name is still taken for any reason (e.g. duplicate
        // OSM IDs within the same import), auto-generate a unique variant instead of the
        // engine's default behavior of a fatal crash (UWorld::SpawnActor, Required_Fatal).
        SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;

        AStaticMeshActor* BuildingActor = World->SpawnActor<AStaticMeshActor>(
            AStaticMeshActor::StaticClass(),
            FTransform(FRotator::ZeroRotator, ExtrudeResult.Centroid),
            SpawnParams);

        UMaterialInterface* WallMat = DefaultWallMaterial;
        if (!WallMat)
        {
            WallMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
        }

        UMaterialInterface* RoofMat = DefaultRoofMaterial;
        if (!RoofMat)
        {
            RoofMat = WallMat;
        }

        // Procedural fallback material variation (plan_v2_workflow.md §7.4): give each
        // building a per-node dynamic material instance with a deterministic color, so a
        // whole region of fallback buildings doesn't render as one flat, uniform material.
        if (bVaryFallbackMaterialColor)
        {
            if (UMaterialInstanceDynamic* WallMID = UMaterialInstanceDynamic::Create(WallMat, BuildingActor))
            {
                const FLinearColor WallColor = MakeVariedColor(Feature->OSMId, 0.15f, 0.35f, 0.55f, 0.85f);
                for (const FName& ParamName : GVariedColorParamNames)
                {
                    WallMID->SetVectorParameterValue(ParamName, WallColor);
                }
                WallMat = WallMID;
            }

            if (UMaterialInstanceDynamic* RoofMID = UMaterialInstanceDynamic::Create(RoofMat, BuildingActor))
            {
                // Same seed as the wall so a building's roof/wall pairing stays deterministic
                // and related, but darker/less saturated to read as a distinct roof surface.
                const FLinearColor RoofColor = MakeVariedColor(Feature->OSMId, 0.05f, 0.2f, 0.3f, 0.5f);
                for (const FName& ParamName : GVariedColorParamNames)
                {
                    RoofMID->SetVectorParameterValue(ParamName, RoofColor);
                }
                RoofMat = RoofMID;
            }
        }

        BuildingActor->SetActorLabel(MakeAssetName(Feature->OSMId, MeshIndex));
        BuildingActor->GetStaticMeshComponent()->SetStaticMesh(BakedMesh);
        BuildingActor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
        BuildingActor->GetStaticMeshComponent()->SetMaterial(0, WallMat);
        BuildingActor->GetStaticMeshComponent()->SetMaterial(1, RoofMat);

        // ── 7. Attach Metadata ─────────────────────────────────────────────
        UOSMMetadataComponent* MetaComp = NewObject<UOSMMetadataComponent>(BuildingActor);
        MetaComp->FeatureType = Feature->Type;
        MetaComp->SubType = Feature->SubType;
        MetaComp->Tags = Feature->Tags;
        BuildingActor->AddInstanceComponent(MetaComp);
        MetaComp->RegisterComponent();

        OutActors.Add(BuildingActor);
        ++MeshIndex;
#endif
    }

    return true;
}

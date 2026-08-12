// Copyright InviMind. All Rights Reserved.

#include "Terrain/UOSMTerrainGenerator.h"
#include "Terrain/FOSMDEMSampler.h"
#include "Terrain/FOSMHeightmapBuilder.h"
#include "CRS/FOSMCRSTransformer.h"
#include "CRS/FOSMEllipsoid.h"
#include "OSMWorldGenGenerators.h"
#include "EngineUtils.h"

// UE Editor Landscape headers — editor-only module
#if WITH_EDITOR
#include "LandscapeEditorUtils.h"
#include "LandscapeEdit.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#endif

// ---------------------------------------------------------------------------
bool UOSMTerrainGenerator::HasDEM() const
{
    return DEMSampler.IsValid() && DEMSampler->IsLoaded();
}

// ---------------------------------------------------------------------------
double UOSMTerrainGenerator::SampleTerrainElevation(double Latitude, double Longitude) const
{
    if (!HasDEM()) return 0.0;
    return DEMSampler->SampleElevationSafe(Latitude, Longitude, 0.0);
}

// ---------------------------------------------------------------------------
bool UOSMTerrainGenerator::Generate(
    const FOSMGenerationContext& Context,
    const TArray<const FOSMFeature*>& Features,
    TArray<AActor*>& OutActors)
{
#if !WITH_EDITOR
    UE_LOG(LogOSMWorldGenGenerators, Warning, TEXT("TerrainGenerator: Landscape creation requires the editor. Skipping."));
    return true;
#else

    if (!Context.CRSTransformer || !Context.CRSTransformer->IsInitialized())
    {
        UE_LOG(LogOSMWorldGenGenerators, Error, TEXT("TerrainGenerator: CRS Transformer is not initialized!"));
        return false;
    }

    if (!Context.TargetWorld)
    {
        UE_LOG(LogOSMWorldGenGenerators, Error, TEXT("TerrainGenerator: No target world!"));
        return false;
    }

    // ---- 1. Determine geographic bounds ----
    // Order matters: the requested region wins over whatever the source file happened to
    // contain. Feature-table bounds are only a fallback, because a single oversized OSM
    // relation in the file would otherwise size the Landscape to match it.
    double MinLat, MaxLat, MinLon, MaxLon;

    if (Context.bHasTargetBounds)
    {
        MinLat = Context.TargetMinLat;
        MaxLat = Context.TargetMaxLat;
        MinLon = Context.TargetMinLon;
        MaxLon = Context.TargetMaxLon;
    }
    else if (Context.FeatureTable && !Context.FeatureTable->IsEmpty())
    {
        MinLat = Context.FeatureTable->BoundsMinLatLon.X;
        MinLon = Context.FeatureTable->BoundsMinLatLon.Y;
        MaxLat = Context.FeatureTable->BoundsMaxLatLon.X;
        MaxLon = Context.FeatureTable->BoundsMaxLatLon.Y;
    }
    else
    {
        // Fallback: ±0.05° square around CRS origin (~11km)
        const FOSMGeoOrigin& Origin = Context.CRSTransformer->GetOrigin();
        MinLat = Origin.Latitude  - 0.05;
        MaxLat = Origin.Latitude  + 0.05;
        MinLon = Origin.Longitude - 0.05;
        MaxLon = Origin.Longitude + 0.05;
    }

    // ---- 1b. Hard safety clamp ----
    // A Landscape spanning hundreds of km exceeds the renderer's float precision budget and
    // trips "Found precision loss while converting matrix to GPU format" (DoubleFloat.cpp)
    // during distance-field/reflection-capture updates, taking the editor down. Whatever
    // upstream bug produced such bounds, refuse to hand them to the engine: clamp around the
    // centre and say so loudly rather than crashing.
    {
        const double MidLat = (MinLat + MaxLat) * 0.5;
        const double MidLon = (MinLon + MaxLon) * 0.5;
        const double CosMidLat = FMath::Max(0.01, FMath::Cos(FMath::DegreesToRadians(MidLat)));

        const double LatKm = (MaxLat - MinLat) * 111.32;
        const double LonKm = (MaxLon - MinLon) * 111.32 * CosMidLat;

        if (LatKm > MaxTerrainExtentKm || LonKm > MaxTerrainExtentKm)
        {
            const double HalfLatDeg = (MaxTerrainExtentKm * 0.5) / 111.32;
            const double HalfLonDeg = (MaxTerrainExtentKm * 0.5) / (111.32 * CosMidLat);

            UE_LOG(LogOSMWorldGenGenerators, Error,
                TEXT("TerrainGenerator: requested terrain is %.1f x %.1f km, which exceeds the %.1f km safety limit ")
                TEXT("(a Landscape this large breaks renderer float precision). Clamping to %.1f km around ")
                TEXT("lat %.6f lon %.6f. This usually means the source OSM data covers a far larger area than intended."),
                LatKm, LonKm, MaxTerrainExtentKm, MaxTerrainExtentKm, MidLat, MidLon);

            Context.LogWarning(EOSMImportWarningLevel::Warning, 0,
                FText::Format(NSLOCTEXT("OSM", "TerrainExtentClamped",
                    "Terrain extent ({0} x {1} km) exceeded the safety limit and was clamped to {2} km."),
                    FText::AsNumber(LatKm), FText::AsNumber(LonKm), FText::AsNumber(MaxTerrainExtentKm)));

            MinLat = MidLat - HalfLatDeg;  MaxLat = MidLat + HalfLatDeg;
            MinLon = MidLon - HalfLonDeg;  MaxLon = MidLon + HalfLonDeg;
        }
    }

    // ---- 2. Load DEM ----
    DEMSampler = MakeShared<FOSMDEMSampler>();

    if (!TerrainSettings.DEMFilePath.IsEmpty())
    {
        if (Context.bCancelRequested && *Context.bCancelRequested) return false;

        if (!DEMSampler->Load(TerrainSettings.DEMFilePath))
        {
            Context.LogWarning(EOSMImportWarningLevel::Warning, 0,
                FText::Format(NSLOCTEXT("OSM", "DEMLoadFail", "Failed to load DEM '{0}'. Generating flat terrain."),
                FText::FromString(TerrainSettings.DEMFilePath)));
        }
    }
    else
    {
        UE_LOG(LogOSMWorldGenGenerators, Log, TEXT("TerrainGenerator: No DEM specified. Generating flat terrain."));
    }

    // ---- 3. Auto-compute component count if requested ----
    FOSMTerrainSettings Settings = TerrainSettings;
    if (bAutoComputeComponentCount)
    {
        // Approximate extent in km using equirectangular approximation
        const double MidLat = (MinLat + MaxLat) * 0.5;
        const double DeltaLatKm  = (MaxLat - MinLat) * 111.32;
        const double DeltaLonKm  = (MaxLon - MinLon) * 111.32 * FMath::Cos(FMath::DegreesToRadians(MidLat));
        const double MaxExtentKm = FMath::Max(DeltaLatKm, DeltaLonKm);

        Settings.ComponentCount = FOSMHeightmapBuilder::AutoComputeComponentCount(MaxExtentKm, Settings);
        UE_LOG(LogOSMWorldGenGenerators, Log, TEXT("TerrainGenerator: Auto component count = %d (extent %.1f km)"),
            Settings.ComponentCount, MaxExtentKm);
    }

    // ---- 4. Build uint16 heightmap buffer ----
    FOSMHeightmapResult Heightmap;
    const bool bBuilt = FOSMHeightmapBuilder::Build(
        *DEMSampler, Settings,
        MinLat, MaxLat, MinLon, MaxLon,
        Heightmap,
        [&](float P, const FText& Status)
        {
            UE_LOG(LogOSMWorldGenGenerators, Verbose, TEXT("TerrainGen [%.0f%%]: %s"), P * 100.0f, *Status.ToString());
        },
        Context.bCancelRequested);

    if (!bBuilt || !Heightmap.IsValid())
    {
        UE_LOG(LogOSMWorldGenGenerators, Error, TEXT("TerrainGenerator: Heightmap build failed!"));
        return false;
    }

    if (Context.bCancelRequested && *Context.bCancelRequested) return false;

    // ---- 5. Create Landscape actor ----
    AActor* LandscapeActor = CreateLandscapeActor(Context, Heightmap, MinLat, MaxLat, MinLon, MaxLon);
    if (LandscapeActor)
    {
        OutActors.Add(LandscapeActor);
        UE_LOG(LogOSMWorldGenGenerators, Log, TEXT("TerrainGenerator: Landscape created successfully."));
    }
    else
    {
        UE_LOG(LogOSMWorldGenGenerators, Error, TEXT("TerrainGenerator: Failed to create Landscape actor!"));
        return false;
    }

    return true;
#endif // WITH_EDITOR
}

// ---------------------------------------------------------------------------
#if WITH_EDITOR
AActor* UOSMTerrainGenerator::CreateLandscapeActor(
    const FOSMGenerationContext& Context,
    const FOSMHeightmapResult& Heightmap,
    double MinLat, double MaxLat,
    double MinLon, double MaxLon)
{
    UWorld* World = Context.TargetWorld.Get();
    if (!World) return nullptr;

    // Re-running terrain generation should replace the previous landscape rather than
    // leaving a stale one behind at the old location.
    for (TActorIterator<ALandscape> It(World); It; ++It)
    {
        if (It->GetActorLabel() == TEXT("OSM_Landscape"))
        {
            World->DestroyActor(*It);
        }
    }

    const int32 HMSize  = Heightmap.Size;
    const int32 SecSize = TerrainSettings.SectionSize;
    const int32 SecPerC = TerrainSettings.SectionsPerComponent;
    const int32 CompCnt = TerrainSettings.ComponentCount;

    // ---- Compute world-space position and XY scale ----

    // South-West corner of the terrain in UE world space
    const FVector SWCorner = Context.CRSTransformer->TransformToUnreal(MinLat, MinLon, 0.0);
    const FVector NECorner = Context.CRSTransformer->TransformToUnreal(MaxLat, MaxLon, 0.0);

    // Width and height of the terrain in UE cm
    const double WidthCm  = FMath::Abs(NECorner.X - SWCorner.X);
    const double HeightCm = FMath::Abs(NECorner.Y - SWCorner.Y);

    // XY scale: cm per quad along X (East) and Y (North)
    const int32  NumQuads = HMSize - 1;
    const float  XScaleCm = static_cast<float>(WidthCm  / static_cast<double>(FMath::Max(1, NumQuads)));
    const float  YScaleCm = static_cast<float>(HeightCm / static_cast<double>(FMath::Max(1, NumQuads)));

    // Z offset: position at MinElevationMeters, then ZScale spans the range
    const FVector LandscapeLocation(
        SWCorner.X,
        SWCorner.Y,
        static_cast<double>(Heightmap.MinElevationMeters) * FOSMEllipsoid::MetersToUE
    );

    const FVector LandscapeScale(XScaleCm, YScaleCm, Heightmap.LandscapeZScaleCm);

    // Diagnostic: terrain placement depends on the feature-table bounds, the CRS origin,
    // and the derived scale all agreeing. Log them so a misplaced Landscape can be traced
    // to whichever of the three is wrong without needing a debugger attached.
    UE_LOG(LogOSMWorldGenGenerators, Log,
        TEXT("TerrainGen placement: bounds lat[%.6f..%.6f] lon[%.6f..%.6f] | SW=(%.1f, %.1f) NE=(%.1f, %.1f) cm | extent=(%.1f x %.1f) cm | HMSize=%d | scale=(%.3f, %.3f, %.3f)"),
        MinLat, MaxLat, MinLon, MaxLon,
        SWCorner.X, SWCorner.Y, NECorner.X, NECorner.Y,
        WidthCm, HeightCm, HMSize,
        LandscapeScale.X, LandscapeScale.Y, LandscapeScale.Z);

    // ---- Build layer list (single default heightmap layer keyed by FGuid()) ----
    TMap<FGuid, TArray<uint16>> HeightDataPerLayer;
    const FGuid BaseLayerGuid = FGuid(); // UE5 internal height layer key is FGuid() (0,0,0,0)
    HeightDataPerLayer.Add(BaseLayerGuid, Heightmap.Data);

    TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayer;
    MaterialLayerDataPerLayer.Add(BaseLayerGuid, TArray<FLandscapeImportLayerInfo>());

    // ---- Spawn Landscape via editor utility ----
    ALandscape* Landscape = World->SpawnActor<ALandscape>(LandscapeLocation, FRotator::ZeroRotator);
    if (!Landscape)
    {
        UE_LOG(LogOSMWorldGenGenerators, Error, TEXT("TerrainGenerator: SpawnActor<ALandscape> failed!"));
        return nullptr;
    }

    Landscape->SetActorScale3D(LandscapeScale);
    Landscape->SetActorLabel(TEXT("OSM_Landscape"));

    // Import heightmap into Landscape (Param 1 = valid Actor Landscape Guid, Map Keys = BaseLayerGuid)
    const FGuid LandscapeGuid = FGuid::NewGuid();
    Landscape->Import(
        LandscapeGuid,
        0, 0,                                    // X/Y offsets (sections from origin)
        HMSize - 1, HMSize - 1,                  // W/H in vertices (= quads count)
        SecPerC,
        SecSize,
        HeightDataPerLayer,
        nullptr,                                 // visibility mask (null = all visible)
        MaterialLayerDataPerLayer,
        ELandscapeImportAlphamapType::Additive,
        TArrayView<const FLandscapeLayer>());

    ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
    if (LandscapeInfo)
    {
        LandscapeInfo->UpdateLayerInfoMap(Landscape);
    }

    Landscape->RecreateCollisionComponents();
    Landscape->PostEditChange();

    return Landscape;
}
#endif // WITH_EDITOR

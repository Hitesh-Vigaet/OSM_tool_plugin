// Copyright InviMind. All Rights Reserved.

#include "Terrain/FOSMHeightmapBuilder.h"
#include "Terrain/FOSMGeoidCorrection.h"
#include "OSMWorldGenGenerators.h"

// ---------------------------------------------------------------------------
int32 FOSMHeightmapBuilder::AutoComputeComponentCount(double AreaExtentKm, const FOSMTerrainSettings& Settings)
{
    // We want each heightmap pixel to cover roughly 30m (SRTM 1-arcsec ≈ 30m)
    const double TargetPixelSizeMeters = 30.0;
    const double AreaExtentMeters = AreaExtentKm * 1000.0;
    const int32 QuadsPerComponent = (Settings.SectionSize - 1) * Settings.SectionsPerComponent;

    // Total quads needed = area / target pixel size
    const double TotalQuads = AreaExtentMeters / TargetPixelSizeMeters;
    const int32 Components = FMath::CeilToInt(TotalQuads / QuadsPerComponent);
    return FMath::Clamp(Components, 1, 32);
}

// ---------------------------------------------------------------------------
bool FOSMHeightmapBuilder::Build(
    const FOSMDEMSampler& Sampler,
    const FOSMTerrainSettings& Settings,
    double MinLat, double MaxLat,
    double MinLon, double MaxLon,
    FOSMHeightmapResult& OutResult,
    TFunction<void(float Percent, const FText& Status)> OnProgress,
    FThreadSafeBool* bCancel)
{
    const int32 HMSize = Settings.ComputeHeightmapSize();

    OutResult.Size     = HMSize;
    OutResult.bFromDEM = Sampler.IsLoaded();
    OutResult.Data.SetNumUninitialized(HMSize * HMSize);

    // ---- Elevation range for uint16 quantization ----
    float MinElev, MaxElev;

    if (Sampler.IsLoaded())
    {
        // Expand min/max by 5% headroom to avoid clipping at extreme elevations
        const float Range    = Sampler.GetMaxElevation() - Sampler.GetMinElevation();
        const float Headroom = FMath::Max(Range * 0.05f, 10.0f);
        MinElev = Sampler.GetMinElevation() - Headroom;
        MaxElev = Sampler.GetMaxElevation() + Headroom;
    }
    else
    {
        MinElev = Settings.FlatElevationMeters - 1.0f;
        MaxElev = Settings.FlatElevationMeters + Settings.ElevationRangeMeters;
    }

    // Apply vertical scale
    MinElev *= Settings.VerticalScale;
    MaxElev *= Settings.VerticalScale;

    OutResult.MinElevationMeters = MinElev;
    OutResult.MaxElevationMeters = MaxElev;

    // UE Landscape Z-scale:  ZScale in cm/uint16  → stored as LandscapeZScaleCm
    // UE maps uint16 0→0 height, 32768→0m offset, 65535→max height internally.
    // The formula the Editor uses: ZScale = ElevRange(cm) / 512.0
    const float ElevRangeCm = (MaxElev - MinElev) * 100.0f;
    OutResult.LandscapeZScaleCm = ElevRangeCm / 512.0f;

    if (OnProgress)
    {
        OnProgress(0.0f, NSLOCTEXT("OSM", "BuildingHeightmap", "Building heightmap..."));
    }

    if (!Sampler.IsLoaded())
    {
        // Flat terrain: map FlatElevationMeters to mid-uint16 range
        const float NormFlat = (Settings.FlatElevationMeters * Settings.VerticalScale - MinElev) / (MaxElev - MinElev);
        const uint16 FlatValue = static_cast<uint16>(FMath::Clamp(NormFlat, 0.0f, 1.0f) * 65535.0f);
        for (int32 i = 0; i < HMSize * HMSize; ++i)
        {
            OutResult.Data[i] = FlatValue;
        }

        if (OnProgress) OnProgress(1.0f, NSLOCTEXT("OSM", "FlatTerrainDone", "Flat terrain generated."));
        UE_LOG(LogOSMWorldGenGenerators, Log, TEXT("Heightmap: flat %d×%d at %.1fm"), HMSize, HMSize, Settings.FlatElevationMeters);
        return true;
    }

    // ---- DEM resampling ----
    const double LatStep = (MaxLat - MinLat) / static_cast<double>(HMSize - 1);
    const double LonStep = (MaxLon - MinLon) / static_cast<double>(HMSize - 1);
    const float  ElevRange = MaxElev - MinElev;

    for (int32 Row = 0; Row < HMSize; ++Row)
    {
        if (bCancel && *bCancel) return false;

        if (OnProgress && Row % 64 == 0)
        {
            OnProgress(static_cast<float>(Row) / static_cast<float>(HMSize),
                FText::Format(NSLOCTEXT("OSM", "BuildingHMRow", "Sampling DEM row {0} of {1}..."), Row + 1, HMSize));
        }

        // Row corresponds to UE X (Longitude: MinLon -> MaxLon)
        const double Lon = MinLon + static_cast<double>(Row) * LonStep;

        for (int32 Col = 0; Col < HMSize; ++Col)
        {
            // Col corresponds to UE Y (Latitude: MinLat -> MaxLat)
            const double Lat = MinLat + static_cast<double>(Col) * LatStep;

            double RawElev = Sampler.SampleElevationSafe(Lat, Lon, static_cast<double>(Settings.FlatElevationMeters));

            if (Settings.bApplyGeoidCorrection)
            {
                RawElev += FOSMGeoidCorrection::GetGeoidSeparation(Lat, Lon);
            }

            RawElev *= Settings.VerticalScale;

            // Normalise to [0, 65535]
            const float Normalized = FMath::Clamp(
                static_cast<float>((RawElev - MinElev) / ElevRange),
                0.0f, 1.0f);

            OutResult.Data[Row * HMSize + Col] = static_cast<uint16>(Normalized * 65535.0f);
        }
    }

    if (OnProgress) OnProgress(1.0f, NSLOCTEXT("OSM", "HeightmapDone", "Heightmap build complete."));

    UE_LOG(LogOSMWorldGenGenerators, Log,
        TEXT("Heightmap: %d×%d from DEM. ElevRange [%.1f, %.1f]m. ZScale=%.4f cm."),
        HMSize, HMSize, MinElev, MaxElev, OutResult.LandscapeZScaleCm);

    return true;
}

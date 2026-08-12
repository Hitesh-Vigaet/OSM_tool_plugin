// Copyright InviMind. All Rights Reserved.

#include "Terrain/FOSMDEMSampler.h"
#include "Terrain/FOSMGeoTIFFReader.h"
#include "OSMWorldGenGenerators.h"

// ---------------------------------------------------------------------------
bool FOSMDEMSampler::Load(const FString& FilePath)
{
    Reset();

    if (!FOSMGeoTIFFReader::Load(FilePath, Tile, HeightData))
    {
        UE_LOG(LogOSMWorldGenGenerators, Error, TEXT("DEMSampler: failed to load '%s'"), *FilePath);
        return false;
    }

    bIsLoaded = (Tile.Width > 0 && Tile.Height > 0 && HeightData.Num() == Tile.Width * Tile.Height);

    if (bIsLoaded)
    {
        UE_LOG(LogOSMWorldGenGenerators, Log,
            TEXT("DEMSampler: loaded %d×%d DEM. Bounds Lat[%.4f, %.4f] Lon[%.4f, %.4f]. Elev [%.1f, %.1f]m"),
            Tile.Width, Tile.Height,
            Tile.GetMinLat(), Tile.GetMaxLat(),
            Tile.GetMinLon(), Tile.GetMaxLon(),
            Tile.MinElevation, Tile.MaxElevation);
    }

    return bIsLoaded;
}

// ---------------------------------------------------------------------------
void FOSMDEMSampler::Reset()
{
    bIsLoaded = false;
    HeightData.Empty();
    Tile = FOSMGeoTIFFTile{};
}

// ---------------------------------------------------------------------------
float FOSMDEMSampler::GetPixelValue(int32 Col, int32 Row) const
{
    if (Col < 0 || Col >= Tile.Width || Row < 0 || Row >= Tile.Height)
    {
        return static_cast<float>(Tile.NoDataValue);
    }
    return HeightData[Row * Tile.Width + Col];
}

// ---------------------------------------------------------------------------
bool FOSMDEMSampler::IsNoData(float Val) const
{
    if (!Tile.bHasNoData) return false;
    return FMath::Abs(static_cast<double>(Val) - Tile.NoDataValue) < 0.5;
}

// ---------------------------------------------------------------------------
bool FOSMDEMSampler::ContainsCoordinate(double Latitude, double Longitude) const
{
    return bIsLoaded && Tile.ContainsCoordinate(Latitude, Longitude);
}

// ---------------------------------------------------------------------------
double FOSMDEMSampler::GetResolutionArcSeconds() const
{
    return bIsLoaded ? Tile.GetResolutionArcSeconds() : 0.0;
}

// ---------------------------------------------------------------------------
double FOSMDEMSampler::SampleElevation(double Latitude, double Longitude) const
{
    if (!bIsLoaded)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // Convert lat/lon to floating-point pixel coordinates
    const double FCol = Tile.LonToPixelCol(Longitude);
    const double FRow = Tile.LatToPixelRow(Latitude);

    // If outside raster extent, return NaN
    if (FCol < 0.0 || FCol > static_cast<double>(Tile.Width  - 1) ||
        FRow < 0.0 || FRow > static_cast<double>(Tile.Height - 1))
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // Four surrounding pixels for bilinear interpolation
    const int32 C0 = static_cast<int32>(FMath::FloorToDouble(FCol));
    const int32 R0 = static_cast<int32>(FMath::FloorToDouble(FRow));
    const int32 C1 = FMath::Min(C0 + 1, Tile.Width  - 1);
    const int32 R1 = FMath::Min(R0 + 1, Tile.Height - 1);

    const double tC = FCol - static_cast<double>(C0); // fractional col [0,1]
    const double tR = FRow - static_cast<double>(R0); // fractional row [0,1]

    // Sample the 2×2 neighbourhood
    const float V00 = GetPixelValue(C0, R0);
    const float V10 = GetPixelValue(C1, R0);
    const float V01 = GetPixelValue(C0, R1);
    const float V11 = GetPixelValue(C1, R1);

    // For bilinear interp, replace NoData samples with the average of valid neighbours
    // (simple fallback — avoids NaN spreading along coastlines / voids)
    float ValidSum = 0.0f;
    int32 ValidCount = 0;
    auto Accumulate = [&](float V) { if (!IsNoData(V)) { ValidSum += V; ++ValidCount; } };
    Accumulate(V00); Accumulate(V10); Accumulate(V01); Accumulate(V11);

    if (ValidCount == 0)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const float FillValue = ValidSum / static_cast<float>(ValidCount);
    const float S00 = IsNoData(V00) ? FillValue : V00;
    const float S10 = IsNoData(V10) ? FillValue : V10;
    const float S01 = IsNoData(V01) ? FillValue : V01;
    const float S11 = IsNoData(V11) ? FillValue : V11;

    // Standard bilinear: blend along col first, then row
    const double Top    = FMath::Lerp(static_cast<double>(S00), static_cast<double>(S10), tC);
    const double Bottom = FMath::Lerp(static_cast<double>(S01), static_cast<double>(S11), tC);
    return FMath::Lerp(Top, Bottom, tR);
}

// ---------------------------------------------------------------------------
double FOSMDEMSampler::SampleElevationSafe(double Latitude, double Longitude, double DefaultValue) const
{
    const double Val = SampleElevation(Latitude, Longitude);
    return FMath::IsNaN(Val) ? DefaultValue : Val;
}

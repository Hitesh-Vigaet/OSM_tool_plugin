// Copyright InviMind. All Rights Reserved.

#include "Elevation/FOSMDEMCRSAlignment.h"

// ---------------------------------------------------------------------------
FVector FOSMDEMCRSAlignment::DEMPixelToUnreal(
    const FOSMGeoTIFFTile& Tile,
    const UOSMCRSTransformer& Transformer,
    int32 Col, int32 Row,
    double ElevMeters)
{
    // 1. Pixel (col, row) → WGS84 Lat/Lon
    const double Lon = Tile.PixelToLon(static_cast<double>(Col));
    const double Lat = Tile.PixelToLat(static_cast<double>(Row));

    // 2. WGS84 Lat/Lon/Elev → UE World Space (cm)
    return Transformer.TransformToUnreal(Lat, Lon, ElevMeters);
}

// ---------------------------------------------------------------------------
float FOSMDEMCRSAlignment::ComputeDEMCoverage(
    const FOSMGeoTIFFTile& Tile,
    double MinLat, double MaxLat,
    double MinLon, double MaxLon)
{
    // Compute intersection rectangle
    const double InterMinLat = FMath::Max(MinLat, Tile.GetMinLat());
    const double InterMaxLat = FMath::Min(MaxLat, Tile.GetMaxLat());
    const double InterMinLon = FMath::Max(MinLon, Tile.GetMinLon());
    const double InterMaxLon = FMath::Min(MaxLon, Tile.GetMaxLon());

    if (InterMinLat >= InterMaxLat || InterMinLon >= InterMaxLon)
    {
        return 0.0f; // No overlap
    }

    const double ImportArea = (MaxLat - MinLat) * (MaxLon - MinLon);
    if (ImportArea <= 0.0) return 1.0f;

    const double InterArea = (InterMaxLat - InterMinLat) * (InterMaxLon - InterMinLon);
    return static_cast<float>(InterArea / ImportArea);
}

// ---------------------------------------------------------------------------
double FOSMDEMCRSAlignment::MeasureAlignmentError(
    const FOSMGeoTIFFTile& Tile,
    const UOSMCRSTransformer& Transformer,
    int32 TestCol, int32 TestRow)
{
    // Original lat/lon
    const double TrueLon = Tile.PixelToLon(static_cast<double>(TestCol));
    const double TrueLat = Tile.PixelToLat(static_cast<double>(TestRow));

    // Transform to UE
    const FVector UEPos = DEMPixelToUnreal(Tile, Transformer, TestCol, TestRow, 100.0);

    // Transform back
    double OutLat, OutLon, OutElev;
    Transformer.TransformToWGS84(UEPos, OutLat, OutLon, OutElev);

    // Error in arc-seconds
    const double ErrorLatArcSec = FMath::Abs(OutLat - TrueLat) * 3600.0;
    const double ErrorLonArcSec = FMath::Abs(OutLon - TrueLon) * 3600.0;

    return FMath::Max(ErrorLatArcSec, ErrorLonArcSec);
}

// ---------------------------------------------------------------------------
FVector2D FOSMDEMCRSAlignment::GetDEMPixelSizeCm(
    const FOSMGeoTIFFTile& Tile,
    const UOSMCRSTransformer& Transformer)
{
    // Take centre pixel and pixel next to it
    const int32 CenterC = Tile.Width / 2;
    const int32 CenterR = Tile.Height / 2;

    const FVector C0 = DEMPixelToUnreal(Tile, Transformer, CenterC, CenterR, 0.0);
    const FVector C1X = DEMPixelToUnreal(Tile, Transformer, CenterC + 1, CenterR, 0.0);
    const FVector C1Y = DEMPixelToUnreal(Tile, Transformer, CenterC, CenterR + 1, 0.0);

    return FVector2D(
        FVector::Dist(C0, C1X), // Width of 1 pixel in cm
        FVector::Dist(C0, C1Y)  // Height of 1 pixel in cm
    );
}

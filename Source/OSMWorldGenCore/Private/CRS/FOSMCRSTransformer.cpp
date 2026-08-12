// Copyright InviMind. All Rights Reserved.

#include "CRS/FOSMCRSTransformer.h"
#include "CRS/FOSMEllipsoid.h"
#include "OSMWorldGenCore.h"

UOSMCRSTransformer::UOSMCRSTransformer()
{
}

void UOSMCRSTransformer::Initialize(const FOSMGeoOrigin& Origin, EOSMProjectionMode Mode)
{
    CachedOrigin = Origin;
    ProjectionMode = Mode;

    if (ProjectionMode == EOSMProjectionMode::ENU)
    {
        PrecomputeENU();
    }
    bIsInitialized = true;

    UE_LOG(LogOSMWorldGen, Log, TEXT("CRS Transformer initialized at Lat: %.6f, Lon: %.6f, Height: %.1fm"),
        CachedOrigin.Latitude, CachedOrigin.Longitude, CachedOrigin.HeightMeters);
}

void UOSMCRSTransformer::PrecomputeENU()
{
    ECEFOrigin = FOSMEllipsoid::GeodeticToECEF(CachedOrigin.Latitude, CachedOrigin.Longitude, CachedOrigin.HeightMeters);

    const double LatRad = FMath::DegreesToRadians(CachedOrigin.Latitude);
    const double LonRad = FMath::DegreesToRadians(CachedOrigin.Longitude);

    const double SinLat = FMath::Sin(LatRad);
    const double CosLat = FMath::Cos(LatRad);
    const double SinLon = FMath::Sin(LonRad);
    const double CosLon = FMath::Cos(LonRad);

    // ECEF to ENU rotation matrix
    // Row 0 (East):  [-sinLon, cosLon, 0]
    // Row 1 (North): [-sinLat*cosLon, -sinLat*sinLon, cosLat]
    // Row 2 (Up):    [cosLat*cosLon, cosLat*sinLon, sinLat]
    ENURotationMatrix = FMatrix(
        FPlane(-SinLon, -SinLat * CosLon, CosLat * CosLon, 0.0),
        FPlane(CosLon, -SinLat * SinLon, CosLat * SinLon, 0.0),
        FPlane(0.0, CosLat, SinLat, 0.0),
        FPlane(0.0, 0.0, 0.0, 1.0)
    );

    InverseENURotationMatrix = ENURotationMatrix.GetTransposed();
}

FVector UOSMCRSTransformer::TransformToUnreal(double Latitude, double Longitude, double HeightMeters) const
{
    if (!bIsInitialized)
    {
        UE_LOG(LogOSMWorldGen, Warning, TEXT("TransformToUnreal called on uninitialized CRS Transformer!"));
        return FVector::ZeroVector;
    }

    const FVector3d ECEFPos = FOSMEllipsoid::GeodeticToECEF(Latitude, Longitude, HeightMeters);
    const FVector3d DeltaECEF = ECEFPos - ECEFOrigin;

    // Local Tangent Plane (ENU):
    const double LatRad = FMath::DegreesToRadians(CachedOrigin.Latitude);
    const double LonRad = FMath::DegreesToRadians(CachedOrigin.Longitude);

    const double SinLat = FMath::Sin(LatRad);
    const double CosLat = FMath::Cos(LatRad);
    const double SinLon = FMath::Sin(LonRad);
    const double CosLon = FMath::Cos(LonRad);

    const double East  = -SinLon * DeltaECEF.X + CosLon * DeltaECEF.Y;
    const double North = -SinLat * CosLon * DeltaECEF.X - SinLat * SinLon * DeltaECEF.Y + CosLat * DeltaECEF.Z;
    const double Up    =  CosLat * CosLon * DeltaECEF.X + CosLat * SinLon * DeltaECEF.Y + SinLat * DeltaECEF.Z;

    // Convert East -> UE X (cm), North -> UE Y (cm), Up -> UE Z (cm)
    return FVector(
        static_cast<float>(East * FOSMEllipsoid::MetersToUE),
        static_cast<float>(North * FOSMEllipsoid::MetersToUE),
        static_cast<float>(Up * FOSMEllipsoid::MetersToUE)
    );
}

void UOSMCRSTransformer::BatchTransformToUnreal(
    const TArrayView<const FVector2D>& LatLonPairs,
    double DefaultHeight,
    TArray<FVector>& OutPositions) const
{
    OutPositions.Reset(LatLonPairs.Num());
    for (const FVector2D& LatLon : LatLonPairs)
    {
        OutPositions.Add(TransformToUnreal(LatLon.X, LatLon.Y, DefaultHeight));
    }
}

void UOSMCRSTransformer::TransformToWGS84(const FVector& UnrealPos, double& OutLat, double& OutLon, double& OutHeight) const
{
    // Convert UE cm -> ENU meters
    const double East  = UnrealPos.X / FOSMEllipsoid::MetersToUE;
    const double North = UnrealPos.Y / FOSMEllipsoid::MetersToUE;
    const double Up    = UnrealPos.Z / FOSMEllipsoid::MetersToUE;

    const double LatRad = FMath::DegreesToRadians(CachedOrigin.Latitude);
    const double LonRad = FMath::DegreesToRadians(CachedOrigin.Longitude);

    const double SinLat = FMath::Sin(LatRad);
    const double CosLat = FMath::Cos(LatRad);
    const double SinLon = FMath::Sin(LonRad);
    const double CosLon = FMath::Cos(LonRad);

    // ENU to Delta ECEF (inverse rotation)
    const double DeltaX = -SinLon * East - SinLat * CosLon * North + CosLat * CosLon * Up;
    const double DeltaY =  CosLon * East - SinLat * SinLon * North + CosLat * SinLon * Up;
    const double DeltaZ =  0.0 * East    + CosLat * North          + SinLat * Up;

    const FVector3d ECEFPos = ECEFOrigin + FVector3d(DeltaX, DeltaY, DeltaZ);

    // ECEF -> Geodetic (WGS84) iterative solution
    const double P = FMath::Sqrt(ECEFPos.X * ECEFPos.X + ECEFPos.Y * ECEFPos.Y);
    OutLon = FMath::RadiansToDegrees(FMath::Atan2(ECEFPos.Y, ECEFPos.X));

    double Lat = FMath::Atan2(ECEFPos.Z, P * (1.0 - FOSMEllipsoid::WGS84_E2));
    double H = 0.0;
    for (int i = 0; i < 5; ++i)
    {
        const double SinL = FMath::Sin(Lat);
        const double N = FOSMEllipsoid::WGS84_A / FMath::Sqrt(1.0 - FOSMEllipsoid::WGS84_E2 * SinL * SinL);
        H = P / FMath::Cos(Lat) - N;
        Lat = FMath::Atan2(ECEFPos.Z, P * (1.0 - FOSMEllipsoid::WGS84_E2 * (N / (N + H))));
    }

    OutLat = FMath::RadiansToDegrees(Lat);
    OutHeight = H;
}

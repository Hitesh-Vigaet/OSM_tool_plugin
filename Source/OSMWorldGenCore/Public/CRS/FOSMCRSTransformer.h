// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CRS/FOSMGeoOrigin.h"
#include "CRS/EOSMProjectionMode.h"
#include "FOSMCRSTransformer.generated.h"

/**
 * Coordinate Reference System (CRS) Transformer.
 *
 * Provides single, authoritative, shared transformation between WGS84 geographic
 * coordinates (Lat/Lon/H) and Unreal Engine 3D world space coordinates (cm).
 */
UCLASS(BlueprintType)
class OSMWORLDGENCORE_API UOSMCRSTransformer : public UObject
{
    GENERATED_BODY()

public:
    UOSMCRSTransformer();

    /** Initialize transformer with origin and projection mode */
    UFUNCTION(BlueprintCallable, Category = "OSM|CRS")
    void Initialize(const FOSMGeoOrigin& Origin, EOSMProjectionMode Mode = EOSMProjectionMode::ENU);

    /** Check if transformer has been initialized */
    UFUNCTION(BlueprintCallable, Category = "OSM|CRS")
    bool IsInitialized() const { return bIsInitialized; }

    /** Get current origin */
    UFUNCTION(BlueprintCallable, Category = "OSM|CRS")
    const FOSMGeoOrigin& GetOrigin() const { return CachedOrigin; }

    /** Transform Lat/Lon/Height to Unreal World Space (cm) */
    UFUNCTION(BlueprintCallable, Category = "OSM|CRS")
    FVector TransformToUnreal(double Latitude, double Longitude, double HeightMeters = 0.0) const;

    /** Batch transform Lat/Lon pairs to Unreal World Space (cm) */
    void BatchTransformToUnreal(
        const TArrayView<const FVector2D>& LatLonPairs,
        double DefaultHeight,
        TArray<FVector>& OutPositions) const;

    /** Inverse transform: Unreal World Space (cm) -> Lat/Lon/Height */
    UFUNCTION(BlueprintCallable, Category = "OSM|CRS")
    void TransformToWGS84(const FVector& UnrealPos, double& OutLat, double& OutLon, double& OutHeight) const;

private:
    bool bIsInitialized = false;
    FOSMGeoOrigin CachedOrigin;
    EOSMProjectionMode ProjectionMode = EOSMProjectionMode::ENU;

    // Pre-computed origin ECEF and rotation matrix
    FVector3d ECEFOrigin = FVector3d::ZeroVector;
    FMatrix ENURotationMatrix = FMatrix::Identity;
    FMatrix InverseENURotationMatrix = FMatrix::Identity;

    void PrecomputeENU();
};

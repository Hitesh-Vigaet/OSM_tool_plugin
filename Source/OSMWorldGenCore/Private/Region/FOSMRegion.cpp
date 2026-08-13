// Copyright InviMind. All Rights Reserved.

#include "Region/FOSMRegion.h"

namespace
{
    /**
     * Km per degree of longitude at a given latitude. Collapses to zero at the poles, which is
     * why the factories reject centres beyond +/-85 degrees rather than dividing by ~0.
     */
    double KmPerDegreeLonAt(double Latitude)
    {
        return OSMRegionLimits::KmPerDegreeLat * FMath::Cos(FMath::DegreesToRadians(Latitude));
    }

    bool IsFiniteCoordinate(double Value)
    {
        return !FMath::IsNaN(Value) && FMath::IsFinite(Value);
    }
}

// ---------------------------------------------------------------------------
bool FOSMRegion::BuildChecked(
    double InMinLat, double InMinLon, double InMaxLat, double InMaxLon,
    bool bEnforceSizeLimits, FOSMRegion& OutRegion, FString& OutError)
{
    OutRegion = FOSMRegion();

    if (!IsFiniteCoordinate(InMinLat) || !IsFiniteCoordinate(InMaxLat)
        || !IsFiniteCoordinate(InMinLon) || !IsFiniteCoordinate(InMaxLon))
    {
        OutError = TEXT("Region bounds contain a non-finite value (NaN or infinity).");
        return false;
    }

    if (InMinLat < -90.0 || InMaxLat > 90.0)
    {
        OutError = FString::Printf(
            TEXT("Latitude out of range: [%.6f, %.6f] is outside [-90, 90]."), InMinLat, InMaxLat);
        return false;
    }

    if (InMinLon < -180.0 || InMaxLon > 180.0)
    {
        OutError = FString::Printf(
            TEXT("Longitude out of range: [%.6f, %.6f] is outside [-180, 180]."), InMinLon, InMaxLon);
        return false;
    }

    if (InMaxLat <= InMinLat || InMaxLon <= InMinLon)
    {
        OutError = FString::Printf(
            TEXT("Degenerate or inverted bounding box: lat [%.6f, %.6f], lon [%.6f, %.6f]. ")
            TEXT("Min must be strictly less than max."),
            InMinLat, InMaxLat, InMinLon, InMaxLon);
        return false;
    }

    FOSMRegion Candidate;
    Candidate.MinLat = InMinLat;
    Candidate.MaxLat = InMaxLat;
    Candidate.MinLon = InMinLon;
    Candidate.MaxLon = InMaxLon;
    Candidate.bValid = true;

    if (bEnforceSizeLimits)
    {
        // Reject near-polar regions: the longitude scale collapses there, so a "1 km^2" box
        // would be meaninglessly wide in degrees and the equirectangular approximation this
        // plugin uses throughout stops holding.
        if (FMath::Abs(Candidate.GetCenterLat()) > 85.0)
        {
            OutError = FString::Printf(
                TEXT("Region centre latitude %.4f is inside the polar cutoff (+/-85 degrees), ")
                TEXT("where the plugin's flat-earth approximation breaks down."),
                Candidate.GetCenterLat());
            return false;
        }

        const double AreaSqKm = Candidate.GetAreaSqKm();

        if (AreaSqKm > OSMRegionLimits::MaxAreaSqKm)
        {
            OutError = FString::Printf(
                TEXT("Region is %.2f km2 (%.2f x %.2f km), over the %.0f km2 limit."),
                AreaSqKm, Candidate.GetWidthKm(), Candidate.GetHeightKm(), OSMRegionLimits::MaxAreaSqKm);
            return false;
        }

        if (AreaSqKm < OSMRegionLimits::MinAreaSqKm)
        {
            OutError = FString::Printf(
                TEXT("Region is %.4f km2, under the %.2f km2 minimum."),
                AreaSqKm, OSMRegionLimits::MinAreaSqKm);
            return false;
        }
    }

    OutRegion = Candidate;
    OutError.Empty();
    return true;
}

// ---------------------------------------------------------------------------
bool FOSMRegion::FromCenterAndArea(
    double CenterLat, double CenterLon, double AreaSqKm,
    FOSMRegion& OutRegion, FString& OutError)
{
    OutRegion = FOSMRegion();

    if (!IsFiniteCoordinate(CenterLat) || !IsFiniteCoordinate(CenterLon) || !IsFiniteCoordinate(AreaSqKm))
    {
        OutError = TEXT("Region centre or area is not a finite number.");
        return false;
    }

    if (CenterLat < -90.0 || CenterLat > 90.0 || CenterLon < -180.0 || CenterLon > 180.0)
    {
        OutError = FString::Printf(
            TEXT("Region centre (%.6f, %.6f) is not a valid WGS84 coordinate."), CenterLat, CenterLon);
        return false;
    }

    // Checked before the size limits in BuildChecked so the error names the area the user typed
    // rather than the derived box, which is what they can actually act on.
    if (AreaSqKm > OSMRegionLimits::MaxAreaSqKm || AreaSqKm < OSMRegionLimits::MinAreaSqKm)
    {
        OutError = FString::Printf(
            TEXT("Requested area %.4f km2 is outside the allowed range %.2f - %.0f km2."),
            AreaSqKm, OSMRegionLimits::MinAreaSqKm, OSMRegionLimits::MaxAreaSqKm);
        return false;
    }

    if (FMath::Abs(CenterLat) > 85.0)
    {
        OutError = FString::Printf(
            TEXT("Region centre latitude %.4f is inside the polar cutoff (+/-85 degrees), ")
            TEXT("where the plugin's flat-earth approximation breaks down."),
            CenterLat);
        return false;
    }

    const double SideKm = FMath::Sqrt(AreaSqKm);
    const double HalfLatDeg = 0.5 * SideKm / OSMRegionLimits::KmPerDegreeLat;
    const double HalfLonDeg = 0.5 * SideKm / KmPerDegreeLonAt(CenterLat);

    // A square in km is not a square in degrees, so clamping a near-pole or near-dateline box
    // would silently change its size. The polar cutoff above rules out the latitude case; guard
    // the longitude case explicitly rather than producing a wrapped box.
    if (CenterLon - HalfLonDeg < -180.0 || CenterLon + HalfLonDeg > 180.0)
    {
        OutError = TEXT("Region would cross the antimeridian, which is not supported.");
        return false;
    }

    return BuildChecked(
        CenterLat - HalfLatDeg, CenterLon - HalfLonDeg,
        CenterLat + HalfLatDeg, CenterLon + HalfLonDeg,
        /*bEnforceSizeLimits*/ true, OutRegion, OutError);
}

// ---------------------------------------------------------------------------
bool FOSMRegion::FromBoundingBox(
    double MinLat, double MinLon, double MaxLat, double MaxLon,
    FOSMRegion& OutRegion, FString& OutError)
{
    return BuildChecked(MinLat, MinLon, MaxLat, MaxLon, /*bEnforceSizeLimits*/ true, OutRegion, OutError);
}

// ---------------------------------------------------------------------------
bool FOSMRegion::ObservedBounds(
    double MinLat, double MinLon, double MaxLat, double MaxLon,
    FOSMRegion& OutRegion, FString& OutError)
{
    return BuildChecked(MinLat, MinLon, MaxLat, MaxLon, /*bEnforceSizeLimits*/ false, OutRegion, OutError);
}

// ---------------------------------------------------------------------------
double FOSMRegion::GetHeightKm() const
{
    return bValid ? (MaxLat - MinLat) * OSMRegionLimits::KmPerDegreeLat : 0.0;
}

double FOSMRegion::GetWidthKm() const
{
    return bValid ? (MaxLon - MinLon) * KmPerDegreeLonAt(GetCenterLat()) : 0.0;
}

double FOSMRegion::GetAreaSqKm() const
{
    return GetWidthKm() * GetHeightKm();
}

// ---------------------------------------------------------------------------
bool FOSMRegion::ContainsCoordinate(double Lat, double Lon) const
{
    return bValid && Lat >= MinLat && Lat <= MaxLat && Lon >= MinLon && Lon <= MaxLon;
}

bool FOSMRegion::Intersects(const FOSMRegion& Other) const
{
    if (!bValid || !Other.bValid) return false;
    return Other.MinLat < MaxLat && Other.MaxLat > MinLat
        && Other.MinLon < MaxLon && Other.MaxLon > MinLon;
}

bool FOSMRegion::Contains(const FOSMRegion& Other) const
{
    if (!bValid || !Other.bValid) return false;
    return Other.MinLat >= MinLat && Other.MaxLat <= MaxLat
        && Other.MinLon >= MinLon && Other.MaxLon <= MaxLon;
}

// ---------------------------------------------------------------------------
double FOSMRegion::CoverageBy(const FOSMRegion& Other) const
{
    if (!bValid || !Other.bValid) return 0.0;

    const double OverlapLat = FMath::Max(0.0, FMath::Min(MaxLat, Other.MaxLat) - FMath::Max(MinLat, Other.MinLat));
    const double OverlapLon = FMath::Max(0.0, FMath::Min(MaxLon, Other.MaxLon) - FMath::Max(MinLon, Other.MinLon));

    const double SelfLat = MaxLat - MinLat;
    const double SelfLon = MaxLon - MinLon;
    if (SelfLat <= 0.0 || SelfLon <= 0.0) return 0.0;

    // Computed in degrees rather than km: the cos(lat) factor is common to both numerator and
    // denominator over a city-scale box, so it cancels and the ratio stays exact.
    return FMath::Clamp((OverlapLat * OverlapLon) / (SelfLat * SelfLon), 0.0, 1.0);
}

// ---------------------------------------------------------------------------
bool FOSMRegion::EqualsWithin(const FOSMRegion& Other, double ToleranceDegrees) const
{
    if (!bValid || !Other.bValid) return false;
    return FMath::Abs(MinLat - Other.MinLat) <= ToleranceDegrees
        && FMath::Abs(MaxLat - Other.MaxLat) <= ToleranceDegrees
        && FMath::Abs(MinLon - Other.MinLon) <= ToleranceDegrees
        && FMath::Abs(MaxLon - Other.MaxLon) <= ToleranceDegrees;
}

// ---------------------------------------------------------------------------
FOSMRegion FOSMRegion::Expanded(double MarginDegrees) const
{
    if (!bValid) return FOSMRegion();

    FOSMRegion Result = *this;
    Result.MinLat = FMath::Max(MinLat - MarginDegrees, -90.0);
    Result.MaxLat = FMath::Min(MaxLat + MarginDegrees, 90.0);
    Result.MinLon = FMath::Max(MinLon - MarginDegrees, -180.0);
    Result.MaxLon = FMath::Min(MaxLon + MarginDegrees, 180.0);
    return Result;
}

// ---------------------------------------------------------------------------
FString FOSMRegion::ToString() const
{
    if (!bValid) return TEXT("<invalid region>");
    return FString::Printf(TEXT("lat %.6f..%.6f, lon %.6f..%.6f"), MinLat, MaxLat, MinLon, MaxLon);
}

FString FOSMRegion::ToCacheKeyString() const
{
    // 7 decimal places is ~1 cm — far finer than any fetch source resolves, so two requests a
    // user would consider identical always produce the same key, and genuinely different
    // regions never collide.
    return FString::Printf(TEXT("%.7f,%.7f,%.7f,%.7f"), MinLat, MinLon, MaxLat, MaxLon);
}

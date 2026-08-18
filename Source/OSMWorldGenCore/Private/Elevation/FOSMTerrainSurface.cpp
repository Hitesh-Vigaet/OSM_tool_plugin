// Copyright InviMind. All Rights Reserved.

#include "Elevation/FOSMTerrainSurface.h"
#include "Elevation/FOSMDEMSampler.h"
#include "Elevation/FOSMGeoTIFFTile.h"

namespace
{
    constexpr double MetersToCm = 100.0;

    /** Below 16 divisions a grid stops describing a surface; above 512 it stops being cheap. */
    constexpr int32 MinDivisions = 16;
    constexpr int32 MaxDivisions = 512;
}

// ---------------------------------------------------------------------------
FOSMLocalProjection::FOSMLocalProjection(double InOriginLat, double InOriginLon)
    : OriginLat(InOriginLat)
    , OriginLon(InOriginLon)
    , LonScale(MetersPerDegreeLat * FMath::Cos(FMath::DegreesToRadians(InOriginLat)))
{
}

FVector2D FOSMLocalProjection::ToMeters(const FVector2D& LatLon) const
{
    return FVector2D((LatLon.X - OriginLat) * MetersPerDegreeLat,
                     (LatLon.Y - OriginLon) * LonScale);
}

FVector2D FOSMLocalProjection::ToLatLon(const FVector2D& Meters) const
{
    return FVector2D(OriginLat + Meters.X / MetersPerDegreeLat,
                     OriginLon + (LonScale != 0.0 ? Meters.Y / LonScale : 0.0));
}

// ---------------------------------------------------------------------------
bool FOSMTerrainSurface::Build(const FOSMDEMSampler* Sampler, const FOSMLocalProjection& InProjection,
                               const FBox2D& BoundsMeters, double QuadMeters)
{
    Heights.Reset();

    if (!BoundsMeters.bIsValid || BoundsMeters.GetSize().GetMin() <= 0.0)
    {
        return false;
    }

    Projection = InProjection;

    MinX = BoundsMeters.Min.X;
    MinY = BoundsMeters.Min.Y;
    MaxX = BoundsMeters.Max.X;
    MaxY = BoundsMeters.Max.Y;

    const double Quad = FMath::Max(QuadMeters, 1.0);
    DivX = FMath::Clamp(FMath::RoundToInt((MaxX - MinX) / Quad), MinDivisions, MaxDivisions);
    DivY = FMath::Clamp(FMath::RoundToInt((MaxY - MinY) / Quad), MinDivisions, MaxDivisions);

    StepX = (MaxX - MinX) / DivX;
    StepY = (MaxY - MinY) / DivY;

    BaseElevation = (Sampler && Sampler->IsLoaded()) ? Sampler->GetMinElevation() : 0.0;

    Heights.SetNumZeroed((DivX + 1) * (DivY + 1));

    if (!Sampler || !Sampler->IsLoaded())
    {
        // A flat ground at zero. Not an error: a region with no elevation data still needs a
        // surface for everything else to sit on, and a flat one is correct for flat ground.
        return true;
    }

    const FOSMGeoTIFFTile& Tile = Sampler->GetTileMetadata();

    for (int32 IndexX = 0; IndexX <= DivX; ++IndexX)
    {
        for (int32 IndexY = 0; IndexY <= DivY; ++IndexY)
        {
            const FVector2D Meters(MinX + IndexX * StepX, MinY + IndexY * StepY);
            FVector2D LatLon = Projection.ToLatLon(Meters);

            // Clamped into the DEM before sampling, so a node beyond the raster takes the height
            // of the nearest real data rather than falling back to a default.
            //
            // The default used to be zero — the DEM's LOWEST elevation — so every road, park and
            // water body that extended past the raster dropped to the bottom of the valley. That
            // was the single largest grounding error in the scene, up to 21 m, and it looked like
            // geometry floating in the sky because the surrounding ground was high, not because
            // the geometry was.
            LatLon.X = FMath::Clamp(LatLon.X, Tile.GetMinLat(), Tile.GetMaxLat());
            LatLon.Y = FMath::Clamp(LatLon.Y, Tile.GetMinLon(), Tile.GetMaxLon());

            const double Elevation = Sampler->SampleElevation(LatLon.X, LatLon.Y);

            Heights[IndexX * (DivY + 1) + IndexY] = FMath::IsNaN(Elevation)
                ? 0.0
                : (Elevation - BaseElevation) * MetersToCm;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
double FOSMTerrainSurface::NodeHeightCm(int32 IndexX, int32 IndexY) const
{
    IndexX = FMath::Clamp(IndexX, 0, DivX);
    IndexY = FMath::Clamp(IndexY, 0, DivY);
    return Heights[IndexX * (DivY + 1) + IndexY];
}

// ---------------------------------------------------------------------------
double FOSMTerrainSurface::HeightCmAt(double XMeters, double YMeters) const
{
    if (!IsValid())
    {
        return 0.0;
    }

    // Clamped rather than extrapolated: beyond the grid the nearest edge height is a plausible
    // ground, while an extrapolated slope would run away.
    const double LocalX = FMath::Clamp((XMeters - MinX) / StepX, 0.0, static_cast<double>(DivX));
    const double LocalY = FMath::Clamp((YMeters - MinY) / StepY, 0.0, static_cast<double>(DivY));

    const int32 CellX = FMath::Min(static_cast<int32>(LocalX), DivX - 1);
    const int32 CellY = FMath::Min(static_cast<int32>(LocalY), DivY - 1);

    const double FracX = LocalX - CellX;
    const double FracY = LocalY - CellY;

    // Must match BuildMesh exactly. The quad is split along the A-C diagonal, so which triangle a
    // point falls in is decided by FracY against FracX. Bilinear interpolation here instead would
    // be smooth, plausible and WRONG by a few centimetres everywhere off the diagonal — which is
    // precisely the size of the clearances that keep roads above ground.
    const double HeightA = NodeHeightCm(CellX,     CellY);       // (0,0)
    const double HeightB = NodeHeightCm(CellX,     CellY + 1);   // (0,1)
    const double HeightC = NodeHeightCm(CellX + 1, CellY + 1);   // (1,1)
    const double HeightD = NodeHeightCm(CellX + 1, CellY);       // (1,0)

    if (FracY >= FracX)
    {
        // Triangle A, B, C
        return HeightA + FracY * (HeightB - HeightA) + FracX * (HeightC - HeightB);
    }

    // Triangle A, C, D
    return HeightA + FracX * (HeightD - HeightA) + FracY * (HeightC - HeightD);
}

// ---------------------------------------------------------------------------
namespace
{
    /** Even-odd point-in-polygon. Footprints are small — the largest observed has 34 vertices. */
    bool IsInsideRing(TArrayView<const FVector2D> Ring, double X, double Y)
    {
        bool bInside = false;
        for (int32 Index = 0, Previous = Ring.Num() - 1; Index < Ring.Num(); Previous = Index++)
        {
            const FVector2D& A = Ring[Index];
            const FVector2D& B = Ring[Previous];

            if (((A.Y > Y) != (B.Y > Y))
                && (X < (B.X - A.X) * (Y - A.Y) / (B.Y - A.Y) + A.X))
            {
                bInside = !bInside;
            }
        }
        return bInside;
    }

    /**
     * Parameters in (0,1) where a linear function crosses an integer.
     *
     * Used to find where a footprint edge passes from one terrain triangle into the next: in grid
     * units those are exactly the points where u, v or (v - u) becomes whole.
     */
    void AddIntegerCrossings(double From, double To, TArray<double>& OutParams)
    {
        const double Delta = To - From;
        if (FMath::Abs(Delta) < UE_DOUBLE_SMALL_NUMBER)
        {
            return;
        }

        const int32 First = FMath::FloorToInt(FMath::Min(From, To)) + 1;
        const int32 Last = FMath::CeilToInt(FMath::Max(From, To)) - 1;

        // A footprint spanning thousands of quads is not a footprint; refuse rather than stall.
        if (Last - First > 4096)
        {
            return;
        }

        for (int32 Crossing = First; Crossing <= Last; ++Crossing)
        {
            const double Param = (Crossing - From) / Delta;
            if (Param > 0.0 && Param < 1.0)
            {
                OutParams.Add(Param);
            }
        }
    }
}

void FOSMTerrainSurface::MinMaxHeightCmOverPolygon(TArrayView<const FVector2D> RingMeters,
                                                   double& OutMinCm, double& OutMaxCm) const
{
    OutMinCm = TNumericLimits<double>::Max();
    OutMaxCm = TNumericLimits<double>::Lowest();

    if (RingMeters.Num() == 0)
    {
        OutMinCm = OutMaxCm = 0.0;
        return;
    }

    auto Consider = [this, &OutMinCm, &OutMaxCm](double X, double Y)
    {
        const double Height = HeightCmAt(X, Y);
        OutMinCm = FMath::Min(OutMinCm, Height);
        OutMaxCm = FMath::Max(OutMaxCm, Height);
    };

    // Ring vertices always count, even without a grid — a flat surface still has a height.
    FBox2D Bounds(ForceInit);
    for (const FVector2D& Point : RingMeters)
    {
        Consider(Point.X, Point.Y);
        Bounds += Point;
    }

    if (!IsValid())
    {
        return;
    }

    // ---- along the boundary ----
    TArray<double> Params;
    for (int32 Index = 0; Index < RingMeters.Num(); ++Index)
    {
        const FVector2D& A = RingMeters[Index];
        const FVector2D& B = RingMeters[(Index + 1) % RingMeters.Num()];

        // Grid units, where triangle boundaries are simply whole numbers.
        const double FromU = (A.X - MinX) / StepX;
        const double ToU   = (B.X - MinX) / StepX;
        const double FromV = (A.Y - MinY) / StepY;
        const double ToV   = (B.Y - MinY) / StepY;

        Params.Reset();
        AddIntegerCrossings(FromU, ToU, Params);                        // vertical grid lines
        AddIntegerCrossings(FromV, ToV, Params);                        // horizontal grid lines
        AddIntegerCrossings(FromV - FromU, ToV - ToU, Params);          // the A-C diagonals

        for (const double Param : Params)
        {
            Consider(FMath::Lerp(A.X, B.X, Param), FMath::Lerp(A.Y, B.Y, Param));
        }
    }

    // ---- grid nodes inside the footprint ----
    TArray<FVector2D> Nodes;
    GetGridNodesInsidePolygon(RingMeters, Nodes);
    for (const FVector2D& Node : Nodes)
    {
        Consider(Node.X, Node.Y);
    }
}

// ---------------------------------------------------------------------------
void FOSMTerrainSurface::GetGridNodesInsidePolygon(TArrayView<const FVector2D> RingMeters,
                                                   TArray<FVector2D>& OutNodes) const
{
    OutNodes.Reset();

    if (!IsValid() || RingMeters.Num() < 3)
    {
        return;
    }

    FBox2D Bounds(ForceInit);
    for (const FVector2D& Point : RingMeters)
    {
        Bounds += Point;
    }

    const int32 FirstX = FMath::Clamp(FMath::CeilToInt((Bounds.Min.X - MinX) / StepX), 0, DivX);
    const int32 LastX  = FMath::Clamp(FMath::FloorToInt((Bounds.Max.X - MinX) / StepX), 0, DivX);
    const int32 FirstY = FMath::Clamp(FMath::CeilToInt((Bounds.Min.Y - MinY) / StepY), 0, DivY);
    const int32 LastY  = FMath::Clamp(FMath::FloorToInt((Bounds.Max.Y - MinY) / StepY), 0, DivY);

    for (int32 IndexX = FirstX; IndexX <= LastX; ++IndexX)
    {
        const double X = MinX + IndexX * StepX;
        for (int32 IndexY = FirstY; IndexY <= LastY; ++IndexY)
        {
            const double Y = MinY + IndexY * StepY;

            if (IsInsideRing(RingMeters, X, Y))
            {
                OutNodes.Emplace(X, Y);
            }
        }
    }
}

// ---------------------------------------------------------------------------
void FOSMTerrainSurface::SplitAtTriangleBoundaries(TArrayView<const FVector2D> LineMeters, bool bClosed,
                                                   TArray<FVector2D>& OutLine) const
{
    OutLine.Reset();

    if (LineMeters.Num() == 0)
    {
        return;
    }

    if (!IsValid() || LineMeters.Num() == 1)
    {
        OutLine.Append(LineMeters.GetData(), LineMeters.Num());
        return;
    }

    const int32 LastEdge = bClosed ? LineMeters.Num() : LineMeters.Num() - 1;

    TArray<double> Params;
    for (int32 Index = 0; Index < LastEdge; ++Index)
    {
        const FVector2D& A = LineMeters[Index];
        const FVector2D& B = LineMeters[(Index + 1) % LineMeters.Num()];

        OutLine.Add(A);

        // Grid units, where every triangle boundary is a whole number.
        const double FromU = (A.X - MinX) / StepX;
        const double ToU   = (B.X - MinX) / StepX;
        const double FromV = (A.Y - MinY) / StepY;
        const double ToV   = (B.Y - MinY) / StepY;

        Params.Reset();
        AddIntegerCrossings(FromU, ToU, Params);                 // vertical grid lines
        AddIntegerCrossings(FromV, ToV, Params);                 // horizontal grid lines
        AddIntegerCrossings(FromV - FromU, ToV - ToU, Params);   // the A-C diagonals

        Params.Sort();

        // Merge only crossings that are the same POINT — where the line passes through a grid node,
        // or where a grid line and a diagonal meet. The threshold is therefore a distance, not a
        // fraction of the span.
        //
        // Comparing fractions instead is wrong in a way that hides: a tolerance of 1e-4 in
        // parameter space is 4 cm along a 400 m road, so genuine boundaries a few centimetres
        // apart were being discarded, and the segment that replaced them spanned two triangles.
        // That alone left the drape 3.7 cm off the ground where it should be exact.
        const double Length = FVector2D::Distance(A, B);
        const double MinParamStep = Length > UE_DOUBLE_SMALL_NUMBER ? 0.001 / Length : 1.0;

        double Previous = 0.0;
        for (const double Param : Params)
        {
            if (Param - Previous > MinParamStep && 1.0 - Param > MinParamStep)
            {
                OutLine.Emplace(FMath::Lerp(A.X, B.X, Param), FMath::Lerp(A.Y, B.Y, Param));
                Previous = Param;
            }
        }
    }

    if (!bClosed)
    {
        OutLine.Add(LineMeters[LineMeters.Num() - 1]);
    }
}

// ---------------------------------------------------------------------------
void FOSMTerrainSurface::BuildMesh(TArray<FVector>& OutVertices, TArray<int32>& OutTriangles,
                                   TArray<FVector>& OutNormals, TArray<FVector2D>& OutUVs) const
{
    OutVertices.Reset();
    OutTriangles.Reset();
    OutNormals.Reset();
    OutUVs.Reset();

    if (!IsValid())
    {
        return;
    }

    const int32 SideY = DivY + 1;

    OutVertices.Reserve((DivX + 1) * SideY);
    OutTriangles.Reserve(DivX * DivY * 6);

    for (int32 IndexX = 0; IndexX <= DivX; ++IndexX)
    {
        const double X = MinX + IndexX * StepX;
        for (int32 IndexY = 0; IndexY <= DivY; ++IndexY)
        {
            const double Y = MinY + IndexY * StepY;

            OutVertices.Emplace(X * MetersToCm, Y * MetersToCm, NodeHeightCm(IndexX, IndexY));

            // Flat up-normals. This pass is about where things are, and shaded relief makes it
            // harder, not easier, to see whether a road follows the ground.
            OutNormals.Emplace(0.0, 0.0, 1.0);
            OutUVs.Emplace(X, Y);
        }
    }

    for (int32 IndexX = 0; IndexX < DivX; ++IndexX)
    {
        for (int32 IndexY = 0; IndexY < DivY; ++IndexY)
        {
            const int32 A = IndexX * SideY + IndexY;
            const int32 B = A + 1;
            const int32 C = A + SideY + 1;
            const int32 D = A + SideY;

            // Split along A-C, matching HeightCmAt. Wound for a -Z geometric normal so the ground
            // is visible from above — see the convention on FOSMBuildingMesher.h.
            OutTriangles.Append({ A, C, D });
            OutTriangles.Append({ A, B, C });
        }
    }
}

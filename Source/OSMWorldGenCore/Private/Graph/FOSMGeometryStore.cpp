// Copyright InviMind. All Rights Reserved.

#include "Graph/FOSMGeometryStore.h"
#include "Graph/EOSMGraphTypes.h"

// ---------------------------------------------------------------------------
FString OSMNodeTypeToString(EOSMNodeType Type)
{
    switch (Type)
    {
    case EOSMNodeType::Building:       return TEXT("Building");
    case EOSMNodeType::RoadSegment:    return TEXT("Road Segment");
    case EOSMNodeType::Junction:       return TEXT("Junction");
    case EOSMNodeType::WaterBody:      return TEXT("Water Body");
    case EOSMNodeType::Waterway:       return TEXT("Waterway");
    case EOSMNodeType::VegetationArea: return TEXT("Vegetation Area");
    case EOSMNodeType::LanduseZone:    return TEXT("Land Use Zone");
    case EOSMNodeType::LeisureArea:    return TEXT("Leisure Area");
    case EOSMNodeType::Railway:        return TEXT("Railway");
    case EOSMNodeType::Barrier:        return TEXT("Barrier");
    case EOSMNodeType::PowerLine:      return TEXT("Power Line");
    case EOSMNodeType::Amenity:        return TEXT("Amenity");
    case EOSMNodeType::TerrainTile:    return TEXT("Terrain Tile");
    case EOSMNodeType::Block:          return TEXT("Block");
    default:                           return TEXT("Unknown");
    }
}

FString OSMRelationshipTypeToString(EOSMRelationshipType Type)
{
    switch (Type)
    {
    case EOSMRelationshipType::SharesNode:  return TEXT("Shares Node");
    case EOSMRelationshipType::ConnectsTo:  return TEXT("Connects To");
    case EOSMRelationshipType::PartOf:      return TEXT("Part Of");
    case EOSMRelationshipType::Bounds:      return TEXT("Bounds");
    case EOSMRelationshipType::Contains:    return TEXT("Contains");
    case EOSMRelationshipType::FrontsOnto:  return TEXT("Fronts Onto");
    case EOSMRelationshipType::AdjacentTo:  return TEXT("Adjacent To");
    case EOSMRelationshipType::Crosses:     return TEXT("Crosses");
    default:                            return TEXT("Unknown");
    }
}

FString OSMGroupKindToString(EOSMGroupKind Kind)
{
    switch (Kind)
    {
    case EOSMGroupKind::Block:    return TEXT("Block");
    case EOSMGroupKind::Corridor: return TEXT("Corridor");
    default:                      return TEXT("Category");
    }
}

// ---------------------------------------------------------------------------
FOSMGeometryHandle FOSMGeometryStore::AddArea(const TArray<TArray<FVector>>& InRings)
{
    FOSMGeometryHandle Handle;
    if (InRings.Num() == 0)
    {
        return Handle;
    }

    FOSMGeometryRecord Record;
    Record.bIsArea = true;
    Record.FirstRing = Rings.Num();
    Record.RingCount = 0;

    for (const TArray<FVector>& Ring : InRings)
    {
        FOSMRingSpan Span;
        Span.Offset = Points.Num();
        Span.Count = Ring.Num();

        Points.Reserve(Points.Num() + Ring.Num());
        for (const FVector& Point : Ring)
        {
            // Source geometry is lat/lon in X/Y with an unused Z; the store drops Z rather than
            // carrying a column of zeroes through every serialisation.
            Points.Emplace(Point.X, Point.Y);
        }

        Rings.Add(Span);
        ++Record.RingCount;
    }

    Handle.RecordIndex = Records.Add(Record);
    return Handle;
}

// ---------------------------------------------------------------------------
FOSMGeometryHandle FOSMGeometryStore::AddPolyline(const TArray<FVector>& InPoints)
{
    FOSMGeometryHandle Handle;
    if (InPoints.Num() == 0)
    {
        return Handle;
    }

    FOSMRingSpan Span;
    Span.Offset = Points.Num();
    Span.Count = InPoints.Num();

    Points.Reserve(Points.Num() + InPoints.Num());
    for (const FVector& Point : InPoints)
    {
        Points.Emplace(Point.X, Point.Y);
    }

    FOSMGeometryRecord Record;
    Record.bIsArea = false;
    Record.FirstRing = Rings.Add(Span);
    Record.RingCount = 1;

    Handle.RecordIndex = Records.Add(Record);
    return Handle;
}

// ---------------------------------------------------------------------------
int32 FOSMGeometryStore::GetRingCount(const FOSMGeometryHandle& Handle) const
{
    return Records.IsValidIndex(Handle.RecordIndex) ? Records[Handle.RecordIndex].RingCount : 0;
}

bool FOSMGeometryStore::IsArea(const FOSMGeometryHandle& Handle) const
{
    return Records.IsValidIndex(Handle.RecordIndex) && Records[Handle.RecordIndex].bIsArea;
}

TArrayView<const FVector2D> FOSMGeometryStore::GetRing(const FOSMGeometryHandle& Handle, int32 RingIndex) const
{
    if (!Records.IsValidIndex(Handle.RecordIndex))
    {
        return TArrayView<const FVector2D>();
    }

    const FOSMGeometryRecord& Record = Records[Handle.RecordIndex];
    if (RingIndex < 0 || RingIndex >= Record.RingCount)
    {
        return TArrayView<const FVector2D>();
    }

    const FOSMRingSpan& Span = Rings[Record.FirstRing + RingIndex];
    if (Span.Offset < 0 || Span.Offset + Span.Count > Points.Num())
    {
        return TArrayView<const FVector2D>();
    }

    return TArrayView<const FVector2D>(Points.GetData() + Span.Offset, Span.Count);
}

// ---------------------------------------------------------------------------
bool FOSMGeometryStore::GetBounds(const FOSMGeometryHandle& Handle, FVector2D& OutMin, FVector2D& OutMax) const
{
    const int32 RingCount = GetRingCount(Handle);
    if (RingCount == 0)
    {
        return false;
    }

    OutMin = FVector2D(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
    OutMax = FVector2D(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());

    bool bAny = false;
    for (int32 RingIndex = 0; RingIndex < RingCount; ++RingIndex)
    {
        for (const FVector2D& Point : GetRing(Handle, RingIndex))
        {
            OutMin.X = FMath::Min(OutMin.X, Point.X);
            OutMin.Y = FMath::Min(OutMin.Y, Point.Y);
            OutMax.X = FMath::Max(OutMax.X, Point.X);
            OutMax.Y = FMath::Max(OutMax.Y, Point.Y);
            bAny = true;
        }
    }

    return bAny;
}

// ---------------------------------------------------------------------------
bool FOSMGeometryStore::GetCentroid(const FOSMGeometryHandle& Handle, FVector2D& OutCentroid) const
{
    const int32 RingCount = GetRingCount(Handle);
    if (RingCount == 0)
    {
        return false;
    }

    const TArrayView<const FVector2D> Ring = GetRing(Handle, 0);
    if (Ring.Num() == 0)
    {
        return false;
    }

    if (IsArea(Handle) && Ring.Num() >= 3)
    {
        // Area-weighted centroid (the polygon centroid formula). Falls through to the vertex
        // mean below when the signed area is ~0, which happens for degenerate rings where the
        // formula divides by nothing useful.
        double SignedArea = 0.0;
        double Cx = 0.0;
        double Cy = 0.0;

        for (int32 Index = 0; Index < Ring.Num(); ++Index)
        {
            const FVector2D& P0 = Ring[Index];
            const FVector2D& P1 = Ring[(Index + 1) % Ring.Num()];
            const double Cross = P0.X * P1.Y - P1.X * P0.Y;
            SignedArea += Cross;
            Cx += (P0.X + P1.X) * Cross;
            Cy += (P0.Y + P1.Y) * Cross;
        }

        SignedArea *= 0.5;
        if (FMath::Abs(SignedArea) > UE_DOUBLE_SMALL_NUMBER)
        {
            OutCentroid = FVector2D(Cx / (6.0 * SignedArea), Cy / (6.0 * SignedArea));
            return true;
        }
    }

    FVector2D Sum = FVector2D::ZeroVector;
    for (const FVector2D& Point : Ring)
    {
        Sum += Point;
    }
    OutCentroid = Sum / static_cast<double>(Ring.Num());
    return true;
}

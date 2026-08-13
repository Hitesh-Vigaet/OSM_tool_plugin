// Copyright InviMind. All Rights Reserved.

#include "Graph/FOSMGraphBuilder.h"
#include "Model/FOSMFeature.h"
#include "Model/FOSMFeatureTable.h"
#include "OSMWorldGenCore.h"

namespace
{
    /** Mean metres per degree of latitude. Matches OSMRegionLimits::KmPerDegreeLat. */
    constexpr double MetersPerDegreeLat = 111320.0;

    double MetersPerDegreeLonAt(double Latitude)
    {
        return MetersPerDegreeLat * FMath::Cos(FMath::DegreesToRadians(Latitude));
    }

    /**
     * Local metric frame for a region.
     *
     * Distances in this phase are computed by scaling degrees at a fixed reference latitude
     * rather than by projecting. Over a region capped at 5 km the error is well under a metre,
     * and it keeps the graph free of any projection choice — which is a rendering concern, and
     * baking one in is what made the old pipeline impossible to re-inspect.
     */
    struct FLocalMetric
    {
        double LatScale = MetersPerDegreeLat;
        double LonScale = MetersPerDegreeLat;

        explicit FLocalMetric(double ReferenceLat)
            : LonScale(MetersPerDegreeLonAt(ReferenceLat))
        {
        }

        FVector2D ToMeters(const FVector2D& LatLon) const
        {
            return FVector2D(LatLon.X * LatScale, LatLon.Y * LonScale);
        }

        double DistanceMeters(const FVector2D& A, const FVector2D& B) const
        {
            return FVector2D::Distance(ToMeters(A), ToMeters(B));
        }
    };

    /** Shoelace area in m^2 for a ring given in lat/lon. */
    double RingAreaSqm(TArrayView<const FVector2D> Ring, const FLocalMetric& Metric)
    {
        if (Ring.Num() < 3) return 0.0;

        double Twice = 0.0;
        for (int32 Index = 0; Index < Ring.Num(); ++Index)
        {
            const FVector2D P0 = Metric.ToMeters(Ring[Index]);
            const FVector2D P1 = Metric.ToMeters(Ring[(Index + 1) % Ring.Num()]);
            Twice += P0.X * P1.Y - P1.X * P0.Y;
        }
        return FMath::Abs(Twice) * 0.5;
    }

    double RingLengthMeters(TArrayView<const FVector2D> Ring, const FLocalMetric& Metric, bool bClosed)
    {
        if (Ring.Num() < 2) return 0.0;

        double Length = 0.0;
        const int32 Last = bClosed ? Ring.Num() : Ring.Num() - 1;
        for (int32 Index = 0; Index < Last; ++Index)
        {
            Length += Metric.DistanceMeters(Ring[Index], Ring[(Index + 1) % Ring.Num()]);
        }
        return Length;
    }

    /** Standard ray-casting point-in-polygon, in lat/lon space. */
    bool PointInRing(const FVector2D& Point, TArrayView<const FVector2D> Ring)
    {
        if (Ring.Num() < 3) return false;

        bool bInside = false;
        for (int32 i = 0, j = Ring.Num() - 1; i < Ring.Num(); j = i++)
        {
            const FVector2D& Pi = Ring[i];
            const FVector2D& Pj = Ring[j];

            if (((Pi.Y > Point.Y) != (Pj.Y > Point.Y))
                && (Point.X < (Pj.X - Pi.X) * (Point.Y - Pi.Y) / (Pj.Y - Pi.Y) + Pi.X))
            {
                bInside = !bInside;
            }
        }
        return bInside;
    }

    /** Distance in metres from a point to a segment. */
    double PointToSegmentMeters(
        const FVector2D& Point, const FVector2D& SegA, const FVector2D& SegB, const FLocalMetric& Metric)
    {
        const FVector2D P = Metric.ToMeters(Point);
        const FVector2D A = Metric.ToMeters(SegA);
        const FVector2D B = Metric.ToMeters(SegB);

        const FVector2D AB = B - A;
        const double LengthSq = AB.SizeSquared();
        if (LengthSq < UE_DOUBLE_SMALL_NUMBER)
        {
            return FVector2D::Distance(P, A);
        }

        const double T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / LengthSq, 0.0, 1.0);
        return FVector2D::Distance(P, A + AB * T);
    }

    /**
     * Quantised coordinate key, for detecting that two ways share a node.
     *
     * The feature table carries geometry but not OSM node ids, so shared nodes are recovered by
     * coordinate. 1e-7 degrees is ~1 cm — far finer than any two distinct OSM nodes, and coarse
     * enough that the identical source coordinate always produces the identical key.
     */
    uint64 QuantiseCoordinate(const FVector2D& LatLon)
    {
        const int64 Lat = FMath::RoundToInt64(LatLon.X * 1.0e7);
        const int64 Lon = FMath::RoundToInt64(LatLon.Y * 1.0e7);
        return HashCombine(GetTypeHash(Lat), GetTypeHash(Lon));
    }

    /** Node types that are linear roads for topology purposes. */
    bool IsRoadLike(EOSMNodeType Type)
    {
        return Type == EOSMNodeType::RoadSegment;
    }

    /** Node types that enclose an area a building can sit inside. */
    bool IsZoneLike(EOSMNodeType Type)
    {
        return Type == EOSMNodeType::LanduseZone
            || Type == EOSMNodeType::LeisureArea
            || Type == EOSMNodeType::VegetationArea;
    }
}

// ---------------------------------------------------------------------------
EOSMNodeType FOSMGraphBuilder::MapFeatureToNodeType(const FOSMFeature& Feature)
{
    switch (Feature.Type)
    {
    case EOSMFeatureType::Building:    return EOSMNodeType::Building;
    case EOSMFeatureType::Highway:     return EOSMNodeType::RoadSegment;
    case EOSMFeatureType::Waterway:    return EOSMNodeType::Waterway;
    case EOSMFeatureType::WaterArea:   return EOSMNodeType::WaterBody;
    case EOSMFeatureType::NaturalArea: return EOSMNodeType::VegetationArea;
    case EOSMFeatureType::Landuse:     return EOSMNodeType::LanduseZone;
    case EOSMFeatureType::Leisure:     return EOSMNodeType::LeisureArea;
    case EOSMFeatureType::Railway:     return EOSMNodeType::Railway;
    case EOSMFeatureType::Barrier:     return EOSMNodeType::Barrier;
    case EOSMFeatureType::Power:       return EOSMNodeType::PowerLine;
    case EOSMFeatureType::Amenity:     return EOSMNodeType::Amenity;

    // Boundaries are administrative metadata, not city fabric — they describe who governs the
    // ground rather than what is on it, and they span far beyond any region we import.
    case EOSMFeatureType::Boundary:    return EOSMNodeType::Unknown;
    default:                           return EOSMNodeType::Unknown;
    }
}

// ---------------------------------------------------------------------------
void FOSMGraphBuilder::BuildNodes(
    const FOSMFeatureTable& Features, UOSMCityGraph& Graph, FOSMGraphReport& Report)
{
    const FLocalMetric Metric(0.5 * (Graph.RegionMinLat + Graph.RegionMaxLat));

    // Features are visited in table order, which is parse order, which is deterministic for a
    // given file — so node ids are stable across runs without needing an explicit sort.
    for (uint8 TypeIndex = 0; TypeIndex < static_cast<uint8>(EOSMFeatureType::MAX); ++TypeIndex)
    {
        const EOSMFeatureType FeatureType = static_cast<EOSMFeatureType>(TypeIndex);
        const TArray<const FOSMFeature*> OfType = Features.GetFeaturesByType(FeatureType);

        for (const FOSMFeature* FeaturePtr : OfType)
        {
            if (!FeaturePtr) continue;
            const FOSMFeature& Feature = *FeaturePtr;
            ++Report.FeaturesConsidered;

            const EOSMNodeType NodeType = MapFeatureToNodeType(Feature);
            if (NodeType == EOSMNodeType::Unknown)
            {
                Report.RejectedFeatures.Add(FString::Printf(
                    TEXT("OSM %lld (%s/%s): no graph node type maps to this classification."),
                    Feature.OSMId, *OSMFeatureTypeToString(Feature.Type), *Feature.SubType));
                continue;
            }

            const bool bHasArea = Feature.HasPolygon();
            const bool bHasLine = Feature.HasPolyline();

            if (!bHasArea && !bHasLine)
            {
                Report.RejectedFeatures.Add(FString::Printf(
                    TEXT("OSM %lld (%s/%s): no usable geometry (needs >=3 ring points or >=2 line points)."),
                    Feature.OSMId, *OSMFeatureTypeToString(Feature.Type), *Feature.SubType));
                continue;
            }

            FOSMGraphNode Node;
            Node.Id = Graph.Nodes.Num();
            Node.Type = NodeType;
            Node.SubType = Feature.SubType;
            Node.OSMId = Feature.OSMId;
            Node.Tags = Feature.Tags;
            Node.Name = Feature.Computed.Name;

            if (Node.Name.IsEmpty())
            {
                if (const FString* NameTag = Feature.Tags.Find(TEXT("name")))
                {
                    Node.Name = *NameTag;
                }
            }

            if (bHasArea)
            {
                Node.Geometry = Graph.Geometry.AddArea(Feature.Polygons);
                const TArrayView<const FVector2D> Outer = Graph.Geometry.GetOuterRing(Node.Geometry);
                Node.Metrics.AreaSqm = RingAreaSqm(Outer, Metric);
                Node.Metrics.LengthMeters = RingLengthMeters(Outer, Metric, /*bClosed*/ true);
            }
            else
            {
                Node.Geometry = Graph.Geometry.AddPolyline(Feature.Polyline);
                const TArrayView<const FVector2D> Line = Graph.Geometry.GetOuterRing(Node.Geometry);
                Node.Metrics.LengthMeters = RingLengthMeters(Line, Metric, /*bClosed*/ false);
            }

            Graph.Geometry.GetCentroid(Node.Geometry, Node.Metrics.CentroidLatLon);
            Node.Metrics.HeightMeters = Feature.Computed.HeightMeters;
            Node.Metrics.WidthMeters = Feature.Computed.WidthMeters;

            Graph.Nodes.Add(MoveTemp(Node));
            ++Report.NodesCreated;
        }
    }
}

// ---------------------------------------------------------------------------
void FOSMGraphBuilder::BuildTopology(UOSMCityGraph& Graph, FOSMGraphReport& Report)
{
    // Map every road vertex to the segments that use it. A coordinate used by two or more
    // distinct segments is a junction — this catches both the common case (ways split at
    // intersections) and the case where one road passes through another's interior vertex.
    TMap<uint64, TArray<int32>> CoordToSegments;
    TMap<uint64, FVector2D> CoordToPosition;

    for (const FOSMGraphNode& Node : Graph.Nodes)
    {
        if (!IsRoadLike(Node.Type) || !Node.HasGeometry()) continue;

        for (const FVector2D& Point : Graph.Geometry.GetOuterRing(Node.Geometry))
        {
            const uint64 Key = QuantiseCoordinate(Point);
            TArray<int32>& Segments = CoordToSegments.FindOrAdd(Key);
            Segments.AddUnique(Node.Id);
            CoordToPosition.FindOrAdd(Key, Point);
        }
    }

    // Sorted before use: TMap iteration order is unspecified, and letting it decide junction ids
    // would make the graph hash differ between runs on identical input.
    TArray<uint64> JunctionKeys;
    for (const TPair<uint64, TArray<int32>>& Pair : CoordToSegments)
    {
        if (Pair.Value.Num() >= 2)
        {
            JunctionKeys.Add(Pair.Key);
        }
    }
    JunctionKeys.Sort();

    for (const uint64 Key : JunctionKeys)
    {
        TArray<int32> Segments = CoordToSegments[Key];
        Segments.Sort();

        FOSMGraphNode Junction;
        Junction.Id = Graph.Nodes.Num();
        Junction.Type = EOSMNodeType::Junction;
        Junction.SubType = TEXT("junction");
        Junction.Metrics.CentroidLatLon = CoordToPosition[Key];
        Graph.Nodes.Add(MoveTemp(Junction));

        const int32 JunctionId = Graph.Nodes.Num() - 1;
        ++Report.NodesCreated;

        // Segment <-> junction, both directions, so the junction is reachable from a road and
        // vice versa without scanning every edge.
        for (const int32 SegmentId : Segments)
        {
            FOSMGraphEdge ToJunction;
            ToJunction.Type = EOSMRelationshipType::SharesNode;
            ToJunction.FromNode = SegmentId;
            ToJunction.ToNode = JunctionId;
            Graph.Edges.Add(ToJunction);
        }

        // Segment-to-segment connectivity through this junction. Emitted once per unordered
        // pair; the road network is undirected here, so both orderings would be redundant.
        for (int32 i = 0; i < Segments.Num(); ++i)
        {
            for (int32 j = i + 1; j < Segments.Num(); ++j)
            {
                FOSMGraphEdge Connects;
                Connects.Type = EOSMRelationshipType::ConnectsTo;
                Connects.FromNode = Segments[i];
                Connects.ToNode = Segments[j];
                Graph.Edges.Add(Connects);
            }
        }
    }

    Report.EdgesCreated = Graph.Edges.Num();
}

// ---------------------------------------------------------------------------
void FOSMGraphBuilder::BuildSpatial(
    UOSMCityGraph& Graph, const FOSMGraphBuildOptions& Options, FOSMGraphReport& Report)
{
    const FLocalMetric Metric(0.5 * (Graph.RegionMinLat + Graph.RegionMaxLat));

    const TArray<int32> Buildings = Graph.GetNodesOfType(EOSMNodeType::Building);
    const TArray<int32> Roads = Graph.GetNodesOfType(EOSMNodeType::RoadSegment);

    // ---- Contains: which zone is each building in ----
    if (Options.bComputeContains)
    {
        TArray<int32> Zones;
        for (const FOSMGraphNode& Node : Graph.Nodes)
        {
            if (IsZoneLike(Node.Type) && Node.HasGeometry() && Graph.Geometry.IsArea(Node.Geometry))
            {
                Zones.Add(Node.Id);
            }
        }

        for (const int32 BuildingId : Buildings)
        {
            const FVector2D Centroid = Graph.Nodes[BuildingId].Metrics.CentroidLatLon;

            for (const int32 ZoneId : Zones)
            {
                const FOSMGeometryHandle& Handle = Graph.Nodes[ZoneId].Geometry;
                if (!PointInRing(Centroid, Graph.Geometry.GetOuterRing(Handle)))
                {
                    continue;
                }

                // Inside the outer ring, but a courtyard or lake hole means it is not really in
                // the zone. Checking holes is what stops a building in a park's central pond
                // from being reported as parkland.
                bool bInHole = false;
                for (int32 RingIndex = 1; RingIndex < Graph.Geometry.GetRingCount(Handle); ++RingIndex)
                {
                    if (PointInRing(Centroid, Graph.Geometry.GetRing(Handle, RingIndex)))
                    {
                        bInHole = true;
                        break;
                    }
                }
                if (bInHole) continue;

                FOSMGraphEdge Contains;
                Contains.Type = EOSMRelationshipType::Contains;
                Contains.FromNode = ZoneId;
                Contains.ToNode = BuildingId;
                Graph.Edges.Add(Contains);
            }
        }
    }

    // ---- FrontsOnto: which street does each building face ----
    if (Options.bComputeFrontsOnto && Roads.Num() > 0)
    {
        // Uniform grid over road geometry. Without it this is O(buildings x road vertices),
        // which on a dense region is tens of millions of distance computations.
        const double CellSizeMeters = FMath::Max(Options.FrontsOntoCutoffMeters, 10.0);
        const double CellLatDeg = CellSizeMeters / MetersPerDegreeLat;
        const double CellLonDeg = CellSizeMeters / FMath::Max(1.0, Metric.LonScale);

        auto CellKey = [CellLatDeg, CellLonDeg](const FVector2D& LatLon)
        {
            const int64 Row = FMath::FloorToInt64(LatLon.X / CellLatDeg);
            const int64 Col = FMath::FloorToInt64(LatLon.Y / CellLonDeg);
            return HashCombine(GetTypeHash(Row), GetTypeHash(Col));
        };

        // Index each road by every cell its vertices fall in, so a long road is findable from
        // anywhere along its length rather than only near its centroid.
        TMap<uint32, TArray<int32>> Grid;
        for (const int32 RoadId : Roads)
        {
            const FOSMGraphNode& Road = Graph.Nodes[RoadId];
            if (!Road.HasGeometry()) continue;

            for (const FVector2D& Point : Graph.Geometry.GetOuterRing(Road.Geometry))
            {
                Grid.FindOrAdd(CellKey(Point)).AddUnique(RoadId);
            }
        }

        for (const int32 BuildingId : Buildings)
        {
            const FVector2D Centroid = Graph.Nodes[BuildingId].Metrics.CentroidLatLon;

            // Gather candidates from the 3x3 neighbourhood: a building near a cell edge must
            // still see a road just across the boundary.
            TArray<int32> Candidates;
            for (int32 RowOffset = -1; RowOffset <= 1; ++RowOffset)
            {
                for (int32 ColOffset = -1; ColOffset <= 1; ++ColOffset)
                {
                    const FVector2D Probe(
                        Centroid.X + RowOffset * CellLatDeg,
                        Centroid.Y + ColOffset * CellLonDeg);

                    if (const TArray<int32>* Cell = Grid.Find(CellKey(Probe)))
                    {
                        for (const int32 RoadId : *Cell)
                        {
                            Candidates.AddUnique(RoadId);
                        }
                    }
                }
            }

            // Sorted so that an exact distance tie resolves the same way on every run.
            Candidates.Sort();

            int32 NearestRoad = INDEX_NONE;
            double NearestDistance = TNumericLimits<double>::Max();

            for (const int32 RoadId : Candidates)
            {
                const FOSMGraphNode& Road = Graph.Nodes[RoadId];
                if (!Road.HasGeometry()) continue;

                const TArrayView<const FVector2D> Line = Graph.Geometry.GetOuterRing(Road.Geometry);
                for (int32 Index = 0; Index + 1 < Line.Num(); ++Index)
                {
                    const double Distance =
                        PointToSegmentMeters(Centroid, Line[Index], Line[Index + 1], Metric);

                    if (Distance < NearestDistance)
                    {
                        NearestDistance = Distance;
                        NearestRoad = RoadId;
                    }
                }
            }

            if (NearestRoad != INDEX_NONE && NearestDistance <= Options.FrontsOntoCutoffMeters)
            {
                FOSMGraphEdge Fronts;
                Fronts.Type = EOSMRelationshipType::FrontsOnto;
                Fronts.FromNode = BuildingId;
                Fronts.ToNode = NearestRoad;
                Fronts.Value = NearestDistance;
                Fronts.ToleranceMeters = Options.FrontsOntoCutoffMeters;
                Graph.Edges.Add(Fronts);
            }
            else
            {
                // Flagged rather than silently unlinked: a building with no street is either a
                // data gap or a cutoff that is too tight, and both are worth seeing.
                Graph.Nodes[BuildingId].ValidationFlags.AddUnique(TEXT("node.building.nostreet"));
            }
        }
    }

    Report.EdgesCreated = Graph.Edges.Num();
}

// ---------------------------------------------------------------------------
void FOSMGraphBuilder::BuildGroups(
    UOSMCityGraph& Graph, const FOSMGraphBuildOptions& Options, FOSMGraphReport& Report)
{
    // ---- Category groups ----
    //
    // This is the "keep buildings as one group" requirement: one handle for management, with
    // every individual node still addressable underneath. Merging them into a single mesh would
    // have satisfied the first half and destroyed the second.
    for (uint8 TypeIndex = 0; TypeIndex < static_cast<uint8>(EOSMNodeType::MAX); ++TypeIndex)
    {
        const EOSMNodeType Type = static_cast<EOSMNodeType>(TypeIndex);
        TArray<int32> Members = Graph.GetNodesOfType(Type);
        if (Members.Num() == 0) continue;

        FOSMNodeGroup Group;
        Group.Kind = EOSMGroupKind::Category;
        Group.NodeType = Type;
        // Naive "+s" produced "Amenitys". The label is what the user reads in the Control
        // Center, so it is worth getting right.
        const FString TypeName = OSMNodeTypeToString(Type);
        Group.Name = TypeName.EndsWith(TEXT("y"))
            ? TypeName.LeftChop(1) + TEXT("ies")
            : TypeName + TEXT("s");
        Group.NodeIds = MoveTemp(Members);
        Graph.Groups.Add(MoveTemp(Group));
    }

    // ---- Corridors: a street is one entity, not forty fragments ----
    if (Options.bBuildCorridors)
    {
        TMap<FString, TArray<int32>> ByName;
        for (const int32 RoadId : Graph.GetNodesOfType(EOSMNodeType::RoadSegment))
        {
            const FString& Name = Graph.Nodes[RoadId].Name;
            if (!Name.IsEmpty())
            {
                ByName.FindOrAdd(Name).Add(RoadId);
            }
        }

        // Sorted: map order would otherwise decide group order and change the graph hash.
        TArray<FString> Names;
        ByName.GetKeys(Names);
        Names.Sort();

        for (const FString& Name : Names)
        {
            const TArray<int32>& Members = ByName[Name];
            if (Members.Num() < 2) continue;  // a single segment is not a corridor

            FOSMNodeGroup Group;
            Group.Kind = EOSMGroupKind::Corridor;
            Group.NodeType = EOSMNodeType::RoadSegment;
            Group.Name = Name;
            Group.NodeIds = Members;
            Graph.Groups.Add(MoveTemp(Group));
        }
    }

    Report.GroupsCreated = Graph.Groups.Num();
}

// ---------------------------------------------------------------------------
void FOSMGraphBuilder::ValidateGraph(
    UOSMCityGraph& Graph, const FOSMRegion& Region, FOSMGraphReport& Report)
{
    TMap<int64, int32> SeenOSMIds;
    int32 OutsideRegion = 0;
    int32 DegenerateGeometry = 0;
    int32 DuplicateIds = 0;
    int32 RoadsWithoutJunction = 0;

    // Which roads touch a junction — needed to spot disconnected network islands.
    TSet<int32> RoadsWithJunction;
    for (const FOSMGraphEdge& Edge : Graph.Edges)
    {
        if (Edge.Type == EOSMRelationshipType::SharesNode)
        {
            RoadsWithJunction.Add(Edge.FromNode);
        }
    }

    for (FOSMGraphNode& Node : Graph.Nodes)
    {
        // ---- Duplicate OSM ids ----
        if (Node.OSMId != 0)
        {
            if (int32* Existing = SeenOSMIds.Find(Node.OSMId))
            {
                Node.ValidationFlags.AddUnique(TEXT("node.osmid.duplicate"));
                ++DuplicateIds;
            }
            else
            {
                SeenOSMIds.Add(Node.OSMId, Node.Id);
            }
        }

        if (!Node.HasGeometry())
        {
            // Junctions are points and legitimately carry no geometry record.
            if (Node.Type != EOSMNodeType::Junction)
            {
                Node.ValidationFlags.AddUnique(TEXT("node.geometry.missing"));
                ++DegenerateGeometry;
            }
            continue;
        }

        // ---- Degenerate geometry ----
        const bool bIsArea = Graph.Geometry.IsArea(Node.Geometry);
        const TArrayView<const FVector2D> Outer = Graph.Geometry.GetOuterRing(Node.Geometry);

        if ((bIsArea && Outer.Num() < 3) || (!bIsArea && Outer.Num() < 2))
        {
            Node.ValidationFlags.AddUnique(TEXT("node.geometry.degenerate"));
            ++DegenerateGeometry;
        }
        else if (bIsArea && Node.Metrics.AreaSqm <= 0.0)
        {
            Node.ValidationFlags.AddUnique(TEXT("node.geometry.zeroarea"));
            ++DegenerateGeometry;
        }
        else if (!bIsArea && Node.Metrics.LengthMeters <= 0.0)
        {
            Node.ValidationFlags.AddUnique(TEXT("node.geometry.zerolength"));
            ++DegenerateGeometry;
        }

        // ---- Geometry outside the region ----
        //
        // Must be zero: clipping runs before the graph is built, so anything outside means the
        // clip did not do its job. This is the check that would have caught the 40 km way.
        FVector2D BoundsMin, BoundsMax;
        if (Graph.Geometry.GetBounds(Node.Geometry, BoundsMin, BoundsMax))
        {
            // The same allowance feature clipping used, so the two cannot disagree. Anything
            // beyond this did not come from a boundary-straddling feature and means the clip
            // did not run or ran against different bounds.
            constexpr double EdgeToleranceDeg = OSMRegionLimits::ClipMarginDegrees + 1e-9;
            const bool bOutside =
                BoundsMin.X < Region.GetMinLat() - EdgeToleranceDeg ||
                BoundsMax.X > Region.GetMaxLat() + EdgeToleranceDeg ||
                BoundsMin.Y < Region.GetMinLon() - EdgeToleranceDeg ||
                BoundsMax.Y > Region.GetMaxLon() + EdgeToleranceDeg;

            if (bOutside)
            {
                Node.ValidationFlags.AddUnique(TEXT("node.geometry.outsideregion"));
                ++OutsideRegion;
            }
        }

        // ---- Disconnected roads ----
        if (IsRoadLike(Node.Type) && !RoadsWithJunction.Contains(Node.Id))
        {
            Node.ValidationFlags.AddUnique(TEXT("node.road.nojunction"));
            ++RoadsWithoutJunction;
        }
    }

    // ---- Report ----
    if (OutsideRegion > 0)
    {
        // Fatal: this means clipping failed, and building on it reproduces the exact defect
        // that scattered the city across several kilometres.
        Report.Validation.AddFatal(TEXT("graph.geometry.outsideregion"),
            FString::Printf(
                TEXT("%d node(s) have geometry outside the region. Clipping should have made this ")
                TEXT("impossible, so the region or the clip bounds disagree."),
                OutsideRegion));
    }

    if (DegenerateGeometry > 0)
    {
        Report.Validation.AddWarning(TEXT("graph.geometry.degenerate"),
            FString::Printf(
                TEXT("%d node(s) have degenerate geometry (too few points, or zero area/length). ")
                TEXT("Kept and flagged; they will not produce usable geometry."),
                DegenerateGeometry));
    }

    if (DuplicateIds > 0)
    {
        Report.Validation.AddWarning(TEXT("graph.osmid.duplicate"),
            FString::Printf(TEXT("%d node(s) share an OSM id with an earlier node."), DuplicateIds));
    }

    if (RoadsWithoutJunction > 0)
    {
        Report.Validation.AddWarning(TEXT("graph.road.nojunction"),
            FString::Printf(
                TEXT("%d road segment(s) meet no other road. Normal at the region edge, where a ")
                TEXT("street was cut off; suspicious anywhere else."),
                RoadsWithoutJunction));
    }

    const int32 NoStreet = Graph.GetNodesOfType(EOSMNodeType::Building).FilterByPredicate(
        [&Graph](int32 NodeId)
        {
            return Graph.Nodes[NodeId].ValidationFlags.Contains(TEXT("node.building.nostreet"));
        }).Num();

    if (NoStreet > 0)
    {
        Report.Validation.AddWarning(TEXT("graph.building.nostreet"),
            FString::Printf(
                TEXT("%d building(s) have no road within the FrontsOnto cutoff."), NoStreet));
    }

    // The "nothing vanishes" criterion, checked rather than assumed.
    if (!Report.IsComplete())
    {
        Report.Validation.AddFatal(TEXT("graph.features.unaccounted"),
            FString::Printf(
                TEXT("%d feature(s) considered but only %d nodes and %d stated rejections — some ")
                TEXT("features disappeared without explanation."),
                Report.FeaturesConsidered, Report.NodesCreated, Report.RejectedFeatures.Num()));
    }

    Report.FlaggedNodes = Graph.GetFlaggedNodes().Num();
}

// ---------------------------------------------------------------------------
UOSMCityGraph* FOSMGraphBuilder::Build(
    const FOSMFeatureTable& Features,
    const FOSMRegion& Region,
    const FOSMGraphBuildOptions& Options,
    UObject* Outer,
    FOSMGraphReport& OutReport)
{
    const double StartTime = FPlatformTime::Seconds();
    OutReport = FOSMGraphReport();

    if (!Outer)
    {
        OutReport.Validation.AddFatal(TEXT("graph.outer.null"),
            TEXT("No owning object supplied for the city graph."));
        return nullptr;
    }

    UOSMCityGraph* Graph = NewObject<UOSMCityGraph>(Outer);

    if (!Region.IsValid())
    {
        OutReport.Validation.AddFatal(TEXT("graph.region.invalid"),
            TEXT("Cannot build a city graph without a valid region."));
        return Graph;
    }

    // The region is copied in, never re-derived from the geometry that follows.
    Graph->RegionMinLat = Region.GetMinLat();
    Graph->RegionMaxLat = Region.GetMaxLat();
    Graph->RegionMinLon = Region.GetMinLon();
    Graph->RegionMaxLon = Region.GetMaxLon();

    BuildNodes(Features, *Graph, OutReport);
    BuildTopology(*Graph, OutReport);
    BuildSpatial(*Graph, Options, OutReport);
    BuildGroups(*Graph, Options, OutReport);
    ValidateGraph(*Graph, Region, OutReport);

    OutReport.EdgesCreated = Graph->Edges.Num();
    OutReport.DurationSeconds = FPlatformTime::Seconds() - StartTime;

    UE_LOG(LogOSMWorldGen, Log, TEXT("City graph built: %d nodes, %d edges, %d groups in %.2fs"),
        Graph->Nodes.Num(), Graph->Edges.Num(), Graph->Groups.Num(), OutReport.DurationSeconds);

    return Graph;
}

// ---------------------------------------------------------------------------
FString FOSMGraphReport::ToDisplayString() const
{
    TArray<FString> Lines;

    Lines.Add(IsAccepted()
        ? TEXT("City graph built successfully — representation only, nothing generated.")
        : TEXT("City graph build FAILED — see the reasons below."));
    Lines.Add(TEXT(""));

    if (!IsAccepted())
    {
        Lines.Add(TEXT("Reason:"));
        for (const FOSMValidationIssue& Issue : Validation.Issues)
        {
            if (Issue.Severity == EOSMIssueSeverity::Fatal)
            {
                Lines.Add(FString::Printf(TEXT("  %s"), *Issue.Message));
            }
        }
        Lines.Add(TEXT(""));
    }

    Lines.Add(FString::Printf(TEXT("Features considered: %d"), FeaturesConsidered));
    Lines.Add(FString::Printf(TEXT("Nodes created:       %d"), NodesCreated));
    Lines.Add(FString::Printf(TEXT("Relationships:       %d"), EdgesCreated));
    Lines.Add(FString::Printf(TEXT("Groups:              %d"), GroupsCreated));
    Lines.Add(FString::Printf(TEXT("Flagged nodes:       %d  (kept, not dropped)"), FlaggedNodes));

    if (RejectedFeatures.Num() > 0)
    {
        Lines.Add(TEXT(""));
        Lines.Add(FString::Printf(TEXT("Features that produced no node (%d):"), RejectedFeatures.Num()));

        // Capped: a systematic problem shows up in the first few, and a wall of identical lines
        // buries everything else in the report.
        constexpr int32 MaxListed = 10;
        for (int32 Index = 0; Index < FMath::Min(RejectedFeatures.Num(), MaxListed); ++Index)
        {
            Lines.Add(FString::Printf(TEXT("  - %s"), *RejectedFeatures[Index]));
        }
        if (RejectedFeatures.Num() > MaxListed)
        {
            Lines.Add(FString::Printf(TEXT("  ... and %d more"), RejectedFeatures.Num() - MaxListed));
        }
    }

    const int32 WarningCount = Validation.CountOf(EOSMIssueSeverity::Warning);
    if (WarningCount > 0)
    {
        Lines.Add(TEXT(""));
        Lines.Add(FString::Printf(TEXT("Warnings (%d):"), WarningCount));
        for (const FOSMValidationIssue& Issue : Validation.Issues)
        {
            if (Issue.Severity == EOSMIssueSeverity::Warning)
            {
                Lines.Add(FString::Printf(TEXT("  - %s"), *Issue.Message));
            }
        }
    }

    Lines.Add(TEXT(""));
    Lines.Add(FString::Printf(TEXT("Built in %.2f s."), DurationSeconds));

    return FString::Join(Lines, TEXT("\n"));
}

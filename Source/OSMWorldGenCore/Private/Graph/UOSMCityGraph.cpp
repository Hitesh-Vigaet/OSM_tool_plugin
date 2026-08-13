// Copyright InviMind. All Rights Reserved.

#include "Graph/UOSMCityGraph.h"
#include "Misc/SecureHash.h"

// ---------------------------------------------------------------------------
int32 UOSMCityGraph::CountNodesOfType(EOSMNodeType Type) const
{
    int32 Count = 0;
    for (const FOSMGraphNode& Node : Nodes)
    {
        if (Node.Type == Type) ++Count;
    }
    return Count;
}

int32 UOSMCityGraph::CountEdgesOfType(EOSMRelationshipType Type) const
{
    int32 Count = 0;
    for (const FOSMGraphEdge& Edge : Edges)
    {
        if (Edge.Type == Type) ++Count;
    }
    return Count;
}

TArray<int32> UOSMCityGraph::GetNodesOfType(EOSMNodeType Type) const
{
    TArray<int32> Result;
    for (const FOSMGraphNode& Node : Nodes)
    {
        if (Node.Type == Type) Result.Add(Node.Id);
    }
    return Result;
}

TArray<int32> UOSMCityGraph::GetOutgoingEdges(int32 NodeId) const
{
    TArray<int32> Result;
    for (int32 Index = 0; Index < Edges.Num(); ++Index)
    {
        if (Edges[Index].FromNode == NodeId) Result.Add(Index);
    }
    return Result;
}

TArray<int32> UOSMCityGraph::GetIncomingEdges(int32 NodeId) const
{
    TArray<int32> Result;
    for (int32 Index = 0; Index < Edges.Num(); ++Index)
    {
        if (Edges[Index].ToNode == NodeId) Result.Add(Index);
    }
    return Result;
}

const FOSMNodeGroup* UOSMCityGraph::FindGroup(const FString& GroupName) const
{
    return Groups.FindByPredicate(
        [&GroupName](const FOSMNodeGroup& Group) { return Group.Name == GroupName; });
}

TArray<int32> UOSMCityGraph::GetFlaggedNodes() const
{
    TArray<int32> Result;
    for (const FOSMGraphNode& Node : Nodes)
    {
        if (Node.IsFlagged()) Result.Add(Node.Id);
    }
    return Result;
}

// ---------------------------------------------------------------------------
FString UOSMCityGraph::ComputeContentHash() const
{
    // Built from a canonical text rendering rather than raw memory: struct padding and pointer
    // values are not content, and hashing them would make the hash differ between runs that
    // produced identical graphs — which would make the determinism test useless.
    FMD5 Hash;

    auto Feed = [&Hash](const FString& Text)
    {
        const FTCHARToUTF8 Utf8(*Text);
        Hash.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
    };

    Feed(FString::Printf(TEXT("region:%.7f,%.7f,%.7f,%.7f|"),
        RegionMinLat, RegionMinLon, RegionMaxLat, RegionMaxLon));

    for (const FOSMGraphNode& Node : Nodes)
    {
        // Coordinates are rounded to 7 decimals (~1 cm) so that a difference far below the
        // precision of the source data cannot flip the hash.
        Feed(FString::Printf(TEXT("n:%d,%d,%s,%lld,%.7f,%.7f,%.3f,%.3f,%d|"),
            Node.Id, static_cast<int32>(Node.Type), *Node.SubType, Node.OSMId,
            Node.Metrics.CentroidLatLon.X, Node.Metrics.CentroidLatLon.Y,
            Node.Metrics.AreaSqm, Node.Metrics.LengthMeters,
            Node.ValidationFlags.Num()));
    }

    for (const FOSMGraphEdge& Edge : Edges)
    {
        Feed(FString::Printf(TEXT("e:%d,%d,%d,%.3f|"),
            static_cast<int32>(Edge.Type), Edge.FromNode, Edge.ToNode, Edge.Value));
    }

    for (const FOSMNodeGroup& Group : Groups)
    {
        Feed(FString::Printf(TEXT("g:%s,%d,%d|"),
            *Group.Name, static_cast<int32>(Group.Kind), Group.NodeIds.Num()));
    }

    Feed(FString::Printf(TEXT("geo:%d,%d,%d|"),
        Geometry.Points.Num(), Geometry.Rings.Num(), Geometry.Records.Num()));

    uint8 Digest[16];
    Hash.Final(Digest);

    FString Result;
    for (int32 Index = 0; Index < 16; ++Index)
    {
        Result += FString::Printf(TEXT("%02x"), Digest[Index]);
    }
    return Result;
}

// ---------------------------------------------------------------------------
FString UOSMCityGraph::ToSummaryString() const
{
    TArray<FString> Lines;

    Lines.Add(FString::Printf(TEXT("City Graph: %d nodes, %d relationships, %d groups"),
        Nodes.Num(), Edges.Num(), Groups.Num()));
    Lines.Add(FString::Printf(TEXT("Region:     lat %.6f..%.6f, lon %.6f..%.6f"),
        RegionMinLat, RegionMaxLat, RegionMinLon, RegionMaxLon));
    Lines.Add(TEXT(""));

    Lines.Add(TEXT("Nodes by type:"));
    for (uint8 TypeIndex = 0; TypeIndex < static_cast<uint8>(EOSMNodeType::MAX); ++TypeIndex)
    {
        const EOSMNodeType Type = static_cast<EOSMNodeType>(TypeIndex);
        const int32 Count = CountNodesOfType(Type);
        if (Count > 0)
        {
            Lines.Add(FString::Printf(TEXT("  %-18s %d"), *OSMNodeTypeToString(Type), Count));
        }
    }

    Lines.Add(TEXT(""));
    Lines.Add(TEXT("Relationships by type:"));
    for (uint8 TypeIndex = 0; TypeIndex < static_cast<uint8>(EOSMRelationshipType::MAX); ++TypeIndex)
    {
        const EOSMRelationshipType Type = static_cast<EOSMRelationshipType>(TypeIndex);
        const int32 Count = CountEdgesOfType(Type);
        if (Count > 0)
        {
            Lines.Add(FString::Printf(TEXT("  %-18s %d%s"),
                *OSMRelationshipTypeToString(Type), Count,
                IsSpatialRelationship(Type) ? TEXT("  (derived)") : TEXT("")));
        }
    }

    if (Groups.Num() > 0)
    {
        Lines.Add(TEXT(""));
        Lines.Add(TEXT("Groups:"));
        for (const FOSMNodeGroup& Group : Groups)
        {
            Lines.Add(FString::Printf(TEXT("  %-18s %d nodes  (%s)"),
                *Group.Name, Group.Num(), *OSMGroupKindToString(Group.Kind)));
        }
    }

    const TArray<int32> Flagged = GetFlaggedNodes();
    if (Flagged.Num() > 0)
    {
        Lines.Add(TEXT(""));
        Lines.Add(FString::Printf(
            TEXT("%d node(s) flagged by validation — kept, not dropped. See the graph report."),
            Flagged.Num()));
    }

    Lines.Add(TEXT(""));
    Lines.Add(FString::Printf(TEXT("Geometry:   %d points in %d records"),
        Geometry.Points.Num(), Geometry.Records.Num()));
    Lines.Add(FString::Printf(TEXT("Hash:       %s"), *ComputeContentHash()));

    return FString::Join(Lines, TEXT("\n"));
}

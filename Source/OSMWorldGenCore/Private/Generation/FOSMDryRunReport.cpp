// Copyright InviMind. All Rights Reserved.

#include "Generation/FOSMDryRunReport.h"
#include "Graph/UOSMCityGraph.h"

// ---------------------------------------------------------------------------
FString FOSMDryRun::GetPCGDataTypeFor(EOSMNodeType NodeType)
{
    switch (NodeType)
    {
    case EOSMNodeType::Building:
        // Point for placement, polygon for the footprint. Buildings need both: the transform
        // decides where an asset goes, the footprint decides what it must fit.
        return TEXT("UPCGBasePointData + UPCGPolygon2DData");

    case EOSMNodeType::RoadSegment:
    case EOSMNodeType::Waterway:
    case EOSMNodeType::Railway:
    case EOSMNodeType::Barrier:
    case EOSMNodeType::PowerLine:
        // Splines, never point clouds: continuity along the length is the entire reason
        // corridors exist as a grouping.
        return TEXT("UPCGSplineData");

    case EOSMNodeType::WaterBody:
    case EOSMNodeType::VegetationArea:
    case EOSMNodeType::LanduseZone:
    case EOSMNodeType::LeisureArea:
        return TEXT("UPCGPolygon2DData");

    case EOSMNodeType::Amenity:
        return TEXT("UPCGBasePointData");

    case EOSMNodeType::TerrainTile:
        return TEXT("UPCGLandscapeData (sampled)");

    default:
        return TEXT("—");
    }
}

// ---------------------------------------------------------------------------
FOSMDryRunReport FOSMDryRun::Run(const UOSMCityGraph& Graph)
{
    FOSMDryRunReport Report;

    Report.bHasTerrain = Graph.CountNodesOfType(EOSMNodeType::TerrainTile) > 0;

    const TArray<FOSMAssetSelection> Selections = FOSMAssetSelector::SelectForGraph(Graph);

    // Keyed by node type so the report reads in the same order as the explorer.
    TMap<EOSMNodeType, FOSMDryRunCategory> ByType;

    for (const FOSMAssetSelection& Selection : Selections)
    {
        FOSMDryRunCategory& Category = ByType.FindOrAdd(Selection.NodeType);
        Category.NodeType = Selection.NodeType;
        Category.PCGDataType = GetPCGDataTypeFor(Selection.NodeType);
        ++Category.NodeCount;
        ++Report.TotalNodes;

        if (const FOSMGraphNode* Node = Graph.FindNode(Selection.NodeId))
        {
            if (Node->IsFlagged())
            {
                ++Category.FlaggedCount;
            }
        }

        if (Selection.IsFallback())
        {
            ++Category.FallbackCount;
            ++Report.TotalFallbacks;
            continue;
        }

        ++Category.SelectedCount;
        ++Report.TotalSelected;

        const FString Label = Selection.Label.IsEmpty()
            ? Selection.Asset.ToString()
            : Selection.Label;

        ++Category.AssetCounts.FindOrAdd(Label);

        if (!Selection.CorridorName.IsEmpty())
        {
            ++Report.CorridorSizes.FindOrAdd(Selection.CorridorName);
        }
    }

    // Declaration order, so the report is stable between runs rather than following map order.
    for (uint8 Index = 0; Index < static_cast<uint8>(EOSMNodeType::MAX); ++Index)
    {
        const EOSMNodeType Type = static_cast<EOSMNodeType>(Index);
        if (const FOSMDryRunCategory* Category = ByType.Find(Type))
        {
            Report.Categories.Add(*Category);
        }
    }

    return Report;
}

// ---------------------------------------------------------------------------
FString FOSMDryRunReport::ToDisplayString() const
{
    TArray<FString> Lines;

    Lines.Add(TEXT("DRY RUN — what generation would produce. Nothing has been created."));
    Lines.Add(TEXT(""));

    if (IsEmpty())
    {
        // Said plainly rather than shown as a table of zeroes: an empty run almost always means
        // no assets have been assigned yet, and that is worth stating outright.
        Lines.Add(TEXT("Nothing would be generated — no category has a usable asset assigned."));
        Lines.Add(TEXT("Assign assets in the Assets & Ratios panel, then run this again."));
        Lines.Add(TEXT(""));
    }

    Lines.Add(FString::Printf(TEXT("Nodes considered: %d    would place: %d    fallbacks: %d"),
        TotalNodes, TotalSelected, TotalFallbacks));
    Lines.Add(FString::Printf(TEXT("Terrain: %s"),
        bHasTerrain ? TEXT("present — placements can be grounded")
                    : TEXT("absent — placements would sit on a flat plane")));
    Lines.Add(TEXT(""));

    for (const FOSMDryRunCategory& Category : Categories)
    {
        Lines.Add(FString::Printf(TEXT("%s   %d node(s)   ->   %s"),
            *OSMNodeTypeToString(Category.NodeType), Category.NodeCount, *Category.PCGDataType));

        if (Category.SelectedCount == 0)
        {
            Lines.Add(TEXT("    no asset assigned — every node would fall back"));
        }
        else
        {
            // Sorted by count so the dominant choice reads first, and the actual ratio is
            // visible next to the one that was configured.
            TArray<TPair<FString, int32>> Sorted;
            for (const TPair<FString, int32>& Pair : Category.AssetCounts)
            {
                Sorted.Add(Pair);
            }
            Sorted.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
            {
                return A.Value > B.Value;
            });

            for (const TPair<FString, int32>& Pair : Sorted)
            {
                const double Percent = Category.SelectedCount > 0
                    ? 100.0 * Pair.Value / static_cast<double>(Category.SelectedCount)
                    : 0.0;

                Lines.Add(FString::Printf(TEXT("    %5.1f%%  %-28s %d"),
                    Percent, *Pair.Key, Pair.Value));
            }
        }

        if (Category.FallbackCount > 0 && Category.SelectedCount > 0)
        {
            Lines.Add(FString::Printf(TEXT("    %d node(s) would fall back"), Category.FallbackCount));
        }

        if (Category.FlaggedCount > 0)
        {
            Lines.Add(FString::Printf(
                TEXT("    %d node(s) carry validation flags and would still be generated"),
                Category.FlaggedCount));
        }

        Lines.Add(TEXT(""));
    }

    if (CorridorSizes.Num() > 0)
    {
        // The road-continuity rule, made visible: these segments share one choice, so a street
        // cannot alternate surfaces along its length.
        TArray<TPair<FString, int32>> Corridors;
        for (const TPair<FString, int32>& Pair : CorridorSizes)
        {
            Corridors.Add(Pair);
        }
        Corridors.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
        {
            return A.Value > B.Value;
        });

        Lines.Add(FString::Printf(TEXT("Corridors sharing one choice (%d):"), Corridors.Num()));
        for (int32 Index = 0; Index < FMath::Min(Corridors.Num(), 8); ++Index)
        {
            Lines.Add(FString::Printf(TEXT("    %-32s %d segment(s)"),
                *Corridors[Index].Key, Corridors[Index].Value));
        }
        if (Corridors.Num() > 8)
        {
            Lines.Add(FString::Printf(TEXT("    ... and %d more"), Corridors.Num() - 8));
        }
    }

    return FString::Join(Lines, TEXT("\n"));
}

// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/FOSMNode.h"
#include "Model/FOSMWay.h"
#include "Model/FOSMRelation.h"
#include "FOSMParseResult.generated.h"

/**
 * Result of stage 1 parsing (.osm XML or .osm.pbf).
 * Holds parsed raw OSM nodes, ways, and relations before feature classification.
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMParseResult
{
    GENERATED_BODY()

    /** Nodes lookup table: Node ID -> FOSMNode */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Parse")
    TMap<int64, FOSMNode> Nodes;

    /** Ways lookup table: Way ID -> FOSMWay */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Parse")
    TMap<int64, FOSMWay> Ways;

    /** Relations lookup table: Relation ID -> FOSMRelation */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Parse")
    TMap<int64, FOSMRelation> Relations;

    /** Minimum bounding lat/lon from raw metadata if present */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Parse")
    FVector2D MinLatLon = FVector2D(90.0, 180.0);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Parse")
    FVector2D MaxLatLon = FVector2D(-90.0, -180.0);

    /** Parse duration in seconds */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Parse")
    float ParseTimeSeconds = 0.0f;

    /** Total elements processed */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Parse")
    int64 TotalElementsProcessed = 0;

    /** Reset all parsed structures */
    void Reset()
    {
        Nodes.Empty();
        Ways.Empty();
        Relations.Empty();
        MinLatLon = FVector2D(90.0, 180.0);
        MaxLatLon = FVector2D(-90.0, -180.0);
        ParseTimeSeconds = 0.0f;
        TotalElementsProcessed = 0;
    }

    /** Resolve way node coordinates from the nodes map */
    void ResolveWayCoordinates()
    {
        for (auto& WayPair : Ways)
        {
            FOSMWay& Way = WayPair.Value;
            Way.ResolvedCoords.Empty(Way.NodeRefs.Num());
            for (int64 NodeId : Way.NodeRefs)
            {
                if (const FOSMNode* Node = Nodes.Find(NodeId))
                {
                    Way.ResolvedCoords.Add(Node->GetLatLon());

                    // Update geographic bounds
                    MinLatLon.X = FMath::Min(MinLatLon.X, Node->Latitude);
                    MinLatLon.Y = FMath::Min(MinLatLon.Y, Node->Longitude);
                    MaxLatLon.X = FMath::Max(MaxLatLon.X, Node->Latitude);
                    MaxLatLon.Y = FMath::Max(MaxLatLon.Y, Node->Longitude);
                }
            }
        }
    }
};

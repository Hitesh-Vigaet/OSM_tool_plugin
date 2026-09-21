// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Materials/EOSMSurfaceCategory.h"
#include "Graph/EOSMGraphTypes.h"

/**
 * Result of resolving materials for a city element.
 * For buildings: WallCategory, RoofCategory, GroundFloorCategory.
 * For roads/terrain/areas: SurfaceCategory.
 */
struct OSMWORLDGENCORE_API FOSMMaterialAssignment
{
    EOSMSurfaceCategory WallCategory        = EOSMSurfaceCategory::Unknown;
    EOSMSurfaceCategory RoofCategory        = EOSMSurfaceCategory::Unknown;
    EOSMSurfaceCategory GroundFloorCategory = EOSMSurfaceCategory::Unknown;
    EOSMSurfaceCategory SurfaceCategory     = EOSMSurfaceCategory::Unknown;

    /** Returns the dominant category for component-wide stencil assignment */
    EOSMSurfaceCategory GetDominantCategory(EOSMNodeType NodeType) const
    {
        if (NodeType == EOSMNodeType::Building)
        {
            return WallCategory != EOSMSurfaceCategory::Unknown ? WallCategory : EOSMSurfaceCategory::Concrete;
        }
        return SurfaceCategory != EOSMSurfaceCategory::Unknown ? SurfaceCategory : EOSMSurfaceCategory::Concrete;
    }
};

/**
 * Procedural material classifier.
 * Evaluates OSM tags, node type, and subtypes to determine physical/thermal surface categories.
 * Pure functional: deterministic, stateless, and unit-testable.
 */
class OSMWORLDGENCORE_API FOSMMaterialResolver
{
public:
    /**
     * Resolve material assignment for a given node type, subtype, and tag dictionary.
     */
    static FOSMMaterialAssignment Resolve(
        EOSMNodeType NodeType,
        const FString& SubType,
        const TMap<FString, FString>& Tags);

    /** Parse building:material tag string to category */
    static EOSMSurfaceCategory ParseBuildingMaterial(const FString& Value);

    /** Parse roof:material tag string to category */
    static EOSMSurfaceCategory ParseRoofMaterial(const FString& Value);

    /** Parse road/area surface tag string to category */
    static EOSMSurfaceCategory ParseSurfaceMaterial(const FString& Value);

    /** Infer wall material from building subtype when tags are missing */
    static EOSMSurfaceCategory InferWallFromSubType(const FString& SubType, const TMap<FString, FString>& Tags);

    /** Infer roof material from building subtype when tags are missing */
    static EOSMSurfaceCategory InferRoofFromSubType(const FString& SubType, const TMap<FString, FString>& Tags);
};

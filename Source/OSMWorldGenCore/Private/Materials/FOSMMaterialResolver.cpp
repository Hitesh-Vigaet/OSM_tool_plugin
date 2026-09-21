// Copyright InviMind. All Rights Reserved.

#include "Materials/FOSMMaterialResolver.h"

namespace
{
    int32 GetLevelsFromTags(const TMap<FString, FString>& Tags)
    {
        if (const FString* LevelsStr = Tags.Find(TEXT("building:levels")))
        {
            return FCString::Atoi(**LevelsStr);
        }
        return 0;
    }
}

EOSMSurfaceCategory FOSMMaterialResolver::ParseBuildingMaterial(const FString& Value)
{
    const FString Clean = Value.TrimStartAndEnd().ToLower();

    if (Clean.IsEmpty()) return EOSMSurfaceCategory::Unknown;

    if (Clean == TEXT("brick") || Clean == TEXT("bricks"))
        return EOSMSurfaceCategory::Brick;

    if (Clean == TEXT("concrete") || Clean == TEXT("reinforced_concrete") || Clean == TEXT("cement"))
        return EOSMSurfaceCategory::Concrete;

    if (Clean == TEXT("glass") || Clean == TEXT("mirror"))
        return EOSMSurfaceCategory::Glass;

    if (Clean == TEXT("wood") || Clean == TEXT("timber") || Clean == TEXT("log"))
        return EOSMSurfaceCategory::Wood;

    if (Clean == TEXT("metal") || Clean == TEXT("steel") || Clean == TEXT("iron") ||
        Clean == TEXT("aluminium") || Clean == TEXT("corrugated_iron") || Clean == TEXT("tin"))
        return EOSMSurfaceCategory::Metal;

    if (Clean == TEXT("stone") || Clean == TEXT("limestone") || Clean == TEXT("sandstone") ||
        Clean == TEXT("granite") || Clean == TEXT("marble") || Clean == TEXT("rock") || Clean == TEXT("rubblestone"))
        return EOSMSurfaceCategory::Stone;

    if (Clean == TEXT("plaster") || Clean == TEXT("stucco") || Clean == TEXT("render") || Clean == TEXT("daub"))
        return EOSMSurfaceCategory::Plaster;

    if (Clean == TEXT("cement_block") || Clean == TEXT("cinder_block") || Clean == TEXT("concrete_blocks"))
        return EOSMSurfaceCategory::CementBlock;

    if (Clean == TEXT("plastic") || Clean == TEXT("vinyl") || Clean == TEXT("composite"))
        return EOSMSurfaceCategory::Plastic;

    if (Clean == TEXT("tiles") || Clean == TEXT("clay"))
        return EOSMSurfaceCategory::RoofTile;

    return EOSMSurfaceCategory::Unknown;
}

EOSMSurfaceCategory FOSMMaterialResolver::ParseRoofMaterial(const FString& Value)
{
    const FString Clean = Value.TrimStartAndEnd().ToLower();

    if (Clean.IsEmpty()) return EOSMSurfaceCategory::Unknown;

    if (Clean == TEXT("tiles") || Clean == TEXT("clay") || Clean == TEXT("ceramic") ||
        Clean == TEXT("tile") || Clean == TEXT("roof_tiles"))
        return EOSMSurfaceCategory::RoofTile;

    if (Clean == TEXT("metal") || Clean == TEXT("steel") || Clean == TEXT("tin") ||
        Clean == TEXT("corrugated_iron") || Clean == TEXT("zinc") || Clean == TEXT("copper") || Clean == TEXT("sheet_metal"))
        return EOSMSurfaceCategory::Metal;

    if (Clean == TEXT("concrete") || Clean == TEXT("cement"))
        return EOSMSurfaceCategory::Concrete;

    if (Clean == TEXT("glass"))
        return EOSMSurfaceCategory::Glass;

    if (Clean == TEXT("slate") || Clean == TEXT("stone") || Clean == TEXT("shingle"))
        return EOSMSurfaceCategory::Stone;

    if (Clean == TEXT("thatch") || Clean == TEXT("grass") || Clean == TEXT("green"))
        return EOSMSurfaceCategory::Vegetation;

    if (Clean == TEXT("wood") || Clean == TEXT("tar_paper") || Clean == TEXT("felt") || Clean == TEXT("asphalt"))
        return EOSMSurfaceCategory::Asphalt;

    if (Clean == TEXT("plastic") || Clean == TEXT("polycarbonate"))
        return EOSMSurfaceCategory::Plastic;

    return EOSMSurfaceCategory::Unknown;
}

EOSMSurfaceCategory FOSMMaterialResolver::ParseSurfaceMaterial(const FString& Value)
{
    const FString Clean = Value.TrimStartAndEnd().ToLower();

    if (Clean.IsEmpty()) return EOSMSurfaceCategory::Unknown;

    if (Clean == TEXT("asphalt") || Clean == TEXT("paved") || Clean == TEXT("tarmac") || Clean == TEXT("bitumen"))
        return EOSMSurfaceCategory::Asphalt;

    if (Clean == TEXT("concrete") || Clean == TEXT("concrete:plates") || Clean == TEXT("concrete:lanes"))
        return EOSMSurfaceCategory::Concrete;

    if (Clean == TEXT("cobblestone") || Clean == TEXT("sett") || Clean == TEXT("paving_stones") ||
        Clean == TEXT("stone") || Clean == TEXT("unhewn_cobblestone"))
        return EOSMSurfaceCategory::Stone;

    if (Clean == TEXT("gravel") || Clean == TEXT("fine_gravel") || Clean == TEXT("pebbles") ||
        Clean == TEXT("compacted") || Clean == TEXT("crushed_stone"))
        return EOSMSurfaceCategory::Stone;

    if (Clean == TEXT("dirt") || Clean == TEXT("earth") || Clean == TEXT("ground") ||
        Clean == TEXT("mud") || Clean == TEXT("unpaved") || Clean == TEXT("soil") || Clean == TEXT("sand"))
        return EOSMSurfaceCategory::Soil;

    if (Clean == TEXT("grass") || Clean == TEXT("grass_paver"))
        return EOSMSurfaceCategory::Grass;

    if (Clean == TEXT("wood"))
        return EOSMSurfaceCategory::Wood;

    if (Clean == TEXT("metal"))
        return EOSMSurfaceCategory::Metal;

    return EOSMSurfaceCategory::Unknown;
}

EOSMSurfaceCategory FOSMMaterialResolver::InferWallFromSubType(const FString& SubType, const TMap<FString, FString>& Tags)
{
    const FString LowerSub = SubType.TrimStartAndEnd().ToLower();

    if (LowerSub == TEXT("commercial") || LowerSub == TEXT("office") || LowerSub == TEXT("retail"))
    {
        const int32 Levels = GetLevelsFromTags(Tags);
        return Levels > 4 ? EOSMSurfaceCategory::Glass : EOSMSurfaceCategory::Concrete;
    }

    if (LowerSub == TEXT("industrial") || LowerSub == TEXT("warehouse") || LowerSub == TEXT("hangar") ||
        LowerSub == TEXT("factory") || LowerSub == TEXT("storage_tank"))
    {
        return EOSMSurfaceCategory::Metal;
    }

    if (LowerSub == TEXT("residential") || LowerSub == TEXT("house") || LowerSub == TEXT("apartments") ||
        LowerSub == TEXT("terrace") || LowerSub == TEXT("detached"))
    {
        const int32 Levels = GetLevelsFromTags(Tags);
        return Levels <= 3 ? EOSMSurfaceCategory::Plaster : EOSMSurfaceCategory::Concrete;
    }

    if (LowerSub == TEXT("church") || LowerSub == TEXT("cathedral") || LowerSub == TEXT("mosque") ||
        LowerSub == TEXT("temple") || LowerSub == TEXT("castle") || LowerSub == TEXT("monument"))
    {
        return EOSMSurfaceCategory::Stone;
    }

    if (LowerSub == TEXT("garage") || LowerSub == TEXT("shed") || LowerSub == TEXT("outbuilding") ||
        LowerSub == TEXT("cabin") || LowerSub == TEXT("barn"))
    {
        return EOSMSurfaceCategory::CementBlock;
    }

    if (LowerSub == TEXT("greenhouse"))
    {
        return EOSMSurfaceCategory::Glass;
    }

    // Default building wall
    return EOSMSurfaceCategory::Concrete;
}

EOSMSurfaceCategory FOSMMaterialResolver::InferRoofFromSubType(const FString& SubType, const TMap<FString, FString>& Tags)
{
    const FString LowerSub = SubType.TrimStartAndEnd().ToLower();

    if (LowerSub == TEXT("industrial") || LowerSub == TEXT("warehouse") || LowerSub == TEXT("hangar") ||
        LowerSub == TEXT("garage") || LowerSub == TEXT("shed"))
    {
        return EOSMSurfaceCategory::Metal;
    }

    if (LowerSub == TEXT("residential") || LowerSub == TEXT("house") || LowerSub == TEXT("detached") ||
        LowerSub == TEXT("terrace"))
    {
        return EOSMSurfaceCategory::RoofTile;
    }

    if (LowerSub == TEXT("commercial") || LowerSub == TEXT("office") || LowerSub == TEXT("apartments"))
    {
        return EOSMSurfaceCategory::Concrete; // flat concrete slab roof
    }

    if (LowerSub == TEXT("church") || LowerSub == TEXT("temple") || LowerSub == TEXT("mosque"))
    {
        return EOSMSurfaceCategory::RoofTile;
    }

    if (LowerSub == TEXT("greenhouse"))
    {
        return EOSMSurfaceCategory::Glass;
    }

    return EOSMSurfaceCategory::Concrete;
}

FOSMMaterialAssignment FOSMMaterialResolver::Resolve(
    EOSMNodeType NodeType,
    const FString& SubType,
    const TMap<FString, FString>& Tags)
{
    FOSMMaterialAssignment Result;

    switch (NodeType)
    {
    case EOSMNodeType::Building:
    {
        // 1. Explicit building:material tag
        if (const FString* MatTag = Tags.Find(TEXT("building:material")))
        {
            Result.WallCategory = ParseBuildingMaterial(*MatTag);
        }
        else if (const FString* FacadeTag = Tags.Find(TEXT("building:facade:material")))
        {
            Result.WallCategory = ParseBuildingMaterial(*FacadeTag);
        }

        if (Result.WallCategory == EOSMSurfaceCategory::Unknown)
        {
            Result.WallCategory = InferWallFromSubType(SubType, Tags);
        }

        // 2. Explicit roof:material tag
        if (const FString* RoofTag = Tags.Find(TEXT("roof:material")))
        {
            Result.RoofCategory = ParseRoofMaterial(*RoofTag);
        }

        if (Result.RoofCategory == EOSMSurfaceCategory::Unknown)
        {
            Result.RoofCategory = InferRoofFromSubType(SubType, Tags);
        }

        // 3. Ground floor: defaults to wall unless building has shopfront/retail
        if (const FString* ShopTag = Tags.Find(TEXT("shop")))
        {
            Result.GroundFloorCategory = EOSMSurfaceCategory::Glass;
        }
        else
        {
            Result.GroundFloorCategory = Result.WallCategory;
        }

        Result.SurfaceCategory = Result.WallCategory;
        break;
    }

    case EOSMNodeType::RoadSegment:
    case EOSMNodeType::Junction:
    {
        if (const FString* SurfTag = Tags.Find(TEXT("surface")))
        {
            Result.SurfaceCategory = ParseSurfaceMaterial(*SurfTag);
        }

        if (Result.SurfaceCategory == EOSMSurfaceCategory::Unknown)
        {
            Result.SurfaceCategory = EOSMSurfaceCategory::Asphalt;
        }

        Result.WallCategory = Result.SurfaceCategory;
        Result.RoofCategory = Result.SurfaceCategory;
        Result.GroundFloorCategory = Result.SurfaceCategory;
        break;
    }

    case EOSMNodeType::Railway:
    {
        Result.SurfaceCategory = EOSMSurfaceCategory::Stone; // ballast + steel rails
        Result.WallCategory = Result.SurfaceCategory;
        Result.RoofCategory = Result.SurfaceCategory;
        break;
    }

    case EOSMNodeType::WaterBody:
    case EOSMNodeType::Waterway:
    {
        Result.SurfaceCategory = EOSMSurfaceCategory::Water;
        Result.WallCategory = EOSMSurfaceCategory::Water;
        Result.RoofCategory = EOSMSurfaceCategory::Water;
        break;
    }

    case EOSMNodeType::VegetationArea:
    {
        Result.SurfaceCategory = EOSMSurfaceCategory::Vegetation;
        Result.WallCategory = Result.SurfaceCategory;
        Result.RoofCategory = Result.SurfaceCategory;
        break;
    }

    case EOSMNodeType::LeisureArea:
    {
        const FString LowerSub = SubType.ToLower();
        if (LowerSub == TEXT("pitch") || LowerSub == TEXT("track"))
        {
            if (const FString* SurfTag = Tags.Find(TEXT("surface")))
            {
                Result.SurfaceCategory = ParseSurfaceMaterial(*SurfTag);
            }
            if (Result.SurfaceCategory == EOSMSurfaceCategory::Unknown)
            {
                Result.SurfaceCategory = EOSMSurfaceCategory::Grass;
            }
        }
        else
        {
            Result.SurfaceCategory = EOSMSurfaceCategory::Grass;
        }
        Result.WallCategory = Result.SurfaceCategory;
        Result.RoofCategory = Result.SurfaceCategory;
        break;
    }

    case EOSMNodeType::LanduseZone:
    {
        const FString LowerSub = SubType.ToLower();
        if (LowerSub == TEXT("forest") || LowerSub == TEXT("wood"))
            Result.SurfaceCategory = EOSMSurfaceCategory::Vegetation;
        else if (LowerSub == TEXT("grass") || LowerSub == TEXT("meadow") || LowerSub == TEXT("recreation_ground"))
            Result.SurfaceCategory = EOSMSurfaceCategory::Grass;
        else if (LowerSub == TEXT("farmland") || LowerSub == TEXT("farmyard") || LowerSub == TEXT("allotments"))
            Result.SurfaceCategory = EOSMSurfaceCategory::Soil;
        else if (LowerSub == TEXT("industrial") || LowerSub == TEXT("commercial"))
            Result.SurfaceCategory = EOSMSurfaceCategory::Concrete;
        else
            Result.SurfaceCategory = EOSMSurfaceCategory::Soil;

        Result.WallCategory = Result.SurfaceCategory;
        Result.RoofCategory = Result.SurfaceCategory;
        break;
    }

    case EOSMNodeType::Amenity:
    {
        const FString LowerSub = SubType.ToLower();
        if (LowerSub == TEXT("parking") || LowerSub == TEXT("fuel"))
            Result.SurfaceCategory = EOSMSurfaceCategory::Asphalt;
        else
            Result.SurfaceCategory = EOSMSurfaceCategory::Concrete;

        Result.WallCategory = Result.SurfaceCategory;
        Result.RoofCategory = Result.SurfaceCategory;
        break;
    }

    case EOSMNodeType::TerrainTile:
    default:
    {
        Result.SurfaceCategory = EOSMSurfaceCategory::Soil;
        Result.WallCategory = EOSMSurfaceCategory::Soil;
        Result.RoofCategory = EOSMSurfaceCategory::Soil;
        break;
    }
    }

    return Result;
}

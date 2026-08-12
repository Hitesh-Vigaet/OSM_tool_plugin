// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Parsing/FOSMParseResult.h"

/**
 * PBF (.osm.pbf) parser using libosmium when OSM_WITH_LIBOSMIUM is enabled.
 */
class OSMWORLDGENCORE_API FOSMPBFParser
{
public:
    static bool Parse(
        const FString& FilePath,
        FOSMParseResult& OutResult,
        TFunction<void(float Percent, const FText& Status)> OnProgress = nullptr,
        FThreadSafeBool* bCancel = nullptr);
};

// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Parsing/FOSMParseResult.h"

/**
 * Lightweight streaming XML parser for `.osm` files.
 */
class OSMWORLDGENCORE_API FOSMXMLParser
{
public:
    static bool Parse(
        const FString& FilePath,
        FOSMParseResult& OutResult,
        TFunction<void(float Percent, const FText& Status)> OnProgress = nullptr,
        FThreadSafeBool* bCancel = nullptr);
};

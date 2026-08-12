// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Parsing/FOSMParseResult.h"

/**
 * Main parser entry point for Stage 1 (Import).
 * Supports streaming `.osm` (XML) and `.osm.pbf` (Protocol Buffer) files.
 */
class OSMWORLDGENCORE_API FOSMParser
{
public:
    /**
     * Parse an OSM file (XML or PBF, auto-detected by extension/magic bytes).
     *
     * @param FilePath      Path to the file on disk
     * @param OutResult     Result container to populate
     * @param OnProgress    Optional progress callback (percent 0..1)
     * @param bCancel       Optional thread-safe cancel flag
     * @return              True if parsing completed successfully
     */
    static bool Parse(
        const FString& FilePath,
        FOSMParseResult& OutResult,
        TFunction<void(float Percent, const FText& Status)> OnProgress = nullptr,
        FThreadSafeBool* bCancel = nullptr);

    /** Check if PBF support is compiled in */
    static bool IsPBFSupported();
};

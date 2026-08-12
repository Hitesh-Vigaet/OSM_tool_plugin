// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/FOSMFeature.h"
#include "Model/FOSMFeatureTable.h"
#include "Parsing/FOSMParseResult.h"

/**
 * Tag classifier engine for Stage 2 (Parse & Tag).
 * Converts raw parsed OSM ways and relations into classified FOSMFeature items in an FOSMFeatureTable.
 */
class OSMWORLDGENCORE_API FOSMTagClassifier
{
public:
    /**
     * Classify raw parse results into a feature table.
     *
     * @param ParseResult   Input raw parse data
     * @param OutTable      Output feature table
     * @param OnProgress    Optional progress callback
     * @param bCancel       Optional cancel flag
     * @return              True if classification succeeded
     */
    static bool ClassifyAll(
        const FOSMParseResult& ParseResult,
        FOSMFeatureTable& OutTable,
        TFunction<void(float Percent, const FText& Status)> OnProgress = nullptr,
        FThreadSafeBool* bCancel = nullptr);

    /** Classify a single tag map */
    static EOSMFeatureType Classify(const TMap<FString, FString>& Tags, FString& OutSubType);

    /** Resolve tag-derived height, width, and other properties */
    static FOSMComputedProperties ResolveProperties(
        EOSMFeatureType Type,
        const FString& SubType,
        const TMap<FString, FString>& Tags);
};

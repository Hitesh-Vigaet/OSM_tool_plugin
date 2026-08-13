// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Region/FOSMRegion.h"
#include "Validation/FOSMValidation.h"

struct FOSMParseResult;

/**
 * The `.osm` validation gate (plan_v3_pipeline.md Phase 1.3).
 *
 * Runs in two stages, because the cheap failures must be caught before the expensive one:
 *
 *   1. ValidateFile  — byte/structure level. Reads the header only. Catches empty, truncated,
 *                      non-XML, and mis-declared-encoding files without paying to parse a
 *                      multi-megabyte document that was never going to work.
 *   2. ValidateParsed — semantic level. Coordinate ranges, element counts, bbox agreement with
 *                      the requested region, dangling node references.
 *
 * ValidateAgainstRegion runs both and is what callers normally want.
 */
class OSMWORLDGENCORE_API FOSMDataValidator
{
public:
    /** Extra facts gathered while validating, for the import report. */
    struct FStats
    {
        int32 NodeCount = 0;
        int32 WayCount = 0;
        int32 RelationCount = 0;

        /** Extent of the geometry actually present in the file. NOT the import region. */
        FOSMRegion DataBounds;

        /** Ways referencing at least one node absent from the file. */
        int32 WaysWithMissingNodes = 0;
        /** Total missing node references across all ways. */
        int32 MissingNodeRefs = 0;
    };

    /**
     * Structural check on the file itself, without a full parse.
     * @param FilePath  File to inspect.
     */
    static FOSMValidationResult ValidateFile(const FString& FilePath);

    /**
     * Semantic check on already-parsed data against the region it was fetched for.
     * @param Parsed          Parse output to inspect.
     * @param RequestedRegion The region this data is supposed to describe.
     * @param OutStats        Populated with counts and observed bounds.
     */
    static FOSMValidationResult ValidateParsed(
        const FOSMParseResult& Parsed,
        const FOSMRegion& RequestedRegion,
        FStats& OutStats);

    /**
     * Full gate: structure, then parse, then semantics. Short-circuits on fatal findings so a
     * structurally broken file is never handed to the parser.
     *
     * @param OutParsed  Receives the parse output on success, so callers need not parse twice.
     */
    static FOSMValidationResult ValidateAgainstRegion(
        const FString& FilePath,
        const FOSMRegion& RequestedRegion,
        FOSMParseResult& OutParsed,
        FStats& OutStats);

    /**
     * Multiple of the requested area beyond which the data bbox is treated as suspicious.
     *
     * Overpass's bbox filter matches any way whose own bounding box *overlaps* the query box —
     * it does not clip geometry. So a single motorway or administrative boundary crossing the
     * region legitimately drags the data bbox far outside it. That is expected and handled by
     * clipping; this threshold only exists to flag it loudly, because an unnoticed 40 km way
     * inside a 1.5 km region is exactly what produced a 500 km landscape.
     */
    static constexpr double SuspiciousAreaMultiple = 3.0;
};

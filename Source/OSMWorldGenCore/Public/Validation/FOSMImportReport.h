// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/EOSMFeatureType.h"
#include "Region/FOSMRegion.h"
#include "Validation/FOSMDataValidator.h"
#include "Validation/FOSMDEMValidator.h"
#include "Validation/FOSMValidation.h"

struct FOSMFeatureTable;

/**
 * The complete, structured outcome of an import (plan_v3_pipeline.md Phase 1.4).
 *
 * This is what makes import verifiable rather than hopeful: after a run, every question worth
 * asking — which region, which files, how many of each feature, does the elevation actually
 * cover the city, what went wrong — is answerable from this one object, without reading logs.
 */
struct OSMWORLDGENCORE_API FOSMImportReport
{
    /** The region the import was run for. The authority; everything else is measured against it. */
    FOSMRegion Region;

    FString OSMFilePath;
    FString DEMFilePath;

    /** Findings from all three gates, merged in the order they ran. */
    FOSMValidationResult Validation;

    FOSMDataValidator::FStats OSMStats;
    FOSMDEMValidator::FStats DEMStats;

    /** True when a DEM was supplied at all. Import without one is allowed. */
    bool bHasDEM = false;

    /** Fraction of the requested region covered by the DEM, in [0,1]. */
    double DEMCoverageFraction = 0.0;

    /** Feature counts after classification and clipping, indexed by EOSMFeatureType. */
    TMap<EOSMFeatureType, int32> FeatureCounts;

    /** Total classified features retained after clipping to the region. */
    int32 TotalFeatures = 0;

    /** Wall-clock duration of the whole import. */
    double DurationSeconds = 0.0;

    /** The decision. Accepted exactly when no gate raised a fatal issue. */
    bool IsAccepted() const { return Validation.IsAccepted(); }

    /** Multi-line human summary for the wizard and the log. */
    FString ToDisplayString() const;
};

/**
 * Runs the full import: both file gates, the cross-file gate, then classification.
 *
 * Ordering is deliberate and load-bearing — each stage is skipped when an earlier one failed
 * fatally, so a bad file is never handed to the code that would crash on it. This is the only
 * supported way to get from "paths on disk" to "data ready for Phase 2".
 */
class OSMWORLDGENCORE_API FOSMImportValidator
{
public:
    /**
     * @param Region       The requested region — the single source of truth for this import.
     * @param OSMFilePath  Required.
     * @param DEMFilePath  Optional; pass empty to import without elevation.
     * @param OutFeatures  Populated with classified, region-clipped features on success.
     */
    static FOSMImportReport Run(
        const FOSMRegion& Region,
        const FString& OSMFilePath,
        const FString& DEMFilePath,
        FOSMFeatureTable& OutFeatures);

    /**
     * The cross-file gate in isolation (plan_v3_pipeline.md Phase 1.3): does the elevation
     * data actually describe the same piece of ground as the vector data?
     *
     * Exposed separately so it can be tested without files on disk.
     */
    static FOSMValidationResult ValidateCrossFile(
        const FOSMRegion& Region,
        const FOSMRegion& OSMDataBounds,
        const FOSMRegion& DEMBounds,
        double& OutCoverageFraction);

    /**
     * Minimum fraction of the requested region the DEM must cover before the import is
     * refused. Below this, so much of the city would sit on interpolated or absent ground
     * that the terrain is not meaningfully derived from the data.
     */
    static constexpr double MinDEMCoverageFraction = 0.9;
};

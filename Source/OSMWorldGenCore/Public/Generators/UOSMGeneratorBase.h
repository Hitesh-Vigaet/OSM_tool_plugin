// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Generators/FOSMGenerationContext.h"
#include "Model/EOSMFeatureType.h"
#include "Model/FOSMFeature.h"
#include "UOSMGeneratorBase.generated.h"

/**
 * Abstract base class for all feature generators (Terrain, Roads, Buildings, Water, Landuse).
 */
UCLASS(Abstract, BlueprintType)
class OSMWORLDGENCORE_API UOSMGeneratorBase : public UObject
{
    GENERATED_BODY()

public:
    /**
     * Generate actors for all features matching this generator's accepted types.
     *
     * @param Context     Shared context with CRS transformer and target world
     * @param Features    The subset of features this generator should handle
     * @param OutActors   Generated actors appended to this array
     * @return            True if generation completed successfully
     */
    virtual bool Generate(
        const FOSMGenerationContext& Context,
        const TArray<const FOSMFeature*>& Features,
        TArray<AActor*>& OutActors);

    /** Which feature types this generator handles. Used for feature dispatching. */
    virtual TArray<EOSMFeatureType> GetAcceptedFeatureTypes() const;

    /** Display name for UI progress reporting */
    virtual FText GetDisplayName() const;

    /** Estimated work weight relative to other generators */
    virtual float GetEstimatedWeight() const { return 1.0f; }
};
